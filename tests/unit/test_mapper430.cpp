#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper430.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kStateSize = 26UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kSelectOffset = 11UZ;
constexpr std::size_t kModeOffset = 12UZ;
constexpr std::size_t kBanksOffset = 13UZ;
constexpr std::size_t kMirrorOffset = 21UZ;
constexpr std::size_t kCounterOffset = 22UZ;
constexpr std::size_t kReloadOffset = 23UZ;
constexpr std::size_t kIrqOffset = 24UZ;
constexpr std::size_t kRamFlagsOffset = 25UZ;
constexpr std::size_t kWramOffset = 26UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

enum class RamKind { None, Volatile, Nonvolatile };

INesHeader header(RamKind ram = RamKind::None, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 430U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = ram == RamKind::Nonvolatile;
    result.prg_ram_size = ram == RamKind::Volatile ? Mapper430::kWramSize : 0U;
    result.prg_nvram_size = ram == RamKind::Nonvolatile ? Mapper430::kWramSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper430> board(std::size_t prg_size = 0x20000UZ,
                                 std::size_t chr_size = 0x20000UZ,
                                 RamKind ram = RamKind::None,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(ram, mirror);
    REQUIRE(Mapper430::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper430>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x35U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper430& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper430& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper430& mapper, Byte value) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(0x6000U, value);
}

unsigned mmc3_prg_bank(Address addr, Byte r6, Byte r7, Byte mode, Byte outer_bank) {
    unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if ((outer_bank & 8U) != 0U) {
        slot &= 1U;
    }
    return slot == 1U                               ? r7 & 0x3FU
           : slot == 3U                             ? 15U
           : (slot == 0U) == ((mode & 0x40U) != 0U) ? 14U
                                                    : r6 & 0x3FU;
}

unsigned expected_prg_bank(Address addr, Byte r6, Byte r7, Byte mode, Byte outer_bank) {
    return (mmc3_prg_bank(addr, r6, r7, mode, outer_bank) & 0x0FU) | ((outer_bank & 3U) << 4U);
}

Byte raw_chr_bank(Address addr, const std::array<Byte, 8>& banks, Byte mode) {
    const unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    return slot < 4U ? static_cast<Byte>((banks[slot / 2U] & 0xFEU) | (slot & 1U))
                     : banks[slot - 2U];
}

unsigned expected_chr_bank(Address addr,
                           const std::array<Byte, 8>& banks,
                           Byte mode,
                           Byte outer_bank) {
    const unsigned inner = raw_chr_bank(addr, banks, mode);
    const unsigned a17 = (outer_bank & 4U) != 0U ? outer_bank & 1U : (inner >> 7U) & 1U;
    return (inner & 0x7FU) | (a17 << 7U) | (((outer_bank >> 1U) & 1U) << 8U);
}

TEST_CASE("Mapper 430 validates ROM WRAM and CIRAM geometry and bounded reset state",
          "[mapper430][geometry][ram][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const auto ram : {RamKind::None, RamKind::Volatile, RamKind::Nonvolatile}) {
            auto mapper = board(kMinPrgSize, kMinChrSize, ram, mirror);
            CAPTURE(ram, mirror);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(state(*mapper).size() ==
                    kStateSize + (ram == RamKind::None ? 0U : Mapper430::kWramSize));
            REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
            REQUIRE(mapper->read_prg(0xC000U) == cell(2UZ * kPrgBankSize));
            REQUIRE(mapper->read_prg(0xE000U) == cell(3UZ * kPrgBankSize));
            REQUIRE(mapper->battery_ram().size() ==
                    (ram == RamKind::Nonvolatile ? Mapper430::kWramSize : 0U));
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            auto other_rom = board(kMinPrgSize, kMinChrSize, ram, mirror, 0x80U);
            REQUIRE(state(*other_rom) == state(*mapper));
        }
    }
    for (const std::size_t prg : {kMinPrgSize, 0xA000UZ, 0x18000UZ, kMaxPrgSize}) {
        for (const std::size_t chr : {kMinChrSize, 0x2400UZ, 0x6000UZ, kMaxChrSize}) {
            for (const auto ram : {RamKind::None, RamKind::Volatile, RamKind::Nonvolatile}) {
                CAPTURE(prg, chr, ram);
                REQUIRE(Mapper430::validate_geometry(header(ram), prg, chr));
            }
        }
    }

    const auto reject = [](INesHeader h, std::size_t prg, std::size_t chr) {
        CAPTURE(h.mapper_number,
                h.is_nes2,
                h.submapper,
                h.has_battery,
                h.has_trainer,
                h.prg_ram_size,
                h.prg_nvram_size,
                h.chr_ram_size,
                h.chr_nvram_size,
                h.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper430::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 45U, 256U, 429U, 431U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, kMinChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, kMinChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, kMaxPrgSize + kPrgBankSize}) {
        reject(header(), size, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x400UZ, 0x1FFFUZ, 0x2001UZ, kMaxChrSize + kChrBankSize}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(RamKind::None, mirror), kMinPrgSize, kMinChrSize);
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x1000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x1000U;
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = Mapper430::kWramSize;
            h.prg_nvram_size = Mapper430::kWramSize;
            h.has_battery = true;
            break;
        case 4U:
            h.prg_nvram_size = Mapper430::kWramSize;
            break;
        case 5U:
            h.prg_ram_size = Mapper430::kWramSize;
            h.has_battery = true;
            break;
        case 6U:
            h.chr_ram_size = 0x2000U;
            break;
        case 7U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        reject(h, kMinPrgSize, kMinChrSize);
    }
}

