#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper361.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kOuterBankSize = 0x020000UZ;
constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kChrSize = 0x200000UZ;
constexpr std::size_t kRamSize = 0x2000UZ;
constexpr std::size_t kStateHeaderSize = 35U;
constexpr std::size_t kOuterOffset = 12U;
constexpr std::size_t kBankSelectOffset = 13U;
constexpr std::size_t kFirstBankOffset = 14U;
constexpr std::size_t kMirrorOffset = 22U;
constexpr std::size_t kIrqFlagsOffset = 25U;
constexpr std::size_t kRamFlagsOffset = 26U;
constexpr std::size_t kRamLengthOffset = 27U;

enum class RamPopulation { None, Volatile, Battery };

INesHeader header(RamPopulation population = RamPopulation::None,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 361U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = population == RamPopulation::Volatile ? kRamSize : 0U;
    result.prg_nvram_size = population == RamPopulation::Battery ? kRamSize : 0U;
    result.has_battery = population == RamPopulation::Battery;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 19U + (offset >> 8U) * 31U + (offset >> 13U) * 47U +
                             (offset >> 17U) * 59U + (offset >> 21U) * 61U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper361> board(RamPopulation population = RamPopulation::None,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(population, mirror);
    REQUIRE(Mapper361::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper361>(h, rom(prg_size, salt), rom(chr_size, salt + 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper361& mapper, unsigned index, Byte value, Byte control = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | control));
    mapper.write_prg(0x8001U, value);
}

unsigned expected_prg_bank(Byte select, const std::array<Byte, 8>& banks, Address addr) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool mode = (select & 0x40U) != 0U;
    switch (slot) {
    case 0U:
        return mode ? 14U : banks[6U] & 0x0FU;
    case 1U:
        return banks[7U] & 0x0FU;
    case 2U:
        return mode ? banks[6U] & 0x0FU : 14U;
    default:
        return 15U;
    }
}

Byte expected_chr_bank(Byte select, const std::array<Byte, 8>& banks, Address addr) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return banks[slot - 2U];
}

TEST_CASE("Mapper 361 validates NES 2.0 ROM RAM and CIRAM geometry", "[mapper361]") {
    for (const auto population :
         {RamPopulation::None, RamPopulation::Volatile, RamPopulation::Battery}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (std::size_t prg = 0x8000UZ; prg <= kPrgSize; prg *= 2U) {
                for (std::size_t chr = 0x2000UZ; chr <= kChrSize; chr *= 2U) {
                    CAPTURE(population, mirror, prg, chr);
                    REQUIRE(Mapper361::validate_geometry(header(population, mirror), prg, chr));
                }
            }
        }
    }

    for (const unsigned id : {0U, 4U, 205U, 333U, 360U, 362U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper361::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper361::validate_geometry(h, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper361::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x4000UZ, 0x7FFFUZ, 0xC000UZ, kPrgSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper361::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x3000UZ, 0x2800UZ, kChrSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper361::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(
            Mapper361::validate_geometry(header(RamPopulation::None, mirror), kPrgSize, kChrSize));
    }

    for (unsigned field = 0U; field < 12U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x1000U;
            break;
        case 1U:
            h.prg_ram_size = 0x4000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x1000U;
            h.has_battery = true;
            break;
        case 3U:
            h.prg_nvram_size = kRamSize;
            break;
        case 4U:
            h.has_battery = true;
            break;
        case 5U:
            h.prg_ram_size = kRamSize;
            h.prg_nvram_size = kRamSize;
            h.has_battery = true;
            break;
        case 6U:
            h.prg_ram_size = kRamSize;
            h.has_battery = true;
            break;
        case 7U:
            h.chr_ram_size = kRamSize;
            break;
        case 8U:
            h.chr_nvram_size = kRamSize;
            break;
        case 9U:
            h.prg_ram_size = kRamSize;
            h.prg_nvram_size = 0x1000U;
            break;
        case 10U:
            h.prg_nvram_size = 0x4000U;
            h.has_battery = true;
            break;
        default:
            h.chr_ram_size = kRamSize;
            h.chr_nvram_size = kRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper361::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 361 decodes only literal 7000 and the E001 MMC3 aliases", "[mapper361][bus]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper361& mapper) {
        mapper.reset();
        select_bank(mapper, 7U, 0xABU, 0x40U);
        mapper.write_prg(0xA000U, 1U);
        mapper.write_prg(0xA001U, 0xC0U);
        mapper.write_prg(0xC000U, 2U);
        mapper.write_prg(0xC001U, 0U);
        mapper.write_prg(0xE001U, 0U);
        mapper.clock_irq_counter();
    };

    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        prepare(*actual);
        prepare(*expected);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw == 0x7000U) {
            expected->write_prg(0x7000U, value);
        } else if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }

    actual->reset();
    for (unsigned value = 0U; value < 256U; ++value) {
        actual->write_prg(0x7000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*actual)[kOuterOffset] == (value & 0x0FU));
    }
}

