#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper362.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kRomSize = 0x080000UZ;
constexpr std::size_t kOuterBankSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kPrgBanksOffset = 11UZ;
constexpr std::size_t kChrBanksOffset = 13UZ;
constexpr std::size_t kOuterBankOffset = 29UZ;
constexpr std::size_t kMirrorOffset = 30UZ;
constexpr std::size_t kPrgControlOffset = 31UZ;
constexpr std::size_t kIrqPrescalerOffset = 34UZ;
constexpr std::size_t kIrqControlOffset = 36UZ;
constexpr std::size_t kIrqPendingOffset = 37UZ;
constexpr std::size_t kStateSize = 38UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 362U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 19U + (offset >> 8U) * 31U + (offset >> 13U) * 47U +
                             (offset >> 17U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper362> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper362::validate_geometry(h, kRomSize, kRomSize));
    return std::make_unique<Mapper362>(h, rom(kRomSize, salt), rom(kRomSize, salt + 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Address chr_register(unsigned slot, bool high = false) {
    return static_cast<Address>(0xB000U + (slot / 2U) * 0x1000U + (slot & 1U) * 2U +
                                (high ? 1U : 0U));
}

void set_chr_bank(Mapper362& mapper, unsigned slot, unsigned bank) {
    mapper.write_prg(chr_register(slot), static_cast<Byte>(bank));
    mapper.write_prg(chr_register(slot, true), static_cast<Byte>(bank >> 4U));
}

void select_outer(Mapper362& mapper, unsigned outer, unsigned slot = 0U) {
    set_chr_bank(mapper, slot, (outer << 7U) | slot);
    mapper.observe_ppu_address(static_cast<Address>(slot * kChrBankSize));
}

TEST_CASE("Mapper 362 validates the four-game ROM-only NES 2.0 geometry", "[mapper362]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper362::validate_geometry(header(mirror), kRomSize, kRomSize));
    }
    for (const unsigned id : {0U, 4U, 27U, 359U, 360U, 361U, 363U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper362::validate_geometry(h, kRomSize, kRomSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper362::validate_geometry(h, kRomSize, kRomSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper362::validate_geometry(h, kRomSize, kRomSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x20000UZ, 0x40000UZ, 0x7FFFFUZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper362::validate_geometry(header(), size, kRomSize));
        REQUIRE_FALSE(Mapper362::validate_geometry(header(), kRomSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper362::validate_geometry(header(mirror), kRomSize, kRomSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        h = header();
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
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper362::validate_geometry(h, kRomSize, kRomSize));
    }
}

TEST_CASE("Mapper 362 decodes VRC4f F00F aliases and ignores B004", "[mapper362][bus]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(raw & 0xF00FU), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->mirror_mode() == expected->mirror_mode());
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        actual->reset();
        const auto initial = state(*actual);
        actual->write_prg(0xB004U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*actual) == initial);
        actual->write_prg(0xB000U, static_cast<Byte>(value));
        actual->write_prg(0xB001U, static_cast<Byte>(value));
        const auto programmed = state(*actual);
        StateReader reader{std::span(programmed).subspan(kChrBanksOffset)};
        REQUIRE(reader.read<std::uint16_t>() == ((value & 15U) | ((value & 31U) << 4U)));
    }
}

TEST_CASE("Mapper 362 PRG uses four inner bits fixed pages and the active outer bank",
          "[mapper362][banking]") {
    auto mapper = board();
    constexpr std::array<Address, 4> addresses{0x8123U, 0xA123U, 0xC123U, 0xE123U};
    for (unsigned outer = 0U; outer < 4U; ++outer) {
        for (unsigned reg = 0U; reg < 2U; ++reg) {
            for (const bool swapped : {false, true}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->reset();
                    select_outer(*mapper, outer);
                    mapper->write_prg(reg == 0U ? 0x8000U : 0xA000U, static_cast<Byte>(value));
                    mapper->write_prg(0x9002U, swapped ? 0xFEU : 0xFCU);
                    std::array<Byte, 2> banks{0U, 1U};
                    banks[reg] = static_cast<Byte>(value & 31U);
                    for (unsigned slot = 0U; slot < addresses.size(); ++slot) {
                        unsigned bank = 15U;
                        if (slot == 1U) {
                            bank = banks[1U] & 15U;
                        } else if (slot == 0U || slot == 2U) {
                            bank = (slot == 0U) != swapped ? banks[0U] & 15U : 14U;
                        }
                        const auto offset = static_cast<std::size_t>(outer) * kOuterBankSize +
                                            bank * kPrgBankSize + 0x123U;
                        CAPTURE(outer, reg, swapped, value, slot, bank);
                        REQUIRE(mapper->read_prg(addresses[slot]) == cell(offset));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 362 CHR retains seven inner and two outer bits in every slot",
          "[mapper362][banking]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 512U; ++bank) {
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            set_chr_bank(*mapper, slot, bank);
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
            const auto offset = static_cast<std::size_t>((bank >> 7U) & 3U) * kOuterBankSize +
                                (bank & 0x7FU) * kChrBankSize + 0x155U;
            CAPTURE(bank, slot);
            REQUIRE(mapper->read_chr(addr) == cell(offset, 0x35U));
            REQUIRE(state(*mapper)[kOuterBankOffset] == ((bank >> 7U) & 3U));
            mapper->write_chr(addr, static_cast<Byte>(cell(offset, 0x35U) ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == cell(offset, 0x35U));
        }
    }
}

TEST_CASE("Mapper 362 PPU accesses steer PRG independently of cached CHR reads",
          "[mapper362][banking][bus]") {
    auto mapper = board();
    REQUIRE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr_bank(*mapper, slot, ((slot & 3U) << 7U) | (slot * 13U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x3FFU);
        mapper->observe_ppu_address(addr);
        CAPTURE(slot);
        REQUIRE(state(*mapper)[kOuterBankOffset] == (slot & 3U));
        REQUIRE(mapper->read_prg(0xE123U) ==
                cell(static_cast<std::size_t>(slot & 3U) * kOuterBankSize + 15UZ * kPrgBankSize +
                     0x123U));
        mapper->observe_ppu_address(static_cast<Address>(addr + 0x2000U));
        REQUIRE(state(*mapper)[kOuterBankOffset] == (slot & 3U));
    }

    select_outer(*mapper, 1U);
    const auto outer_one = state(*mapper);
    set_chr_bank(*mapper, 0U, 3U << 7U);
    REQUIRE(state(*mapper)[kOuterBankOffset] == outer_one[kOuterBankOffset]);
    REQUIRE(mapper->read_prg(0xE123U) == cell(kOuterBankSize + 15UZ * kPrgBankSize + 0x123U));
    mapper->observe_ppu_address(0U);
    REQUIRE(state(*mapper)[kOuterBankOffset] == 3U);
    REQUIRE(mapper->read_prg(0xE123U) == cell(3UZ * kOuterBankSize + 15UZ * kPrgBankSize + 0x123U));
}

TEST_CASE("Mapper 362 claims only PRG ROM and exposes no RAM or expansion audio",
          "[mapper362][bus][ram][audio]") {
    auto mapper = board();
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned raw = 0U; raw <= 0xFFFFU; raw += 0x101U) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE(mapper->maps_prg(addr) == (addr >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (addr >= 0x8000U ? 0xFFU : 0U));
        if (addr < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
    }

    mapper->write_prg(0x9002U, 1U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE_FALSE(mapper->maps_prg(0x7FFFU));
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
        mapper->write_expansion(static_cast<Address>(raw), static_cast<Byte>(raw));
    }
    mapper->write_prg_cpu(0xB004U, 0xFFU, true);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 362 provides VRC4 CIRAM modes without custom nametable storage",
          "[mapper362][ciram]") {
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

TEST_CASE("Mapper 362 VRC IRQ covers every latch and acknowledge-only F003", "[mapper362][irq]") {
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
        const auto pending = state(*mapper);
        mapper->write_prg(0xF003U, 0xFFU);
        REQUIRE_FALSE(mapper->irq_pending());
        const auto acknowledged = state(*mapper);
        CAPTURE(latch);
        REQUIRE(acknowledged[kIrqControlOffset] == 6U);
        REQUIRE(acknowledged[kIrqControlOffset] == pending[kIrqControlOffset]);
        REQUIRE(acknowledged[kIrqPrescalerOffset] == pending[kIrqPrescalerOffset]);
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

TEST_CASE("Mapper 362 scanline IRQ keeps the VRC 114-114-113 cadence and partial state",
          "[mapper362][irq][state]") {
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
    StateReader reader(partial);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    for (unsigned cycle = 0U; cycle < 13U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 362 reset restores deterministic VRC and outer-bank defaults",
          "[mapper362][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        auto fresh = board(mirror);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            set_chr_bank(*mapper, slot, 0x1FFU - slot);
        }
        mapper->write_prg(0x8000U, 0x1FU);
        mapper->write_prg(0xA000U, 0x1EU);
        mapper->write_prg(0x9000U, 3U);
        mapper->write_prg(0x9002U, 3U);
        mapper->write_prg(0xF002U, 6U);
        mapper->observe_ppu_address(0x1FFFU);
        mapper->clock_cpu();
        REQUIRE(state(*mapper) != state(*fresh));

        mapper->reset();
        REQUIRE(state(*mapper) == state(*fresh));
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xA000U) == cell(kPrgBankSize));
        REQUIRE(mapper->read_prg(0xC000U) == cell(14UZ * kPrgBankSize));
        REQUIRE(mapper->read_prg(0xE000U) == cell(15UZ * kPrgBankSize));
        mapper->write_prg(0x9002U, 6U);
        mapper->observe_ppu_address(0U);
        mapper->power_on();
        REQUIRE(state(*mapper) == state(*fresh));
    }
}

TEST_CASE("Mapper 362 bounded snapshots replay all state without ROM", "[mapper362][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 9U);
    mapper->write_prg(0xA000U, 11U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr_bank(*mapper, slot, 0x80U + slot * 19U);
    }
    mapper->observe_ppu_address(0x1400U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x9002U, 2U);
    mapper->write_prg(0xF000U, 10U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 6U);
    for (unsigned cycle = 0U; cycle < 6U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kOuterBankSize);

    auto copy = board(MirrorMode::Vertical, 0x80U);
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

TEST_CASE("Mapper 362 rejects malformed and incompatible states atomically", "[mapper362][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x1FU);
    mapper->write_prg(0xA000U, 0x1EU);
    set_chr_bank(*mapper, 0U, 0x1ABU);
    mapper->observe_ppu_address(0U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x9002U, 2U);
    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF001U, 15U);
    mapper->write_prg(0xF002U, 6U);
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
    for (std::size_t offset = 0U; offset < kPrgBanksOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
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
    for (const Byte value : {4U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kOuterBankOffset] = value;
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