TEST_CASE("Mapper 430 decodes every outer alias under WRAM gates and optional RAM",
          "[mapper430][decoding][bus][ram]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0x4020U; raw < 0x8000U; ++raw) {
        mapper->reset();
        mapper->write_prg(0xA001U, 0x80U);
        const auto addr = static_cast<Address>(raw);
        auto wanted = initial;
        mapper->write_prg(addr, 0xA5U);
        if (raw >= 0x6000U) {
            wanted[kOuterOffset] = 5U;
        }
        CAPTURE(raw);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xFFU);
        REQUIRE(state(*mapper) == wanted);
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        outer(*mapper, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x0FU));
    }
    for (const Byte control : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
        auto ram = board(0x20000UZ, 0x20000UZ, RamKind::Volatile);
        outer(*ram, 7U);
        ram->write_prg(0x6123U, 0x5AU);
        ram->write_prg(0xA001U, control);
        ram->write_prg(0x6000U, 0xA5U);
        ram->write_prg(0x6123U, 0xA6U);
        const auto bytes = state(*ram);
        CAPTURE(control);
        REQUIRE(bytes[kOuterOffset] == (control == 0x80U ? 6U : 10U));
        REQUIRE(bytes[kWramOffset + 0x123U] == (control == 0x80U ? 0xA6U : 0x5AU));
        REQUIRE(ram->read_prg(0x6123U) ==
                ((control & 0x80U) != 0U ? bytes[kWramOffset + 0x123U] : 0U));
        REQUIRE(ram->maps_prg(0x6123U) == ((control & 0x80U) != 0U));
        REQUIRE(ram->prg_read_mask(0x6123U) == ((control & 0x80U) != 0U ? 0xFFU : 0U));
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->write_ppu_register(0x2012U, 0xA5U));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2012U, 0xA7U));
}

