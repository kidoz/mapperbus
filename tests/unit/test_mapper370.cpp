#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper370.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMaxRomSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kRamSize = 0x2000UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kSelectOffset = 13UZ;
constexpr std::size_t kBanksOffset = 14UZ;
constexpr std::size_t kMirrorOffset = 22UZ;
constexpr std::size_t kIrqCounterOffset = 23UZ;
constexpr std::size_t kIrqLatchOffset = 24UZ;
constexpr std::size_t kIrqFlagsOffset = 25UZ;
constexpr std::size_t kRamFlagsOffset = 26UZ;
constexpr std::size_t kRamLengthOffset = 27UZ;
constexpr std::size_t kRamOffset = 35UZ;

enum class RamKind { None, Volatile, Nvram };

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, RamKind ram = RamKind::None) {
    INesHeader result{};
    result.mapper_number = 370U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = ram == RamKind::Volatile ? kRamSize : 0U;
    result.prg_nvram_size = ram == RamKind::Nvram ? kRamSize : 0U;
    result.has_battery = ram == RamKind::Nvram;
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

std::unique_ptr<Mapper370> board(std::size_t prg_size = kMaxRomSize,
                                 std::size_t chr_size = kMaxRomSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 RamKind ram = RamKind::None,
                                 Byte solder_pad = 0U,
                                 Byte salt = 0U) {
    const auto h = header(mirror, ram);
    REQUIRE(Mapper370::validate_geometry(h, prg_size, chr_size));
    auto mapper = std::make_unique<Mapper370>(h, rom(prg_size, salt), rom(chr_size, salt + 0x55U));
    REQUIRE(mapper->configure_board_jumpers(solder_pad));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_outer(Mapper370& mapper, Byte outer) {
    mapper.write_expansion(0x5000U, outer);
}

void select_bank(Mapper370& mapper, Byte index, Byte value, Byte control = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | control));
    mapper.write_prg(0x8001U, value);
}

unsigned expected_prg_bank(Byte outer,
                           Byte select,
                           const std::array<Byte, 8>& banks,
                           Address addr) {
    const unsigned count = (outer & 0x20U) != 0U ? 32U : 16U;
    const unsigned mask = count - 1U;
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
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

unsigned expected_chr_bank(Byte select, const std::array<Byte, 8>& banks, Address addr) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((select & 0x80U) != 0U ? 4U : 0U);
    return slot < 4U ? (banks[slot / 2U] & 0xFEU) | (slot & 1U) : banks[slot - 2U];
}

std::size_t expected_prg_offset(Byte outer,
                                Byte select,
                                const std::array<Byte, 8>& banks,
                                Address addr) {
    const unsigned inner_mask = (outer & 0x20U) != 0U ? 0x1FU : 0x0FU;
    const unsigned outer_bank = (outer >> 3U) & 3U;
    const unsigned bank = expected_prg_bank(outer, select, banks, addr);
    return (((outer_bank << 4U) | (bank & inner_mask)) * kPrgBankSize) + (addr & 0x1FFFU);
}

std::size_t expected_chr_offset(Byte outer,
                                Byte select,
                                const std::array<Byte, 8>& banks,
                                Address addr) {
    const unsigned inner_mask = (outer & 4U) != 0U ? 0xFFU : 0x7FU;
    const unsigned bank = expected_chr_bank(select, banks, addr);
    return ((((outer & 3U) << 7U) | (bank & inner_mask)) * kChrBankSize) + (addr & 0x03FFU);
}

