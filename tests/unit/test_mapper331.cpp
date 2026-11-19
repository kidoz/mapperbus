#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper331.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 16384U;
constexpr std::size_t kChrBankSize = 4096U;
constexpr std::size_t kOuterBankSize = 128U * 1024U;
constexpr std::size_t kRomSize = 512U * 1024U;
constexpr std::size_t kStateSize = 14U;

INesHeader header() {
    INesHeader result{};
    result.mapper_number = 331U;
    result.is_nes2 = true;
    result.mirror_mode = MirrorMode::Vertical;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 5U + (offset >> 8U) * 11U + (offset >> 12U) * 29U +
                             (offset >> 17U) * 37U);
}

std::vector<Byte> rom(std::size_t salt) {
    std::vector<Byte> bytes(kRomSize);
    for (std::size_t offset = 0U; offset < bytes.size(); ++offset) {
        bytes[offset] = static_cast<Byte>(cell(offset) + salt);
    }
    return bytes;
}

std::unique_ptr<Mapper331> board(MirrorMode mirror = MirrorMode::Vertical) {
    auto h = header();
    h.mirror_mode = mirror;
    REQUIRE(Mapper331::validate_geometry(h, kRomSize, kRomSize));
    return std::make_unique<Mapper331>(h, rom(0U), rom(0xA5U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte inner, Byte outer, Address addr) {
    std::size_t bank = inner & 7U;
    if (addr >= 0xC000U) {
        bank = (outer & 8U) == 0U ? 7U : bank | 1U;
    }
    const std::size_t offset = static_cast<std::size_t>(outer & 3U) * kOuterBankSize +
                               bank * kPrgBankSize + (addr & 0x3FFFU);
    return cell(offset);
}

Byte expected_chr(Byte inner, Byte outer, Address addr) {
    const std::size_t bank = (inner >> 3U) & 0x0FU;
    const std::size_t offset = static_cast<std::size_t>(outer & 3U) * kOuterBankSize +
                               bank * kChrBankSize + (addr & 0x0FFFU);
    return static_cast<Byte>(cell(offset) + 0xA5U);
}

TEST_CASE("Mapper 331 accepts only the identified native ROM population", "[mapper331]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE(Mapper331::validate_geometry(h, kRomSize, kRomSize));
    }

    for (std::size_t size : {0UZ,
                             1UZ,
                             4096UZ,
                             8192UZ,
                             16384UZ,
                             131072UZ,
                             262144UZ,
                             524287UZ,
                             524289UZ,
                             1048576UZ,
                             4194304UZ}) {
        REQUIRE_FALSE(Mapper331::validate_geometry(header(), size, kRomSize));
        REQUIRE_FALSE(Mapper331::validate_geometry(header(), kRomSize, size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper331::validate_geometry(h, kRomSize, kRomSize));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper331::validate_geometry(h, kRomSize, kRomSize));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.mapper_number = 130U;
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
            h.chr_ram_size = 8192U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper331::validate_geometry(h, kRomSize, kRomSize));
    }
}

TEST_CASE("Mapper 331 decodes all three register windows and ignores other CPU writes",
          "[mapper331]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw_addr = 0U; raw_addr < 0x8000U; ++raw_addr) {
        mapper->write_prg(static_cast<Address>(raw_addr), static_cast<Byte>(raw_addr * 17U));
        REQUIRE(state(*mapper) == initial);
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(raw_addr)));
    }

    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        const auto value = static_cast<Byte>(raw_addr * 29U + (raw_addr >> 8U));
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        const auto saved = state(*mapper);
        CAPTURE(raw_addr, value);
        switch (raw_addr & 0xE000U) {
        case 0x8000U:
            REQUIRE(saved == initial);
            break;
        case 0xA000U:
            REQUIRE(saved[10U] == value);
            REQUIRE(saved[11U] == 0U);
            REQUIRE(saved[12U] == 0U);
            break;
        case 0xC000U:
            REQUIRE(saved[10U] == 0U);
            REQUIRE(saved[11U] == value);
            REQUIRE(saved[12U] == 0U);
            break;
        case 0xE000U:
            REQUIRE(saved[10U] == 0U);
            REQUIRE(saved[11U] == 0U);
            REQUIRE(saved[12U] == (value & 0x0FU));
            break;
        }
    }
}

