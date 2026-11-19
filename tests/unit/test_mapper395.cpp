#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper395.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kChrSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kOuter1Offset = 10UZ;
constexpr std::size_t kOuter2Offset = 11UZ;
constexpr std::size_t kBankSelectOffset = 12UZ;
constexpr std::size_t kBankModeOffset = 13UZ;
constexpr std::size_t kBanksOffset = 14UZ;
constexpr std::size_t kIrqCounterOffset = 23UZ;
constexpr std::size_t kIrqReloadOffset = 24UZ;
constexpr std::size_t kIrqFlagsOffset = 25UZ;
constexpr std::size_t kRamFlagsOffset = 26UZ;
constexpr std::size_t kStateSize = 27UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 395U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 14U) * 37U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 79U + (offset >> 20U) * 83U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper395> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper395::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper395>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper395& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper395& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper395& mapper, Byte first, Byte second = 0U) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(0x6000U, first);
    mapper.write_prg(0x6010U, second);
}

unsigned expected_inner_prg(Address addr, Byte outer2, Byte mode, Byte r6, Byte r7) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (mode & 0x40U) != 0U;
    const unsigned mask = (outer2 & 8U) != 0U ? 15U : 31U;
    if (slot == 1U) {
        return r7 & mask;
    }
    if ((slot == 0U) != swapped) {
        return r6 & mask;
    }
    return slot == 3U ? mask : mask - 1U;
}

std::size_t expected_prg(Address addr, Byte outer1, Byte outer2, Byte mode, Byte r6, Byte r7) {
    const unsigned inner = expected_inner_prg(addr, outer2, mode, r6, r7);
    const unsigned a17 = (outer2 & 8U) != 0U ? outer2 & 1U : (inner >> 4U) & 1U;
    const unsigned physical = (inner & 15U) | (a17 << 4U) | (((outer1 >> 4U) & 1U) << 5U) |
                              (((outer1 >> 5U) & 1U) << 6U) | (((outer1 >> 3U) & 1U) << 7U);
    return physical * kPrgBankSize + (addr & 0x1FFFU);
}

unsigned expected_inner_chr(Address addr, Byte mode, const std::array<Byte, 6>& values) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (values[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return values[slot - 2U];
}

std::size_t expected_chr(
    Address addr, Byte outer1, Byte outer2, Byte mode, const std::array<Byte, 6>& values) {
    const unsigned inner = expected_inner_chr(addr, mode, values);
    const unsigned a17 = (outer2 & 0x40U) != 0U ? ((outer2 >> 4U) & 1U) : (inner >> 7U) & 1U;
    const unsigned physical = (inner & 0x7FU) | (a17 << 7U) | (((outer1 >> 4U) & 1U) << 8U) |
                              (((outer1 >> 5U) & 1U) << 9U) | (((outer2 >> 5U) & 1U) << 10U);
    return physical * kChrBankSize + (addr & 0x03FFU);
}

TEST_CASE("Mapper 395 validates decoded ROM bounds memory trainer and CIRAM metadata",
          "[mapper395][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        CAPTURE(mirror);
        auto mapper = board(kPrgSize, kChrSize, mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper395::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size = 0x2000UZ; size <= kChrSize; size += kChrBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper395::validate_geometry(header(), kPrgSize, size));
    }

    for (const unsigned id : {0U, 4U, 391U, 394U, 396U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper395::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper395::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper395::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x200001UZ, 0x400000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper395::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2200UZ, 0x200001UZ, 0x400000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper395::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper395::validate_geometry(header(mirror), kPrgSize, kChrSize));
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
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper395::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 395 decodes every outer alias and masks all register data bits",
          "[mapper395][bus][decoding]") {
    auto mapper = board();
    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw), 0xA5U);
        const auto current = state(*mapper);
        const bool first = (raw & 0xE010U) == 0x6000U;
        const bool second = (raw & 0xE010U) == 0x6010U;
        CAPTURE(raw, first, second);
        REQUIRE(current[kOuter1Offset] == (first ? 0x20U : 0U));
        REQUIRE(current[kOuter2Offset] == (second ? 0xA1U : 0U));
        REQUIRE((first || second));
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(raw)));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(raw)) == 0U);
        REQUIRE(mapper->read_prg(static_cast<Address>(raw)) == 0U);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0x6E00U, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kOuter1Offset] == (value & 0x38U));
        mapper->write_prg(0x6FF0U, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kOuter2Offset] == (value & 0xF9U));
    }
}

