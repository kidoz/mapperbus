#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper516.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kDefaultPrgSize = 0x080000UZ;
constexpr std::size_t kDefaultChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kOuterStateOffset = 10UZ;
constexpr std::size_t kBankSelectStateOffset = 11UZ;
constexpr std::size_t kBankRegistersStateOffset = 12UZ;
constexpr std::size_t kMirrorStateOffset = 20UZ;
constexpr std::size_t kIrqLatchStateOffset = 21UZ;
constexpr std::size_t kIrqCounterStateOffset = 22UZ;
constexpr std::size_t kIrqFlagsStateOffset = 23UZ;
constexpr std::size_t kStateSize = 24UZ;
constexpr std::array<Address, 12> kPrgAddresses{0x8000U,
                                                0x8001U,
                                                0x9234U,
                                                0x9FFFU,
                                                0xA000U,
                                                0xBFFFU,
                                                0xC000U,
                                                0xD234U,
                                                0xDFFFU,
                                                0xE000U,
                                                0xFFFEU,
                                                0xFFFFU};
constexpr std::array<Address, 16> kChrAddresses{0x0000U,
                                                0x0001U,
                                                0x03FFU,
                                                0x0400U,
                                                0x07FFU,
                                                0x0800U,
                                                0x0FFFU,
                                                0x1000U,
                                                0x1234U,
                                                0x13FFU,
                                                0x1400U,
                                                0x17FFU,
                                                0x1800U,
                                                0x1BFFU,
                                                0x1C00U,
                                                0x1FFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 516U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kDefaultPrgSize;
    result.chr_rom_size = kDefaultChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 12U) * 47U +
                             (offset >> 15U) * 59U + (offset >> 17U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper516> board(std::size_t prg_size = kDefaultPrgSize,
                                 std::size_t chr_size = kDefaultChrSize,
                                 MirrorMode mirror = MirrorMode::Horizontal,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper516::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper516>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x60U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper516& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper516& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x1234U);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x1234U) == chr);
}

void select_bank(Mapper516& mapper, Byte select, unsigned index, Byte value) {
    mapper.write_prg(0x8000U, static_cast<Byte>(select | index));
    mapper.write_prg(0x8001U, value);
}

std::size_t expected_prg_offset(Address addr,
                                Byte outer,
                                Byte select,
                                const std::array<Byte, 8>& registers) {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    unsigned inner = 15U;
    if ((slot == 0U && (select & 0x40U) == 0U) || (slot == 2U && (select & 0x40U) != 0U)) {
        inner = registers[6U] & 0x0FU;
    } else if (slot == 1U) {
        inner = registers[7U] & 0x0FU;
    } else if ((slot == 2U && (select & 0x40U) == 0U) || (slot == 0U && (select & 0x40U) != 0U)) {
        inner = 14U;
    }
    return static_cast<std::size_t>(outer & 3U) * 0x20000U + inner * kPrgBankSize +
           (addr & (kPrgBankSize - 1U));
}