TEST_CASE("Mapper 370 validates ROM populations RAM declarations and CIRAM metadata",
          "[mapper370]") {
    for (const std::size_t prg_size :
         {0x008000UZ, 0x010000UZ, 0x018000UZ, 0x020000UZ, 0x048000UZ, 0x080000UZ}) {
        for (const std::size_t chr_size :
             {0x002000UZ, 0x003000UZ, 0x020000UZ, 0x038000UZ, 0x080000UZ}) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                for (const auto ram : {RamKind::None, RamKind::Volatile, RamKind::Nvram}) {
                    CAPTURE(prg_size, chr_size, mirror, ram);
                    REQUIRE(Mapper370::validate_geometry(header(mirror, ram), prg_size, chr_size));
                }
            }
        }
    }
    for (const unsigned id : {0U, 4U, 118U, 367U, 369U, 371U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper370::validate_geometry(h, kMaxRomSize, kMaxRomSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper370::validate_geometry(h, kMaxRomSize, kMaxRomSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper370::validate_geometry(h, kMaxRomSize, kMaxRomSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x001FFFUZ, 0x004000UZ, 0x007FFFUZ, 0x008001UZ, 0x080001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper370::validate_geometry(header(), size, kMaxRomSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x000400UZ, 0x001FFFUZ, 0x002001UZ, 0x080001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper370::validate_geometry(header(), kMaxRomSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper370::validate_geometry(header(mirror), kMaxRomSize, kMaxRomSize));
    }
    for (unsigned field = 0U; field < 10U; ++field) {
        h = header(MirrorMode::Vertical, RamKind::Nvram);
        switch (field) {
        case 0U:
            h.has_battery = false;
            break;
        case 1U:
            h.prg_ram_size = kRamSize;
            break;
        case 2U:
            h.prg_nvram_size = 0U;
            break;
        case 3U:
            h.prg_nvram_size = 0x1000U;
            break;
        case 4U:
            h.prg_nvram_size = 0x4000U;
            break;
        case 5U:
            h.prg_ram_size = 0x1000U;
            h.prg_nvram_size = 0U;
            h.has_battery = false;
            break;
        case 6U:
            h.prg_ram_size = kRamSize;
            h.prg_nvram_size = 0U;
            h.has_battery = true;
            break;
        case 7U:
            h.prg_ram_size = 0x4000U;
            h.prg_nvram_size = 0U;
            h.has_battery = false;
            break;
        case 8U:
            h.chr_ram_size = kRamSize;
            break;
        default:
            h.chr_nvram_size = kRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper370::validate_geometry(h, kMaxRomSize, kMaxRomSize));
    }
}

TEST_CASE("Mapper 370 decodes F000 outer aliases and drives only the solder pad on D7",
          "[mapper370][bus]") {
    auto mapper = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, RamKind::None, 1U);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const bool decoded = (raw & 0xF000U) == 0x5000U;
        mapper->reset();
        mapper->write_expansion(addr, 0xAAU);
        CAPTURE(raw, decoded);
        REQUIRE(state(*mapper)[kOuterOffset] == (decoded ? 0x2AU : 0U));
        REQUIRE(mapper->maps_expansion(addr) == decoded);
        REQUIRE(mapper->expansion_read_mask(addr) == (decoded ? 0x80U : 0U));
        REQUIRE(mapper->read_expansion(addr) == (decoded ? 0x80U : 0U));
    }

    select_outer(*mapper, 0U);
    mapper->write_prg(0x5000U, 0x3FU);
    mapper->write_prg(0x5FFFU, 0x3FU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    for (const Byte value : {0U, 1U}) {
        REQUIRE(mapper->configure_board_jumpers(value));
        REQUIRE(mapper->read_expansion(0x5000U) == static_cast<Byte>(value << 7U));
        REQUIRE(mapper->read_expansion(0x5FFFU) == static_cast<Byte>(value << 7U));
    }
    const auto before = state(*mapper);
    for (const Byte value : {2U, 3U, 0x7FU, 0x80U, 0xFFU}) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(value));
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 370 PRG banking combines outer pins inner masks and both MMC3 modes",
          "[mapper370][banking]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 64U; ++outer) {
        select_outer(*mapper, static_cast<Byte>(outer | 0xC0U));
        REQUIRE(state(*mapper)[kOuterOffset] == outer);
        for (const Byte control : {0U, 0x40U}) {
            std::array<Byte, 8> banks{};
            banks[6U] = static_cast<Byte>(outer * 11U + control + 0x40U);
            banks[7U] = static_cast<Byte>(~banks[6U] + 0x80U);
            select_bank(*mapper, 6U, banks[6U], control);
            select_bank(*mapper, 7U, banks[7U], control);
            banks[6U] &= 0x3FU;
            banks[7U] &= 0x3FU;
            const auto select = static_cast<Byte>(control | 7U);
            REQUIRE(state(*mapper)[kSelectOffset] == select);
            for (const Address base : {0x8000U, 0xA000U, 0xC000U, 0xE000U}) {
                for (const Address offset : {0U, 1U, 0x123U, 0x1FFFU}) {
                    const auto addr = static_cast<Address>(base + offset);
                    const auto expected = expected_prg_offset(outer, select, banks, addr);
                    CAPTURE(outer, control, addr, expected);
                    REQUIRE(mapper->read_prg(addr) == cell(expected % kMaxRomSize));
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
            }
        }
    }
    const auto rom_value = mapper->read_prg(0x8123U);
    mapper->write_prg(0x8123U, static_cast<Byte>(~rom_value));
    REQUIRE(mapper->read_prg(0x8123U) == rom_value);
}

TEST_CASE("Mapper 370 CHR banking applies pairing inversion masks and outer pins",
          "[mapper370][banking]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 64U; ++outer) {
        select_outer(*mapper, outer);
        for (const Byte control : {0U, 0x80U}) {
            std::array<Byte, 8> banks{};
            for (Byte index = 0U; index < 6U; ++index) {
                banks[index] = static_cast<Byte>(outer * 17U + index * 29U + control);
                select_bank(*mapper, index, banks[index], control);
            }
            const auto select = static_cast<Byte>(control | 5U);
            for (Address slot = 0U; slot < 8U; ++slot) {
                for (const Address offset : {0U, 1U, 0x234U, 0x3FFU}) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + offset);
                    const auto expected = expected_chr_offset(outer, select, banks, addr);
                    CAPTURE(outer, control, addr, expected);
                    REQUIRE(mapper->read_chr(addr) == cell(expected % kMaxRomSize, 0x55U));
                }
            }
        }
    }
    const auto rom_value = mapper->read_chr(0x1234U);
    mapper->write_chr(0x1234U, static_cast<Byte>(~rom_value));
    REQUIRE(mapper->read_chr(0x1234U) == rom_value);
}