TEST_CASE("Mapper 331 maps every PRG inner, outer, mode and PPU-A12 combination", "[mapper331]") {
    auto mapper = board();
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0xA000U, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (unsigned outer = 0U; outer < 16U; ++outer) {
        mapper->write_prg(0xE000U, static_cast<Byte>(outer));
        for (unsigned inner = 0U; inner < 256U; ++inner) {
            mapper->write_prg(0xA000U, 0x96U);
            mapper->write_prg(0xC000U, static_cast<Byte>(inner));
            mapper->observe_ppu_address(0x1000U);
            for (const auto addr : addresses) {
                CAPTURE(outer, inner, addr);
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(static_cast<Byte>(inner), static_cast<Byte>(outer), addr));
            }

            mapper->write_prg(0xA000U, static_cast<Byte>(inner));
            mapper->write_prg(0xC000U, 0x69U);
            mapper->observe_ppu_address(0x2000U);
            for (const auto addr : addresses) {
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(static_cast<Byte>(inner), static_cast<Byte>(outer), addr));
            }
        }
    }

    mapper->reset();
    mapper->write_prg(0xA000U, 2U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xE000U, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(2U * kPrgBankSize));
    REQUIRE(mapper->read_prg(0xC000U) == cell(7U * kPrgBankSize));
    mapper->write_prg(0xE000U, 8U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(2U * kPrgBankSize));
    REQUIRE(mapper->read_prg(0xC000U) == cell(3U * kPrgBankSize));
}

TEST_CASE("Mapper 331 maps and write-protects all CHR inner and outer banks", "[mapper331]") {
    auto mapper = board();
    constexpr std::array<Address, 5> offsets{0U, 1U, 0x123U, 0xABCU, 0xFFFU};
    for (unsigned outer = 0U; outer < 16U; ++outer) {
        mapper->write_prg(0xFFFFU, static_cast<Byte>(outer));
        for (unsigned inner = 0U; inner < 256U; ++inner) {
            mapper->write_prg(0xBFFFU, 0x96U);
            mapper->write_prg(0xDFFFU, static_cast<Byte>(inner));
            mapper->observe_ppu_address(0x1000U);
            for (const auto offset : offsets) {
                for (const Address addr : {offset, static_cast<Address>(offset + 0x1000U)}) {
                    CAPTURE(outer, inner, addr);
                    REQUIRE(mapper->read_chr(addr) ==
                            expected_chr(static_cast<Byte>(inner), static_cast<Byte>(outer), addr));
                    mapper->write_chr(addr, static_cast<Byte>(inner ^ outer ^ addr));
                    REQUIRE(mapper->read_chr(addr) ==
                            expected_chr(static_cast<Byte>(inner), static_cast<Byte>(outer), addr));
                }
            }

            mapper->write_prg(0xBFFFU, static_cast<Byte>(inner));
            mapper->write_prg(0xDFFFU, 0x69U);
            mapper->observe_ppu_address(0x0000U);
            for (const auto offset : offsets) {
                REQUIRE(mapper->read_chr(offset) ==
                        expected_chr(static_cast<Byte>(inner), static_cast<Byte>(outer), offset));
            }
        }
    }
}

