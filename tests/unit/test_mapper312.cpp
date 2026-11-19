#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper312.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 16384U;
constexpr std::size_t kPrgSize = 8U * kPrgBankSize;
constexpr std::size_t kChrSize = 8192U;
constexpr std::size_t kStateSize = 4U + kChrSize;

INesHeader header(bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 312U;
    result.is_nes2 = true;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    result.chr_ram_size = kChrSize;
    return result;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 41U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper312> board(bool vertical = true) {
    const auto h = header(vertical);
    REQUIRE(Mapper312::validate_geometry(h, kPrgSize, 0U));
    std::vector<Byte> prg(kPrgSize);
    for (std::size_t offset = 0U; offset < prg.size(); ++offset) {
        prg[offset] = prg_cell(offset / kPrgBankSize, offset % kPrgBankSize);
    }
    return std::make_unique<Mapper312>(h, std::move(prg), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 312 accepts only the documented native board population", "[mapper312]") {
    for (bool vertical : {false, true}) {
        REQUIRE(Mapper312::validate_geometry(header(vertical), kPrgSize, 0U));
    }

    for (std::size_t size : {0UZ, 1UZ, 8192UZ, 16384UZ, 65536UZ, 131071UZ, 131073UZ, 262144UZ}) {
        REQUIRE_FALSE(Mapper312::validate_geometry(header(), size, 0U));
    }
    for (std::size_t size : {1UZ, 4096UZ, 8192UZ, 131072UZ}) {
        REQUIRE_FALSE(Mapper312::validate_geometry(header(), kPrgSize, size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper312::validate_geometry(h, kPrgSize, 0U));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        REQUIRE_FALSE(Mapper312::validate_geometry(h, kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.mapper_number = 2U;
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
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 4096U;
            break;
        case 7U:
            h.chr_ram_size = 16384U;
            break;
        case 8U:
            h.chr_nvram_size = 8192U;
            break;
        default:
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper312::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 312 maps every switchable cell and keeps bank 7 fixed", "[mapper312]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(0x6000U, static_cast<Byte>(0xF8U | bank));
        for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    prg_cell(bank, offset));
            REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) ==
                    prg_cell(7U, offset));
        }
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x7FFFU, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->read_prg(0x8123U) == prg_cell(value & 7U, 0x123U));
        REQUIRE(mapper->read_prg(0xBFFFU) == prg_cell(value & 7U, 0x3FFFU));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(7U, 0U));
        REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(7U, 0x3FFFU));
    }
}

TEST_CASE("Mapper 312 PRG select covers the complete 6000-7FFF register window", "[mapper312]") {
    auto mapper = board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->reset();
        const auto value = static_cast<Byte>(0xF8U | (addr & 7U));
        mapper->write_prg(static_cast<Address>(addr), value);
        CAPTURE(addr);
        REQUIRE(mapper->read_prg(0x9234U) == prg_cell(addr & 7U, 0x1234U));
        REQUIRE(mapper->read_prg(0xD234U) == prg_cell(7U, 0x1234U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }

    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr < 0x6000U; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 312 mirroring select covers every upper CPU alias and ignores other bits",
          "[mapper312]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(0x6000U, 5U);
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 29U));
        CAPTURE(addr);
        REQUIRE(mapper->mirror_mode() ==
                ((addr & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->read_prg(0x8123U) == prg_cell(5U, 0x123U));
        REQUIRE(mapper->read_prg(0xC123U) == prg_cell(7U, 0x123U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
            mapper->write_prg(addr, static_cast<Byte>(value));
            CAPTURE(addr, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
}

TEST_CASE("Mapper 312 provides unbanked volatile CHR RAM and no persistent memory", "[mapper312]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 17U + (addr >> 8U)));
    }
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(0x6000U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < kChrSize; ++addr) {
            CAPTURE(bank, addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                    static_cast<Byte>(addr * 17U + (addr >> 8U)));
        }
    }
    REQUIRE(mapper->read_chr(0x2123U) == mapper->read_chr(0x0123U));
    REQUIRE(mapper->battery_ram().empty());
    const std::array<Byte, 4> imported{1U, 2U, 3U, 4U};
    mapper->set_battery_ram(imported);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
}

TEST_CASE("Mapper 312 leaves write-only and unrelated CPU windows undriven", "[mapper312]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x6ABCU, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(state(*mapper) == before);
    for (Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 312 reset clears latches while only cold power clears CHR RAM", "[mapper312]") {
    auto mapper = board(false);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0x6123U, 6U);
    mapper->write_prg(0xFFFFU, 1U);
    mapper->write_chr(0x0321U, 0xA7U);

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);

    mapper->write_prg(0x6000U, 6U);
    mapper->write_prg(0x8000U, 1U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->read_prg(0xC000U) == prg_cell(7U, 0U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
    }
}

TEST_CASE("Mapper 312 snapshots replay all mutable state without ROM", "[mapper312][state]") {
    auto mapper = board();
    mapper->write_prg(0x6FFFU, 0xFDU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U + 7U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[2U] == 5U);
    REQUIRE(saved[3U] == 1U);

    auto copy = board(false);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == prg_cell(5U, 0U));
    REQUIRE(copy->read_prg(0xC000U) == prg_cell(7U, 0U));
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 13U + 7U));
    }
}

TEST_CASE("Mapper 312 bounded malformed states reject atomically", "[mapper312][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 5U);
    mapper->write_prg(0x8000U, 1U);
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
    for (unsigned bank = 8U; bank < 256U; ++bank) {
        auto invalid = saved;
        invalid[2U] = static_cast<Byte>(bank);
        CAPTURE(bank);
        reject(invalid);
    }
    for (unsigned mirror = 2U; mirror < 256U; ++mirror) {
        auto invalid = saved;
        invalid[3U] = static_cast<Byte>(mirror);
        CAPTURE(mirror);
        reject(invalid);
    }

    for (unsigned bank = 0U; bank < 8U; ++bank) {
        for (unsigned mirror = 0U; mirror < 2U; ++mirror) {
            auto valid = saved;
            valid[2U] = static_cast<Byte>(bank);
            valid[3U] = static_cast<Byte>(mirror);
            StateReader reader(valid);
            mapper->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == valid);
        }
    }
}

} // namespace