TEST_CASE("Mapper 370 exposes only declared RAM and implements MMC3 RAM gates",
          "[mapper370][bus][ram]") {
    auto no_ram = board();
    const auto no_ram_before = state(*no_ram);
    for (const Address addr : {0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU, 0x6000U, 0x6123U, 0x7FFFU}) {
        CAPTURE(addr);
        REQUIRE(no_ram->maps_prg(addr) == (addr >= 0x8000U));
        REQUIRE(no_ram->prg_read_mask(addr) == 0U);
        REQUIRE(no_ram->read_prg(addr) == 0U);
        no_ram->write_prg(addr, 0xA5U);
    }
    REQUIRE(state(*no_ram) == no_ram_before);
    REQUIRE(no_ram->battery_ram().empty());

    for (const auto kind : {RamKind::Volatile, RamKind::Nvram}) {
        auto mapper = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, kind);
        for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
            const auto value = cell(addr, kind == RamKind::Nvram ? 0x91U : 0x37U);
            mapper->write_prg(addr, value);
            REQUIRE(mapper->read_prg(addr) == value);
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
        mapper->write_prg(0xA001U, 0U);
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->prg_read_mask(0x7FFFU) == 0U);
        REQUIRE(mapper->read_prg(0x6123U) == 0U);
        const auto retained = mapper->read_prg(0x6123U);
        mapper->write_prg(0xA001U, 0x80U);
        const auto stored = mapper->read_prg(0x6123U);
        REQUIRE(stored != retained);
        mapper->write_prg(0xA001U, 0U);
        mapper->write_prg(0x6123U, static_cast<Byte>(~stored));
        mapper->write_prg(0xA001U, 0xC0U);
        REQUIRE(mapper->read_prg(0x6123U) == stored);
        mapper->write_prg(0x6123U, static_cast<Byte>(~stored));
        REQUIRE(mapper->read_prg(0x6123U) == stored);
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(0x6123U, static_cast<Byte>(~stored));
        REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(~stored));

        if (kind == RamKind::Nvram) {
            REQUIRE(mapper->battery_ram().size() == kRamSize);
            const auto before = state(*mapper);
            mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
            REQUIRE(state(*mapper) == before);
            std::array<Byte, kRamSize> imported{};
            for (std::size_t index = 0U; index < imported.size(); ++index) {
                imported[index] = cell(index, 0xA6U);
            }
            mapper->set_battery_ram(imported);
            REQUIRE(std::equal(imported.begin(), imported.end(), mapper->battery_ram().begin()));
        } else {
            REQUIRE(mapper->battery_ram().empty());
        }
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
    }
}

