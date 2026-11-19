#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper359.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kPrgBaseOffset = 14UZ;
constexpr std::size_t kBankSizeOffset = 15UZ;
constexpr std::size_t kMirrorOffset = 16UZ;
constexpr std::size_t kChrBaseOffset = 17UZ;
constexpr std::size_t kM2CounterOffset = 26UZ;
constexpr std::size_t kPa12CounterOffset = 29UZ;
constexpr std::size_t kIrqModeOffset = 30UZ;
constexpr std::size_t kIrqFlagsOffset = 31UZ;
constexpr std::size_t kA12PinsOffset = 32UZ;
constexpr std::size_t kChrRamOffset = 33UZ;
constexpr std::size_t kRomStateSize = kChrRamOffset;
constexpr std::size_t kRamStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 5> kPrgSlots{0x6123U, 0x8123U, 0xA123U, 0xC123U, 0xE123U};
constexpr std::array<unsigned, 4> kPrgMasks{0x3FU, 0x1FU, 0x2FU, 0x0FU};

INesHeader header(bool chr_rom = false, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 359U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = chr_rom ? 0U : kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 17U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper359> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(chr_size != 0U, mirror);
    REQUIRE(Mapper359::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper359>(h, rom(prg_size, salt), rom(chr_size, salt + 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void edge(Mapper359& mapper, unsigned low_cycles = 3U) {
    mapper.observe_ppu_a12(false);
    for (unsigned cycle = 0U; cycle < low_cycles; ++cycle) {
        mapper.clock_cpu();
    }
    mapper.observe_ppu_a12(true);
}

TEST_CASE("Mapper 359 validates NES 2.0 ROM RAM and CIRAM geometry", "[mapper359]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg : {0x2000UZ, 0xA000UZ, 0x20000UZ, kPrgSize}) {
            for (const std::size_t chr : {0UZ, 0x2000UZ, 0x2400UZ, kChrSize}) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper359::validate_geometry(header(chr != 0U, mirror), prg, chr));
            }
        }
    }
    for (const auto id : {0U, 4U, 358U, 360U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper359::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper359::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper359::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x3000UZ, kPrgSize + 0x2000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper359::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size :
         {1UZ, 0x400UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, kChrSize + 0x400UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper359::validate_geometry(header(true), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper359::validate_geometry(header(false, mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = kChrRamSize;
            break;
        case 2U:
            h.prg_nvram_size = kChrRamSize;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        case 6U:
            h.chr_nvram_size = kChrRamSize;
            break;
        case 7U:
            h = header(true);
            h.chr_ram_size = kChrRamSize;
            break;
        default:
            h = header(true);
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper359::validate_geometry(h, kPrgSize, field >= 7U ? kChrSize : 0U));
    }
}

TEST_CASE("Mapper 359 claims only the documented CPU ROM aperture and has no audio",
          "[mapper359][bus][audio]") {
    for (const std::size_t chr : {0UZ, kChrSize}) {
        auto mapper = board(kPrgSize, chr);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        REQUIRE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
        for (unsigned raw = 0U; raw <= 0xFFFFU; raw += 0x101U) {
            const auto addr = static_cast<Address>(raw);
            CAPTURE(chr, addr);
            REQUIRE(mapper->maps_prg(addr) == (addr >= 0x6000U));
            REQUIRE(mapper->prg_read_mask(addr) == (addr >= 0x6000U ? 0xFFU : 0U));
            if (addr < 0x6000U) {
                REQUIRE(mapper->read_prg(addr) == 0U);
            }
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            REQUIRE(mapper->read_expansion(addr) == 0U);
        }
        const auto before = state(*mapper);
        mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
        mapper->clock_irq_counter();
        mapper->clock_cpu_falling_edge();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
        mapper->clock_audio();
        mapper->set_region(Region::PAL);
        REQUIRE(state(*mapper) == before);
        REQUIRE(mapper->audio_output() == 0.0F);
    }
}

TEST_CASE("Mapper 359 decodes every write alias exactly with F003", "[mapper359][bus]") {
    auto mapper = board(kPrgSize, kChrSize);
    auto expected = board(kPrgSize, kChrSize);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        expected->write_prg(static_cast<Address>(raw & 0xF003U), value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == state(*expected));
    }
    mapper->reset();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
    }
    REQUIRE(state(*mapper) == initial);
    mapper->write_prg_cpu(0x9002U, 0x83U, true);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
}

TEST_CASE("Mapper 359 applies all PRG masks outer banks and fixed-last pages",
          "[mapper359][banking]") {
    auto mapper = board();
    for (unsigned size = 0U; size < 4U; ++size) {
        for (unsigned base = 0U; base < 8U; ++base) {
            mapper->write_prg(0x9000U, static_cast<Byte>((base << 3U) | 0xC7U));
            mapper->write_prg(0x9001U, static_cast<Byte>(size | 0xBCU));
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                std::array<Byte, 4> banks{};
                for (unsigned reg = 0U; reg < 4U; ++reg) {
                    banks[reg] = static_cast<Byte>(raw + reg * 19U);
                    mapper->write_prg(static_cast<Address>(0x8000U + reg), banks[reg]);
                }
                const std::array<unsigned, 5> inner{
                    banks[3], banks[0], banks[1], banks[2], kPrgMasks[size]};
                for (unsigned slot = 0U; slot < inner.size(); ++slot) {
                    const unsigned bank = base * 16U | (inner[slot] & kPrgMasks[size]);
                    CAPTURE(size, base, raw, slot, bank);
                    REQUIRE(mapper->read_prg(kPrgSlots[slot]) ==
                            cell(static_cast<std::size_t>(bank) * 0x2000U + 0x123U));
                }
            }
        }
    }
}

TEST_CASE("Mapper 359 wraps fitted PRG and CHR only after pin decoding", "[mapper359][banking]") {
    auto mapper = board(0xA000U, 0x2400U);
    mapper->write_prg(0x9000U, 0x38U);
    mapper->write_prg(0x8000U, 0xFFU);
    const auto prg_offset = (127UZ * 0x2000U + 0x123U) % 0xA000U;
    REQUIRE(mapper->read_prg(0x8123U) == cell(prg_offset));

    mapper->write_prg(0x9001U, 0x40U);
    mapper->write_prg(0x9003U, 3U);
    mapper->write_prg(0xA000U, 0xFFU);
    const auto chr_offset = (511UZ * 0x400U + 0x123U) % 0x2400U;
    REQUIRE(mapper->read_chr(0x0123U) == cell(chr_offset, 0x55U));
}

TEST_CASE("Mapper 359 applies CHR size and outer selection to all eight windows",
          "[mapper359][banking]") {
    auto mapper = board(kPrgSize, kChrSize);
    for (unsigned wide = 0U; wide < 2U; ++wide) {
        for (unsigned base = 0U; base < 4U; ++base) {
            mapper->write_prg(0x9001U, static_cast<Byte>(wide << 6U));
            mapper->write_prg(0x9003U, static_cast<Byte>(base | 0xFCU));
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const unsigned mask = wide != 0U ? 0xFFU : 0x7FU;
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto value = static_cast<Byte>(raw + slot * 13U);
                    mapper->write_prg(
                        static_cast<Address>((slot < 4U ? 0xA000U : 0xB000U) | (slot & 3U)), value);
                    const unsigned bank = base * 128U | (value & mask);
                    const auto addr = static_cast<Address>(slot * 0x400U + 0x123U);
                    CAPTURE(wide, base, raw, slot, bank);
                    REQUIRE(mapper->read_chr(addr) ==
                            cell(static_cast<std::size_t>(bank) * 0x400U + 0x123U, 0x55U));
                }
            }
        }
    }
}

