#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper339.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 8192UZ;
constexpr std::size_t kChrBankSize = 1024UZ;
constexpr std::size_t kPrgSize = 131072UZ;
constexpr std::size_t kChrSize = 524288UZ;
constexpr std::size_t kRamSize = 8192UZ;
constexpr std::size_t kStateHeaderSize = 35UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kBankSelectOffset = 13UZ;
constexpr std::size_t kFirstBankOffset = 14UZ;
constexpr std::size_t kMirrorOffset = 22UZ;
constexpr std::size_t kIrqFlagsOffset = 25UZ;
constexpr std::size_t kRamFlagsOffset = 26UZ;
constexpr std::size_t kRamLengthOffset = 27UZ;

enum class RamPopulation { None, Volatile, Battery };

INesHeader header(RamPopulation population = RamPopulation::None,
                  Byte submapper = 0U,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 339U;
    result.is_nes2 = true;
    result.submapper = submapper;
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

std::unique_ptr<Mapper339> board(RamPopulation population = RamPopulation::None,
                                 Byte submapper = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(population, submapper, mirror);
    REQUIRE(Mapper339::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper339>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper339& mapper, unsigned index, Byte value, bool prg_mode = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | (prg_mode ? 0x40U : 0U)));
    mapper.write_prg(0x8001U, value);
}

bool nrom256(Byte submapper, Byte outer) {
    switch (submapper) {
    case 0U:
        return (outer & 0x06U) == 0x06U;
    case 1U:
        return (outer & 0x04U) != 0U;
    case 2U:
        return (outer & 0x11U) != 0U;
    case 3U:
        return (outer & 0x18U) != 0U;
    default:
        return (outer & 0x14U) != 0U;
    }
}

Byte expected_chr_bank(Address addr, bool inversion, const std::array<Byte, 8>& registers) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (inversion ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers[slot - 2U];
}