TEST_CASE("Mapper 370 uses normal MMC3 CIRAM except for all TLSROM arrangements",
          "[mapper370][ciram]") {
    auto mapper = board(kMaxRomSize, kMaxRomSize, MirrorMode::Horizontal);
    for (Byte outer = 0U; outer < 64U; ++outer) {
        select_outer(*mapper, outer);
        mapper->write_prg(0xA000U, 0U);
        if ((outer & 7U) != 1U) {
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE_FALSE(mapper->nametable_address(0x2000U));
            mapper->write_prg(0xA000U, 1U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        }
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
    }

    for (unsigned arrangement = 0U; arrangement < 16U; ++arrangement) {
        for (const Byte control : {0U, 0x80U}) {
            mapper->reset();
            select_outer(*mapper, 1U);
            std::array<Byte, 8> banks{};
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const bool high = (arrangement & (1U << slot)) != 0U;
                const auto value = static_cast<Byte>(high ? 0x80U | slot : slot);
                banks[slot / 2U] |= static_cast<Byte>(value & 0xFEU);
                select_bank(*mapper, static_cast<Byte>(slot / 2U), banks[slot / 2U], control);
                banks[slot / 2U] |= static_cast<Byte>(value & 1U);
            }
            select_bank(*mapper, 2U, 0U, control);
            const auto select = static_cast<Byte>(control | 2U);
            mapper->write_prg(0xA000U, arrangement & 1U);
            std::array<unsigned, 4> pins{};
            for (unsigned table = 0U; table < 4U; ++table) {
                const auto addr = static_cast<Address>(table * 0x400U);
                pins[table] = (expected_chr_bank(select, banks, addr) & 0x80U) >> 7U;
                for (const Address offset : {0U, 1U, 0x123U, 0x3FFU}) {
                    for (const Address base : {0x2000U, 0x3000U}) {
                        const auto nt = static_cast<Address>(base + addr + offset);
                        const auto expected = static_cast<Address>(pins[table] * 0x400U + offset);
                        CAPTURE(arrangement, control, nt, expected);
                        REQUIRE(mapper->nametable_address(nt) == expected);
                    }
                }
            }
            MirrorMode expected_shape = MirrorMode::Vertical;
            if (pins[0] == pins[1] && pins[1] == pins[2] && pins[2] == pins[3]) {
                expected_shape = pins[0] != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
            } else if (pins[0] == pins[1] && pins[2] == pins[3]) {
                expected_shape = MirrorMode::Horizontal;
            }
            REQUIRE(mapper->mirror_mode() == expected_shape);
        }
    }
}

TEST_CASE("Mapper 370 MMC3 IRQ reloads repeats acknowledges and disables", "[mapper370][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xE001U, 0U);
    REQUIRE(state(*mapper)[kIrqLatchOffset] == 2U);
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 2U);
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 2U);

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 3U);
    select_outer(*mapper, 1U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 370 reset restores registers retains RAM and has no audio or extra IRQ clocks",
          "[mapper370][reset][audio]") {
    auto mapper = board(kMaxRomSize, kMaxRomSize, MirrorMode::Horizontal, RamKind::Nvram, 1U);
    mapper->write_prg(0xA000U, 0U);
    select_outer(*mapper, 1U);
    select_bank(*mapper, 6U, 0x3FU, 0xC0U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    auto fresh = board(kMaxRomSize, kMaxRomSize, MirrorMode::Horizontal, RamKind::Nvram, 1U);
    fresh->set_battery_ram(mapper->battery_ram());
    mapper->reset();
    REQUIRE(state(*mapper) == state(*fresh));
    REQUIRE(mapper->battery_ram()[0x123U] == 0xA5U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_expansion(0x5000U) == 0x80U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xA001U, 0x80U);
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
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->set_region(Region::PAL);
    mapper->observe_cpu_read(0x5000U, 0x80U);
    mapper->observe_cpu_write(0x5000U, 0x3FU);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->audio_output() == 0.0F);

    auto volatile_ram = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, RamKind::Volatile);
    volatile_ram->write_prg(0x6123U, 0xA5U);
    volatile_ram->reset();
    REQUIRE(volatile_ram->read_prg(0x6123U) == 0xA5U);
    volatile_ram->power_on();
    REQUIRE(volatile_ram->read_prg(0x6123U) == 0U);
}

