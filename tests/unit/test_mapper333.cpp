#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper333.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::size_t kChrBankSize = 1024U;
constexpr std::size_t kOuterBankSize = 128U * 1024U;
constexpr std::size_t kPrgSize = 512U * 1024U;
constexpr std::size_t kChrSize = 512U * 1024U;
constexpr std::size_t kRamSize = 8192U;
constexpr std::size_t kStateHeaderSize = 35U;
constexpr std::size_t kOuterOffset = 12U;
constexpr std::size_t kBankSelectOffset = 13U;
constexpr std::size_t kFirstBankOffset = 14U;
constexpr std::size_t kMirrorOffset = 22U;
constexpr std::size_t kIrqFlagsOffset = 25U;
constexpr std::size_t kRamFlagsOffset = 26U;
constexpr std::size_t kRamLengthOffset = 27U;

enum class RamPopulation { None, Volatile, Battery };

INesHeader header(RamPopulation population = RamPopulation::None,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 333U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = population == RamPopulation::Volatile ? kRamSize : 0U;
    result.prg_nvram_size = population == RamPopulation::Battery ? kRamSize : 0U;
    result.has_battery = population == RamPopulation::Battery;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 17U + (offset >> 8U) * 31U + (offset >> 13U) * 47U +
                             (offset >> 17U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper333> board(RamPopulation population = RamPopulation::None,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(population, mirror);
    REQUIRE(Mapper333::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper333>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper333& mapper, unsigned index, Byte value, bool prg_mode = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | (prg_mode ? 0x40U : 0U)));
    mapper.write_prg(0x8001U, value);
}

Byte expected_chr_bank(Address addr, bool inversion, const std::array<Byte, 8>& registers) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (inversion ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers[slot - 2U];
}

Byte expected_prg(Byte outer,
                  Byte select,
                  const std::array<Byte, 8>& registers,
                  Address addr,
                  std::size_t prg_size) {
    const std::size_t outer_base = static_cast<std::size_t>((outer >> 2U) & 3U) * kOuterBankSize;
    std::size_t offset = 0U;
    if ((outer & 0x10U) == 0U) {
        offset = outer_base + static_cast<std::size_t>(outer & 3U) * 32768U + (addr & 0x7FFFU);
    } else {
        const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
        const bool mode = (select & 0x40U) != 0U;
        unsigned bank = 0U;
        switch (slot) {
        case 0U:
            bank = mode ? 14U : registers[6U] & 0x0FU;
            break;
        case 1U:
            bank = registers[7U] & 0x0FU;
            break;
        case 2U:
            bank = mode ? registers[6U] & 0x0FU : 14U;
            break;
        default:
            bank = 15U;
            break;
        }
        offset = outer_base + bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    return cell(offset % prg_size);
}

Byte expected_chr(Byte outer,
                  bool inversion,
                  const std::array<Byte, 8>& registers,
                  Address addr,
                  std::size_t chr_size) {
    const std::size_t outer_base = static_cast<std::size_t>((outer >> 2U) & 3U) * kOuterBankSize;
    const auto bank = expected_chr_bank(addr, inversion, registers) & 0x7FU;
    const auto offset = outer_base + bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    return cell(offset % chr_size);
}

TEST_CASE("Mapper 333 validates mapper, submapper, ROM and memory population", "[mapper333]") {
    for (auto population : {RamPopulation::None, RamPopulation::Volatile, RamPopulation::Battery}) {
        for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (std::size_t prg : {32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ}) {
                for (std::size_t chr :
                     {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ}) {
                    REQUIRE(Mapper333::validate_geometry(header(population, mirror), prg, chr));
                }
            }
        }
    }

    for (unsigned mapper : {0U, 4U, 321U, 332U, 334U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(mapper);
        REQUIRE_FALSE(Mapper333::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper333::validate_geometry(legacy, kPrgSize, kChrSize));
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper333::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (std::size_t size : {0UZ, 1UZ, 8192UZ, 16384UZ, 32767UZ, 49152UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper333::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size : {0UZ, 1UZ, 4096UZ, 8191UZ, 12288UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper333::validate_geometry(header(), kPrgSize, size));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(
            Mapper333::validate_geometry(header(RamPopulation::None, mirror), kPrgSize, kChrSize));
    }

    for (unsigned kind = 0U; kind < 12U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.prg_ram_size = 4096U;
            break;
        case 1U:
            h.prg_ram_size = 16384U;
            break;
        case 2U:
            h.prg_nvram_size = 4096U;
            h.has_battery = true;
            break;
        case 3U:
            h.prg_nvram_size = kRamSize;
            break;
        case 4U:
            h.has_battery = true;
            break;
        case 5U:
            h.prg_ram_size = kRamSize;
            h.prg_nvram_size = kRamSize;
            h.has_battery = true;
            break;
        case 6U:
            h.prg_ram_size = kRamSize;
            h.has_battery = true;
            break;
        case 7U:
            h.chr_ram_size = kRamSize;
            break;
        case 8U:
            h.chr_nvram_size = kRamSize;
            h.has_battery = true;
            break;
        case 9U:
            h.prg_ram_size = kRamSize;
            h.prg_nvram_size = 4096U;
            break;
        case 10U:
            h.prg_nvram_size = 16384U;
            h.has_battery = true;
            break;
        default:
            h.chr_ram_size = kRamSize;
            h.chr_nvram_size = kRamSize;
            break;
        }
        CAPTURE(kind);
        REQUIRE_FALSE(Mapper333::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 333 decodes every CPU address through only its documented masks", "[mapper333]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper333& mapper) {
        mapper.reset();
        mapper.write_prg(0x9000U, 0x1DU);
        select_bank(mapper, 7U, 0xABU, true);
        mapper.write_prg(0xA000U, 1U);
        mapper.write_prg(0xA001U, 0xC0U);
        mapper.write_prg(0xC000U, 2U);
        mapper.write_prg(0xC001U, 0U);
        mapper.write_prg(0xE001U, 0U);
        mapper.clock_irq_counter();
    };

    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        prepare(*actual);
        prepare(*expected);
        const auto value = static_cast<Byte>(raw * 29U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            const auto canonical = (raw & 0x9000U) == 0x9000U ? 0x9000U : raw & 0xF001U;
            expected->write_prg(static_cast<Address>(canonical), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }
}

TEST_CASE("Mapper 333 NROM mode maps every inner and outer 32 KiB page", "[mapper333]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 16U; ++outer) {
        select_bank(*mapper, 6U, 0x3FU, true);
        select_bank(*mapper, 7U, 0x2AU, true);
        mapper->write_prg(0x9000U, static_cast<Byte>(outer | 0xE0U));
        REQUIRE(state(*mapper)[kOuterOffset] == outer);
        std::array<Byte, 8> registers{};
        registers[6U] = 0x3FU;
        registers[7U] = 0x2AU;
        for (unsigned offset = 0U; offset < 32768U; ++offset) {
            const auto addr = static_cast<Address>(0x8000U + offset);
            CAPTURE(outer, offset);
            REQUIRE(mapper->read_prg(addr) ==
                    expected_prg(static_cast<Byte>(outer), 0x40U, registers, addr, kPrgSize));
        }
    }
}

TEST_CASE("Mapper 333 MMC3 PRG banking stays inside each outer 128 KiB bank", "[mapper333]") {
    auto mapper = board();
    constexpr std::array<Address, 4> addresses{0x8000U, 0xA000U, 0xC000U, 0xE000U};
    for (unsigned outer = 0U; outer < 4U; ++outer) {
        mapper->write_prg(0xF000U, static_cast<Byte>(0x10U | (outer << 2U) | 3U));
        for (unsigned reg = 6U; reg <= 7U; ++reg) {
            for (bool mode : {false, true}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->reset();
                    mapper->write_prg(0xB000U, static_cast<Byte>(0x10U | (outer << 2U)));
                    select_bank(*mapper, reg, static_cast<Byte>(value), mode);
                    std::array<Byte, 8> registers{};
                    registers[reg] = static_cast<Byte>(value & 0x3FU);
                    for (unsigned slot = 0U; slot < addresses.size(); ++slot) {
                        const auto addr = static_cast<Address>(addresses[slot] + 0x1A5U);
                        CAPTURE(outer, reg, mode, value, slot);
                        REQUIRE(mapper->read_prg(addr) ==
                                expected_prg(static_cast<Byte>(0x10U | (outer << 2U)),
                                             static_cast<Byte>(mode ? 0x40U : 0U),
                                             registers,
                                             addr,
                                             kPrgSize));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 333 wraps disconnected ROM pins only after outer decoding", "[mapper333]") {
    for (std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ}) {
        auto mapper = board(RamPopulation::None, prg_size, kChrSize);
        mapper->write_prg(0x9000U, 0x0FU);
        for (unsigned offset = 0U; offset < 32768U; ++offset) {
            const auto addr = static_cast<Address>(0x8000U + offset);
            const auto physical = (3U * kOuterBankSize + 3U * 32768U + offset) % prg_size;
            CAPTURE(prg_size, offset);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }
    for (std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ}) {
        auto mapper = board(RamPopulation::None, kPrgSize, chr_size);
        mapper->write_prg(0x9000U, 0x0CU);
        select_bank(*mapper, 2U, 0xFFU);
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            const auto physical = (3U * kOuterBankSize + 0x7FU * kChrBankSize + offset) % chr_size;
            CAPTURE(chr_size, offset);
            REQUIRE(mapper->read_chr(static_cast<Address>(0x1000U + offset)) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 333 implements CHR pairing, inversion and outer banking in both PRG modes",
          "[mapper333]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 4U; ++outer) {
        for (bool mmc3_mode : {false, true}) {
            for (unsigned reg = 0U; reg < 6U; ++reg) {
                for (bool inversion : {false, true}) {
                    for (unsigned value = 0U; value < 256U; ++value) {
                        mapper->reset();
                        mapper->write_prg(
                            0xD000U, static_cast<Byte>((mmc3_mode ? 0x10U : 0U) | outer << 2U));
                        mapper->write_prg(0x8000U,
                                          static_cast<Byte>(reg | (inversion ? 0x80U : 0U)));
                        mapper->write_prg(0x8001U, static_cast<Byte>(value));
                        std::array<Byte, 8> registers{};
                        registers[reg] = static_cast<Byte>(value);
                        const auto control =
                            static_cast<Byte>((mmc3_mode ? 0x10U : 0U) | outer << 2U);
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                            CAPTURE(outer, mmc3_mode, reg, inversion, value, slot);
                            const auto expected =
                                expected_chr(control, inversion, registers, addr, kChrSize);
                            REQUIRE(mapper->read_chr(addr) == expected);
                            mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
                            REQUIRE(mapper->read_chr(addr) == expected);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 333 controls CIRAM and MMC3 IRQs independently of outer aliases", "[mapper333]") {
    auto mapper = board();
    for (unsigned addr = 0xA000U; addr < 0xB000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->write_prg(static_cast<Address>(addr), 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->write_prg(static_cast<Address>(addr + 0x1000U), 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }

    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xD000U, 0x1FU);
    mapper->write_prg(0xF000U, 0U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 333 implements optional RAM gates and leaves unpopulated windows open",
          "[mapper333]") {
    auto mapper = board(RamPopulation::Volatile);
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 23U));
    }
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 23U));
    }
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->maps_prg(0x7FFFU));
    REQUIRE(mapper->battery_ram().empty());

    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0xFFU);
    mapper->write_prg(0xA001U, 0x80U);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x123U * 23U));
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6123U, 0xFFU);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x123U * 23U));

    auto absent = board();
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->read_prg(0x6000U) == 0U);
    REQUIRE(absent->battery_ram().empty());
    REQUIRE_FALSE(absent->has_flash_storage());
    REQUIRE_FALSE(absent->has_expansion_audio());
    REQUIRE(absent->audio_output() == 0.0F);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(absent->maps_prg(addr));
        REQUIRE_FALSE(absent->maps_expansion(addr));
        REQUIRE(absent->read_prg(addr) == 0U);
    }
}

