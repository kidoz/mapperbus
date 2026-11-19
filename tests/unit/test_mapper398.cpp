#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper398.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x040000UZ;
constexpr std::size_t kChrSize = 0x0A0000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kPrgBanksOffset = 11UZ;
constexpr std::size_t kChrBanksOffset = 13UZ;
constexpr std::size_t kMirrorOffset = 29UZ;
constexpr std::size_t kPrgControlOffset = 30UZ;
constexpr std::size_t kIrqPrescalerOffset = 33UZ;
constexpr std::size_t kIrqControlOffset = 35UZ;
constexpr std::size_t kIrqPendingOffset = 36UZ;
constexpr std::size_t kPrgChrA12Offset = 37UZ;
constexpr std::size_t kStateSize = 38UZ;
constexpr std::array<Address, 4> kPrgSlots{0x8123U, 0xA123U, 0xC123U, 0xE123U};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 398U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 17U) * 71U + (offset >> 19U) * 83U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper398> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper398::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper398>(h, rom(kPrgSize, salt), rom(kChrSize, salt + 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper398& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void set_outer(Mapper398& mapper, Byte outer) {
    mapper.write_prg(0x8004U, outer);
}

Address chr_register(unsigned slot, bool high = false) {
    return static_cast<Address>(0xB000U + (slot / 2U) * 0x1000U + (slot & 1U) * 2U +
                                (high ? 1U : 0U));
}

void set_chr_bank(Mapper398& mapper, unsigned slot, unsigned bank) {
    const auto outer = state(mapper)[kOuterOffset];
    mapper.write_prg(chr_register(slot), static_cast<Byte>(bank));
    mapper.write_prg(chr_register(slot, true), static_cast<Byte>(bank >> 4U));
    set_outer(mapper, outer);
}

void select_chr(Mapper398& mapper, unsigned slot) {
    mapper.observe_ppu_address(static_cast<Address>(slot * kChrBankSize));
}

std::size_t expected_vrc_prg(Address addr,
                             Byte outer,
                             const std::array<Byte, 2>& banks,
                             Byte control) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (control & 2U) != 0U;
    unsigned bank = 15U;
    if (slot == 1U) {
        bank = banks[1U] & 15U;
    } else if (slot == 0U || slot == 2U) {
        bank = (slot == 0U) != swapped ? banks[0U] & 15U : 14U;
    }
    return ((outer & 0x40U) != 0U ? 0x020000UZ : 0UZ) + bank * kPrgBankSize + (addr & 0x1FFFU);
}

std::size_t expected_gnrom_prg(Address addr, Byte outer, unsigned chr_a12) {
    return 0x020000UZ + ((outer & 0x40U) != 0U ? 0x010000UZ : 0UZ) +
           (chr_a12 != 0U ? 0x008000UZ : 0UZ) + (addr & 0x7FFFU);
}

std::size_t expected_vrc_chr(Address addr, Byte outer, unsigned bank) {
    return static_cast<std::size_t>(bank & 0x01BFU) * kChrBankSize +
           ((outer & 0x40U) != 0U ? 0x010000UZ : 0UZ) + (addr & 0x03FFU);
}

std::size_t expected_gnrom_chr(Address addr, Byte outer, unsigned bank) {
    return 0x080000UZ + ((outer & 0x40U) != 0U ? 0x010000UZ : 0UZ) +
           static_cast<std::size_t>((bank >> 2U) & 7U) * 0x2000UZ + (addr & 0x1FFFU);
}

TEST_CASE("Mapper 398 validates the single multicart ROM-only geometry", "[mapper398][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        const auto initial = state(*mapper);
        REQUIRE(initial.size() == kStateSize);
        REQUIRE(initial[kOuterOffset] == 0xC0U);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const unsigned id : {0U, 23U, 27U, 362U, 397U, 399U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper398::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper398::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper398::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x20000UZ, 0x3FFFFUZ, 0x40001UZ, 0x80000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper398::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x80000UZ, 0x9FFFFUZ, 0xA0001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper398::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper398::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper398::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 398 decodes every VRC4f alias and overlaps the outer register",
          "[mapper398][bus][decoding]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            set_outer(*expected, value);
            expected->write_prg(static_cast<Address>(raw & 0xF00FU), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        actual->reset();
        actual->write_prg(0x8004U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*actual)[kOuterOffset] == (value & 0xC0U));
        actual->write_prg(0xB004U, static_cast<Byte>(value));
        REQUIRE(state(*actual)[kOuterOffset] == (value & 0xC0U));
        REQUIRE(state(*actual)[kChrBanksOffset] == 0U);
        REQUIRE(state(*actual)[kChrBanksOffset + 1U] == 0U);
    }
}

TEST_CASE("Mapper 398 VRC4 PRG banking applies outer A16 and four inner bank bits",
          "[mapper398][banking]") {
    auto mapper = board();
    for (const Byte outer : {0U, 0x40U}) {
        for (unsigned reg = 0U; reg < 2U; ++reg) {
            for (const bool swapped : {false, true}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->reset();
                    set_outer(*mapper, outer);
                    mapper->write_prg(reg == 0U ? 0x8000U : 0xA000U, static_cast<Byte>(value));
                    mapper->write_prg(0x9002U, swapped ? 0xFEU : 0xFCU);
                    set_outer(*mapper, outer);
                    std::array<Byte, 2> banks{0U, 1U};
                    banks[reg] = static_cast<Byte>(value & 31U);
                    const Byte control = swapped ? 2U : 0U;
                    for (const auto addr : kPrgSlots) {
                        const auto physical = expected_vrc_prg(addr, outer, banks, control);
                        CAPTURE(outer, reg, swapped, value, addr, physical);
                        REQUIRE(mapper->read_prg(addr) == cell(physical));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 398 GNROM-like PRG follows PPU-selected VRC CHR A12",
          "[mapper398][banking][bus]") {
    auto mapper = board();
    REQUIRE(mapper->observes_ppu_address());
    for (const Byte outer : {0x80U, 0xC0U}) {
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            for (const unsigned bank : {slot, slot | 0x10U, slot | 0x1E0U}) {
                mapper->reset();
                set_outer(*mapper, outer);
                set_chr_bank(*mapper, slot, bank);
                select_chr(*mapper, slot);
                REQUIRE(state(*mapper)[kPrgChrA12Offset] == ((bank >> 4U) & 1U));
                for (const auto addr : kPrgSlots) {
                    const auto physical = expected_gnrom_prg(addr, outer, (bank >> 4U) & 1U);
                    CAPTURE(outer, slot, bank, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }

    mapper->reset();
    set_outer(*mapper, 0x80U);
    set_chr_bank(*mapper, 0U, 0U);
    set_chr_bank(*mapper, 1U, 0x10U);
    select_chr(*mapper, 0U);
    REQUIRE(mapper->read_prg(0xE123U) == cell(expected_gnrom_prg(0xE123U, 0x80U, 0U)));
    select_chr(*mapper, 1U);
    REQUIRE(mapper->read_prg(0xE123U) == cell(expected_gnrom_prg(0xE123U, 0x80U, 1U)));
    mapper->observe_ppu_address(0x2400U);
    REQUIRE(mapper->read_prg(0xE123U) == cell(expected_gnrom_prg(0xE123U, 0x80U, 1U)));
    set_chr_bank(*mapper, 1U, 0U);
    REQUIRE(mapper->read_prg(0xE123U) == cell(expected_gnrom_prg(0xE123U, 0x80U, 1U)));
    REQUIRE(mapper->read_chr(0x0456U) == cell(expected_gnrom_chr(0x0456U, 0x80U, 0U), 0x35U));
    REQUIRE(mapper->read_prg(0xE123U) == cell(expected_gnrom_prg(0xE123U, 0x80U, 0U)));
}

TEST_CASE("Mapper 398 VRC4 CHR retains all inner lines except overridden A16",
          "[mapper398][banking]") {
    auto mapper = board();
    for (const Byte outer : {0U, 0x40U}) {
        set_outer(*mapper, outer);
        for (unsigned bank = 0U; bank < 512U; ++bank) {
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                set_chr_bank(*mapper, slot, bank);
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                const auto physical = expected_vrc_chr(addr, outer, bank);
                CAPTURE(outer, bank, slot, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                const auto before_write = state(*mapper);
                mapper->write_chr(addr, static_cast<Byte>(cell(physical, 0x35U) ^ 0xFFU));
                REQUIRE(state(*mapper) == before_write);
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
            }
        }
    }
}

TEST_CASE("Mapper 398 GNROM-like CHR uses inner A12-A10 and the second chip",
          "[mapper398][banking]") {
    auto mapper = board();
    for (const Byte outer : {0x80U, 0xC0U}) {
        for (unsigned bank = 0U; bank < 512U; ++bank) {
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                set_outer(*mapper, outer);
                set_chr_bank(*mapper, slot, bank);
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x2ABU);
                const auto physical = expected_gnrom_chr(addr, outer, bank);
                CAPTURE(outer, bank, slot, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
            }
        }
    }
}

TEST_CASE("Mapper 398 provides VRC4 CIRAM modes without custom nametable RAM",
          "[mapper398][ciram][ram]") {
    auto mapper = board(MirrorMode::Horizontal);
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x9000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() == kModes[value & 3U]);
        for (const Address addr : {0x2000U, 0x2400U, 0x2FFFU, 0x3000U, 0x3EFFU}) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 398 exposes no CPU RAM expansion device or audio",
          "[mapper398][bus][ram][audio]") {
    auto mapper = board();
    mapper->write_prg(0x9002U, 1U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->prg_read_mask(0xFFFFU) == 0xFFU);

    const auto before = state(*mapper);
    mapper->write_prg_cpu(0x7FFFU, 0xA5U, true);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->expansion_read_mask(0x5000U) == 0xFFU);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 398 VRC cycle IRQ covers every latch and F003 acknowledgment",
          "[mapper398][irq]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xF000U, static_cast<Byte>(latch));
        mapper->write_prg(0xF001U, static_cast<Byte>(latch >> 4U));
        mapper->write_prg(0xF002U, 6U);
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF003U, 0xFFU);
        REQUIRE_FALSE(mapper->irq_pending());
        CAPTURE(latch);
        REQUIRE(state(*mapper)[kIrqControlOffset] == 4U);
        for (unsigned cycle = 0U; cycle < 256U - latch; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());

        mapper->write_prg(0xF002U, 6U);
        for (unsigned cycle = 0U; cycle < 256U - latch; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper)[kIrqControlOffset] == 6U);
    }

    mapper->reset();
    mapper->write_prg(0xF000U, 14U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 7U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF003U, 0U);
    REQUIRE(state(*mapper)[kIrqControlOffset] == 7U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF002U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqControlOffset] == 0U);
    REQUIRE(state(*mapper)[kIrqPrescalerOffset] == 0x55U);
    REQUIRE(state(*mapper)[kIrqPrescalerOffset + 1U] == 0x01U);
}

TEST_CASE("Mapper 398 scanline IRQ keeps VRC cadence and partial prescaler state",
          "[mapper398][irq][state]") {
    auto mapper = board();
    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 3U);
    for (const unsigned period : {114U, 114U, 113U, 114U}) {
        for (unsigned cycle = 1U; cycle < period; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(period);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF003U, 0U);
        REQUIRE(state(*mapper)[kIrqControlOffset] == 3U);
    }

    mapper->reset();
    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 3U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto partial = state(*mapper);
    mapper->reset();
    load(*mapper, partial);
    for (unsigned cycle = 0U; cycle < 13U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 398 reset restores the documented outer value and VRC defaults",
          "[mapper398][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        auto fresh = board(mirror);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            set_chr_bank(*mapper, slot, 0x1F0U + slot);
        }
        mapper->write_prg(0x8000U, 0x1FU);
        mapper->write_prg(0xA000U, 0x1EU);
        mapper->write_prg(0x9000U, 3U);
        mapper->write_prg(0x9002U, 3U);
        mapper->write_prg(0xF002U, 6U);
        select_chr(*mapper, 7U);
        mapper->clock_cpu();
        REQUIRE(state(*mapper) != state(*fresh));

        mapper->reset();
        REQUIRE(state(*mapper) == state(*fresh));
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->read_prg(0x8000U) == cell(0x030000UZ));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x037FFFUZ));
        REQUIRE(mapper->read_chr(0x0123U) == cell(0x090123UZ, 0x35U));
        mapper->write_prg(0xF002U, 6U);
        select_chr(*mapper, 7U);
        mapper->power_on();
        REQUIRE(state(*mapper) == state(*fresh));
    }
}