TEST_CASE("Mapper 395 outer lock and MMC3 WRAM gates cover the whole low window",
          "[mapper395][bus][ram][reset]") {
    auto mapper = board();
    outer(*mapper, 0x38U, 0xF9U);
    const auto locked = state(*mapper);
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0x6010U, 0U);
    REQUIRE(state(*mapper)[kOuter1Offset] == locked[kOuter1Offset]);
    REQUIRE(state(*mapper)[kOuter2Offset] == locked[kOuter2Offset]);
    bank(*mapper, 6U, 19U);
    REQUIRE(state(*mapper)[kBanksOffset + 6U] == 19U);

    mapper->write_prg(0xA001U, 0U);
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(state(*mapper)[kOuter1Offset] == locked[kOuter1Offset]);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6010U, 0U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(state(*mapper)[kOuter2Offset] == locked[kOuter2Offset]);
    for (const Address addr : {0x6000U, 0x6010U, 0x6123U, 0x6FF0U, 0x7000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }

    mapper->reset();
    mapper->write_prg(0x6000U, 0x18U);
    mapper->write_prg(0x6010U, 0x49U);
    REQUIRE(state(*mapper)[kOuter1Offset] == 0x18U);
    REQUIRE(state(*mapper)[kOuter2Offset] == 0x49U);
    mapper->write_prg(0x6000U, 0xA5U);
    mapper->write_prg(0x6010U, 0xA5U);
    REQUIRE(state(*mapper)[kOuter1Offset] == 0x20U);
    REQUIRE(state(*mapper)[kOuter2Offset] == 0xA1U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6010U) == 0U);
}