std::size_t expected_chr_offset(Address addr,
                                Byte outer,
                                Byte select,
                                const std::array<Byte, 8>& registers) {
    const auto slot =
        static_cast<unsigned>(addr / kChrBankSize) ^ ((select & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner = slot < 4U ? ((registers[slot / 2U] & 0xFEU) | (slot & 1U)) & 0x7FU
                                     : registers[slot - 2U] & 0x7FU;
    return static_cast<std::size_t>((outer >> 2U) & 3U) * 0x20000U + inner * kChrBankSize +
           (addr & (kChrBankSize - 1U));
}

void require_prg(Mapper516& mapper,
                 Address addr,
                 Byte outer,
                 Byte select,
                 const std::array<Byte, 8>& registers,
                 std::size_t prg_size,
                 Byte salt = 0U) {
    const auto physical = expected_prg_offset(addr, outer, select, registers) % prg_size;
    CAPTURE(addr, outer, select, physical, prg_size, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper516& mapper,
                 Address addr,
                 Byte outer,
                 Byte select,
                 const std::array<Byte, 8>& registers,
                 std::size_t chr_size,
                 Byte salt = 0x60U) {
    const auto physical = expected_chr_offset(addr, outer, select, registers) % chr_size;
    CAPTURE(addr, outer, select, physical, chr_size, salt);
    REQUIRE(mapper.read_chr(addr) == cell(physical, salt));
}

TEST_CASE("Mapper 516 validates ROM-only NES 2.0 geometry and bounded state",
          "[mapper516][geometry][ram][ciram][state]") {
    for (std::size_t prg = 0x8000UZ; prg <= kDefaultPrgSize; prg *= 2U) {
        for (std::size_t chr = 0x2000UZ; chr <= kDefaultChrSize; chr *= 2U) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(prg, chr, mirror);
                REQUIRE(Mapper516::validate_geometry(header(mirror), prg, chr));
            }
        }
    }
    for (const auto sizes : {std::pair<std::size_t, std::size_t>{0x8000U, 0x2000U},
                             {0x10000U, 0x40000U},
                             {kDefaultPrgSize, kDefaultChrSize}}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(sizes.first, sizes.second, mirror, 0x35U);
            auto different_rom = board(sizes.first, sizes.second, mirror, 0x80U);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_prg(0x7FFFU) == 0U);
            REQUIRE_FALSE(mapper->maps_prg(0x6000U));
            REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
        }
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
        REQUIRE_FALSE(Mapper516::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 512U, 513U, 515U, 517U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kDefaultPrgSize, kDefaultChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0xC000UZ, 0x18000UZ, 0x100000UZ, 0x200000UZ}) {
        reject(header(), size, kDefaultChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x3000UZ, 0x18000UZ, 0x100000UZ, 0x200000UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, kDefaultChrSize);
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
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
}

TEST_CASE("Mapper 516 exhaustively separates outer and MMC3 CPU register decoders",
          "[mapper516][decoding][bus][ram][irq]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if ((addr & 0x8010U) == 0x8010U) {
            expected[kOuterStateOffset] = value & 0x0FU;
        } else if (addr >= 0x8000U) {
            switch (addr & 0xE011U) {
            case 0x8000U:
                expected[kBankSelectStateOffset] = value & 0xC7U;
                break;
            case 0x8001U:
                expected[kBankRegistersStateOffset] = value;
                break;
            case 0xA000U:
                expected[kMirrorStateOffset] = value & 1U;
                break;
            case 0xC000U:
                expected[kIrqLatchStateOffset] = value;
                break;
            case 0xC001U:
                expected[kIrqCounterStateOffset] = 0U;
                expected[kIrqFlagsStateOffset] |= 1U;
                break;
            case 0xE000U:
                expected[kIrqFlagsStateOffset] &= ~6U;
                break;
            case 0xE001U:
                expected[kIrqFlagsStateOffset] |= 2U;
                break;
            default:
                break;
            }
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    mapper->write_prg_cpu(0x8010U, 0xFFU, true);
    REQUIRE(state(*mapper)[kOuterStateOffset] == 0x0FU);
    mapper->write_prg_cpu(0x8000U, 0xFFU, false);
    REQUIRE(state(*mapper)[kBankSelectStateOffset] == 0xC7U);
}

TEST_CASE("Mapper 516 applies four-bit inner PRG banks inside independent outer banks",
          "[mapper516][banking][prg][bus]") {
    auto mapper = board();
    std::array<Byte, 8> registers{};
    for (Byte outer = 0U; outer < 4U; ++outer) {
        mapper->write_prg(0x8010U, outer);
        for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
            for (const Byte r6 :
                 {Byte{0U}, Byte{1U}, Byte{14U}, Byte{15U}, Byte{0x3FU}, Byte{0xFFU}}) {
                for (const Byte r7 : {Byte{0U}, Byte{2U}, Byte{14U}, Byte{15U}, Byte{0xF0U}}) {
                    registers[6U] = r6;
                    registers[7U] = r7;
                    select_bank(*mapper, mode, 6U, r6);
                    select_bank(*mapper, mode, 7U, r7);
                    mapper->write_prg(0x8000U, mode);
                    for (const auto addr : kPrgAddresses) {
                        require_prg(*mapper, addr, outer, mode, registers, kDefaultPrgSize);
                    }
                }
            }
        }
    }

    registers[6U] = 0xFEU;
    registers[7U] = 0xF1U;
    mapper->write_prg(0xFFFFU, 0x03U);
    select_bank(*mapper, 0x40U, 6U, registers[6U]);
    select_bank(*mapper, 0x40U, 7U, registers[7U]);
    mapper->write_prg(0x8000U, 0x40U);
    for (Address addr = 0x8000U;; ++addr) {
        require_prg(*mapper, addr, 0x03U, 0x40U, registers, kDefaultPrgSize);
        if (addr == 0xFFFFU) {
            break;
        }
    }

    registers[6U] = 0xFFU;
    registers[7U] = 0xFEU;
    for (const auto prg_size : {0x8000UZ, 0x10000UZ, 0x20000UZ, 0x40000UZ}) {
        auto small = board(prg_size, 0x2000U, MirrorMode::Horizontal, 0x35U);
        small->write_prg(0x8010U, 0x03U);
        select_bank(*small, 0U, 6U, 0xFFU);
        select_bank(*small, 0U, 7U, 0xFEU);
        small->write_prg(0x8000U, 0U);
        for (const auto addr : kPrgAddresses) {
            require_prg(*small, addr, 0x03U, 0U, registers, prg_size, 0x35U);
        }
    }
}