TEST_CASE("Mapper 430 composes MMC3 outer PRG pins and GNROM A14 before wrapping",
          "[mapper430][decoding][banking][bus]") {
    auto mapper = board(kMaxPrgSize, kMinChrSize);
    for (Byte outer_bank = 0U; outer_bank < 16U; ++outer_bank) {
        outer(*mapper, outer_bank);
        for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto r6 = static_cast<Byte>(raw);
                const auto r7 = static_cast<Byte>(raw ^ 0x35U);
                bank(*mapper, 6U, r6, mode);
                bank(*mapper, 7U, r7, mode);
                for (const auto addr : kPrgAddresses) {
                    const auto selected = expected_prg_bank(addr, r6, r7, mode, outer_bank);
                    const auto physical = selected * kPrgBankSize + (addr & (kPrgBankSize - 1U));
                    CAPTURE(outer_bank, mode, raw, addr, selected, physical);
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0xA000UZ, 0x18000UZ}) {
        auto fitted = board(size, kMinChrSize);
        outer(*fitted, 0x0FU);
        bank(*fitted, 6U, 0x2AU);
        for (const auto addr : kPrgAddresses) {
            const auto selected = expected_prg_bank(addr, 0x2AU, 0U, 0U, 0x0FU);
            const auto physical = (selected * kPrgBankSize + (addr & (kPrgBankSize - 1U))) % size;
            CAPTURE(size, addr, selected, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 430 applies CHR pairing inversion and outer A17/A18 selection",
          "[mapper430][decoding][banking][chr]") {
    auto mapper = board(kMinPrgSize, kMaxChrSize);
    for (Byte outer_bank = 0U; outer_bank < 16U; ++outer_bank) {
        outer(*mapper, outer_bank);
        for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                std::array<Byte, 8> banks{};
                for (Byte index = 0U; index < 6U; ++index) {
                    banks[index] = static_cast<Byte>(raw + index * 37U);
                    bank(*mapper, index, banks[index], mode);
                }
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
                    const auto selected = expected_chr_bank(addr, banks, mode, outer_bank);
                    const auto physical = selected * kChrBankSize + 0x123U;
                    CAPTURE(outer_bank, mode, raw, slot, selected, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                    mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                }
            }
        }
    }

    for (const std::size_t size : {kMinChrSize, 0x6000UZ}) {
        auto fitted = board(kMinPrgSize, size);
        outer(*fitted, 7U);
        bank(*fitted, 2U, 0xB4U);
        const auto selected = expected_chr_bank(0x1123U, {0U, 0U, 0xB4U}, 0U, 7U);
        const auto physical = (selected * kChrBankSize + 0x123U) % size;
        CAPTURE(size, selected, physical);
        REQUIRE(fitted->read_chr(0x1123U) == cell(physical, 0x35U));
    }
}

TEST_CASE("Mapper 430 decodes every MMC3 alias and preserves ordinary CIRAM routing",
          "[mapper430][decoding][ciram][irq]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        auto wanted = initial;
        switch ((raw >> 13U) & 3U) {
        case 0U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xC6U);
                wanted[kSelectOffset] = 6U;
                wanted[kModeOffset] = 0xC0U;
            } else {
                mapper->write_prg(addr, 0xFFU);
                wanted[kBanksOffset] = 0xFFU;
            }
            break;
        case 1U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 1U);
                wanted[kMirrorOffset] = 1U;
            } else {
                mapper->write_prg(addr, 0xC0U);
                wanted[kRamFlagsOffset] = 0xC0U;
            }
            break;
        case 2U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xA5U);
                wanted[kReloadOffset] = 0xA5U;
            } else {
                mapper->write_prg(addr, 0U);
                wanted[kIrqOffset] = 1U;
            }
            break;
        default:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(0xE001U, 0U);
                mapper->write_prg(0xC000U, 0U);
                mapper->write_prg(0xC001U, 0U);
                mapper->clock_irq_counter();
                mapper->write_prg(addr, 0U);
            } else {
                mapper->write_prg(addr, 0U);
                wanted[kIrqOffset] = 2U;
            }
            break;
        }
        CAPTURE(raw);
        REQUIRE(state(*mapper) == wanted);
    }

    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_prg(0xA000U, static_cast<Byte>(raw));
        CAPTURE(raw);
        REQUIRE(mapper->mirror_mode() ==
                ((raw & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x2000U, 0x2ABCU, 0x3000U, 0x3EFFU}) {
            REQUIRE_FALSE(mapper->ciram_address(addr));
            if (addr >= 0x2000U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
            }
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    mapper->set_shared_nvram(std::array<Byte, Mapper430::kWramSize>{});
    REQUIRE(mapper->shared_nvram().empty());
}

TEST_CASE("Mapper 430 retains MMC3 IRQ behavior and has no expansion audio path",
          "[mapper430][irq][audio]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xC000U, static_cast<Byte>(latch));
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        CAPTURE(latch);
        REQUIRE(state(*mapper)[kCounterOffset] == latch);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        for (unsigned count = 0U; count < latch; ++count) {
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->reset();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    outer(*mapper, 0x0FU);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    for (unsigned repeat = 0U; repeat < 3U; ++repeat) {
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    load(*mapper, state(*board()));

    const auto before = state(*mapper);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1234U);
    mapper->observe_ppu_read(0x1234U);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x6000U, 0xA5U);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 430 reset restores registers and preserves only appropriate WRAM",
          "[mapper430][reset][ram][irq][ciram]") {
    for (const auto ram : {RamKind::None, RamKind::Volatile, RamKind::Nonvolatile}) {
        for (const bool cold : {false, true}) {
            auto mapper = board(0x20000UZ, 0x20000UZ, ram, MirrorMode::Horizontal);
            outer(*mapper, 0x0FU);
            for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
                mapper->write_prg(addr, cell(addr));
            }
            bank(*mapper, 6U, 9U, 0xC0U);
            mapper->write_prg(0xA000U, 0U);
            mapper->write_prg(0xA001U, 0U);
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(0xC001U, 0U);
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending());

            if (cold) {
                mapper->power_on();
            } else {
                mapper->reset();
            }
            CAPTURE(ram, cold);
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            REQUIRE(state(*mapper)[kOuterOffset] == 0U);
            REQUIRE(state(*mapper)[kRamFlagsOffset] == 0x80U);
            REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
            REQUIRE(mapper->read_prg(0xC000U) == cell(14UZ * kPrgBankSize));
            REQUIRE(mapper->read_prg(0xE000U) == cell(15UZ * kPrgBankSize));
            if (ram != RamKind::None) {
                const Byte expected = cold && ram == RamKind::Volatile ? 0U : cell(0x6123U);
                REQUIRE(mapper->read_prg(0x6123U) == expected);
            }
        }
    }

    auto imported = board(0x20000UZ, 0x20000UZ, RamKind::Nonvolatile);
    std::array<Byte, Mapper430::kWramSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0x5BU);
    }
    imported->set_battery_ram(bytes);
    REQUIRE(
        std::equal(imported->battery_ram().begin(), imported->battery_ram().end(), bytes.begin()));
    const auto before = state(*imported);
    imported->set_battery_ram(std::span<const Byte>(bytes).first(bytes.size() - 1U));
    REQUIRE(state(*imported) == before);
    std::vector<Byte> oversized(bytes.begin(), bytes.end());
    oversized.push_back(0U);
    imported->set_battery_ram(oversized);
    REQUIRE(state(*imported) == before);
    imported->power_on();
    REQUIRE(imported->read_prg(0x6123U) == bytes[0x123U]);
}