TEST_CASE("Mapper 359 CHR RAM is flat writable and CHR ROM is read-only",
          "[mapper359][ram][banking]") {
    auto ram = board();
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        ram->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        ram->write_prg(0x9003U, static_cast<Byte>(value));
        ram->write_prg(0xA000U, static_cast<Byte>(value));
        ram->write_prg(0xB003U, static_cast<Byte>(value ^ 0xFFU));
        for (unsigned addr = 0U; addr < kChrRamSize; addr += 0x101U) {
            CAPTURE(value, addr);
            REQUIRE(ram->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
        }
    }
    REQUIRE(ram->read_chr(0x0123U) == ram->read_chr(0x2123U));

    auto rom_mapper = board(kPrgSize, kChrSize);
    rom_mapper->write_prg(0xA000U, 0U);
    const auto original = rom_mapper->read_chr(0x0123U);
    rom_mapper->write_chr(0x0123U, static_cast<Byte>(original ^ 0xFFU));
    REQUIRE(rom_mapper->read_chr(0x0123U) == original);
}

TEST_CASE("Mapper 359 selects all four CIRAM modes without custom nametable storage",
          "[mapper359][ciram]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    constexpr std::array<MirrorMode, 4> modes{MirrorMode::Vertical,
                                              MirrorMode::Horizontal,
                                              MirrorMode::SingleLower,
                                              MirrorMode::SingleUpper};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x9002U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() == modes[value & 3U]);
        REQUIRE(state(*mapper)[kMirrorOffset] == (value & 3U));
        REQUIRE(mapper->maps_nametable(0x2000U));
        REQUIRE(mapper->maps_nametable(0x3EFFU));
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->nametable_address(0x2000U));
        REQUIRE_FALSE(mapper->ciram_address(0x2000U));
        REQUIRE_FALSE(mapper->read_nametable(0x2000U));
        REQUIRE_FALSE(mapper->write_nametable(0x2000U, 0xA5U));
    }
}

