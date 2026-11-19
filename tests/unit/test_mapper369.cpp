#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper369.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kNvramSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kStateSize = 8223UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kNtdecBankOffset = 13UZ;
constexpr std::size_t kNtdecCounterOffset = 14UZ;
constexpr std::size_t kNtdecEnabledOffset = 16UZ;
constexpr std::size_t kMmcSelectOffset = 17UZ;
constexpr std::size_t kMmcBanksOffset = 18UZ;
constexpr std::size_t kMmcMirrorOffset = 26UZ;
constexpr std::size_t kMmcCounterOffset = 27UZ;
constexpr std::size_t kMmcLatchOffset = 28UZ;
constexpr std::size_t kMmcIrqOffset = 29UZ;
constexpr std::size_t kRamFlagsOffset = 30UZ;
constexpr std::size_t kNvramOffset = 31UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 369U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = true;
    result.prg_nvram_size = kNvramSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 37U +
                             (offset >> 16U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper369> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper369::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper369>(h, rom(kPrgSize, salt), rom(kChrSize, salt + 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_outer(Mapper369& mapper, Byte value) {
    mapper.write_expansion(0x4100U, value);
}

void select_mmc_bank(Mapper369& mapper, unsigned index, Byte value, Byte control = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | control));
    mapper.write_prg(0x8001U, value);
}

std::size_t prg_base(Byte outer) {
    switch (outer) {
    case 0x00U:
        return 0x00000UZ;
    case 0x01U:
        return 0x02000UZ;
    case 0x13U:
        return 0x10000UZ;
    case 0x37U:
        return 0x20000UZ;
    default:
        return 0x40000UZ;
    }
}

std::size_t chr_base(Byte outer) {
    switch (outer) {
    case 0x00U:
        return 0x00000UZ;
    case 0x01U:
        return 0x02000UZ;
    case 0x13U:
        return 0x06000UZ;
    case 0x37U:
        return 0x20000UZ;
    default:
        return 0x40000UZ;
    }
}

unsigned expected_mmc_prg_bank(Byte outer,
                               Byte select,
                               const std::array<Byte, 8>& banks,
                               Address addr) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned count = outer == 0x37U ? 16U : 32U;
    const unsigned mask = count - 1U;
    const bool swapped = (select & 0x40U) != 0U;
    switch (slot) {
    case 0U:
        return swapped ? count - 2U : banks[6U] & mask;
    case 1U:
        return banks[7U] & mask;
    case 2U:
        return swapped ? banks[6U] & mask : count - 2U;
    default:
        return count - 1U;
    }
}

unsigned expected_mmc_chr_bank(Byte select, const std::array<Byte, 8>& banks, Address addr) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (banks[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return banks[slot - 2U];
}

void clock_ntdec(Mapper369& mapper, unsigned cycles) {
    for (unsigned cycle = 0U; cycle < cycles; ++cycle) {
        mapper.clock_cpu();
    }
}

TEST_CASE("Mapper 369 validates exact NES 2.0 ROM NVRAM and CIRAM geometry", "[mapper369]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper369::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (const unsigned id : {0U, 4U, 40U, 367U, 368U, 370U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper369::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper369::validate_geometry(h, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper369::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x10000UZ, 0x40000UZ, 0x7FFFFUZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper369::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x10000UZ, 0x40000UZ, 0x7FFFFUZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper369::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper369::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = false;
            break;
        case 1U:
            h.prg_ram_size = kNvramSize;
            break;
        case 2U:
            h.prg_nvram_size = 0U;
            break;
        case 3U:
            h.prg_nvram_size = 0x4000U;
            break;
        case 4U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper369::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 369 decodes only C100 outer aliases and ignores unknown values",
          "[mapper369][bus]") {
    auto mapper = board();
    const auto actual_rom = rom(kPrgSize);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const bool decoded = (raw & 0xC100U) == 0x4100U;
        mapper->write_expansion(addr, 1U);
        CAPTURE(raw, decoded);
        REQUIRE(mapper->read_prg(0x8000U) == actual_rom[decoded ? 0x2000UZ : 0UZ]);
        REQUIRE(mapper->maps_expansion(addr) == (decoded && raw >= 0x4020U && raw < 0x6000U));
        REQUIRE(mapper->expansion_read_mask(addr) == 0U);
        REQUIRE(mapper->read_expansion(addr) == 0U);

        mapper->reset();
        mapper->write_prg(addr, 1U);
        REQUIRE(mapper->read_prg(0x8000U) == actual_rom[decoded ? 0x2000UZ : 0UZ]);
    }

    select_outer(*mapper, 0xFFU);
    const auto selected = state(*mapper);
    for (unsigned value = 0U; value < 256U; ++value) {
        if (value == 0U || value == 1U || value == 0x13U || value == 0x37U || value == 0xFFU) {
            continue;
        }
        select_outer(*mapper, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper) == selected);
    }
}