Byte expected_prg(Byte submapper,
                  Byte outer,
                  Byte select,
                  const std::array<Byte, 8>& registers,
                  Address addr,
                  std::size_t prg_size,
                  Byte pad = 0U,
                  Byte salt = 0U) {
    std::size_t offset = 0U;
    if ((outer & 0x20U) == 0U) {
        const std::size_t base = static_cast<std::size_t>((outer >> 1U) & 3U) * 32768UZ;
        const std::size_t a14 = nrom256(submapper, outer)
                                    ? addr & 0x4000U
                                    : static_cast<std::size_t>(outer & 1U) << 14U;
        offset = base + a14 + (addr & 0x3FFFU);
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
        offset = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    if ((outer & 0x80U) != 0U) {
        offset = (offset & ~3UZ) | pad;
    }
    return cell(offset % prg_size, salt);
}

Byte expected_chr(Byte outer,
                  bool inversion,
                  const std::array<Byte, 8>& registers,
                  Address addr,
                  std::size_t chr_size,
                  Byte salt = 0U) {
    const std::size_t base = static_cast<std::size_t>((outer >> 3U) & 3U) * 131072UZ;
    const auto bank = expected_chr_bank(addr, inversion, registers) & 0x7FU;
    const auto offset = base + bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    return cell(offset % chr_size, salt);
}

TEST_CASE("Mapper 339 validates mapper, submapper, ROM and memory population", "[mapper339]") {
    for (Byte submapper = 0U; submapper <= 4U; ++submapper) {
        for (auto population :
             {RamPopulation::None, RamPopulation::Volatile, RamPopulation::Battery}) {
            for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                for (std::size_t prg : {32768UZ, 65536UZ, 131072UZ}) {
                    for (std::size_t chr :
                         {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ}) {
                        REQUIRE(Mapper339::validate_geometry(
                            header(population, submapper, mirror), prg, chr));
                    }
                }
            }
        }
    }

    for (unsigned mapper : {0U, 4U, 333U, 338U, 340U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(mapper);
        REQUIRE_FALSE(Mapper339::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper339::validate_geometry(legacy, kPrgSize, kChrSize));
    for (unsigned submapper = 5U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper339::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (std::size_t size : {0UZ, 1UZ, 8192UZ, 16384UZ, 32767UZ, 49152UZ, 262144UZ}) {
        REQUIRE_FALSE(Mapper339::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size : {0UZ, 1UZ, 4096UZ, 8191UZ, 12288UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper339::validate_geometry(header(), kPrgSize, size));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper339::validate_geometry(
            header(RamPopulation::None, 0U, mirror), kPrgSize, kChrSize));
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
        REQUIRE_FALSE(Mapper339::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 339 decodes every CPU address through documented masks", "[mapper339]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper339& mapper) {
        mapper.reset();
        mapper.write_prg(0x6038U, 0U);
        select_bank(mapper, 7U, 0x2AU, true);
        mapper.write_prg(0xA000U, 1U);
        mapper.write_prg(0xA001U, 0x80U);
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
            expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
        } else if (raw >= 0x6000U) {
            expected->write_prg(static_cast<Address>(0x6000U | (raw & 0xFFU)), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }
}

TEST_CASE("Mapper 339 outer latch uses low address bits and honors A001 gates", "[mapper339]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(static_cast<Address>(0x7F00U | value), static_cast<Byte>(value ^ 0xFFU));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0xBFU));
    }
    for (const Byte control : {0x00U, 0x40U, 0x80U, 0xC0U}) {
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(0x6038U, 0U);
        const auto before = state(*mapper)[kOuterOffset];
        mapper->write_prg(0xA001U, control);
        mapper->write_prg(0x6007U, 0xA5U);
        REQUIRE(state(*mapper)[kOuterOffset] == (control == 0x80U ? 7U : before));
    }
}

TEST_CASE("Mapper 339 implements every submapper NROM-128 and NROM-256 condition", "[mapper339]") {
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    const std::array<Byte, 8> registers{};
    for (Byte submapper = 0U; submapper <= 4U; ++submapper) {
        auto mapper = board(RamPopulation::None, submapper);
        for (unsigned outer = 0U; outer < 256U; ++outer) {
            mapper->write_prg(0xA001U, 0x80U);
            mapper->write_prg(static_cast<Address>(0x6000U | outer), 0x5AU);
            for (const auto addr : addresses) {
                CAPTURE(submapper, outer, addr);
                REQUIRE(mapper->read_prg(addr) == expected_prg(submapper,
                                                               static_cast<Byte>(outer & 0xBFU),
                                                               0U,
                                                               registers,
                                                               addr,
                                                               kPrgSize));
            }
        }
    }
}

TEST_CASE("Mapper 339 NROM modes cover every byte and wrap smaller ROMs", "[mapper339]") {
    const std::array<Byte, 8> registers{};
    for (const std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ}) {
        for (const Byte outer : {0x00U, 0x01U, 0x02U, 0x06U, 0x07U, 0x0FU}) {
            auto mapper = board(RamPopulation::None, 0U, prg_size);
            mapper->write_prg(0x6000U | outer, 0U);
            for (unsigned offset = 0U; offset < 32768U; ++offset) {
                const auto addr = static_cast<Address>(0x8000U + offset);
                CAPTURE(prg_size, outer, addr);
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(0U, outer, 0U, registers, addr, prg_size));
            }
        }
    }
}

TEST_CASE("Mapper 339 MMC3 PRG banking masks disconnected pins and honors both modes",
          "[mapper339]") {
    for (const Byte outer : {0x20U, 0x38U, 0xA0U, 0xB8U}) {
        auto mapper = board();
        REQUIRE(mapper->configure_board_jumpers(2U));
        mapper->write_prg(0x6000U | outer, 0U);
        for (const bool mode : {false, true}) {
            for (const Byte r6 : {0U, 1U, 14U, 15U, 16U, 63U}) {
                for (const Byte r7 : {0U, 5U, 15U, 42U, 63U}) {
                    select_bank(*mapper, 6U, r6, mode);
                    select_bank(*mapper, 7U, r7, mode);
                    std::array<Byte, 8> registers{};
                    registers[6U] = r6;
                    registers[7U] = r7;
                    for (const Address addr :
                         {0x8000U, 0x9123U, 0xA000U, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU}) {
                        CAPTURE(outer, mode, r6, r7, addr);
                        REQUIRE(mapper->read_prg(addr) ==
                                expected_prg(
                                    0U, outer, mode ? 0x40U : 0U, registers, addr, kPrgSize, 2U));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 339 CHR banking applies outer pins after MMC3 pairing and inversion",
          "[mapper339]") {
    for (const Byte outer : {0x00U, 0x08U, 0x18U, 0x38U, 0xB8U}) {
        auto mapper = board(RamPopulation::None, 0U, kPrgSize, kChrSize, MirrorMode::Vertical, 0U);
        mapper->write_prg(0x6000U | outer, 0U);
        for (const bool inversion : {false, true}) {
            std::array<Byte, 8> registers{};
            for (unsigned index = 0U; index < 6U; ++index) {
                const auto value = static_cast<Byte>(index * 37U + 0x81U);
                select_bank(*mapper, index, value);
                registers[index] = value;
            }
            mapper->write_prg(0x8000U, inversion ? 0x80U : 0U);
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const auto address = static_cast<Address>(addr);
                CAPTURE(outer, inversion, address);
                REQUIRE(mapper->read_chr(address) ==
                        expected_chr(outer, inversion, registers, address, kChrSize));
            }
        }
    }
    auto mapper = board();
    std::array<Byte, 8> registers{};
    for (unsigned index = 0U; index < 6U; ++index) {
        for (unsigned value = 0U; value < 256U; ++value) {
            select_bank(*mapper, index, static_cast<Byte>(value));
            registers[index] = static_cast<Byte>(value);
            for (const Address addr : {0U, 0x0400U, 0x0800U, 0x1000U, 0x1C00U}) {
                CAPTURE(index, value, addr);
                REQUIRE(mapper->read_chr(addr) ==
                        expected_chr(0U, false, registers, addr, kChrSize));
            }
        }
    }
}

TEST_CASE("Mapper 339 CHR ROM and low open-bus windows stay protected", "[mapper339]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto address = static_cast<Address>(addr);
        const auto original = mapper->read_chr(address);
        mapper->write_chr(address, static_cast<Byte>(original ^ 0xFFU));
        REQUIRE(mapper->read_chr(address) == original);
    }
    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 339 menu pads replace only PRG A1 through A0", "[mapper339]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    REQUIRE_FALSE(mapper->configure_board_jumpers(4U));
    REQUIRE(state(*mapper) == initial);
    for (Byte pad = 0U; pad <= 3U; ++pad) {
        REQUIRE(mapper->configure_board_jumpers(pad));
        for (const Byte outer : {0x80U, 0xA0U, 0xB8U}) {
            mapper->write_prg(0x6000U | outer, 0U);
            select_bank(*mapper, 6U, 3U, true);
            select_bank(*mapper, 7U, 9U, true);
            std::array<Byte, 8> registers{};
            registers[6U] = 3U;
            registers[7U] = 9U;
            for (const Address addr : {0x8000U, 0x8001U, 0x8002U, 0x8003U, 0xC000U, 0xFFFFU}) {
                CAPTURE(pad, outer, addr);
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(0U, outer, 0x40U, registers, addr, kPrgSize, pad));
            }
        }
    }
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U));
}

TEST_CASE("Mapper 339 MMC3 mirroring and optional RAM follow A000/A001", "[mapper339]") {
    auto mapper = board(RamPopulation::Volatile);
    for (unsigned addr = 0xA000U; addr <= 0xBFFFU; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), addr & 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->write_prg(static_cast<Address>(addr), 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA7U);
    REQUIRE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6123U, 0x55U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    mapper->write_prg(0xA001U, 0x00U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    mapper->write_prg(0x6123U, 0x55U);
    mapper->write_prg(0xA001U, 0x80U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 339 reset and power retain only documented persistent memory",
          "[mapper339][reset]") {
    auto volatile_ram = board(RamPopulation::Volatile);
    volatile_ram->write_prg(0x6123U, 0xA7U);
    volatile_ram->write_prg(0xA000U, 1U);
    volatile_ram->reset();
    REQUIRE(volatile_ram->read_prg(0x6123U) == 0xA7U);
    REQUIRE(volatile_ram->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(state(*volatile_ram)[kOuterOffset] == 0U);
    volatile_ram->power_on();
    REQUIRE(volatile_ram->read_prg(0x6123U) == 0U);

    auto nvram = board(RamPopulation::Battery);
    std::vector<Byte> saved_ram(kRamSize);
    for (std::size_t index = 0U; index < saved_ram.size(); ++index) {
        saved_ram[index] = static_cast<Byte>(index * 13U + 7U);
    }
    nvram->set_battery_ram(saved_ram);
    REQUIRE(std::ranges::equal(nvram->battery_ram(), saved_ram));
    nvram->power_on();
    REQUIRE(std::ranges::equal(nvram->battery_ram(), saved_ram));
    const auto before_bad_import = state(*nvram);
    nvram->set_battery_ram(std::span<const Byte>(saved_ram).first(kRamSize - 1U));
    REQUIRE(state(*nvram) == before_bad_import);
    REQUIRE(board()->battery_ram().empty());
}

TEST_CASE("Mapper 339 clocks standard MMC3 IRQ reload disable and zero-latch behavior",
          "[mapper339][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
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
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 339 snapshots replay registers and RAM without ROM serialization",
          "[mapper339][state]") {
    auto mapper = board(RamPopulation::Volatile, 3U);
    REQUIRE(mapper->configure_board_jumpers(2U));
    mapper->write_prg(0x60BBU, 0U);
    select_bank(*mapper, 6U, 0x2AU, true);
    select_bank(*mapper, 7U, 0x15U, true);
    for (unsigned index = 0U; index < 6U; ++index) {
        select_bank(*mapper, index, static_cast<Byte>(index * 31U + 3U), true);
    }
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0x80U);
    for (unsigned addr = 0U; addr < kRamSize; ++addr) {
        mapper->write_prg(static_cast<Address>(0x6000U + addr), static_cast<Byte>(addr * 13U + 7U));
    }
    mapper->write_prg(0x60BBU, static_cast<Byte>(0xBBU * 13U + 7U));
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateHeaderSize + kRamSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kOuterOffset] == 0xBBU);

    auto copy = board(RamPopulation::Volatile, 3U, kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    REQUIRE(copy->configure_board_jumpers(2U));
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->irq_pending());
    for (std::size_t addr = 0U; addr < kRamSize; ++addr) {
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + addr)) ==
                static_cast<Byte>(addr * 13U + 7U));
    }
    for (const Address addr : {0x8000U, 0x9123U, 0xA000U, 0xC000U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 339 rejects bounded malformed and incompatible states atomically",
          "[mapper339][state]") {
    auto mapper = board(RamPopulation::Volatile, 2U);
    REQUIRE(mapper->configure_board_jumpers(1U));
    mapper->write_prg(0x60B5U, 0U);
    select_bank(*mapper, 6U, 0x3FU, true);
    mapper->write_prg(0xE001U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->clock_irq_counter();
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
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (std::size_t offset = 2U; offset < kOuterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (const Byte outer : {0x40U, 0x41U, 0xC0U, 0xFFU}) {
        auto invalid = saved;
        invalid[kOuterOffset] = outer;
        reject(invalid);
    }
    for (const Byte select : {0x08U, 0x10U, 0x20U, 0x38U, 0xFFU}) {
        auto invalid = saved;
        invalid[kBankSelectOffset] = select;
        reject(invalid);
    }
    for (const Byte bank : {0x40U, 0x80U, 0xFFU}) {
        for (const std::size_t offset : {kFirstBankOffset + 6U, kFirstBankOffset + 7U}) {
            auto invalid = saved;
            invalid[offset] = bank;
            reject(invalid);
        }
    }
    for (const Byte mirror : {2U, 3U, 0xFFU}) {
        auto invalid = saved;
        invalid[kMirrorOffset] = mirror;
        reject(invalid);
    }
    for (const Byte flags : {4U, 8U, 0x0BU, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqFlagsOffset] = flags;
        reject(invalid);
    }
    for (const Byte flags : {1U, 0x3FU, 0xFFU}) {
        auto invalid = saved;
        invalid[kRamFlagsOffset] = flags;
        reject(invalid);
    }
    for (const std::uint64_t length : {0UZ, 4096UZ, 8193UZ, 0xFFFFFFFFFFFFFFFFUZ}) {
        auto invalid = saved;
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            invalid[kRamLengthOffset + byte] = static_cast<Byte>(length >> (byte * 8U));
        }
        reject(invalid);
    }

    const auto incompatible = [&](std::unique_ptr<Mapper339> other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    incompatible(board(RamPopulation::Volatile, 1U));
    incompatible(board(RamPopulation::Volatile, 2U, kPrgSize / 2U));
    incompatible(board(RamPopulation::Volatile, 2U, kPrgSize, kChrSize / 2U));
    incompatible(board(RamPopulation::None, 2U));
    auto other_pad = board(RamPopulation::Volatile, 2U);
    REQUIRE(other_pad->configure_board_jumpers(3U));
    incompatible(std::move(other_pad));
}

TEST_CASE("Mapper 339 state validation accepts every reachable outer latch", "[mapper339][state]") {
    auto mapper = board();
    const auto saved = state(*mapper);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        auto candidate = saved;
        candidate[kOuterOffset] = static_cast<Byte>(outer);
        StateReader reader(candidate);
        mapper->load_state(reader);
        if ((outer & 0x40U) == 0U) {
            REQUIRE(reader.ok());
            REQUIRE(reader.remaining() == 0U);
            REQUIRE(state(*mapper) == candidate);
        } else {
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
        mapper->reset();
    }
}

} // namespace
