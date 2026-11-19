#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper384.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x000800UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kPrgBanksOffset = 11UZ;
constexpr std::size_t kChrBanksOffset = 13UZ;
constexpr std::size_t kMirrorOffset = 29UZ;
constexpr std::size_t kModeOffset = 30UZ;
constexpr std::size_t kIrqPrescalerOffset = 33UZ;
constexpr std::size_t kIrqControlOffset = 35UZ;
constexpr std::size_t kIrqFlagsOffset = 36UZ;
constexpr std::size_t kWramOffset = 37UZ;
constexpr std::size_t kStateSize = kWramOffset + kWramSize;
constexpr std::array<Address, 5> kPrgAddresses{0x8000U, 0xA000U, 0xC000U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 384U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kWramSize;
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

std::unique_ptr<Mapper384> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper384::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper384>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper384& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void set_chr_bank(Mapper384& mapper, unsigned slot, unsigned bank) {
    const auto reg = static_cast<Address>(0xB000U + slot / 2U * 0x1000U + (slot & 1U) * 8U);
    mapper.write_prg(reg, static_cast<Byte>(bank & 0x0FU));
    mapper.write_prg(static_cast<Address>(reg + 4U), static_cast<Byte>(bank >> 4U));
}

std::size_t prg_offset(Address addr, Byte low_bank, Byte high_bank, Byte outer, Byte mode) {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    Byte inner = 15U;
    if (slot == 1U) {
        inner = high_bank;
    } else if (slot != 3U) {
        inner = (slot == 0U) != ((mode & 2U) != 0U) ? low_bank : 14U;
    }
    const auto bank = static_cast<std::size_t>((inner & 0x0FU) | ((outer & 3U) << 4U));
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

TEST_CASE("Mapper 384 validates L1A16 ROM bounds and volatile WRAM metadata", "[mapper384]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = 0x8000UZ; prg <= kPrgSize; prg *= 2U) {
            for (std::size_t chr = 0x2000UZ; chr <= kChrSize; chr *= 2U) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper384::validate_geometry(header(mirror), prg, chr));
            }
        }
        auto mapper = board(kPrgSize, kChrSize, mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 2085U);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const unsigned id : {0U, 23U, 25U, 252U, 383U, 385U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper384::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper384::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper384::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x7FFFUZ, 0x8001UZ, 0xA000UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper384::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1800UZ, 0x1FFFUZ, 0x2001UZ, 0x2400UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper384::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper384::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0U;
            break;
        case 2U:
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = kWramSize;
            break;
        case 4U:
            h.chr_ram_size = 0x2000U;
            break;
        case 5U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.prg_ram_size = 0x1000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper384::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 384 decodes the outer register and mirrored WRAM independently",
          "[mapper384][bus][ram]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->power_on();
        mapper->write_prg(0x9008U, 1U);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        const auto addr = static_cast<Address>(raw);
        mapper->write_prg(addr, value);
        const auto expected_outer =
            raw >= 0x6800U && raw < 0x7000U ? static_cast<Byte>(value & 0x0FU) : Byte{0U};
        CAPTURE(raw, value);
        REQUIRE(state(*mapper)[kOuterOffset] == expected_outer);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x6000U && raw < 0x7000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x6000U && raw < 0x7000U ? 0xFFU : 0U));
        if (raw >= 0x6000U && raw < 0x7000U) {
            REQUIRE(mapper->read_prg(addr) == value);
            REQUIRE(mapper->read_prg(static_cast<Address>(raw ^ 0x0800U)) == value);
        } else {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    mapper->write_prg(0x6800U, 0x0FU);
    for (const Address addr : {0x6800U, 0x6801U, 0x69FFU, 0x6FFFU}) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
        CAPTURE(addr);
        REQUIRE(state(*mapper)[kOuterOffset] == 0x0FU);
        REQUIRE(mapper->read_prg(addr) == static_cast<Byte>(addr));
    }
    mapper->write_prg(0x7000U, 0U);
    mapper->write_prg(0x7FFFU, 0U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0FU);
}