TEST_CASE("Mapper 395 applies all outer PRG pins and both inner bank widths",
          "[mapper395][banking]") {
    auto mapper = board();
    constexpr Byte kR6 = 0x35U;
    constexpr Byte kR7 = 0x17U;
    for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
        bank(*mapper, 6U, kR6, mode);
        bank(*mapper, 7U, kR7, mode);
        for (unsigned first = 0U; first < 64U; ++first) {
            if ((first & 0x38U) != first) {
                continue;
            }
            for (unsigned pins = 0U; pins < 4U; ++pins) {
                const auto second = static_cast<Byte>((pins & 2U) << 2U | (pins & 1U));
                outer(*mapper, static_cast<Byte>(first), second);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto addr = static_cast<Address>(0x8123U + slot * kPrgBankSize);
                    const auto physical =
                        expected_prg(addr, static_cast<Byte>(first), second, mode, kR6, kR7);
                    CAPTURE(mode, first, second, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }

    for (const std::size_t size : {0x8000UZ, 0x18000UZ, 0x48000UZ, 0x1FE000UZ}) {
        auto fitted = board(size, kChrSize);
        bank(*fitted, 6U, 0x3FU, 0x40U);
        bank(*fitted, 7U, 0x2FU, 0x40U);
        outer(*fitted, 0x38U, 0x09U);
        for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
            const auto physical = expected_prg(addr, 0x38U, 0x09U, 0x40U, 0x3FU, 0x2FU) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 395 combines outer CHR pins with paired and inverted MMC3 banks",
          "[mapper395][banking]") {
    auto mapper = board();
    constexpr std::array<Byte, 6> values = {0xA2U, 0x8BU, 0x93U, 0xACU, 0xB1U, 0x87U};
    for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*mapper, reg, values[reg], mode);
        }
        for (unsigned first = 0U; first < 64U; ++first) {
            if ((first & 0x38U) != first) {
                continue;
            }
            for (unsigned pins = 0U; pins < 8U; ++pins) {
                const auto second =
                    static_cast<Byte>((pins & 4U) << 3U | (pins & 2U) << 5U | (pins & 1U) << 4U);
                outer(*mapper, static_cast<Byte>(first), second);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                    const auto physical =
                        expected_chr(addr, static_cast<Byte>(first), second, mode, values);
                    CAPTURE(mode, first, second, addr, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical));
                    mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == cell(physical));
                }
            }
        }
    }

    for (const std::size_t size : {0x2000UZ, 0x3000UZ, 0x48000UZ, 0x1FFC00UZ}) {
        auto fitted = board(kPrgSize, size);
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*fitted, reg, values[reg]);
        }
        outer(*fitted, 0x38U, 0x79U);
        for (const Address addr : {0x0000U, 0x03FFU, 0x0800U, 0x1000U, 0x1555U, 0x1FFFU}) {
            const auto physical = expected_chr(addr, 0x38U, 0x79U, 0U, values) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_chr(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 395 decodes all MMC3 register aliases without outer side effects",
          "[mapper395][bus][banking][irq]") {
    auto mapper = board();
    outer(*mapper, 0x28U, 0x69U);
    for (unsigned addr = 0x8000U; addr < 0xA000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0xC5U);
        CAPTURE(addr);
        REQUIRE(state(*mapper)[kBankSelectOffset] == 5U);
        REQUIRE(state(*mapper)[kBankModeOffset] == 0xC0U);
    }
    for (unsigned addr = 0x8001U; addr < 0xA000U; addr += 2U) {
        mapper->write_prg(0x8000U, 2U);
        mapper->write_prg(static_cast<Address>(addr), 0xA6U);
        REQUIRE(state(*mapper)[kBanksOffset + 2U] == 0xA6U);
    }
    for (unsigned addr = 0xA000U; addr < 0xC000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr >> 1U));
        REQUIRE(mapper->mirror_mode() ==
                ((addr & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (unsigned addr = 0xA001U; addr < 0xC000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0xC0U);
        REQUIRE(state(*mapper)[kRamFlagsOffset] == 0xC0U);
    }
    for (unsigned addr = 0xC000U; addr < 0xE000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        REQUIRE(state(*mapper)[kIrqReloadOffset] == static_cast<Byte>(addr));
    }
    for (unsigned addr = 0xC001U; addr < 0xE000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0U);
        REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
        REQUIRE((state(*mapper)[kIrqFlagsOffset] & 1U) != 0U);
    }
    mapper->write_prg(0xC000U, 0U);
    for (unsigned addr = 0xE000U; addr < 0x10000U; addr += 2U) {
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(static_cast<Address>(addr), 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    REQUIRE(state(*mapper)[kOuter1Offset] == 0x28U);
    REQUIRE(state(*mapper)[kOuter2Offset] == 0x69U);
}

TEST_CASE("Mapper 395 uses MMC3 horizontal vertical console CIRAM only", "[mapper395][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, kChrSize, initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg((value & 1U) != 0U ? 0xBFFEU : 0xA000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == initial);
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
    }
}

TEST_CASE("Mapper 395 exposes no CPU RAM expansion device or audio",
          "[mapper395][bus][ram][irq][audio]") {
    auto mapper = board();
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    mapper->write_prg(0x6000U, 0x38U);
    mapper->write_prg(0x6010U, 0x09U);
    REQUIRE(mapper->read_prg(0xE000U) == cell(expected_prg(0xE000U, 0x38U, 0x09U, 0U, 0U, 0U)));

    for (Address addr = 0U; addr < 0x6000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE_FALSE(mapper->maps_prg(0x6020U));
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    mapper->reset();
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
    REQUIRE_FALSE(mapper->irq_pending());
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

TEST_CASE("Mapper 395 MMC3 IRQ latch reload acknowledge disable and zero repeats",
          "[mapper395][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    outer(*mapper, 0x38U, 0x69U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    for (unsigned clock = 0U; clock < 3U; ++clock) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
    }
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 395 reset and cold power restore all hardware registers",
          "[mapper395][reset][ram]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    outer(*mapper, 0x38U, 0xF9U);
    bank(*mapper, 6U, 19U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(state(*mapper)[kOuter1Offset] == 0U);
    REQUIRE(state(*mapper)[kOuter2Offset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    outer(*mapper, 0x28U, 0x69U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 395 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper395][state]") {
    auto mapper = board();
    outer(*mapper, 0x38U, 0x69U);
    bank(*mapper, 6U, 19U, 0xC0U);
    bank(*mapper, 7U, 7U, 0xC0U);
    bank(*mapper, 0U, 0xA2U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 27U);
    REQUIRE(saved[kOuter1Offset] == 0x38U);
    REQUIRE(saved[kOuter2Offset] == 0x69U);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
        const auto physical = expected_prg(addr, 0x38U, 0x69U, 0xC0U, 19U, 7U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        REQUIRE(copy->read_prg(addr) == 0U);
    }
    copy->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE_FALSE(copy->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);

    bank(*mapper, 7U, 9U, 0xC0U);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };
    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 14> invalid_fields = {{{0U, 0U},
                                                                              {1U, 0xFFU},
                                                                              {2U, 1U},
                                                                              {6U, 1U},
                                                                              {10U, 1U},
                                                                              {11U, 2U},
                                                                              {12U, 8U},
                                                                              {13U, 1U},
                                                                              {20U, 0x40U},
                                                                              {21U, 0x40U},
                                                                              {22U, 0xFFU},
                                                                              {25U, 8U},
                                                                              {25U, 4U},
                                                                              {26U, 0x3FU}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
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