TEST_CASE("Mapper 430 bounded snapshots replay registers and RAM without either ROM",
          "[mapper430][state][irq][ram]") {
    auto mapper = board(0x20000UZ, 0x20000UZ, RamKind::Nonvolatile);
    outer(*mapper, 7U);
    bank(*mapper, 0U, 0x83U, 0xC0U);
    bank(*mapper, 6U, 0x39U, 0xC0U);
    bank(*mapper, 7U, 0x2AU, 0xC0U);
    mapper->write_prg(0x6123U, 0xA6U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize + Mapper430::kWramSize);
    REQUIRE(saved[kOuterOffset] == 6U);
    REQUIRE(saved[kIrqOffset] == 6U);
    REQUIRE(saved[kWramOffset + 0x123U] == 0xA6U);

    auto copy = board(0x20000UZ, 0x20000UZ, RamKind::Nonvolatile, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (const auto addr : kPrgAddresses) {
        const auto selected = expected_prg_bank(addr, 0x39U, 0x2AU, 0xC0U, 6U);
        const auto physical = (selected * kPrgBankSize + (addr & (kPrgBankSize - 1U))) % 0x20000UZ;
        CAPTURE(addr, selected, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
        const auto selected = expected_chr_bank(addr, {0x83U}, 0xC0U, 6U);
        const auto physical = (selected * kChrBankSize + 0x123U) % 0x20000UZ;
        CAPTURE(addr, selected, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0xB5U));
        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 430 rejects damaged truncated and mismatched states atomically",
          "[mapper430][state][ram]") {
    auto mapper = board(0x20000UZ, 0x20000UZ, RamKind::Nonvolatile);
    outer(*mapper, 7U);
    bank(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0x6123U, 0xA6U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
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
    constexpr std::array<std::pair<std::size_t, Byte>, 11> invalid_fields{{
        {kOuterOffset, 0x10U},
        {kSelectOffset, 8U},
        {kModeOffset, 1U},
        {kBanksOffset + 6U, 64U},
        {kBanksOffset + 7U, 64U},
        {kMirrorOffset, 2U},
        {kIrqOffset, 8U},
        {kIrqOffset, 4U},
        {kRamFlagsOffset, 1U},
        {kRamFlagsOffset, 0x3FU},
        {kModeOffset, 0x3FU},
    }};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper430>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg = board(0x10000UZ, 0x20000UZ, RamKind::Nonvolatile);
    mismatch(wrong_prg);
    auto wrong_chr = board(0x20000UZ, 0x10000UZ, RamKind::Nonvolatile);
    mismatch(wrong_chr);
    auto wrong_mirror = board(0x20000UZ, 0x20000UZ, RamKind::Nonvolatile, MirrorMode::Horizontal);
    mismatch(wrong_mirror);
    auto wrong_ram = board(0x20000UZ, 0x20000UZ, RamKind::Volatile);
    mismatch(wrong_ram);
    auto no_ram = board();
    mismatch(no_ram);

    auto changed = saved;
    changed[kOuterOffset] = 6U;
    changed[kWramOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