TEST_CASE("Mapper 516 pairs and inverts seven-bit CHR banks inside outer banks",
          "[mapper516][banking][chr][bus]") {
    auto mapper = board();
    std::array<Byte, 8> registers{0x03U, 0x05U, 0x07U, 0x0BU, 0x0DU, 0x0FU, 0U, 0U};
    for (unsigned index = 0U; index < 6U; ++index) {
        select_bank(*mapper, 0U, index, registers[index]);
    }
    for (Byte chr_outer = 0U; chr_outer < 4U; ++chr_outer) {
        const auto outer = static_cast<Byte>(chr_outer << 2U);
        mapper->write_prg(0x8010U, outer);
        for (const Byte inversion : {Byte{0U}, Byte{0x80U}}) {
            for (unsigned index = 0U; index < 6U; ++index) {
                for (const Byte value : {Byte{0U},
                                         Byte{1U},
                                         Byte{2U},
                                         Byte{3U},
                                         Byte{0x7EU},
                                         Byte{0x7FU},
                                         Byte{0x80U},
                                         Byte{0xFEU},
                                         Byte{0xFFU}}) {
                    registers[index] = value;
                    select_bank(*mapper, inversion, index, value);
                    mapper->write_prg(0x8000U, inversion);
                    for (const auto addr : kChrAddresses) {
                        require_chr(*mapper, addr, outer, inversion, registers, kDefaultChrSize);
                    }
                }
                registers[index] = static_cast<Byte>(index * 2U + 3U);
                select_bank(*mapper, inversion, index, registers[index]);
            }
        }
    }

    registers = {0xFFU, 0xFEU, 0x7FU, 0x80U, 0x01U, 0x02U, 0U, 0U};
    for (const Byte outer : {Byte{0U}, Byte{0x0CU}}) {
        for (const Byte inversion : {Byte{0U}, Byte{0x80U}}) {
            mapper->write_prg(0x8010U, outer);
            for (unsigned index = 0U; index < 6U; ++index) {
                select_bank(*mapper, inversion, index, registers[index]);
            }
            mapper->write_prg(0x8000U, inversion);
            for (Address addr = 0U; addr < 0x2000U; ++addr) {
                require_chr(*mapper, addr, outer, inversion, registers, kDefaultChrSize);
            }
        }
    }

    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto old = mapper->read_chr(addr);
        mapper->write_chr(addr, static_cast<Byte>(old ^ 0xFFU));
        REQUIRE(mapper->read_chr(addr) == old);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xA5U);
    mapper->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 516 exposes only MMC3 H/V CIRAM arrangement",
          "[mapper516][ciram][bus][decoding]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_prg(0xA000U, static_cast<Byte>(raw));
        const auto expected = (raw & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        REQUIRE(mapper->mirror_mode() == expected);
        REQUIRE(state(*mapper)[kMirrorStateOffset] == (raw & 1U));
        for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
            CAPTURE(raw, addr);
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
}