TEST_CASE("Mapper 361 PRG banking uses four inner bits and fixed pages 14 and 15",
          "[mapper361][banking]") {
    auto mapper = board();
    constexpr std::array<Address, 4> addresses{0x8123U, 0xA123U, 0xC123U, 0xE123U};
    for (unsigned outer = 0U; outer < 16U; ++outer) {
        for (unsigned reg = 6U; reg <= 7U; ++reg) {
            for (const bool mode : {false, true}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->reset();
                    mapper->write_prg(0x7000U, static_cast<Byte>(outer | 0xF0U));
                    const auto select = static_cast<Byte>(reg | (mode ? 0x40U : 0U));
                    select_bank(*mapper, reg, static_cast<Byte>(value), mode ? 0x40U : 0U);
                    std::array<Byte, 8> banks{};
                    banks[reg] = static_cast<Byte>(value & 0x3FU);
                    for (const auto addr : addresses) {
                        const auto bank = expected_prg_bank(select, banks, addr);
                        const auto offset = static_cast<std::size_t>(outer) * kOuterBankSize +
                                            bank * kPrgBankSize + (addr & 0x1FFFU);
                        CAPTURE(outer, reg, mode, value, addr, bank);
                        REQUIRE(mapper->read_prg(addr) == cell(offset));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 361 CHR pairing inversion and seven inner bits precede outer banking",
          "[mapper361][banking]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 16U; ++outer) {
        for (unsigned reg = 0U; reg < 6U; ++reg) {
            for (const bool inversion : {false, true}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->reset();
                    mapper->write_prg(0x7000U, static_cast<Byte>(outer));
                    const auto select = static_cast<Byte>(reg | (inversion ? 0x80U : 0U));
                    select_bank(*mapper, reg, static_cast<Byte>(value), inversion ? 0x80U : 0U);
                    std::array<Byte, 8> banks{};
                    banks[reg] = static_cast<Byte>(value);
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                        const auto bank = expected_chr_bank(select, banks, addr) & 0x7FU;
                        const auto offset = static_cast<std::size_t>(outer) * kOuterBankSize +
                                            static_cast<std::size_t>(bank) * kChrBankSize + 0x155U;
                        CAPTURE(outer, reg, inversion, value, slot, bank);
                        REQUIRE(mapper->read_chr(addr) == cell(offset, 0x35U));
                        mapper->write_chr(addr, static_cast<Byte>(cell(offset, 0x35U) ^ 0xFFU));
                        REQUIRE(mapper->read_chr(addr) == cell(offset, 0x35U));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 361 wraps fitted ROM only after outer and inner pin decoding",
          "[mapper361][banking]") {
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size *= 2U) {
        auto mapper = board(RamPopulation::None, size, kChrSize);
        mapper->write_prg(0x7000U, 0x0FU);
        select_bank(*mapper, 6U, 0xFFU);
        for (unsigned offset = 0U; offset < 0x8000U; ++offset) {
            const auto addr = static_cast<Address>(0x8000U + offset);
            const auto bank =
                expected_prg_bank(0U, std::array<Byte, 8>{0U, 0U, 0U, 0U, 0U, 0U, 0x3FU, 0U}, addr);
            const auto physical =
                (15UZ * kOuterBankSize + bank * kPrgBankSize + (addr & 0x1FFFU)) % size;
            CAPTURE(size, addr);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }
    for (std::size_t size = 0x2000UZ; size <= kChrSize; size *= 2U) {
        auto mapper = board(RamPopulation::None, kPrgSize, size);
        mapper->write_prg(0x7000U, 0x0FU);
        select_bank(*mapper, 2U, 0xFFU);
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            const auto physical = (15UZ * kOuterBankSize + 127UZ * kChrBankSize + offset) % size;
            CAPTURE(size, offset);
            REQUIRE(mapper->read_chr(static_cast<Address>(0x1000U + offset)) ==
                    cell(physical, 0x35U));
        }
    }
}

TEST_CASE("Mapper 361 controls CIRAM through ordinary MMC3 mirroring writes",
          "[mapper361][ciram]") {
    auto mapper = board(RamPopulation::None, kPrgSize, kChrSize, MirrorMode::Horizontal);
    for (unsigned raw = 0xA000U; raw <= 0xBFFFU; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), 0U);
        const bool even = (raw & 1U) == 0U;
        REQUIRE(mapper->mirror_mode() == (even ? MirrorMode::Vertical : MirrorMode::Horizontal));
        mapper->write_prg(static_cast<Address>(raw), 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 361 MMC3 IRQ reload enable acknowledge and zero repetition", "[mapper361][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x7000U, 0x0EU);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 361 optional RAM gates coexist with the WRAM-independent outer latch",
          "[mapper361][ram][bus]") {
    auto mapper = board(RamPopulation::Volatile);
    mapper->write_prg(0x7000U, 0x39U);
    REQUIRE(mapper->read_prg(0x7000U) == 0x39U);
    REQUIRE(state(*mapper)[kOuterOffset] == 9U);
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 23U));
    }
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        CAPTURE(offset);
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 23U));
    }
    REQUIRE(mapper->battery_ram().empty());

    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE_FALSE(mapper->maps_prg(0x7000U));
    REQUIRE(mapper->prg_read_mask(0x7000U) == 0U);
    REQUIRE(mapper->read_prg(0x7000U) == 0U);
    mapper->write_prg(0x7000U, 0xABU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0BU);
    mapper->write_prg(0xA001U, 0x80U);
    REQUIRE(mapper->read_prg(0x7000U) == static_cast<Byte>(0x1000U * 23U));

    mapper->write_prg(0x7000U, 0xCDU);
    REQUIRE(mapper->read_prg(0x7000U) == 0xCDU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0DU);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x7000U, 0xEFU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0FU);
    REQUIRE(mapper->read_prg(0x7000U) == 0xCDU);

    auto absent = board();
    REQUIRE(absent->battery_ram().empty());
    REQUIRE(absent->shared_nvram().empty());
    REQUIRE_FALSE(absent->has_flash_storage());
    REQUIRE_FALSE(absent->has_expansion_audio());
    REQUIRE(absent->audio_output() == 0.0F);
    REQUIRE(absent->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(absent->observes_ppu_a12());
    REQUIRE_FALSE(absent->observes_ppu_address());
    REQUIRE_FALSE(absent->observes_ppu_reads());
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7000U, 0x7FFFU}) {
        REQUIRE_FALSE(absent->maps_prg(addr));
        REQUIRE_FALSE(absent->maps_expansion(addr));
        REQUIRE(absent->read_prg(addr) == 0U);
        REQUIRE(absent->read_expansion(addr) == 0U);
    }

    const auto before = state(*absent);
    absent->write_prg_cpu(0x7001U, 0xFFU, true);
    absent->observe_cpu_read(0x7000U, 0xA5U);
    absent->observe_cpu_write(0x7000U, 0xA5U);
    absent->observe_ppu_a12(true);
    absent->observe_ppu_address(0x1FFFU);
    absent->observe_ppu_read(0x1FFFU);
    absent->on_ppu_hblank();
    absent->clock_cpu();
    absent->clock_cpu_falling_edge();
    absent->on_ppu_frame_start();
    absent->clock_audio();
    absent->set_region(Region::PAL);
    absent->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    absent->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*absent) == before);
    REQUIRE(absent->audio_output() == 0.0F);
}