TEST_CASE("Mapper 384 decodes VRC4e A2 A3 at every CPU register alias",
          "[mapper384][bus][banking][ciram][irq]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw), 0x1BU);
        const auto group = raw >> 12U;
        const auto port = (raw >> 2U) & 3U;
        const bool swap = group == 9U && port == 2U;
        CAPTURE(raw, group, port);
        REQUIRE(mapper->read_prg(0x8000U) == cell(((swap          ? 14U
                                                    : group == 8U ? 27U
                                                                  : 0U) &
                                                   0x0FU) *
                                                  kPrgBankSize));
        REQUIRE(mapper->read_prg(0xA000U) ==
                cell(((group == 10U ? 27U : 1U) & 0x0FU) * kPrgBankSize));
        REQUIRE(mapper->read_prg(0xC000U) == cell((swap ? 0U : 14U) * kPrgBankSize));
        REQUIRE(mapper->read_prg(0xE000U) == cell(15UZ * kPrgBankSize));
        REQUIRE(mapper->maps_prg(0x6000U) == swap);
        REQUIRE(mapper->mirror_mode() ==
                (group == 9U && port == 0U ? MirrorMode::SingleUpper : MirrorMode::Vertical));
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const bool selected =
                group >= 11U && group <= 14U && slot == (group - 11U) * 2U + port / 2U;
            const unsigned bank = selected ? ((port & 1U) == 0U ? 0x00BU : 0x1B0U) : 0U;
            REQUIRE(static_cast<unsigned>(
                        mapper->read_chr(static_cast<Address>(slot * kChrBankSize))) ==
                    static_cast<unsigned>(cell((bank & 0x7FU) * kChrBankSize)));
        }
    }
}

TEST_CASE("Mapper 384 applies outer PRG pins after VRC4 selection and fixed pages",
          "[mapper384][banking]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 4U; ++outer) {
        for (const Byte unused : {0U, 4U}) {
            for (const Byte mode : {0U, 2U}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->write_prg(0x6800U, static_cast<Byte>(outer | unused));
                    mapper->write_prg(0x9008U, mode);
                    mapper->write_prg(0x8000U, static_cast<Byte>(value));
                    mapper->write_prg(0xA000U, static_cast<Byte>(value * 13U + 7U));
                    const Byte low = value & 0x1FU;
                    const Byte high = (value * 13U + 7U) & 0x1FU;
                    for (const auto addr : kPrgAddresses) {
                        const auto physical = prg_offset(addr, low, high, outer, mode);
                        CAPTURE(outer, unused, mode, value, addr, physical);
                        REQUIRE(mapper->read_prg(addr) == cell(physical));
                    }
                }
            }
        }
    }

    auto small = board(0x8000UZ, kChrSize);
    small->write_prg(0x6800U, 3U);
    small->write_prg(0x8000U, 0x1FU);
    small->write_prg(0xA000U, 0x1EU);
    for (const auto addr : kPrgAddresses) {
        const auto physical = prg_offset(addr, 0x1FU, 0x1EU, 3U, 0U);
        CAPTURE(addr, physical);
        REQUIRE(small->read_prg(addr) == cell(physical % 0x8000UZ));
    }
}