TEST_CASE("Mapper 359 M2 IRQ counts to zero and honors manual and auto enables",
          "[mapper359][irq]") {
    auto mapper = board(kPrgSize, kChrSize);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 1U);
    mapper->write_prg(0xC002U, 1U);
    for (unsigned cycle = 1U; cycle <= 258U; ++cycle) {
        mapper->clock_cpu();
        const auto counter = static_cast<std::uint16_t>(258U - cycle);
        CAPTURE(cycle, counter);
        REQUIRE(state(*mapper)[kM2CounterOffset] == (counter & 0xFFU));
        REQUIRE(state(*mapper)[kM2CounterOffset + 1U] == (counter >> 8U));
        REQUIRE(mapper->irq_pending() == (cycle == 258U));
    }
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC003U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xC003U, 0xFFU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC002U, 0xFEU);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC002U, 4U);
    mapper->write_prg(0xC000U, 1U);
    REQUIRE(state(*mapper)[kIrqModeOffset] == 4U);
    mapper->write_prg(0xC001U, 0U);
    REQUIRE(state(*mapper)[kIrqModeOffset] == 5U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqModeOffset] == 4U);
    mapper->write_prg(0xC003U, 1U);
    REQUIRE(state(*mapper)[kIrqModeOffset] == 4U);
    mapper->write_prg(0xC001U, 0U);
    REQUIRE(state(*mapper)[kIrqModeOffset] == 5U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 359 PA12 IRQ follows MMC3 reload and filtered edge behavior",
          "[mapper359][irq]") {
    auto mapper = board(kPrgSize, kChrSize);
    mapper->write_prg(0xC002U, 2U);
    mapper->write_prg(0xC001U, 2U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC003U, 1U);
    for (unsigned pulse = 1U; pulse <= 3U; ++pulse) {
        edge(*mapper);
        CAPTURE(pulse);
        REQUIRE(state(*mapper)[kPa12CounterOffset] == 3U - pulse);
        REQUIRE(mapper->irq_pending() == (pulse == 3U));
    }
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    edge(*mapper);
    REQUIRE(state(*mapper)[kPa12CounterOffset] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC003U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    for (unsigned low = 0U; low <= 4U; ++low) {
        mapper->reset();
        mapper->write_prg(0xC002U, 2U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC003U, 1U);
        edge(*mapper, low);
        CAPTURE(low);
        REQUIRE(mapper->irq_pending() == (low >= 3U));
    }
    mapper->acknowledge_irq();
    edge(*mapper);
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    mapper->write_prg(0xC002U, 2U);
    mapper->write_prg(0xC001U, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC003U, 1U);
    edge(*mapper);
    const auto counter = state(*mapper)[kPa12CounterOffset];
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(true);
    REQUIRE(state(*mapper)[kPa12CounterOffset] == counter);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC002U, 6U);
    mapper->write_prg(0xC001U, 0U);
    REQUIRE(state(*mapper)[kIrqModeOffset] == 7U);
    mapper->write_prg(0xC000U, 0U);
    REQUIRE(state(*mapper)[kIrqModeOffset] == 6U);
    mapper->write_prg(0xC003U, 1U);
    REQUIRE(state(*mapper)[kIrqModeOffset] == 6U);
    edge(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC001U, 0U);
    edge(*mapper);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 359 reset restores registers and cold power clears CHR RAM",
          "[mapper359][reset]") {
    for (const std::size_t chr : {0UZ, kChrSize}) {
        auto mapper = board(kPrgSize, chr, MirrorMode::Horizontal);
        auto expected = board(kPrgSize, chr, MirrorMode::Horizontal);
        mapper->write_prg(0x8000U, 0x91U);
        mapper->write_prg(0x8003U, 0x72U);
        mapper->write_prg(0x9000U, 0x38U);
        mapper->write_prg(0x9001U, 0x43U);
        mapper->write_prg(0x9002U, 3U);
        mapper->write_prg(0x9003U, 3U);
        mapper->write_prg(0xB003U, 0xA6U);
        mapper->write_prg(0xC002U, 1U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->clock_cpu();
        mapper->observe_ppu_a12(false);
        mapper->clock_cpu();
        mapper->clock_cpu();
        if (chr == 0U) {
            mapper->write_chr(0x0321U, 0xA7U);
            expected->write_chr(0x0321U, 0xA7U);
        }
        REQUIRE(mapper->irq_pending());
        REQUIRE(state(*mapper) != state(*expected));

        mapper->reset();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(state(*mapper) == state(*expected));
        if (chr == 0U) {
            REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);
        }
        mapper->power_on();
        REQUIRE(state(*mapper) == state(*board(kPrgSize, chr, MirrorMode::Horizontal)));
        if (chr == 0U) {
            REQUIRE(mapper->read_chr(0x0321U) == 0U);
        }
    }
}

