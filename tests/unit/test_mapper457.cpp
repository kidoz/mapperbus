#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper457.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kChrSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kBankSelectOffset = 13UZ;
constexpr std::size_t kBankModeOffset = 14UZ;
constexpr std::size_t kBankRegistersOffset = 15UZ;
constexpr std::size_t kMirrorOffset = 23UZ;
constexpr std::size_t kIrqCounterOffset = 24UZ;
constexpr std::size_t kIrqReloadOffset = 25UZ;
constexpr std::size_t kIrqFlagsOffset = 26UZ;
constexpr std::size_t kRamFlagsOffset = 27UZ;
constexpr std::size_t kStateSize = 28UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9FFFU, 0xA000U, 0xC123U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 457U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 13U) * 37U + (offset >> 15U) * 43U + (offset >> 17U) * 61U +
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

std::unique_ptr<Mapper457> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper457::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper457>(h, rom(kPrgSize, salt), rom(kChrSize, salt ^ 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper457& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper457& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper457& mapper, Byte value, Address addr = 0x6000U) {
    mapper.write_prg(addr, value);
}

std::size_t expected_prg(Address addr, Byte outer_bank, Byte mode, Byte r6, Byte r7) {
    const unsigned mask = (outer_bank & 8U) != 0U ? 0x1FU : 0x0FU;
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned selected =
        slot == 1U                                                                       ? r7 & mask
        : (((mode & 0x40U) == 0U && slot == 0U) || ((mode & 0x40U) != 0U && slot == 2U)) ? r6 & mask
        : slot == 3U ? mask
                     : mask - 1U;
    const std::size_t outer_size = (outer_bank & 8U) != 0U ? 0x040000UZ : 0x020000UZ;
    const std::size_t physical =
        (outer_bank & 7U) * outer_size + selected * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return physical % kPrgSize;
}

unsigned expected_chr_bank(Address addr,
                           Byte outer_bank,
                           Byte mode,
                           const std::array<Byte, 6>& values) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner =
        slot < 4U ? (values[slot / 2U] & 0xFEU) | (slot & 1U) : values[slot - 2U];
    return inner & ((outer_bank & 8U) != 0U ? 0xFFU : 0x7FU);
}

std::size_t expected_chr(Address addr,
                         Byte outer_bank,
                         Byte mode,
                         const std::array<Byte, 6>& values) {
    const std::size_t outer_size = (outer_bank & 8U) != 0U ? 0x040000UZ : 0x020000UZ;
    const std::size_t physical = (outer_bank & 7U) * outer_size +
                                 expected_chr_bank(addr, outer_bank, mode, values) * kChrBankSize +
                                 (addr & (kChrBankSize - 1U));
    return physical % kChrSize;
}

void require_prg(
    Mapper457& mapper, Address addr, Byte outer_bank, Byte mode, Byte r6, Byte r7, Byte salt = 0U) {
    const auto physical = expected_prg(addr, outer_bank, mode, r6, r7);
    CAPTURE(addr, outer_bank, mode, r6, r7, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 457 validates NES 2.0 ROM-only decoder geometry", "[mapper457][geometry][ram]") {
    for (std::size_t prg = 0x008000UZ; prg <= kPrgSize; prg *= 2U) {
        for (std::size_t chr = 0x002000UZ; chr <= kChrSize; chr *= 2U) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper457::validate_geometry(header(), prg, chr));
        }
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        require_prg(*mapper, 0x8000U, 0U, 0U, 0U, 0U, 0x35U);
        require_prg(*mapper, 0xC000U, 0U, 0U, 0U, 0U, 0x35U);
        require_prg(*mapper, 0xE000U, 0U, 0U, 0U, 0U, 0x35U);
        REQUIRE(mapper->read_chr(0x0000U) == cell(0U, 0x35U ^ 0x55U));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
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
        REQUIRE_FALSE(Mapper457::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 47U, 456U, 458U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, kChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, kChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, kChrSize);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x1000UZ,
                                   0x4000UZ,
                                   0x006000UZ,
                                   0x018000UZ,
                                   kPrgSize - 1U,
                                   kPrgSize + 1U,
                                   kPrgSize * 2U}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x1000UZ,
                                   0x3000UZ,
                                   0x006000UZ,
                                   0x018000UZ,
                                   kChrSize - 1U,
                                   kChrSize + 1U,
                                   kChrSize * 2U}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, kChrSize);
    }
    for (unsigned field = 0U; field < 6U; ++field) {
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
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, kChrSize);
    }
}

