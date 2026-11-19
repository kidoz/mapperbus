#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper328.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 16UZ * 1024UZ;
constexpr std::size_t kChrSize = 2UZ * 1024UZ;
constexpr std::size_t kStateSize = 15UZ;
constexpr Byte kWeakFixed = 0xF2U;
constexpr Byte kWeakMask = 0x0DU;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 328U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 12U) * 31U);
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 43U + (offset >> 7U) * 13U + 5U);
}

std::vector<Byte> prg_rom() {
    std::vector<Byte> bytes(kPrgSize);
    for (std::size_t offset = 0U; offset < bytes.size(); ++offset) {
        bytes[offset] = prg_cell(offset);
    }
    return bytes;
}

std::vector<Byte> chr_rom() {
    std::vector<Byte> bytes(kChrSize);
    for (std::size_t offset = 0U; offset < bytes.size(); ++offset) {
        bytes[offset] = chr_cell(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper328> board(MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper328::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper328>(h, prg_rom(), chr_rom());
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

bool is_weak(Address addr) {
    return (addr >= 0xCE80U && addr <= 0xCEFFU) || (addr >= 0xFE80U && addr <= 0xFEFFU);
}

std::uint16_t weak_checksum(Mapper328& mapper, Address base) {
    std::uint32_t sum = 0U;
    for (unsigned offset = 0U; offset < 128U; ++offset) {
        sum = (sum + mapper.read_prg(static_cast<Address>(base + offset))) & 0xFFFFU;
    }
    return static_cast<std::uint16_t>(sum);
}

TEST_CASE("Mapper 328 validates the exact native memory population", "[mapper328]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper328::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }

    for (std::size_t size : {0UZ, 1UZ, 8192UZ, 16383UZ, 16385UZ, 32768UZ, 65536UZ}) {
        REQUIRE_FALSE(Mapper328::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size : {0UZ, 1UZ, 1024UZ, 2047UZ, 2049UZ, 4096UZ, 8192UZ}) {
        REQUIRE_FALSE(Mapper328::validate_geometry(header(), kPrgSize, size));
    }
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper328::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper328::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }

    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.mapper_number = 327U;
            break;
        case 1U:
            h.is_nes2 = false;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = 8192U;
            break;
        case 4U:
            h.prg_nvram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 2048U;
            break;
        case 6U:
            h.chr_nvram_size = 2048U;
            break;
        }
        INFO(field);
        REQUIRE_FALSE(Mapper328::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 328 mirrors 16 KiB PRG and randomizes only documented weak windows",
          "[mapper328]") {
    auto mapper = board();
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        const auto addr = static_cast<Address>(raw_addr);
        CAPTURE(raw_addr);
        if (is_weak(addr)) {
            const auto value = mapper->read_prg(addr);
            REQUIRE((value & static_cast<Byte>(~kWeakMask)) == kWeakFixed);
        } else {
            REQUIRE(mapper->read_prg(addr) == prg_cell(addr & 0x3FFFU));
            REQUIRE(mapper->read_prg(addr) == prg_cell(addr & 0x3FFFU));
        }
    }

    const auto before = state(*mapper);
    for (unsigned raw_addr = 0U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->write_prg(static_cast<Address>(raw_addr), static_cast<Byte>(raw_addr * 37U));
    }
    REQUIRE(state(*mapper) == before);
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        const auto addr = static_cast<Address>(raw_addr);
        if (!is_weak(addr)) {
            REQUIRE(mapper->read_prg(addr) == prg_cell(addr & 0x3FFFU));
        }
    }
}

TEST_CASE("Mapper 328 weak bits are reproducible and change region checksums", "[mapper328]") {
    auto mapper = board();
    auto replay = board();
    for (Address base : {0xCE80U, 0xFE80U}) {
        Byte observed = 0U;
        for (unsigned pass = 0U; pass < 256U; ++pass) {
            for (unsigned offset = 0U; offset < 128U; ++offset) {
                const auto addr = static_cast<Address>(base + offset);
                const auto value = mapper->read_prg(addr);
                CAPTURE(base, pass, offset, value);
                REQUIRE(value == replay->read_prg(addr));
                REQUIRE((value & static_cast<Byte>(~kWeakMask)) == kWeakFixed);
                observed |= value & kWeakMask;
            }
        }
        REQUIRE(observed == kWeakMask);
    }

    auto first = board();
    auto second = board();
    for (Address base : {0xCE80U, 0xFE80U}) {
        const auto first_checksum = weak_checksum(*first, base);
        REQUIRE(first_checksum == weak_checksum(*second, base));
        const auto changed_checksum = weak_checksum(*first, base);
        REQUIRE(changed_checksum == weak_checksum(*second, base));
        REQUIRE(changed_checksum != first_checksum);
    }
}

TEST_CASE("Mapper 328 provides read-only 2 KiB CHR and fixed hard-wired CIRAM", "[mapper328]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr_cell(addr & 0x07FFU));
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xA5U));
        }
        for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr_cell(addr & 0x07FFU));
        }
        mapper->reset();
        mapper->power_on();
        REQUIRE(mapper->mirror_mode() == mirror);
    }
}