TEST_CASE("Mapper 359 bounded snapshots replay both memory populations without ROM",
          "[mapper359][state]") {
    for (const std::size_t chr : {0UZ, kChrSize}) {
        auto mapper = board(kPrgSize, chr);
        mapper->write_prg(0x8000U, 0x91U);
        mapper->write_prg(0x8003U, 0x72U);
        mapper->write_prg(0x9000U, 0x28U);
        mapper->write_prg(0x9001U, 0x41U);
        mapper->write_prg(0x9002U, 2U);
        mapper->write_prg(0x9003U, 2U);
        mapper->write_prg(0xA000U, 0xC5U);
        mapper->write_prg(0xC002U, 2U);
        mapper->write_prg(0xC001U, 2U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC003U, 1U);
        edge(*mapper);
        mapper->observe_ppu_a12(false);
        mapper->clock_cpu();
        mapper->clock_cpu();
        if (chr == 0U) {
            for (unsigned addr = 0U; addr < kChrRamSize; addr += 0x101U) {
                mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
            }
            mapper->write_chr(0x0123U, cell(0x0123U, 0xA5U));
        }
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == (chr == 0U ? kRamStateSize : kRomStateSize));
        REQUIRE(saved.size() < 0x2100U);

        auto copy = board(kPrgSize, chr, MirrorMode::Vertical, 0x80U);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->mirror_mode() == MirrorMode::SingleLower);
        REQUIRE(copy->read_prg(0x8123U) == static_cast<Byte>(mapper->read_prg(0x8123U) + 0x80U));
        REQUIRE(copy->read_chr(0x0123U) ==
                (chr == 0U ? cell(0x0123U, 0xA5U)
                           : static_cast<Byte>(mapper->read_chr(0x0123U) + 0x80U)));
        copy->clock_cpu();
        mapper->clock_cpu();
        copy->observe_ppu_a12(true);
        mapper->observe_ppu_a12(true);
        REQUIRE(state(*copy) == state(*mapper));
        edge(*copy);
        edge(*mapper);
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

TEST_CASE("Mapper 359 rejects malformed and incompatible states atomically", "[mapper359][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x91U);
    mapper->write_prg(0x9000U, 0x28U);
    mapper->write_prg(0x9001U, 0x41U);
    mapper->write_prg(0x9002U, 3U);
    mapper->write_prg(0x9003U, 2U);
    mapper->write_prg(0xB003U, 0xA6U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xC002U, 1U);
    mapper->clock_cpu();
    mapper->observe_ppu_a12(false);
    mapper->clock_cpu();
    mapper->clock_cpu();
    mapper->write_chr(0x0234U, 0xA7U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 13> invalid_fields{{
        {0U, 0U},
        {2U, 1U},
        {6U, 1U},
        {kPrgBaseOffset, 0x80U},
        {kBankSizeOffset, 0x80U},
        {kMirrorOffset, 4U},
        {kChrBaseOffset, 4U},
        {kIrqModeOffset, 8U},
        {kIrqFlagsOffset, 4U},
        {kIrqModeOffset, 0U},
        {kA12PinsOffset, 7U},
        {kA12PinsOffset, 3U},
        {kA12PinsOffset, 0xFFU},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto other_prg = board(0x80000U);
    const auto prg_before = state(*other_prg);
    StateReader wrong_prg(saved);
    other_prg->load_state(wrong_prg);
    REQUIRE_FALSE(wrong_prg.ok());
    REQUIRE(state(*other_prg) == prg_before);

    auto other_chr = board(kPrgSize, kChrSize);
    const auto chr_before = state(*other_chr);
    StateReader wrong_chr(saved);
    other_chr->load_state(wrong_chr);
    REQUIRE_FALSE(wrong_chr.ok());
    REQUIRE(state(*other_chr) == chr_before);

    auto other_mirror = board(kPrgSize, 0U, MirrorMode::Horizontal);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);
}

} // namespace
