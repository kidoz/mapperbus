#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper294.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 2U * 1024U * 1024U;
constexpr std::size_t kChrRamSize = 8192U;
constexpr std::size_t kInnerBankSize = 16384U;

INesHeader header() {
    INesHeader result{};
    result.mapper_number = 294U;
    result.is_nes2 = true;
    result.mirror_mode = MirrorMode::Vertical;
    result.chr_ram_size = kChrRamSize;
    return result;
}

std::vector<Byte> flat_rom() {
    return std::vector<Byte>(kPrgSize, 0xFFU);
}

std::vector<Byte> patterned_rom() {
    auto bytes = flat_rom();
    for (std::size_t bank = 0U; bank < kPrgSize / kInnerBankSize; ++bank) {
        bytes[bank * kInnerBankSize + 0x123U] = static_cast<Byte>(bank);
        bytes[bank * kInnerBankSize + 0x124U] = static_cast<Byte>(~bank);
    }
    return bytes;
}

std::unique_ptr<Mapper294> board_with_rom(std::vector<Byte> bytes) {
    return std::make_unique<Mapper294>(header(), std::move(bytes), std::vector<Byte>{});
}

std::unique_ptr<Mapper294> board(bool patterned = true) {
    REQUIRE(Mapper294::validate_geometry(header(), kPrgSize, 0U));
    return board_with_rom(patterned ? patterned_rom() : flat_rom());
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 294 validates the exact 63-1601 NES 2.0 population", "[mapper294]") {
    REQUIRE(Mapper294::validate_geometry(header(), kPrgSize, 0U));
    auto horizontal = header();
    horizontal.mirror_mode = MirrorMode::Horizontal;
    REQUIRE(Mapper294::validate_geometry(horizontal, kPrgSize, 0U));

    for (std::size_t size : {0UZ, 16384UZ, 131072UZ, 1048576UZ, 2097151UZ, 2097153UZ, 4194304UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper294::validate_geometry(header(), size, 0U));
    }
    for (std::size_t size : {1UZ, 4096UZ, 8192UZ}) {
        REQUIRE_FALSE(Mapper294::validate_geometry(header(), kPrgSize, size));
    }
    for (Byte submapper = 0U; submapper < 16U; ++submapper) {
        auto invalid = header();
        invalid.submapper = submapper;
        if (submapper == 0U) {
            REQUIRE(Mapper294::validate_geometry(invalid, kPrgSize, 0U));
        } else {
            REQUIRE_FALSE(Mapper294::validate_geometry(invalid, kPrgSize, 0U));
        }
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto invalid = header();
        invalid.mirror_mode = mode;
        REQUIRE_FALSE(Mapper294::validate_geometry(invalid, kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto invalid = header();
        switch (field) {
        case 0U:
            invalid.mapper_number = 2U;
            break;
        case 1U:
            invalid.is_nes2 = false;
            break;
        case 2U:
            invalid.has_battery = true;
            break;
        case 3U:
            invalid.prg_ram_size = 8192U;
            break;
        case 4U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 5U:
            invalid.chr_ram_size = 0U;
            break;
        case 6U:
            invalid.chr_ram_size = 16384U;
            break;
        default:
            invalid.chr_nvram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper294::validate_geometry(invalid, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 294 combines four outer bits with three UNROM inner bits", "[mapper294]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 16U; ++outer) {
        mapper->write_expansion(static_cast<Address>(0x4100U + outer * 16U),
                                static_cast<Byte>(outer | ((outer & 1U) != 0U ? 0x10U : 0U)));
        REQUIRE(mapper->mirror_mode() ==
                ((outer & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (Byte inner = 0U; inner < 8U; ++inner) {
            mapper->write_prg(static_cast<Address>(0x8000U + inner * 0x1000U), inner);
            const Byte selected = static_cast<Byte>(outer * 8U + inner);
            const Byte fixed = static_cast<Byte>(outer * 8U + 7U);
            CAPTURE(outer, inner, selected, fixed);
            REQUIRE(mapper->read_prg(0x8123U) == selected);
            REQUIRE(mapper->read_prg(0x8124U) == static_cast<Byte>(~selected));
            REQUIRE(mapper->read_prg(0xC123U) == fixed);
            REQUIRE(mapper->read_prg(0xC124U) == static_cast<Byte>(~fixed));
            REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        }
    }

    mapper->write_prg(0x8000U, 3U);
    mapper->write_expansion(0x41FFU, 0xE5U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8123U) == 5U * 8U + 3U);
    mapper->write_expansion(0x4100U, 0xFAU);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8123U) == 10U * 8U + 3U);
}

TEST_CASE("Mapper 294 decodes the complete outer mask and no duplicate register", "[mapper294]") {
    auto mapper = board(false);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        if (raw < 0x6000U) {
            mapper->write_expansion(addr, 0x1BU);
        } else {
            mapper->write_prg(addr, 0x1BU);
        }
        const bool outer = (raw & 0xC100U) == 0x4100U;
        const auto saved = state(*mapper);
        CAPTURE(raw, outer);
        REQUIRE(saved[6U] == (outer ? 0x1BU : 0U));
        REQUIRE(saved[7U] == (raw >= 0x8000U ? 3U : 0U));
        REQUIRE(mapper->mirror_mode() == (outer ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
}

TEST_CASE("Mapper 294 applies UNROM AND conflicts to the inner latch", "[mapper294]") {
    auto bytes = patterned_rom();
    bytes[0U] = 0x05U;
    auto mapper = board_with_rom(std::move(bytes));

    mapper->write_prg(0x8000U, 0xFFU);
    REQUIRE(mapper->read_prg(0x8123U) == 5U);
    REQUIRE(state(*mapper)[6U] == 0U);

    mapper->write_prg(0x6100U, 0x1FU);
    REQUIRE(state(*mapper)[6U] == 0x1FU);
    REQUIRE(state(*mapper)[7U] == 0x05U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8123U) == 15U * 8U + 5U);

    mapper->write_prg(0xC100U, 0x02U);
    REQUIRE(state(*mapper)[6U] == 0x1FU);
    REQUIRE(state(*mapper)[7U] == 0x02U);
}

TEST_CASE("Mapper 294 leaves write-only and unmapped CPU windows on open bus", "[mapper294]") {
    auto mapper = board();
    for (Address addr : {0x4020U, 0x40FFU, 0x4100U, 0x417FU, 0x41FFU, 0x4200U, 0x5FFFU}) {
        const bool outer = (addr & 0xC100U) == 0x4100U;
        REQUIRE(mapper->maps_expansion(addr) == outer);
        REQUIRE(mapper->expansion_read_mask(addr) == (outer ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
    for (Address addr : {0x4020U, 0x4100U, 0x5FFFU, 0x6000U, 0x6100U, 0x61FFU, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 294 CHR RAM is volatile while reset clears only the latches", "[mapper294]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U + addr / 64U));
    }
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + kChrRamSize)) ==
                static_cast<Byte>(addr * 13U + addr / 64U));
    }
    mapper->write_expansion(0x4100U, 0x1DU);
    mapper->write_prg(0x8000U, 5U);
    mapper->reset();
    REQUIRE(state(*mapper)[6U] == 0U);
    REQUIRE(state(*mapper)[7U] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_chr(0x123U) != 0U);

    mapper->set_battery_ram(std::vector<Byte>(kChrRamSize, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    mapper->power_on();
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
    }
}

TEST_CASE("Mapper 294 snapshots are bounded replayable and atomically rejected",
          "[mapper294][state]") {
    auto mapper = board();
    auto copy = board();
    mapper->write_expansion(0x4100U, 0x1DU);
    mapper->write_prg(0x8000U, 5U);
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 29U + addr / 31U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 2U + 4U + 1U + 1U + kChrRamSize);
    REQUIRE(saved[6U] == 0x1DU);
    REQUIRE(saved[7U] == 5U);

    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8123U) == mapper->read_prg(0x8123U));
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span(saved).first(size));
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 4U>{
             {{0U, 1U}, {2U, 1U}, {6U, 0x20U}, {7U, 8U}}}) {
        auto invalid = saved;
        invalid[offset] = value;
        invalid[100U] ^= 0xFFU;
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }

    auto smaller = std::make_unique<Mapper294>(
        header(), std::vector<Byte>(kPrgSize / 2U, 0xFFU), std::vector<Byte>{});
    const auto before = state(*smaller);
    StateReader mismatched(saved);
    smaller->load_state(mismatched);
    REQUIRE_FALSE(mismatched.ok());
    REQUIRE(state(*smaller) == before);
}

} // namespace