TEST_CASE("Mapper 333 battery imports are exact and rejection is atomic", "[mapper333]") {
    auto mapper = board(RamPopulation::Battery);
    REQUIRE(mapper->battery_ram().size() == kRamSize);
    std::vector<Byte> initial(kRamSize, 0x39U);
    mapper->set_battery_ram(initial);
    REQUIRE(mapper->read_prg(0x6000U) == 0x39U);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0x39U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::span(initial).first(kRamSize - 1U));
    mapper->set_battery_ram(std::vector<Byte>(kRamSize + 1U, 0xA5U));
    REQUIRE(state(*mapper) == before);

    std::vector<Byte> replacement(kRamSize);
    for (std::size_t offset = 0U; offset < replacement.size(); ++offset) {
        replacement[offset] = static_cast<Byte>(offset * 19U + (offset >> 8U));
    }
    mapper->set_battery_ram(replacement);
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) == replacement[offset]);
    }
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == replacement[0x123U]);
}

TEST_CASE("Mapper 333 reset restores registers and power clears only volatile RAM",
          "[mapper333][reset]") {
    auto mapper = board(RamPopulation::Volatile, kPrgSize, kChrSize, MirrorMode::Horizontal);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_prg(0x9000U, 0x1FU);
    select_bank(*mapper, 6U, 0x2AU, true);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(3U * kPrgBankSize));
    REQUIRE(mapper->read_chr(0U) == cell(0U));

    mapper->write_prg(0x6123U, 0x5BU);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(state(*mapper) ==
            state(*board(RamPopulation::Volatile, kPrgSize, kChrSize, MirrorMode::Horizontal)));

    auto persistent = board(RamPopulation::Battery);
    persistent->write_prg(0x6123U, 0xA7U);
    persistent->write_prg(0x9000U, 0x1FU);
    persistent->reset();
    REQUIRE(persistent->read_prg(0x6123U) == 0xA7U);
    persistent->power_on();
    REQUIRE(persistent->read_prg(0x6123U) == 0xA7U);
}