TEST_CASE("Mapper 516 MMC3 IRQ latch reload enable acknowledge and disable replay",
          "[mapper516][irq][state][reset]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    REQUIRE(state(*mapper)[kIrqFlagsStateOffset] == 3U);
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterStateOffset] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterStateOffset] == 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterStateOffset] == 0U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterStateOffset] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned repeat = 0U; repeat < 4U; ++repeat) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 516 reset clears banks outer latch CIRAM and IRQ state",
          "[mapper516][reset][banking][ciram][irq]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, kDefaultChrSize, mirror);
        const auto cold = state(*mapper);
        mapper->write_prg(0x8010U, 0x0FU);
        for (unsigned index = 0U; index < 8U; ++index) {
            select_bank(*mapper, 0xC0U, index, static_cast<Byte>(0x80U + index));
        }
        mapper->write_prg(0xA000U, mirror == MirrorMode::Horizontal ? 0U : 1U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(state(*mapper) != cold);
        REQUIRE(mapper->irq_pending());

        mapper->reset();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_irq_counter();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0x8010U, 0x0EU);
        mapper->write_prg(0xA000U, mirror == MirrorMode::Horizontal ? 0U : 1U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
    }
}

TEST_CASE("Mapper 516 has no work RAM expansion audio or unrelated observer effects",
          "[mapper516][ram][audio][bus][state]") {
    auto mapper = board();
    mapper->write_prg(0x8010U, 0x0FU);
    mapper->write_prg(0xA001U, 0xC0U);
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x2012U);
    mapper->observe_ppu_read(0x2012U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 516 snapshots replay banks and IRQs without ROM or RAM payloads",
          "[mapper516][state][banking][irq][audio]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
    std::array<Byte, 8> registers{0xFEU, 0xFDU, 0x7FU, 0x80U, 0x01U, 0x02U, 0xF3U, 0xF4U};
    mapper->write_prg(0x8010U, 0x0DU);
    for (unsigned index = 0U; index < registers.size(); ++index) {
        select_bank(*mapper, 0xC0U, index, registers[index]);
    }
    mapper->write_prg(0x8000U, 0xC6U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterStateOffset] == 0x0DU);
    REQUIRE(saved[kBankSelectStateOffset] == 0xC6U);
    REQUIRE(saved[kIrqCounterStateOffset] == 3U);

    auto copy = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x0DU, 0xC6U, registers, kDefaultPrgSize, 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, 0x0DU, 0xC6U, registers, kDefaultChrSize, 0xE0U);
    }
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->audio_output() == 0.0F);
    for (unsigned clock = 0U; clock < 4U; ++clock) {
        mapper->clock_irq_counter();
        copy->clock_irq_counter();
        REQUIRE(state(*copy) == state(*mapper));
        REQUIRE(copy->irq_pending() == mapper->irq_pending());
    }
}

TEST_CASE("Mapper 516 malformed snapshots fail atomically without ROM cloning",
          "[mapper516][state][atomic][banking][irq]") {
    auto source = board();
    source->write_prg(0x8010U, 0x0FU);
    for (unsigned index = 0U; index < 8U; ++index) {
        select_bank(*source, 0xC0U, index, static_cast<Byte>(0xA0U + index));
    }
    source->write_prg(0xA000U, 1U);
    source->write_prg(0xC000U, 0U);
    source->write_prg(0xC001U, 0U);
    source->write_prg(0xE001U, 0U);
    source->clock_irq_counter();
    const auto saved = state(*source);

    auto target = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Horizontal, 0x80U);
    target->write_prg(0x8010U, 0x05U);
    target->write_prg(0xA000U, 0U);
    target->write_prg(0xC000U, 2U);
    target->write_prg(0xC001U, 0U);
    target->write_prg(0xE001U, 0U);
    target->clock_irq_counter();
    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*target, std::span(saved).first(length));
    }

    const auto corrupt = [&](std::size_t offset, Byte value) {
        auto invalid = saved;
        invalid[offset] = value;
        load_rejected(*target, invalid);
    };
    corrupt(0U, saved[0U] ^ 0xFFU);
    corrupt(2U, saved[2U] ^ 0x01U);
    corrupt(6U, saved[6U] ^ 0x01U);
    corrupt(kOuterStateOffset, 0x10U);
    corrupt(kBankSelectStateOffset, 0x08U);
    corrupt(kMirrorStateOffset, 2U);
    corrupt(kIrqFlagsStateOffset, 0x08U);

    auto wrong_mirror = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical);
    load_rejected(*wrong_mirror, saved);
}

} // namespace
