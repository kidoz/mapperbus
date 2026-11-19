#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper520.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kPrgBanksOffset = 6UZ;
constexpr std::size_t kChrBanksOffset = 8UZ;
constexpr std::size_t kMirrorOffset = 16UZ;
constexpr std::size_t kModeOffset = 17UZ;
constexpr std::size_t kPpuSlotOffset = 18UZ;
constexpr std::size_t kIrqPrescalerOffset = 21UZ;
constexpr std::size_t kIrqControlOffset = 23UZ;
constexpr std::size_t kIrqFlagsOffset = 24UZ;
constexpr std::size_t kChrRamOffset = 25UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 5> kPrgAddresses{0x8000U, 0xA000U, 0xC000U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 520U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kMaxPrgSize;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + (offset >> 18U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper520> board(std::size_t prg_size = kMaxPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper520::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper520>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper520& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void set_chr_low(Mapper520& mapper, unsigned slot, Byte value) {
    const auto reg = static_cast<Address>(0xB000U + slot / 2U * 0x1000U + (slot & 1U) * 8U);
    mapper.write_prg(reg, value);
}

void set_chr_high(Mapper520& mapper, unsigned slot, Byte value) {
    const auto reg = static_cast<Address>(0xB004U + slot / 2U * 0x1000U + (slot & 1U) * 8U);
    mapper.write_prg(reg, value);
}

void select_chr_slot(Mapper520& mapper, unsigned slot) {
    mapper.observe_ppu_address(static_cast<Address>(slot * kChrBankSize + 0x123U));
}

std::size_t prg_offset(Address addr, Byte low_bank, Byte high_bank, Byte mode, unsigned outer) {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    Byte inner = 31U;
    if (slot == 1U) {
        inner = high_bank;
    } else if (slot != 3U) {
        inner = (slot == 0U) != ((mode & 2U) != 0U) ? low_bank : 30U;
    }
    const auto bank = static_cast<std::size_t>((inner & 0x1FU) | (outer << 5U));
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

TEST_CASE("Mapper 520 validates PRG plus 8 KiB CHR-RAM geometry and bounded state",
          "[mapper520][geometry][ram][ciram][state]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = kMinPrgSize; prg <= kMaxPrgSize; prg *= 2U) {
            CAPTURE(mirror, prg);
            REQUIRE(Mapper520::validate_geometry(header(mirror), prg, 0U));
        }
        auto mapper = board(kMaxPrgSize, mirror, 0x35U);
        auto different_rom = board(kMaxPrgSize, mirror, 0xA0U);
        const auto cold = state(*mapper);
        REQUIRE(cold.size() == kStateSize);
        REQUIRE(cold.size() == 8217U);
        REQUIRE(cold == state(*different_rom));
        REQUIRE(std::ranges::all_of(std::span(cold).subspan(kChrRamOffset),
                                    [](Byte value) { return value == 0U; }));
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
        REQUIRE(mapper->read_prg(0xA000U) == cell(kPrgBankSize, 0x35U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(30UZ * kPrgBankSize, 0x35U));
        REQUIRE(mapper->read_prg(0xE000U) == cell(31UZ * kPrgBankSize, 0x35U));
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
    }

    const auto reject = [](const INesHeader& h, std::size_t prg, std::size_t chr) {
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
        REQUIRE_FALSE(Mapper520::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 23U, 25U, 384U, 519U, 521U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMaxPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMaxPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMaxPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x7FFFUZ, 0x8001UZ, 0xA000UZ, 0x100000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ, 0x100000UZ}) {
        reject(header(), kMaxPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMaxPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x1000U;
            break;
        case 6U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kMaxPrgSize, 0U);
    }
}

TEST_CASE("Mapper 520 decodes VRC4e A2 A3 at every CPU register alias",
          "[mapper520][decoding][bus][banking][ciram][irq]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const auto addr = static_cast<Address>(raw);
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }

    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        mapper->write_prg(static_cast<Address>(raw), 0x1BU);
        const auto group = raw >> 12U;
        const auto port = (raw >> 2U) & 3U;
        if (group == 8U || group == 10U) {
            expected[kPrgBanksOffset + (group == 8U ? 0U : 1U)] = 0x1BU;
        } else if (group == 9U && port == 0U) {
            expected[kMirrorOffset] = static_cast<Byte>(MirrorMode::SingleUpper);
        } else if (group == 9U && port == 2U) {
            expected[kModeOffset] = 3U;
        } else if (group >= 11U && group <= 14U && (port & 1U) == 0U) {
            expected[kChrBanksOffset + (group - 11U) * 2U + port / 2U] = 0x0BU;
        } else if (group == 15U && port == 0U) {
            expected[19U] = 0x0BU;
        } else if (group == 15U && port == 1U) {
            expected[19U] = 0xB0U;
        } else if (group == 15U && port == 2U) {
            expected[kIrqControlOffset] = 3U;
            expected[kIrqFlagsOffset] = 1U;
        }
        CAPTURE(raw, group, port);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_prg(static_cast<Address>(raw)));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(raw)) == 0xFFU);
    }

    mapper->reset();
    mapper->write_prg_cpu(0x8000U, 0x12U, true);
    REQUIRE(state(*mapper)[kPrgBanksOffset] == 0x12U);
    mapper->write_prg_cpu(0xA000U, 0x34U, false);
    REQUIRE(state(*mapper)[kPrgBanksOffset + 1U] == 0x14U);
}