TEST_CASE("Mapper 333 snapshots replay bounded mutable state without ROM serialization",
          "[mapper333][state]") {
    auto mapper = board(RamPopulation::Battery);
    mapper->write_prg(0x9000U, 0x1AU);
    for (unsigned reg = 0U; reg < 8U; ++reg) {
        select_bank(*mapper, reg, static_cast<Byte>(reg * 11U + 3U), true);
    }
    mapper->write_prg(0x8000U, 0xC7U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 23U));
    }

    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateHeaderSize + kRamSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved.size() < kChrSize);
    auto copy = board(RamPopulation::Battery, kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (Address addr : {0x8000U, 0x9234U, 0xA000U, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x99U);
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(mapper->read_chr(addr) + 0x80U));
    }
    for (std::size_t offset = 0U; offset < kRamSize; ++offset) {
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 23U));
    }

    auto no_ram = board();
    REQUIRE(state(*no_ram).size() == kStateHeaderSize);
}

TEST_CASE("Mapper 333 rejects bounded malformed and mismatched states atomically",
          "[mapper333][state]") {
    auto mapper = board(RamPopulation::Battery);
    mapper->write_prg(0x9000U, 0x1AU);
    select_bank(*mapper, 7U, 0x3FU, true);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->write_prg(0x6123U, 0xA7U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kOuterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (Byte value : {0x20U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kOuterOffset] = value;
        reject(invalid);
    }
    for (Byte reserved : {0x08U, 0x10U, 0x20U, 0x38U}) {
        auto invalid = saved;
        invalid[kBankSelectOffset] |= reserved;
        reject(invalid);
    }
    for (std::size_t offset : {kFirstBankOffset + 6U, kFirstBankOffset + 7U}) {
        auto invalid = saved;
        invalid[offset] = 0x40U;
        reject(invalid);
    }
    for (Byte value : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kMirrorOffset] = value;
        reject(invalid);
    }
    for (Byte value : {4U, 8U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqFlagsOffset] = value;
        reject(invalid);
    }
    for (Byte value : {1U, 0x3FU, 0xFFU}) {
        auto invalid = saved;
        invalid[kRamFlagsOffset] = value;
        reject(invalid);
    }
    auto oversized = saved;
    constexpr std::uint64_t kOversized = kRamSize + 1U;
    std::memcpy(oversized.data() + kRamLengthOffset, &kOversized, sizeof(kOversized));
    reject(oversized);
    auto missing_ram = saved;
    constexpr std::uint64_t kMissing = 0U;
    std::memcpy(missing_ram.data() + kRamLengthOffset, &kMissing, sizeof(kMissing));
    reject(missing_ram);

    auto wrong_rom = board(RamPopulation::Battery, kPrgSize / 2U);
    const auto wrong_rom_before = state(*wrong_rom);
    StateReader wrong_rom_reader(saved);
    wrong_rom->load_state(wrong_rom_reader);
    REQUIRE_FALSE(wrong_rom_reader.ok());
    REQUIRE(state(*wrong_rom) == wrong_rom_before);

    auto wrong_ram = board();
    const auto wrong_ram_before = state(*wrong_ram);
    StateReader wrong_ram_reader(saved);
    wrong_ram->load_state(wrong_ram_reader);
    REQUIRE_FALSE(wrong_ram_reader.ok());
    REQUIRE(state(*wrong_ram) == wrong_ram_before);

    auto wrong_mirror = board(RamPopulation::Battery, kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);
}

} // namespace