TEST_CASE("Mapper 369 NROM modes map every PRG CHR and open-bus cell", "[mapper369][banking]") {
    auto mapper = board(MirrorMode::Horizontal);
    const auto prg = rom(kPrgSize);
    const auto chr = rom(kChrSize, 0x35U);
    for (const Byte outer : {0U, 1U}) {
        select_outer(*mapper, outer);
        const auto initial = state(*mapper);
        for (Address addr = 0x8000U;; ++addr) {
            const auto offset = outer == 0U ? static_cast<std::size_t>(addr & 0x3FFFU)
                                            : 0x2000UZ + static_cast<std::size_t>(addr - 0x8000U);
            CAPTURE(outer, addr);
            REQUIRE(mapper->read_prg(addr) == prg[offset]);
            if (addr == 0xFFFFU) {
                break;
            }
        }
        mapper->write_prg(0x8123U, 0xA5U);
        REQUIRE(mapper->read_prg(0x8123U) == prg[outer == 0U ? 0x0123UZ : 0x2123UZ]);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            const auto offset = chr_base(outer) + addr;
            REQUIRE(mapper->read_chr(addr) == chr[offset]);
            mapper->write_chr(addr, static_cast<Byte>(~chr[offset]));
            REQUIRE(mapper->read_chr(addr) == chr[offset]);
            REQUIRE(mapper->read_chr(addr + 0x2000U) == chr[offset]);
        }
        for (const Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x6123U, 0x7FFFU}) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0U);
            REQUIRE(mapper->read_prg(addr) == 0U);
            mapper->write_prg(addr, 0xA5U);
        }
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(state(*mapper) == initial);
    }
}

TEST_CASE("Mapper 369 NTDEC mode maps every fixed selectable and ROM-backed low cell",
          "[mapper369][banking][ram]") {
    auto mapper = board();
    const auto prg = rom(kPrgSize);
    const auto chr = rom(kChrSize, 0x35U);
    select_outer(*mapper, 0x13U);
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(0xE123U, static_cast<Byte>(0xF8U | bank));
        const std::array<unsigned, 5> banks{6U, 4U, 5U, bank, 7U};
        const std::array<Address, 5> bases{0x6000U, 0x8000U, 0xA000U, 0xC000U, 0xE000U};
        for (unsigned window = 0U; window < bases.size(); ++window) {
            for (Address offset = 0U; offset < kPrgBankSize; ++offset) {
                const auto addr = static_cast<Address>(bases[window] + offset);
                CAPTURE(bank, window, offset);
                REQUIRE(mapper->read_prg(addr) ==
                        prg[0x10000UZ + banks[window] * kPrgBankSize + offset]);
            }
        }
        REQUIRE(state(*mapper)[kNtdecBankOffset] == bank);
    }
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        REQUIRE(mapper->read_chr(addr) == chr[0x6000UZ + addr]);
        mapper->write_chr(addr, 0xFFU);
        REQUIRE(mapper->read_chr(addr) == chr[0x6000UZ + addr]);
    }
    for (Address addr = 0xC000U; addr < 0xE000U; ++addr) {
        if ((addr & 0xC100U) == 0x4100U) {
            continue;
        }
        const auto before = state(*mapper);
        mapper->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE(state(*mapper) == before);
    }
    const auto low_before = mapper->read_prg(0x6123U);
    mapper->write_prg(0x6123U, static_cast<Byte>(~low_before));
    REQUIRE(mapper->read_prg(0x6123U) == low_before);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->maps_prg(0x7FFFU));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0xFFU);
}