TEST_CASE("Mapper 398 bounded snapshots replay without ROM and preserve timing",
          "[mapper398][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 9U);
    mapper->write_prg(0xA000U, 11U);
    set_outer(*mapper, 0x40U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr_bank(*mapper, slot, 0x80U + slot * 19U);
    }
    select_chr(*mapper, 5U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x9002U, 2U);
    mapper->write_prg(0xF000U, 10U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 6U);
    set_outer(*mapper, 0xC0U);
    for (unsigned cycle = 0U; cycle < 6U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 64U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (const Address addr : {0x8000U, 0x9234U, 0xA000U, 0xC000U, 0xD234U, 0xFFFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x99U);
        CAPTURE(slot);
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(mapper->read_chr(addr) + 0x80U));
    }
    mapper->clock_cpu();
    copy->clock_cpu();
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

TEST_CASE("Mapper 398 rejects malformed and incompatible states atomically", "[mapper398][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x1FU);
    mapper->write_prg(0xA000U, 0x1EU);
    set_outer(*mapper, 0x40U);
    set_chr_bank(*mapper, 0U, 0x1ABU);
    select_chr(*mapper, 0U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x9002U, 2U);
    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 6U);
    set_outer(*mapper, 0xC0U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
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
    for (const Byte value : {1U, 0x3FU, 0xFFU}) {
        auto invalid = saved;
        invalid[kOuterOffset] = value;
        reject(invalid);
    }
    for (const std::size_t offset : {kPrgBanksOffset, kPrgBanksOffset + 1U}) {
        auto invalid = saved;
        invalid[offset] = 0x20U;
        reject(invalid);
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        auto invalid = saved;
        constexpr std::uint16_t kInvalidBank = 0x0200U;
        std::memcpy(
            invalid.data() + kChrBanksOffset + slot * 2U, &kInvalidBank, sizeof(kInvalidBank));
        CAPTURE(slot);
        reject(invalid);
    }
    for (const Byte value : {2U, 5U, 0xFFU}) {
        auto invalid = saved;
        invalid[kMirrorOffset] = value;
        reject(invalid);
    }
    for (const Byte value : {4U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kPrgControlOffset] = value;
        reject(invalid);
    }
    for (const std::uint16_t value : {0U, 342U, 0xFFFFU}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kIrqPrescalerOffset, &value, sizeof(value));
        CAPTURE(value);
        reject(invalid);
    }
    for (const Byte value : {8U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqControlOffset] = value;
        reject(invalid);
    }
    for (const Byte value : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqPendingOffset] = value;
        reject(invalid);
    }
    for (const Byte value : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kPrgChrA12Offset] = value;
        reject(invalid);
    }
    auto disabled_pending = saved;
    disabled_pending[kIrqControlOffset] = 5U;
    reject(disabled_pending);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before);
}

} // namespace