TEST_CASE("Mapper 328 has no RAM, persistence, IRQ, audio, or expansion windows", "[mapper328]") {
    auto mapper = board();
    for (Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    for (Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 328 reset and power-on restart the deterministic weak-bit stream",
          "[mapper328]") {
    auto mapper = board();
    std::array<Byte, 512> initial{};
    for (unsigned index = 0U; index < initial.size(); ++index) {
        initial[index] = mapper->read_prg(static_cast<Address>(0xCE80U + (index & 0x7FU)));
    }
    for (unsigned index = 0U; index < 1024U; ++index) {
        static_cast<void>(mapper->read_prg(static_cast<Address>(0xFE80U + (index & 0x7FU))));
    }

    mapper->reset();
    for (unsigned index = 0U; index < initial.size(); ++index) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0xCE80U + (index & 0x7FU))) ==
                initial[index]);
    }
    mapper->power_on();
    for (unsigned index = 0U; index < initial.size(); ++index) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0xCE80U + (index & 0x7FU))) ==
                initial[index]);
    }
}

TEST_CASE("Mapper 328 snapshots replay weak-bit state without ROM", "[mapper328][state]") {
    auto mapper = board(MirrorMode::Horizontal);
    for (unsigned index = 0U; index < 777U; ++index) {
        static_cast<void>(mapper->read_prg(static_cast<Address>(0xFE80U + (index & 0x7FU))));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board(MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned index = 0U; index < 4096U; ++index) {
        const auto addr = static_cast<Address>(0xCE80U + (index & 0x7FU));
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
}

TEST_CASE("Mapper 328 bounded invalid states reject without mutation", "[mapper328][state]") {
    auto mapper = board();
    for (unsigned index = 0U; index < 333U; ++index) {
        static_cast<void>(mapper->read_prg(static_cast<Address>(0xCE80U + (index & 0x7FU))));
    }
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        reject(invalid);
    }
    for (std::size_t offset : {2UZ, 6UZ}) {
        auto zero_size = saved;
        auto large_size = saved;
        for (std::size_t byte = 0U; byte < 4U; ++byte) {
            zero_size[offset + byte] = 0U;
            large_size[offset + byte] = 0xFFU;
        }
        reject(zero_size);
        reject(large_size);
    }
    for (Byte value : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[10] = value;
        reject(invalid);
    }
    auto zero_state = saved;
    for (std::size_t offset = 11U; offset < zero_state.size(); ++offset) {
        zero_state[offset] = 0U;
    }
    reject(zero_state);

    auto other = board(MirrorMode::Horizontal);
    const auto before = state(*other);
    StateReader mismatch(saved);
    other->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
