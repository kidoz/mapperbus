#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper336.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kOuterSize = 128UZ * 1024UZ;
constexpr std::size_t kPrgSize = 8UZ * kOuterSize;
constexpr std::size_t kChrSize = 8192UZ;
constexpr std::size_t kLatchOffset = 6UZ;
constexpr std::size_t kChrStateOffset = 7UZ;
constexpr std::size_t kStateSize = kChrStateOffset + kChrSize;

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 336U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 7U + (offset >> 8U) * 13U + (offset >> 14U) * 31U +
                             (offset >> 17U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset, salt);
    }
    return bytes;
}

std::unique_ptr<Mapper336> board(Byte submapper = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 Byte salt = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper336::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper336>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte latch, Address addr, std::size_t prg_size = kPrgSize, Byte salt = 0U) {
    const std::size_t outer = static_cast<std::size_t>((latch >> 3U) & 7U) * kOuterSize;
    const std::size_t inner = addr >= 0xC000U ? 7UZ : static_cast<std::size_t>(latch & 7U);
    return cell((outer + inner * 16384UZ + (addr & 0x3FFFUZ)) % prg_size, salt);
}

TEST_CASE("Mapper 336 validates mapper submapper and native memory population", "[mapper336]") {
    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (std::size_t size = kOuterSize; size <= kPrgSize; size += kOuterSize) {
                REQUIRE(Mapper336::validate_geometry(header(submapper, mirror), size, 0U));
            }
        }
    }

    for (const auto mapper_number : {0U, 2U, 312U, 335U, 337U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(mapper_number);
        REQUIRE_FALSE(Mapper336::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper336::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 3U; submapper < 16U; ++submapper) {
        h = header(submapper);
        REQUIRE_FALSE(Mapper336::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 16384UZ, 65536UZ, 131071UZ, 131073UZ, 196608UZ, 1048577UZ, 2097152UZ}) {
        REQUIRE_FALSE(Mapper336::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 4096UZ, 8192UZ, 131072UZ}) {
        REQUIRE_FALSE(Mapper336::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        h = header(0U, mirror);
        REQUIRE_FALSE(Mapper336::validate_geometry(h, kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 4096U;
            break;
        case 5U:
            h.chr_ram_size = 16384U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper336::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 336 latch decodes A15 throughout the upper CPU window", "[mapper336]") {
    for (const Byte submapper : {0U, 2U}) {
        auto mapper = board(submapper);
        for (unsigned raw_addr = 0U; raw_addr <= 0xFFFFU; ++raw_addr) {
            const auto value = static_cast<Byte>(raw_addr * 29U + (raw_addr >> 8U));
            mapper->reset();
            mapper->write_prg(static_cast<Address>(raw_addr), value);
            CAPTURE(submapper, raw_addr, value);
            REQUIRE(state(*mapper)[kLatchOffset] == (raw_addr >= 0x8000U ? value : 0U));
        }
    }

    auto mapper = board();
    const auto initial = state(*mapper);
    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == initial);
    for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
}

TEST_CASE("Mapper 336 combines outer and inner PRG banks with fixed inner bank seven",
          "[mapper336]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 8U; ++outer) {
        for (unsigned inner = 0U; inner < 8U; ++inner) {
            const auto latch = static_cast<Byte>(0xC0U | (outer << 3U) | inner);
            mapper->write_prg(0xC000U, latch);
            for (const Address addr : {0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU}) {
                CAPTURE(outer, inner, addr);
                REQUIRE(mapper->read_prg(addr) == expected_prg(latch, addr));
            }
            for (const Address addr : {0xC000U, 0xD234U, 0xE000U, 0xFFFFU}) {
                CAPTURE(outer, inner, addr);
                REQUIRE(mapper->read_prg(addr) == expected_prg(latch, addr));
            }
        }
    }

    for (std::size_t size = kOuterSize; size <= kPrgSize; size += kOuterSize) {
        mapper = board(0U, size);
        for (unsigned latch = 0U; latch < 256U; ++latch) {
            mapper->write_prg(0xFFFFU, static_cast<Byte>(latch));
            for (const Address addr : {0x8000U, 0x9234U, 0xC000U, 0xFEDCU}) {
                CAPTURE(size, latch, addr);
                REQUIRE(mapper->read_prg(addr) == expected_prg(latch, addr, size));
            }
        }
    }
}

TEST_CASE("Mapper 336 submappers select their documented mirroring bit", "[mapper336]") {
    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        auto mapper = board(submapper);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(0xC000U, static_cast<Byte>(value));
            const unsigned mask = submapper == 2U ? 0x10U : 0x20U;
            CAPTURE(submapper, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & mask) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        REQUIRE_FALSE(mapper->nametable_address(0x2000U));
        REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    }
}

TEST_CASE("Mapper 336 submapper one applies normal low-bit and asymmetric D3 conflicts",
          "[mapper336]") {
    auto image = rom(kPrgSize);
    for (unsigned value = 0U; value < 256U; ++value) {
        image[7UZ * 16384UZ + value] = static_cast<Byte>(value);
    }
    auto mapper = std::make_unique<Mapper336>(header(1U), std::move(image), std::vector<Byte>{});

    for (unsigned rom_value = 0U; rom_value < 256U; ++rom_value) {
        for (unsigned cpu_value = 0U; cpu_value < 256U; ++cpu_value) {
            mapper->reset();
            const auto addr = static_cast<Address>(0xC000U + rom_value);
            REQUIRE(mapper->read_prg(addr) == rom_value);
            mapper->write_prg(addr, static_cast<Byte>(cpu_value));
            const auto expected =
                static_cast<Byte>((cpu_value & (rom_value | 0xF8U) & 0xF7U) | (rom_value & 0x08U));
            CAPTURE(rom_value, cpu_value, expected);
            REQUIRE(state(*mapper)[kLatchOffset] == expected);
        }
    }
}

TEST_CASE("Mapper 336 provides unbanked volatile CHR RAM and no auxiliary hardware",
          "[mapper336]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 17U + (addr >> 8U)));
    }
    for (unsigned latch = 0U; latch < 256U; latch += 17U) {
        mapper->write_prg(0x8000U, static_cast<Byte>(latch));
        for (unsigned addr = 0U; addr < kChrSize; ++addr) {
            CAPTURE(latch, addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                    static_cast<Byte>(addr * 17U + (addr >> 8U)));
        }
    }
    REQUIRE(mapper->read_chr(0x2123U) == mapper->read_chr(0x0123U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 336 reset clears the latch while only cold power clears CHR RAM",
          "[mapper336][reset]") {
    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        auto mapper = board(submapper);
        const auto initial = state(*mapper);
        mapper->write_prg(0x8000U, 0xFFU);
        mapper->write_chr(0x0321U, 0xA7U);
        REQUIRE(state(*mapper) != initial);

        mapper->reset();
        REQUIRE(state(*mapper)[kLatchOffset] == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(7UZ * 16384UZ));
        REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);

        mapper->write_prg(0x8000U, 0xFFU);
        mapper->power_on();
        CAPTURE(submapper);
        REQUIRE(state(*mapper) == initial);
        for (unsigned addr = 0U; addr < kChrSize; ++addr) {
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
        }
    }
}

TEST_CASE("Mapper 336 snapshots replay latch and CHR RAM without ROM serialization",
          "[mapper336][state]") {
    auto mapper = board();
    mapper->write_prg(0xFFFFU, 0xADU);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U + 7U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kLatchOffset] == 0xADU);

    auto copy = board(0U, kPrgSize, 0x80U, MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == expected_prg(0xADU, addr, kPrgSize, 0x80U));
    }
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 13U + 7U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 336 rejects bounded malformed and incompatible states atomically",
          "[mapper336][state]") {
    auto mapper = board(2U);
    mapper->write_prg(0x8000U, 0xB5U);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xA5U));
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
        CAPTURE(bit);
        reject(invalid);
    }
    for (std::size_t offset = 2U; offset < kLatchOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }

    for (const Byte submapper : {0U, 1U}) {
        auto other = board(submapper);
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    }
    auto smaller = board(2U, kOuterSize);
    const auto before = state(*smaller);
    StateReader wrong_geometry(saved);
    smaller->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*smaller) == before);
}

TEST_CASE("Mapper 336 state validation accepts every raw latch byte", "[mapper336][state]") {
    auto mapper = board(1U);
    const auto saved = state(*mapper);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        auto valid = saved;
        valid[kLatchOffset] = static_cast<Byte>(latch);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*mapper) == valid);
        REQUIRE(mapper->read_prg(0x8123U) == expected_prg(latch, 0x8123U));
    }
}

} // namespace