TEST_CASE("Mapper 457 decodes and gates every outer-register address",
          "[mapper457][decoding][bus][ram]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        for (unsigned gate = 0U; gate < 256U; ++gate) {
            mapper->reset();
            mapper->write_prg(0xA001U, static_cast<Byte>(gate));
            auto expected = state(*mapper);
            const auto addr = static_cast<Address>(raw);
            const auto value = static_cast<Byte>(raw * 41U + gate + 0xA5U);
            mapper->write_prg(addr, value);
            if (raw >= 0x6000U && (gate & 0x80U) != 0U) {
                expected[kOuterOffset] = value & 0x0FU;
            }
            CAPTURE(raw, gate, value);
            REQUIRE(state(*mapper) == expected);
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0U);
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    mapper->reset();
    mapper->write_prg(0xA001U, 0xC0U);
    outer(*mapper, 0x0FU, 0x7FFFU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0FU);
    REQUIRE(state(*mapper)[kRamFlagsOffset] == 0xC0U);
}

TEST_CASE("Mapper 457 decodes every MMC3 alias without unrelated side effects",
          "[mapper457][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U) + 0xA5U);
        mapper->write_prg(addr, value);
        auto expected = cold;
        const bool even = (raw & 1U) == 0U;
        if (raw < 0xA000U) {
            if (even) {
                expected[kBankSelectOffset] = value & 7U;
                expected[kBankModeOffset] = value & 0xC0U;
            } else {
                expected[kBankRegistersOffset] = value;
            }
        } else if (raw < 0xC000U) {
            if (even) {
                expected[kMirrorOffset] = (value & 1U) ^ 1U;
            } else {
                expected[kRamFlagsOffset] = value & 0xC0U;
            }
        } else if (raw < 0xE000U) {
            if (even) {
                expected[kIrqReloadOffset] = value;
            } else {
                expected[kIrqFlagsOffset] = 1U;
            }
        } else if (!even) {
            expected[kIrqFlagsOffset] = 2U;
        }
        CAPTURE(raw, value, even);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 457 applies SBBB PRG pins after MMC3 bank decoding",
          "[mapper457][banking][prg]") {
    auto mapper = board();
    for (unsigned raw_outer = 0U; raw_outer < 16U; ++raw_outer) {
        const auto outer_bank = static_cast<Byte>(raw_outer);
        outer(*mapper, static_cast<Byte>(0xF0U | outer_bank));
        for (unsigned value = 0U; value < 256U; ++value) {
            for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
                bank(*mapper, 6U, static_cast<Byte>(value), mode);
                bank(*mapper, 7U, static_cast<Byte>(value ^ 0xA5U), mode);
                const auto r6 = static_cast<Byte>(value & 0x3FU);
                const auto r7 = static_cast<Byte>((value ^ 0xA5U) & 0x3FU);
                for (const auto addr : kPrgAddresses) {
                    require_prg(*mapper, addr, outer_bank, mode, r6, r7);
                }
            }
        }
    }

    bank(*mapper, 6U, 0xABU, 0x40U);
    bank(*mapper, 7U, 0xCDU, 0x40U);
    outer(*mapper, 0xFFU, 0x7FFFU);
    for (Address addr = 0x8000U; addr != 0U; ++addr) {
        require_prg(*mapper, addr, 0x0FU, 0x40U, 0x2BU, 0x0DU);
    }
}

TEST_CASE("Mapper 457 applies SBBB CHR pins after pairing and inversion",
          "[mapper457][banking][chr]") {
    constexpr std::array<Byte, 6> values = {0xA2U, 0x8BU, 0x93U, 0xFCU, 0xB1U, 0xC7U};
    auto mapper = board();
    for (Byte outer_bank = 0U; outer_bank < 16U; ++outer_bank) {
        outer(*mapper, static_cast<Byte>(0xF0U | outer_bank));
        for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
            for (Byte reg = 0U; reg < 6U; ++reg) {
                bank(*mapper, reg, values[reg], mode);
            }
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                for (const Address within : {Address{0U}, Address{0x155U}, Address{0x3FFU}}) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + within);
                    const auto physical = expected_chr(addr, outer_bank, mode, values);
                    CAPTURE(outer_bank, mode, addr, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                    mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                }
            }
        }
    }

    outer(*mapper, 0x0FU);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto physical = expected_chr(addr, 0x0FU, 0x80U, values);
        CAPTURE(addr, physical);
        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
    }
}

TEST_CASE("Mapper 457 wraps fitted ROMs only after outer-pin decoding", "[mapper457][banking]") {
    for (const std::size_t size : {0x008000UZ, 0x020000UZ, 0x100000UZ}) {
        auto mapper = std::make_unique<Mapper457>(header(), rom(size, 0x23U), rom(size, 0x91U));
        outer(*mapper, 0x0FU);
        bank(*mapper, 6U, 0xFFU);
        bank(*mapper, 7U, 0xFEU);
        bank(*mapper, 0U, 0xFFU);
        for (const auto addr : kPrgAddresses) {
            CAPTURE(size, addr);
            REQUIRE(mapper->read_prg(addr) ==
                    cell(expected_prg(addr, 0x0FU, 0U, 0x3FU, 0x3EU) % size, 0x23U));
        }
        for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
            CAPTURE(size, addr);
            REQUIRE(mapper->read_chr(addr) ==
                    cell(expected_chr(addr, 0x0FU, 0U, {0xFFU, 0U, 0U, 0U, 0U, 0U}) % size, 0x91U));
        }
    }
}