TEST_CASE("Mapper 520 applies the accessed CHR window's outer bit to every PRG page",
          "[mapper520][banking][prg][bus]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 2U; ++outer) {
        for (const Byte mode : {Byte{0U}, Byte{2U}}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                set_chr_low(*mapper, 0U, static_cast<Byte>(outer << 3U));
                select_chr_slot(*mapper, 0U);
                mapper->write_prg(0x9008U, mode);
                mapper->write_prg(0x8000U, static_cast<Byte>(value));
                const auto high_value = static_cast<Byte>(value * 13U + 7U);
                mapper->write_prg(0xA000U, high_value);
                for (const auto addr : kPrgAddresses) {
                    const auto physical = prg_offset(addr, value, high_value, mode, outer);
                    CAPTURE(outer, mode, value, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }

    mapper->reset();
    mapper->write_prg(0x8000U, 0xFFU);
    mapper->write_prg(0xA000U, 0xFFU);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr_low(*mapper, slot, static_cast<Byte>((slot & 1U) << 3U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        select_chr_slot(*mapper, slot);
        REQUIRE(state(*mapper)[kPpuSlotOffset] == slot);
        for (const auto addr : kPrgAddresses) {
            const auto physical = prg_offset(addr, 0xFFU, 0xFFU, 0U, slot & 1U);
            CAPTURE(slot, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
        for (unsigned other = 0U; other < 8U; ++other) {
            mapper->read_chr(static_cast<Address>(other * kChrBankSize));
            REQUIRE(state(*mapper)[kPpuSlotOffset] == slot);
        }
        for (const Address addr : {0x2000U, 0x23FFU, 0x2FFFU, 0x3000U, 0x3EFFU}) {
            mapper->observe_ppu_address(addr);
            REQUIRE(state(*mapper)[kPpuSlotOffset] == slot);
        }
    }

    select_chr_slot(*mapper, 7U);
    const auto before_high = state(*mapper);
    set_chr_high(*mapper, 7U, 0xFFU);
    REQUIRE(state(*mapper) == before_high);
    const auto outer_seven = mapper->read_prg(0xE000U);
    set_chr_low(*mapper, 7U, 0U);
    REQUIRE(mapper->read_prg(0xE000U) != outer_seven);
    REQUIRE(mapper->read_prg(0xE000U) == cell(31UZ * kPrgBankSize));

    auto small = board(kMinPrgSize);
    set_chr_low(*small, 0U, 0x08U);
    select_chr_slot(*small, 0U);
    small->write_prg(0x8000U, 0x1FU);
    small->write_prg(0xA000U, 0x1EU);
    for (const auto addr : kPrgAddresses) {
        const auto physical = prg_offset(addr, 0x1FU, 0x1EU, 0U, 1U);
        CAPTURE(addr, physical);
        REQUIRE(small->read_prg(addr) == cell(physical % kMinPrgSize));
    }
}

TEST_CASE("Mapper 520 banks writable CHR-RAM ignores high registers and uses console CIRAM",
          "[mapper520][banking][chr][ram][ciram]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned value = 0U; value < 256U; ++value) {
            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
            set_chr_low(*mapper, slot, static_cast<Byte>(value));
            REQUIRE(state(*mapper)[kChrBanksOffset + slot] == (value & 0x0FU));
            mapper->write_chr(addr, static_cast<Byte>(value ^ 0xA5U));
            REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(value ^ 0xA5U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                    static_cast<Byte>(value ^ 0xA5U));

            set_chr_low(*mapper, slot, static_cast<Byte>((value & 0x08U) | ((value + 1U) & 7U)));
            mapper->write_chr(addr, static_cast<Byte>(value ^ 0x5AU));
            set_chr_low(*mapper, slot, static_cast<Byte>(value));
            REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(value ^ 0xA5U));

            const auto before_high = state(*mapper);
            set_chr_high(*mapper, slot, static_cast<Byte>(value ^ 0xFFU));
            REQUIRE(state(*mapper) == before_high);
            REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(value ^ 0xA5U));
        }
    }

    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x9000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() == kModes[value & 3U]);
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 520 exposes no PRG RAM expansion RAM or audio population",
          "[mapper520][bus][ram][audio][irq]") {
    auto mapper = board();
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_read(0x1012U);
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    for (Address addr = 0U; addr < 0x8000U; addr += 0x101U) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, static_cast<Byte>(addr));
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE(mapper->led_outputs() == std::nullopt);
}