TEST_CASE("Mapper 361 battery imports are bounded exact and atomic", "[mapper361][ram]") {
    auto mapper = board(RamPopulation::Battery);
    REQUIRE(mapper->battery_ram().size() == kRamSize);
    std::vector<Byte> initial(kRamSize, 0x39U);
    mapper->set_battery_ram(initial);
    REQUIRE(mapper->read_prg(0x6000U) == 0x39U);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0x39U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::span(initial).first(kRamSize - 1U));
    mapper->set_battery_ram(std::vector<Byte>(kRamSize + 1U, 0xA5U));
    mapper->set_battery_ram({});
    REQUIRE(state(*mapper) == before);

    std::vector<Byte> replacement(kRamSize);
    for (std::size_t offset = 0U; offset < replacement.size(); ++offset) {
        replacement[offset] = static_cast<Byte>(offset * 19U + (offset >> 8U));
    }
    mapper->set_battery_ram(replacement);
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        CAPTURE(offset);
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) == replacement[offset]);
    }
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == replacement[0x123U]);
}

TEST_CASE("Mapper 361 reset restores registers and power clears only volatile RAM",
          "[mapper361][reset]") {
    for (const auto population : {RamPopulation::Volatile, RamPopulation::Battery}) {
        auto mapper = board(population, kPrgSize, kChrSize, MirrorMode::Horizontal);
        auto expected = board(population, kPrgSize, kChrSize, MirrorMode::Horizontal);
        mapper->write_prg(0x6123U, 0xA7U);
        expected->write_prg(0x6123U, 0xA7U);
        mapper->write_prg(0x7000U, 0x0FU);
        expected->write_prg(0x7000U, 0x0FU);
        expected->reset();
        select_bank(*mapper, 6U, 0x2AU, 0xC0U);
        mapper->write_prg(0xA000U, 0U);
        mapper->write_prg(0xA001U, 0xC0U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());

        mapper->reset();
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xE000U) == cell(15UZ * kPrgBankSize));
        REQUIRE(mapper->read_chr(0U) == cell(0U, 0x35U));
        REQUIRE(state(*mapper) == state(*expected));

        mapper->power_on();
        REQUIRE(mapper->read_prg(0x6123U) == (population == RamPopulation::Volatile ? 0U : 0xA7U));
        if (population == RamPopulation::Volatile) {
            REQUIRE(state(*mapper) ==
                    state(*board(population, kPrgSize, kChrSize, MirrorMode::Horizontal)));
        } else {
            REQUIRE(state(*mapper) == state(*expected));
        }
    }
}