TEST_CASE("Mapper 370 bounded atomic snapshots replay registers and RAM without ROM",
          "[mapper370][state]") {
    for (const auto kind : {RamKind::None, RamKind::Volatile, RamKind::Nvram}) {
        auto mapper = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, kind, 1U);
        select_outer(*mapper, 0x3DU);
        select_bank(*mapper, 0U, 0xFEU, 0xC0U);
        select_bank(*mapper, 6U, 0x35U);
        select_bank(*mapper, 7U, 0x13U);
        mapper->write_prg(0xA001U, 0x80U);
        if (kind != RamKind::None) {
            mapper->write_prg(0x6123U, 0xA7U);
        }
        mapper->write_prg(0xC000U, 3U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        const auto saved = state(*mapper);
        const auto ram_size = kind == RamKind::None ? 0UZ : kRamSize;
        REQUIRE(saved.size() == kRamOffset + ram_size);
        REQUIRE(saved.size() < kMaxRomSize);

        auto copy = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, kind, 1U, 0x80U);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->irq_pending() == mapper->irq_pending());
        if (kind != RamKind::None) {
            REQUIRE(copy->read_prg(0x6123U) == 0xA7U);
        }
        std::array<Byte, 8> banks{};
        banks[0U] = 0xFEU;
        banks[6U] = 0x35U;
        banks[7U] = 0x13U;
        for (const Address addr : {0x8000U, 0xA123U, 0xC123U, 0xFFFFU}) {
            const auto expected = expected_prg_offset(0x3DU, 7U, banks, addr) % kMaxRomSize;
            CAPTURE(kind, addr, expected);
            REQUIRE(copy->read_prg(addr) == cell(expected, 0x80U));
        }
        for (const Address addr : {0U, 0x123U, 0x800U, 0x1000U, 0x1FFFU}) {
            const auto expected = expected_chr_offset(0x3DU, 7U, banks, addr) % kMaxRomSize;
            CAPTURE(kind, addr, expected);
            REQUIRE(copy->read_chr(addr) == cell(expected, 0xD5U));
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
    }
}

TEST_CASE("Mapper 370 rejects truncated malformed and incompatible states without mutation",
          "[mapper370][state]") {
    auto mapper = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, RamKind::Nvram, 1U);
    select_outer(*mapper, 0x3DU);
    select_bank(*mapper, 6U, 0x3FU, 0xC0U);
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
    for (const Byte value : {0x40U, 0x80U, 0xFFU}) {
        replace(kOuterOffset, value);
    }
    for (const Byte value : {8U, 0x38U, 0xFFU}) {
        replace(kSelectOffset, value);
    }
    for (const Byte value : {0x40U, 0x80U, 0xFFU}) {
        replace(kBanksOffset + 6U, value);
        replace(kBanksOffset + 7U, value);
    }
    for (const Byte value : {2U, 3U, 0x80U, 0xFFU}) {
        replace(kMirrorOffset, value);
    }
    for (const Byte value : {4U, 5U, 8U, 0xFFU}) {
        replace(kIrqFlagsOffset, value);
    }
    for (const Byte value : {1U, 2U, 0x3FU, 0x7FU, 0xFFU}) {
        replace(kRamFlagsOffset, value);
    }
    for (const std::uint64_t length :
         {0UZ, 1UZ, kRamSize - 1U, kRamSize + 1U, 0x10000UZ, 0xFFFFFFFFFFFFFFFFUZ}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kRamLengthOffset, &length, sizeof(length));
        CAPTURE(length);
        reject(invalid);
    }

    const auto incompatible = [&](std::unique_ptr<Mapper370>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_mirror = board(kMaxRomSize, kMaxRomSize, MirrorMode::Horizontal, RamKind::Nvram, 1U);
    auto wrong_pad = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, RamKind::Nvram, 0U);
    auto wrong_ram = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, RamKind::Volatile, 1U);
    auto no_ram = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, RamKind::None, 1U);
    auto wrong_prg = board(0x040000UZ, kMaxRomSize, MirrorMode::Vertical, RamKind::Nvram, 1U);
    auto wrong_chr = board(kMaxRomSize, 0x040000UZ, MirrorMode::Vertical, RamKind::Nvram, 1U);
    incompatible(wrong_mirror);
    incompatible(wrong_pad);
    incompatible(wrong_ram);
    incompatible(no_ram);
    incompatible(wrong_prg);
    incompatible(wrong_chr);
}

} // namespace