TEST_CASE("Mapper 369 MMC3 PRG banking applies mode swap and 128/256 KiB limits",
          "[mapper369][banking]") {
    auto mapper = board();
    const auto prg = rom(kPrgSize);
    for (const Byte outer : {0x37U, 0xFFU}) {
        select_outer(*mapper, outer);
        for (const Byte control : {0U, 0x40U}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                std::array<Byte, 8> banks{};
                banks[6U] = static_cast<Byte>(value & 0x1FU);
                banks[7U] = static_cast<Byte>((~value) & 0x1FU);
                select_mmc_bank(*mapper, 6U, static_cast<Byte>(value), control);
                select_mmc_bank(*mapper, 7U, static_cast<Byte>(~value), control);
                REQUIRE(state(*mapper)[kMmcSelectOffset] == control + 7U);
                REQUIRE(state(*mapper)[kMmcBanksOffset + 6U] == banks[6U]);
                REQUIRE(state(*mapper)[kMmcBanksOffset + 7U] == banks[7U]);
                for (const Address base : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                    for (const Address offset : {0U, 1U, 0x123U, 0x1FFFU}) {
                        const auto addr = static_cast<Address>(base + offset);
                        const auto bank = expected_mmc_prg_bank(outer, control + 7U, banks, addr);
                        CAPTURE(outer, control, value, addr, bank);
                        REQUIRE(mapper->read_prg(addr) ==
                                prg[prg_base(outer) + bank * kPrgBankSize + offset]);
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 369 MMC3 CHR pairing inversion and outer offsets cover all selectors",
          "[mapper369][banking]") {
    auto mapper = board();
    const auto chr = rom(kChrSize, 0x35U);
    for (const Byte outer : {0x37U, 0xFFU}) {
        select_outer(*mapper, outer);
        for (const Byte control : {0U, 0x80U}) {
            for (unsigned index = 0U; index < 6U; ++index) {
                for (const Byte value : {0U, 1U, 2U, 0x7EU, 0x7FU, 0x80U, 0xFFU}) {
                    mapper->reset();
                    select_outer(*mapper, outer);
                    std::array<Byte, 8> banks{};
                    banks[index] = value;
                    select_mmc_bank(*mapper, index, value, control);
                    for (Address slot = 0U; slot < 8U; ++slot) {
                        for (const Address offset : {0U, 1U, 0x234U, 0x3FFU}) {
                            const auto addr = static_cast<Address>(slot * kChrBankSize + offset);
                            const auto bank = expected_mmc_chr_bank(control + index, banks, addr);
                            CAPTURE(outer, control, index, value, addr, bank);
                            REQUIRE(mapper->read_chr(addr) ==
                                    chr[chr_base(outer) + bank * kChrBankSize + offset]);
                        }
                    }
                }
            }
        }
    }
    const auto before = mapper->read_chr(0x1234U);
    mapper->write_chr(0x1234U, static_cast<Byte>(~before));
    REQUIRE(mapper->read_chr(0x1234U) == before);
}

TEST_CASE("Mapper 369 battery RAM gates persist across modes and ordinary CIRAM remains PPU-owned",
          "[mapper369][ram][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    select_outer(*mapper, 0xFFU);
    mapper->write_prg(0xA001U, 0x80U);
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        auto value = static_cast<Byte>(addr * 11U);
        if ((addr & 0xC100U) == 0x4100U &&
            (value == 0U || value == 1U || value == 0x13U || value == 0x37U || value == 0xFFU)) {
            value = 2U;
        }
        mapper->write_prg(addr, value);
        REQUIRE(mapper->read_prg(addr) == value);
        REQUIRE(mapper->battery_ram()[addr - 0x6000U] == value);
    }
    REQUIRE(mapper->battery_ram().size() == kNvramSize);
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    const auto disabled_value = mapper->battery_ram()[0x123U];
    mapper->write_prg(0x6123U, static_cast<Byte>(~disabled_value));
    REQUIRE(mapper->battery_ram()[0x123U] == disabled_value);

    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->maps_prg(0x7FFFU));
    REQUIRE(mapper->read_prg(0x6123U) == disabled_value);
    mapper->write_prg(0x6123U, static_cast<Byte>(~disabled_value));
    REQUIRE(mapper->read_prg(0x6123U) == disabled_value);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, static_cast<Byte>(~disabled_value));
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(~disabled_value));

    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto ciram = board(mirror);
        select_outer(*ciram, 0xFFU);
        REQUIRE(ciram->mirror_mode() == mirror);
        ciram->write_prg(0xA000U, 0U);
        REQUIRE(ciram->mirror_mode() == MirrorMode::Vertical);
        ciram->write_prg(0xA000U, 0xFFU);
        REQUIRE(ciram->mirror_mode() == MirrorMode::Horizontal);
        select_outer(*ciram, 0U);
        REQUIRE(ciram->mirror_mode() == mirror);
        select_outer(*ciram, 0x37U);
        REQUIRE(ciram->mirror_mode() == MirrorMode::Horizontal);
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(ciram->maps_nametable(addr));
            REQUIRE_FALSE(ciram->nametable_address(addr));
            REQUIRE_FALSE(ciram->ciram_address(addr));
            REQUIRE_FALSE(ciram->read_nametable(addr));
            REQUIRE_FALSE(ciram->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(ciram->unmirrored_nametables());
    }

    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(state(*mapper) == before);
    std::array<Byte, kNvramSize> imported{};
    for (std::size_t index = 0U; index < imported.size(); ++index) {
        imported[index] = cell(index, 0x91U);
    }
    mapper->set_battery_ram(imported);
    REQUIRE(std::equal(imported.begin(), imported.end(), mapper->battery_ram().begin()));
}