TEST_CASE("Mapper 331 tracks physical PPU A12 and outer-register CIRAM wiring", "[mapper331]") {
    auto mapper = board();
    mapper->write_prg(0xA000U, 0x05U);
    mapper->write_prg(0xC000U, 0x4AU);
    REQUIRE(mapper->observes_ppu_address());
    for (unsigned addr = 0U; addr < 0x4000U; ++addr) {
        mapper->observe_ppu_address(static_cast<Address>(addr));
        const bool high = (addr & 0x1000U) != 0U;
        const auto saved = state(*mapper);
        CAPTURE(addr);
        REQUIRE(saved[13U] == (high ? 1U : 0U));
        REQUIRE(mapper->read_prg(0x8000U) == expected_prg(high ? 0x4AU : 0x05U, 0U, 0x8000U));
        REQUIRE(mapper->read_chr(0x0321U) == expected_chr(high ? 0x4AU : 0x05U, 0U, 0x0321U));
    }
    for (unsigned outer = 0U; outer < 16U; ++outer) {
        mapper->write_prg(0xE000U, static_cast<Byte>(outer));
        REQUIRE(mapper->mirror_mode() ==
                ((outer & 4U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
}

TEST_CASE("Mapper 331 reset clears all latches and A12 without persistent memory", "[mapper331]") {
    auto mapper = board(MirrorMode::Horizontal);
    mapper->write_prg(0xA123U, 0xFFU);
    mapper->write_prg(0xC456U, 0xEEU);
    mapper->write_prg(0xE789U, 0x0FU);
    mapper->observe_ppu_address(0x1000U);
    mapper->reset();
    REQUIRE(state(*mapper) == state(*board()));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(7U * kPrgBankSize));
    REQUIRE(mapper->read_chr(0U) == static_cast<Byte>(cell(0U) + 0xA5U));

    mapper->write_prg(0xA000U, 0xFFU);
    mapper->write_prg(0xC000U, 0xFFU);
    mapper->write_prg(0xE000U, 0x0FU);
    mapper->observe_ppu_address(0x1000U);
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board()));
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
}

TEST_CASE("Mapper 331 has no expansion registers, IRQ or audio hardware", "[mapper331]") {
    auto mapper = board();
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 331 snapshots replay all latches and PPU selection without ROM",
          "[mapper331][state]") {
    auto mapper = board();
    mapper->write_prg(0xA000U, 0xD7U);
    mapper->write_prg(0xC000U, 0x6BU);
    mapper->write_prg(0xE000U, 0x0EU);
    mapper->observe_ppu_address(0x1000U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[10U] == 0xD7U);
    REQUIRE(saved[11U] == 0x6BU);
    REQUIRE(saved[12U] == 0x0EU);
    REQUIRE(saved[13U] == 1U);

    auto copy = board(MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    for (const Address addr : {0x0000U, 0x0123U, 0x0FFFU, 0x1000U, 0x1ABCU, 0x1FFFU}) {
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
    }
}

TEST_CASE("Mapper 331 bounded malformed states reject atomically", "[mapper331][state]") {
    auto mapper = board();
    mapper->write_prg(0xA000U, 0xD7U);
    mapper->write_prg(0xC000U, 0x6BU);
    mapper->write_prg(0xE000U, 0x0EU);
    mapper->observe_ppu_address(0x1000U);
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
    for (std::size_t offset = 0U; offset < 10U; ++offset) {
        auto bad = saved;
        bad[offset] ^= 1U;
        reject(bad);
    }
    for (unsigned outer = 16U; outer < 256U; ++outer) {
        auto bad = saved;
        bad[12U] = static_cast<Byte>(outer);
        reject(bad);
    }
    for (unsigned high = 2U; high < 256U; ++high) {
        auto bad = saved;
        bad[13U] = static_cast<Byte>(high);
        reject(bad);
    }

    for (unsigned inner_a = 0U; inner_a < 256U; ++inner_a) {
        for (unsigned inner_b = 0U; inner_b < 256U; ++inner_b) {
            auto valid = saved;
            valid[10U] = static_cast<Byte>(inner_a);
            valid[11U] = static_cast<Byte>(inner_b);
            StateReader reader(valid);
            mapper->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == valid);
        }
    }
}

} // namespace