TEST_CASE("Mapper 384 applies outer CHR pins after nine-bit VRC4 bank composition",
          "[mapper384][banking]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 4U; ++outer) {
        for (const Byte unused : {0U, 4U}) {
            mapper->write_prg(0x6800U, static_cast<Byte>(outer | unused));
            for (unsigned bank = 0U; bank < 512U; ++bank) {
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    set_chr_bank(*mapper, slot, bank);
                }
                const auto physical = ((bank & 0x7FU) | (outer << 7U)) * kChrBankSize + 0x123U;
                for (Address addr = 0x123U; addr < 0x2000U; addr += kChrBankSize) {
                    CAPTURE(outer, unused, bank, addr, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical));
                }
            }
        }
    }
    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto expected = mapper->read_chr(addr);
        mapper->write_chr(addr, static_cast<Byte>(addr ^ 0xFFU));
        REQUIRE(mapper->read_chr(addr) == expected);
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 384 exposes four CIRAM modes without cartridge nametable RAM",
          "[mapper384][ciram][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
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
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 384 gates only 2 KiB WRAM and leaves other CPU windows floating",
          "[mapper384][bus][ram]") {
    auto mapper = board();
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    mapper->write_prg(0x9008U, 1U);
    for (Address addr = 0U; addr < kWramSize; ++addr) {
        mapper->write_prg(static_cast<Address>(0x6000U + addr), cell(addr, 0xA5U));
    }
    for (Address addr = 0U; addr < kWramSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + addr)) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6800U + addr)) == cell(addr, 0xA5U));
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(0x7000U + addr)));
        REQUIRE(mapper->read_prg(static_cast<Address>(0x7000U + addr)) == 0U);
    }
    for (Address addr = 0U; addr < 0x6000U; addr += 0x100U) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 384 clocks every VRC4 IRQ latch in cycle and scanline modes",
          "[mapper384][irq][state]") {
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

    mapper->write_prg(0xF000U, 0xFFU);
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

TEST_CASE("Mapper 384 reset restores VRC4 outer IRQ and header state but retains WRAM",
          "[mapper384][reset][ram][audio]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal, 0x35U);
    const auto initial = state(*mapper);
    mapper->write_prg(0x9008U, 3U);
    mapper->write_prg(0x6800U, 0x0FU);
    mapper->write_prg(0x8000U, 0x17U);
    mapper->write_prg(0xA000U, 0x12U);
    set_chr_bank(*mapper, 0U, 0x1A5U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0xF008U, 6U);
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    const auto programmed = state(*mapper);

    mapper->reset();
    const auto reset = state(*mapper);
    REQUIRE(std::ranges::equal(std::span(reset).first(kWramOffset),
                               std::span(initial).first(kWramOffset)));
    REQUIRE(std::ranges::equal(std::span(reset).subspan(kWramOffset),
                               std::span(programmed).subspan(kWramOffset)));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
    REQUIRE(mapper->read_prg(0xA000U) == cell(kPrgBankSize, 0x35U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(14UZ * kPrgBankSize, 0x35U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(15UZ * kPrgBankSize, 0x35U));
    REQUIRE(mapper->read_chr(0x123U) == cell(0x123U, 0x35U));
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    const auto before = state(*mapper);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 384 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper384][state]") {
    auto mapper = board();
    mapper->write_prg(0x9008U, 3U);
    mapper->write_prg(0x6800U, 0x0FU);
    mapper->write_prg(0x8000U, 0x15U);
    mapper->write_prg(0xA000U, 0x12U);
    set_chr_bank(*mapper, 0U, 0x1A5U);
    set_chr_bank(*mapper, 7U, 0x076U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0x6801U, 0x42U);
    mapper->write_prg(0xF000U, 5U);
    mapper->write_prg(0xF004U, 4U);
    mapper->write_prg(0xF008U, 2U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x3000UZ);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(copy->read_prg(0x6000U) == 0xA7U);
    REQUIRE(copy->read_prg(0x6801U) == 0x42U);
    for (const auto addr : kPrgAddresses) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (Address addr = 0x123U; addr < 0x2000U; addr += kChrBankSize) {
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(mapper->read_chr(addr) + 0x80U));
    }
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);

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
    for (unsigned bit = 0U; bit < kOuterOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    const std::array<std::pair<std::size_t, Byte>, 10> invalid_fields{
        {{kOuterOffset, 0x10U},
         {kPrgBanksOffset, 0x20U},
         {kPrgBanksOffset + 1U, 0xFFU},
         {kMirrorOffset, 2U},
         {kMirrorOffset, 5U},
         {kModeOffset, 4U},
         {kIrqControlOffset, 8U},
         {kIrqFlagsOffset, 4U},
         {kIrqFlagsOffset, 2U},
         {kOuterOffset, 0xFFU}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    auto invalid_chr = saved;
    invalid_chr[kChrBanksOffset] = 0U;
    invalid_chr[kChrBanksOffset + 1U] = 2U;
    reject(invalid_chr);
    for (const std::uint16_t prescaler : {0U, 342U, 0xFFFFU}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kIrqPrescalerOffset, &prescaler, sizeof(prescaler));
        CAPTURE(prescaler);
        reject(invalid);
    }

    auto wrong_prg = board(kPrgSize / 2U, kChrSize);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(kPrgSize, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