TEST_CASE("Mapper 361 bounded snapshots replay state without serializing ROM",
          "[mapper361][state]") {
    auto mapper = board(RamPopulation::Battery);
    mapper->write_prg(0x7000U, 0x0DU);
    for (unsigned reg = 0U; reg < 8U; ++reg) {
        select_bank(*mapper, reg, static_cast<Byte>(reg * 11U + 3U), 0xC0U);
    }
    mapper->write_prg(0x8000U, 0xC7U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 23U));
    }

    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateHeaderSize + kRamSize);
    REQUIRE(saved.size() == 8227U);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved.size() < kChrSize);
    auto copy = board(RamPopulation::Battery, kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (const Address addr : {0x8000U, 0x9234U, 0xA000U, 0xC000U, 0xD234U, 0xFFFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x99U);
        CAPTURE(addr);
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(mapper->read_chr(addr) + 0x80U));
    }
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 23U));
    }
    mapper->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);

    auto no_ram = board();
    REQUIRE(state(*no_ram).size() == kStateHeaderSize);
}

TEST_CASE("Mapper 361 rejects malformed and incompatible states atomically", "[mapper361][state]") {
    auto mapper = board(RamPopulation::Battery);
    mapper->write_prg(0x7000U, 0x0AU);
    select_bank(*mapper, 7U, 0x3FU, 0x40U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->write_prg(0x6123U, 0xA7U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kOuterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte value : {0x10U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kOuterOffset] = value;
        reject(invalid);
    }
    for (const Byte reserved : {0x08U, 0x10U, 0x20U, 0x38U}) {
        auto invalid = saved;
        invalid[kBankSelectOffset] |= reserved;
        reject(invalid);
    }
    for (const std::size_t offset : {kFirstBankOffset + 6U, kFirstBankOffset + 7U}) {
        auto invalid = saved;
        invalid[offset] = 0x40U;
        reject(invalid);
    }
    for (const Byte value : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kMirrorOffset] = value;
        reject(invalid);
    }
    for (const Byte value : {4U, 8U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqFlagsOffset] = value;
        reject(invalid);
    }
    for (const Byte value : {1U, 0x3FU, 0xFFU}) {
        auto invalid = saved;
        invalid[kRamFlagsOffset] = value;
        reject(invalid);
    }
    auto oversized = saved;
    constexpr std::uint64_t kOversized = kRamSize + 1U;
    std::memcpy(oversized.data() + kRamLengthOffset, &kOversized, sizeof(kOversized));
    reject(oversized);
    auto missing_ram = saved;
    constexpr std::uint64_t kMissing = 0U;
    std::memcpy(missing_ram.data() + kRamLengthOffset, &kMissing, sizeof(kMissing));
    reject(missing_ram);

    auto wrong_prg = board(RamPopulation::Battery, kPrgSize / 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(RamPopulation::Battery, kPrgSize, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_ram = board();
    const auto wrong_ram_before = state(*wrong_ram);
    StateReader wrong_ram_reader(saved);
    wrong_ram->load_state(wrong_ram_reader);
    REQUIRE_FALSE(wrong_ram_reader.ok());
    REQUIRE(state(*wrong_ram) == wrong_ram_before);

    auto wrong_mirror = board(RamPopulation::Battery, kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