TEST_CASE("Mapper 520 clocks every VRC4 IRQ latch in cycle and scanline modes",
          "[mapper520][irq][state]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xF000U, static_cast<Byte>(latch & 0x0FU));
        mapper->write_prg(0xF004U, static_cast<Byte>(latch >> 4U));
        mapper->write_prg(0xF008U, 6U);
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(latch);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xF00CU, 0U);
        for (unsigned cycle = 0U; cycle < 300U; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 3U);
    for (const unsigned period : {114U, 114U, 113U, 114U}) {
        for (unsigned cycle = 1U; cycle < period; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(period);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF00CU, 0U);
    }
    mapper->write_prg(0xF008U, 3U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto partial = state(*mapper);
    mapper->write_prg(0xF008U, 0U);
    const auto disabled = state(*mapper);
    std::uint16_t disabled_prescaler = 0U;
    std::memcpy(
        &disabled_prescaler, disabled.data() + kIrqPrescalerOffset, sizeof(disabled_prescaler));
    REQUIRE(disabled_prescaler == 341U);
    REQUIRE(disabled[kIrqControlOffset] == 0U);
    REQUIRE(disabled[kIrqFlagsOffset] == 0U);
    load(*mapper, partial);
    for (unsigned cycle = 0U; cycle < 13U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 520 soft reset restores registers and retains CHR-RAM until power-on",
          "[mapper520][reset][ram][banking][ciram][audio]") {
    auto mapper = board(kMaxPrgSize, MirrorMode::Horizontal, 0x35U);
    const auto initial = state(*mapper);
    mapper->write_prg(0x9008U, 3U);
    mapper->write_prg(0x8000U, 0x17U);
    mapper->write_prg(0xA000U, 0x12U);
    set_chr_low(*mapper, 7U, 0x0FU);
    select_chr_slot(*mapper, 7U);
    mapper->write_chr(0x1FFFU, 0xA7U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF008U, 6U);
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    const auto programmed = state(*mapper);

    mapper->reset();
    const auto reset = state(*mapper);
    REQUIRE(std::ranges::equal(std::span(reset).first(kChrRamOffset),
                               std::span(initial).first(kChrRamOffset)));
    REQUIRE(std::ranges::equal(std::span(reset).subspan(kChrRamOffset),
                               std::span(programmed).subspan(kChrRamOffset)));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
    REQUIRE(mapper->read_prg(0xA000U) == cell(kPrgBankSize, 0x35U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(30UZ * kPrgBankSize, 0x35U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(31UZ * kPrgBankSize, 0x35U));
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
}

TEST_CASE("Mapper 520 bounded snapshots replay registers and CHR-RAM without ROM",
          "[mapper520][state][atomic][banking][ram]") {
    auto mapper = board(kMaxPrgSize, MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0x9008U, 3U);
    mapper->write_prg(0x8000U, 0x15U);
    mapper->write_prg(0xA000U, 0x12U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr_low(*mapper, slot, static_cast<Byte>(slot | ((slot & 1U) << 3U)));
        mapper->write_chr(static_cast<Address>(slot * kChrBankSize + 0x123U),
                          static_cast<Byte>(slot * 29U + 7U));
        set_chr_high(*mapper, slot, 0xFFU);
    }
    select_chr_slot(*mapper, 7U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF000U, 5U);
    mapper->write_prg(0xF004U, 4U);
    mapper->write_prg(0xF008U, 2U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x2100UZ);

    auto copy = board(kMaxPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(state(*copy)[kPpuSlotOffset] == 7U);
    for (const auto addr : kPrgAddresses) {
        const auto physical = prg_offset(addr, 0x15U, 0x12U, 3U, 1U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
        CAPTURE(slot, addr);
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(slot * 29U + 7U));
    }
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 520 rejects truncated damaged and mismatched states atomically",
          "[mapper520][state][atomic][geometry]") {
    auto mapper = board();
    mapper->write_prg(0x9008U, 3U);
    mapper->write_prg(0x8000U, 0x15U);
    mapper->write_prg(0xA000U, 0x12U);
    set_chr_low(*mapper, 7U, 0x0FU);
    select_chr_slot(*mapper, 7U);
    mapper->write_chr(0x1FFFU, 0xA7U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF008U, 2U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
        REQUIRE(mapper->read_chr(0x1FFFU) == 0xA7U);
        REQUIRE(state(*mapper)[kPpuSlotOffset] == 7U);
    };
    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < kPrgBanksOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    const std::array<std::pair<std::size_t, Byte>, 11> invalid_fields{
        {{kPrgBanksOffset, 0x20U},
         {kPrgBanksOffset + 1U, 0xFFU},
         {kChrBanksOffset + 7U, 0x10U},
         {kMirrorOffset, 2U},
         {kMirrorOffset, 5U},
         {kModeOffset, 4U},
         {kPpuSlotOffset, 8U},
         {kIrqControlOffset, 8U},
         {kIrqFlagsOffset, 4U},
         {kIrqFlagsOffset, 2U},
         {kChrBanksOffset + 7U, 0xFFU}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    for (const std::uint16_t prescaler : {0U, 342U, 0xFFFFU}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kIrqPrescalerOffset, &prescaler, sizeof(prescaler));
        CAPTURE(prescaler);
        reject(invalid);
    }

    auto wrong_prg = board(kMaxPrgSize / 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_mirror = board(kMaxPrgSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
