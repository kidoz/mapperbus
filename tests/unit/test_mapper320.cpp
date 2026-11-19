#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper320.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 1024UZ * 1024UZ;
constexpr std::size_t kChrSize = 8192U;
constexpr std::size_t kStateSize = 4U + kChrSize;

INesHeader header() {
    INesHeader result{};
    result.mapper_number = 320U;
    result.is_nes2 = true;
    result.chr_ram_size = kChrSize;
    result.mirror_mode = MirrorMode::Vertical;
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 5U + (offset >> 8U) * 11U + (offset >> 14U) * 37U +
                             (offset >> 20U) * 19U);
}

std::vector<Byte> rom() {
    std::vector<Byte> bytes(kPrgSize);
    for (std::size_t offset = 0U; offset < bytes.size(); ++offset) {
        bytes[offset] = pattern(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper320> board(MirrorMode mirror = MirrorMode::Vertical) {
    auto h = header();
    h.mirror_mode = mirror;
    REQUIRE(Mapper320::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper320>(h, rom(), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte outer, Byte data, Address addr) {
    const bool uorom = (outer & 0x10U) == 0U;
    unsigned inner = 0U;
    if (addr >= 0xC000U) {
        inner = uorom ? 15U : 7U;
    } else {
        inner = data & 7U;
        if (uorom) {
            inner |= (data & 0x10U) >> 1U;
        }
    }
    const std::size_t bank = static_cast<std::size_t>(outer & 0x0FU) * 8U + inner;
    return pattern((bank * 16384U + (addr & 0x3FFFU)) % kPrgSize);
}

TEST_CASE("Mapper 320 validates its native identity and recorded memory population",
          "[mapper320]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE(Mapper320::validate_geometry(h, kPrgSize, 0U));
    }

    for (unsigned mapper : {0U, 2U, 319U, 321U, 65535U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(mapper);
        REQUIRE_FALSE(Mapper320::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    legacy.chr_ram_size = 0U;
    REQUIRE_FALSE(Mapper320::validate_geometry(legacy, kPrgSize, 0U));
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper320::validate_geometry(h, kPrgSize, 0U));
    }
    for (std::size_t size :
         {0UZ, 1UZ, 8192UZ, 524288UZ, 1048575UZ, 1048577UZ, 2097152UZ, 3145728UZ, 4194304UZ}) {
        REQUIRE_FALSE(Mapper320::validate_geometry(header(), size, 0U));
    }
    for (std::size_t size : {1UZ, 8192UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper320::validate_geometry(header(), kPrgSize, size));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper320::validate_geometry(h, kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
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
        case 6U:
            h.chr_nvram_size = kChrSize;
            break;
        }
        REQUIRE_FALSE(Mapper320::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 320 implements both inner bank widths and all outer bank aliases",
          "[mapper320]") {
    auto mapper = board();
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xC123U, 0xDABCU, 0xFFFFU};

    for (unsigned outer = 0U; outer < 32U; ++outer) {
        for (unsigned data = 0U; data < 256U; ++data) {
            mapper->write_prg(static_cast<Address>(0xF0E0U | outer), static_cast<Byte>(data));
            for (const auto addr : addresses) {
                CAPTURE(outer, data, addr);
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(static_cast<Byte>(outer), static_cast<Byte>(data), addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            }
        }
    }
}

TEST_CASE("Mapper 320 decodes every CPU write and updates overlapping registers together",
          "[mapper320]") {
    auto mapper = board();
    for (unsigned raw_addr = 0U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw_addr);
        const auto value = static_cast<Byte>(raw_addr * 29U + (raw_addr >> 8U));
        mapper->write_prg(addr, value);

        const auto expected_inner = static_cast<Byte>(raw_addr >= 0x8000U ? value & 0x17U : 0U);
        const auto expected_outer =
            static_cast<Byte>((raw_addr & 0xFFE0U) == 0xF0E0U ? raw_addr & 0x1FU : 0U);
        const auto saved = state(*mapper);
        CAPTURE(raw_addr, value);
        REQUIRE(saved[2U] == expected_inner);
        REQUIRE(saved[3U] == expected_outer);
        REQUIRE(mapper->read_prg(0x8123U) == expected_prg(expected_outer, expected_inner, 0x8123U));
        REQUIRE(mapper->read_prg(0xC123U) == expected_prg(expected_outer, expected_inner, 0xC123U));
    }
}

TEST_CASE("Mapper 320 CHR RAM is unbanked and hard-wired mirroring does not change",
          "[mapper320]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        for (unsigned addr = 0U; addr < kChrSize; ++addr) {
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
            mapper->write_chr(static_cast<Address>(addr),
                              static_cast<Byte>(addr * 13U + addr / 256U));
        }
        for (unsigned outer = 0U; outer < 32U; ++outer) {
            mapper->write_prg(static_cast<Address>(0xF0E0U | outer), static_cast<Byte>(outer * 7U));
            for (unsigned addr = 0U; addr < kChrSize; ++addr) {
                CAPTURE(mirror, outer, addr);
                const auto expected = static_cast<Byte>(addr * 13U + addr / 256U);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == expected);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == expected);
            }
            REQUIRE(mapper->mirror_mode() == mirror);
        }
    }
}

TEST_CASE("Mapper 320 reset clears latches and power-on clears volatile CHR RAM", "[mapper320]") {
    auto mapper = board();
    mapper->write_prg(0xF0FFU, 0xFFU);
    mapper->write_chr(0x1234U, 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(0x1FU, 0x17U, 0x8000U));
    REQUIRE(mapper->read_prg(0xC000U) == expected_prg(0x1FU, 0x17U, 0xC000U));

    mapper->reset();
    REQUIRE(state(*mapper)[2U] == 0U);
    REQUIRE(state(*mapper)[3U] == 0U);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(0U, 0U, 0x8000U));
    REQUIRE(mapper->read_prg(0xC000U) == expected_prg(0U, 0U, 0xC000U));
    REQUIRE(mapper->read_chr(0x1234U) == 0xA7U);

    mapper->write_prg(0xF0F0U, 0x10U);
    mapper->power_on();
    REQUIRE(state(*mapper)[2U] == 0U);
    REQUIRE(state(*mapper)[3U] == 0U);
    REQUIRE(mapper->read_chr(0x1234U) == 0U);
}

TEST_CASE("Mapper 320 exposes no undocumented CPU RAM persistence IRQ or audio", "[mapper320]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::vector<Byte>(kChrSize, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 320 snapshots replay all mutable state without ROM serialization",
          "[mapper320][state]") {
    auto mapper = board();
    mapper->write_prg(0xF0F7U, 0xD7U);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 3U + addr / 128U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[2U] == 0x17U);
    REQUIRE(saved[3U] == 0x17U);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) ==
                mapper->read_chr(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 320 rejects malformed states without mutation", "[mapper320][state]") {
    auto mapper = board();
    mapper->write_prg(0xF0FFU, 0xFFU);
    mapper->write_chr(0x123U, 0xA7U);
    const auto saved = state(*mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader{std::span(saved).first(size)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset = 0U; offset < 2U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte value : {0x08U, 0x20U, 0x40U, 0x80U, 0xE8U, 0xFFU}) {
        auto invalid = saved;
        invalid[2U] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte value : {0x20U, 0x40U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[3U] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto other_mirror = board(MirrorMode::Horizontal);
    const auto other_saved = state(*other_mirror);
    StateReader mismatch(saved);
    other_mirror->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other_mirror) == other_saved);

    for (unsigned inner = 0U; inner < 256U; ++inner) {
        auto candidate = saved;
        candidate[2U] = static_cast<Byte>(inner);
        StateReader reader(candidate);
        mapper->load_state(reader);
        REQUIRE(reader.ok() == ((inner & ~0x17U) == 0U));
        REQUIRE(state(*mapper) == (reader.ok() ? candidate : saved));
        if (reader.ok()) {
            StateReader restore(saved);
            mapper->load_state(restore);
            REQUIRE(restore.ok());
        }
    }
}

} // namespace