TEST_CASE("Mapper 457 uses MMC3 CIRAM control and exposes no RAM or audio",
          "[mapper457][ciram][ram][audio][bus]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg((value & 1U) != 0U ? 0xBFFEU : 0xA000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        outer(*mapper, 0x0FU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == initial);
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
    }

    auto mapper = board();
    const auto before = state(*mapper);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->maps_expansion(0x6000U));
    REQUIRE(mapper->expansion_read_mask(0x6000U) == 0xFFU);
    REQUIRE(mapper->read_expansion(0x6000U) == 0U);
    mapper->write_expansion(0x6000U, 0x0FU);

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 457 retains MMC3 IRQ acknowledgement and zero repeats", "[mapper457][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    outer(*mapper, 0x0FU);
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
    for (unsigned clock = 0U; clock < 4U; ++clock) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 457 reset clears the outer latch and every MMC3 register", "[mapper457][reset]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        const auto cold = state(*mapper);
        outer(*mapper, 0x0FU);
        bank(*mapper, 6U, 0x19U, 0xC0U);
        bank(*mapper, 7U, 0x07U, 0xC0U);
        mapper->write_prg(0xA000U, initial == MirrorMode::Vertical ? 1U : 0U);
        mapper->write_prg(0xA001U, 0U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        REQUIRE(state(*mapper) != cold);

        mapper->reset();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(state(*mapper)[kOuterOffset] == 0U);
        REQUIRE(mapper->mirror_mode() == initial);
        REQUIRE_FALSE(mapper->irq_pending());
        require_prg(*mapper, 0xE000U, 0U, 0U, 0U, 0U);

        outer(*mapper, 0x0EU);
        bank(*mapper, 0U, 0xFEU, 0x80U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->read_chr(0x0000U) == cell(0U, 0x55U));
    }
}

TEST_CASE("Mapper 457 bounded snapshots replay without ROM and preserve partial IRQ state",
          "[mapper457][state][banking][irq]") {
    auto mapper = board();
    outer(*mapper, 0x0EU);
    bank(*mapper, 0U, 0xA2U, 0xC0U);
    bank(*mapper, 1U, 0x8BU, 0xC0U);
    bank(*mapper, 6U, 0x19U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == 0x0EU);
    REQUIRE(saved[kIrqCounterOffset] == 2U);
    REQUIRE(saved[kIrqFlagsOffset] == 2U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x0EU, 0xC0U, 0x19U, 0x07U, 0x80U);
    }
    constexpr std::array<Byte, 6> chr_values = {0xA2U, 0x8BU, 0U, 0U, 0U, 0U};
    REQUIRE(copy->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0x0EU, 0xC0U, chr_values), 0xD5U));
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
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 457 rejects truncated damaged and mismatched states atomically",
          "[mapper457][state]") {
    auto mapper = board();
    outer(*mapper, 0x0FU);
    bank(*mapper, 0U, 0xFEU, 0xC0U);
    bank(*mapper, 6U, 0x09U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 13> invalid_fields = {
        {{0U, 0U},
         {2U, 1U},
         {6U, 1U},
         {10U, 0U},
         {kOuterOffset, 0x10U},
         {kBankSelectOffset, 8U},
         {kBankModeOffset, 1U},
         {kBankRegistersOffset + 6U, 0x40U},
         {kBankRegistersOffset + 7U, 0x40U},
         {kMirrorOffset, 2U},
         {kIrqFlagsOffset, 8U},
         {kIrqFlagsOffset, 4U},
         {kRamFlagsOffset, 1U}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper457>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_mirror = board(MirrorMode::Horizontal);
    mismatch(wrong_mirror);
    auto wrong_prg = std::make_unique<Mapper457>(header(), rom(kPrgSize / 2U), rom(kChrSize));
    mismatch(wrong_prg);
    auto wrong_chr = std::make_unique<Mapper457>(header(), rom(kPrgSize), rom(kChrSize / 2U));
    mismatch(wrong_chr);

    auto changed = saved;
    changed[kOuterOffset] = 0x09U;
    changed[kBankRegistersOffset] = 0x5AU;
    changed[kBankRegistersOffset + 6U] = 0x0EU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0x8000U, 0x09U, 0xC0U, 0x0EU, 0x07U);
    REQUIRE(mapper->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0x09U, 0xC0U, {0x5AU, 0U, 0U, 0U, 0U, 0U}), 0x55U));
}

} // namespace