TEST_CASE("Mapper 369 MMC3 scanline IRQ reloads repeats acknowledges and hides while inactive",
          "[mapper369][irq]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned clock = 0U; clock < 1000U; ++clock) {
        mapper->clock_irq_counter();
    }
    REQUIRE(state(*mapper) == initial);

    select_outer(*mapper, 0xFFU);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xE001U, 0U);
    REQUIRE(state(*mapper)[kMmcLatchOffset] == 2U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kMmcCounterOffset] == 2U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kMmcCounterOffset] == 1U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kMmcCounterOffset] == 0U);
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kMmcCounterOffset] == 2U);
    REQUIRE(state(*mapper)[kMmcIrqOffset] == 2U);

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    REQUIRE(state(*mapper)[kMmcCounterOffset] == 0U);
    REQUIRE(state(*mapper)[kMmcIrqOffset] == 3U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kMmcIrqOffset] == 0U);

    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    select_outer(*mapper, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    const auto inactive = state(*mapper);
    mapper->clock_irq_counter();
    mapper->acknowledge_irq();
    REQUIRE(state(*mapper) == inactive);
    select_outer(*mapper, 0xFFU);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 369 NTDEC IRQ uses its 13-bit M2 counter and independent enable",
          "[mapper369][irq]") {
    auto mapper = board();
    select_outer(*mapper, 0x13U);
    clock_ntdec(*mapper, 10000U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xA123U, 0xFFU);
    for (unsigned cycle = 1U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        CAPTURE(cycle);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    for (unsigned cycle = 4097U; cycle < 8192U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0x8000U, 0U);
    REQUIRE(state(*mapper)[kNtdecCounterOffset] == 0U);
    REQUIRE(state(*mapper)[kNtdecCounterOffset + 1U] == 0U);
    REQUIRE(state(*mapper)[kNtdecEnabledOffset] == 0U);
    mapper->write_prg(0xBFFFU, 0U);
    clock_ntdec(*mapper, 1234U);
    select_outer(*mapper, 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    const auto inactive = state(*mapper);
    clock_ntdec(*mapper, 9000U);
    mapper->acknowledge_irq();
    REQUIRE(state(*mapper) == inactive);
    select_outer(*mapper, 0x13U);
    clock_ntdec(*mapper, 4096U - 1234U);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 369 reset restores all selected hardware while retaining battery and no audio",
          "[mapper369][reset][audio]") {
    auto mapper = board(MirrorMode::Horizontal);
    select_outer(*mapper, 0x13U);
    mapper->write_prg(0xE000U, 7U);
    mapper->write_prg(0xA000U, 0U);
    clock_ntdec(*mapper, 123U);
    select_outer(*mapper, 0xFFU);
    select_mmc_bank(*mapper, 6U, 31U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    auto fresh = board(MirrorMode::Horizontal);
    fresh->set_battery_ram(mapper->battery_ram());
    mapper->reset();
    REQUIRE(state(*mapper) == state(*fresh));
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->battery_ram()[0x123U] == 0xA5U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());

    select_outer(*mapper, 0xFFU);
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->power_on();
    fresh->set_battery_ram(mapper->battery_ram());
    REQUIRE(state(*mapper) == state(*fresh));
    REQUIRE(mapper->battery_ram()[0x123U] == 0x5AU);

    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->observe_cpu_read(0x4100U, 0xFFU);
    mapper->observe_cpu_write(0x4100U, 0xFFU);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_cpu_falling_edge();
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 369 fixed bounded snapshots replay every device without ROM",
          "[mapper369][state]") {
    auto mapper = board();
    select_outer(*mapper, 0x13U);
    mapper->write_prg(0xE000U, 6U);
    mapper->write_prg(0xA000U, 0U);
    clock_ntdec(*mapper, 1234U);
    select_outer(*mapper, 0xFFU);
    select_mmc_bank(*mapper, 0U, 0xFEU, 0xC0U);
    select_mmc_bank(*mapper, 6U, 27U);
    select_mmc_bank(*mapper, 7U, 19U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0x80U);
    std::array<Byte, kNvramSize> nvram{};
    for (std::size_t index = 0U; index < nvram.size(); ++index) {
        nvram[index] = cell(index, 0x72U);
    }
    mapper->set_battery_ram(nvram);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == kNvramOffset + kNvramSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved.size() < kChrSize);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(copy->irq_pending() == mapper->irq_pending());
    for (const Address addr : {0x6000U, 0x6123U, 0x7FFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    std::array<Byte, 8> banks{};
    banks[0U] = 0xFEU;
    banks[6U] = 27U;
    banks[7U] = 19U;
    for (const Address addr : {0x8000U, 0xA123U, 0xC123U, 0xFFFFU}) {
        const auto bank = expected_mmc_prg_bank(0xFFU, 7U, banks, addr);
        CAPTURE(addr, bank);
        REQUIRE(copy->read_prg(addr) ==
                cell(0x40000UZ + bank * kPrgBankSize + (addr & 0x1FFFU), 0x80U));
    }
    for (const Address addr : {0U, 0x123U, 0x800U, 0x1000U, 0x1FFFU}) {
        const auto bank = expected_mmc_chr_bank(7U, banks, addr);
        CAPTURE(addr, bank);
        REQUIRE(copy->read_chr(addr) ==
                cell(0x40000UZ + bank * kChrBankSize + (addr & 0x03FFU), 0xB5U));
    }
    mapper->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(state(*copy) == state(*mapper));

    select_outer(*mapper, 0x13U);
    select_outer(*copy, 0x13U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        mapper->clock_cpu();
        copy->clock_cpu();
    }
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 369 rejects malformed and incompatible states atomically", "[mapper369][state]") {
    auto mapper = board();
    select_outer(*mapper, 0x13U);
    mapper->write_prg(0xE000U, 7U);
    mapper->write_prg(0xA000U, 0U);
    clock_ntdec(*mapper, 1234U);
    select_outer(*mapper, 0xFFU);
    select_mmc_bank(*mapper, 6U, 31U, 0xC0U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    const auto replace = [&](std::size_t offset, Byte value) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
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
    for (const Byte value : {2U, 3U, 0x12U, 0x14U, 0x36U, 0x38U, 0xFEU}) {
        replace(kOuterOffset, value);
    }
    for (const Byte value : {8U, 0x80U, 0xFFU}) {
        replace(kNtdecBankOffset, value);
    }
    for (const std::uint16_t counter : {0x2000U, 0x2345U, 0xFFFFU}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kNtdecCounterOffset, &counter, sizeof(counter));
        CAPTURE(counter);
        reject(invalid);
    }
    for (const Byte value : {2U, 0x80U, 0xFFU}) {
        replace(kNtdecEnabledOffset, value);
    }
    auto disabled_with_count = saved;
    disabled_with_count[kNtdecEnabledOffset] = 0U;
    reject(disabled_with_count);
    for (const Byte value : {8U, 0x38U, 0xFFU}) {
        replace(kMmcSelectOffset, value);
    }
    for (const Byte value : {0x20U, 0x40U, 0xFFU}) {
        replace(kMmcBanksOffset + 6U, value);
        replace(kMmcBanksOffset + 7U, value);
    }
    for (const Byte value : {2U, 3U, 0x80U, 0xFFU}) {
        replace(kMmcMirrorOffset, value);
    }
    for (const Byte value : {4U, 5U, 8U, 0xFFU}) {
        replace(kMmcIrqOffset, value);
    }
    for (const Byte value : {1U, 2U, 0x3FU, 0x7FU, 0xFFU}) {
        replace(kRamFlagsOffset, value);
    }

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before);
}

} // namespace
