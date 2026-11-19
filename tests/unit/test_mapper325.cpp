#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper325.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::size_t kChrBankSize = 1024U;
constexpr std::size_t kPrgSize = 512U * 1024U;
constexpr std::size_t kChrSize = 256U * 1024U;
constexpr std::size_t kRamSize = 8192U;
constexpr std::size_t kStateHeaderSize = 34U;

enum class RamPopulation { None, Volatile, Battery };

INesHeader header(RamPopulation population = RamPopulation::None,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 325U;
    h.is_nes2 = true;
    h.mirror_mode = mirror;
    h.prg_ram_size = population == RamPopulation::Volatile ? kRamSize : 0U;
    h.prg_nvram_size = population == RamPopulation::Battery ? kRamSize : 0U;
    h.has_battery = population == RamPopulation::Battery;
    return h;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 13U + (offset >> 8U) * 29U + (offset >> 14U) * 47U +
                             (offset >> 18U) * 61U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = cell(offset);
    return data;
}

std::unique_ptr<Mapper325> board(RamPopulation population = RamPopulation::None,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(population, mirror);
    REQUIRE(Mapper325::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper325>(h, rom(prg_size), rom(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Address physical_port(Address logical) {
    if ((logical & 1U) == 0U)
        return logical & 0xE000U;
    return static_cast<Address>((logical & 0xE000U) | ((logical & 0x4000U) != 0U ? 4U : 8U));
}

void write_logical(Mapper325& mapper, Address logical, Byte value) {
    mapper.write_prg(physical_port(logical), value);
}

Address decoded_port(Address raw) {
    const bool high = (raw & 8U) != 0U || ((raw & 0x4000U) != 0U && (raw & 4U) != 0U);
    return static_cast<Address>((raw & 0xE000U) | (high ? 1U : 0U));
}

Byte swap_prg(Byte bank) {
    return static_cast<Byte>((bank & 0xF3U) | ((bank & 4U) << 1U) | ((bank & 8U) >> 1U));
}

Byte swap_chr(Byte bank) {
    return static_cast<Byte>((bank & 0xDDU) | ((bank & 2U) << 4U) | ((bank & 0x20U) >> 4U));
}

Byte expected_prg_bank(unsigned slot, bool mode, Byte r6, Byte r7, std::size_t physical_banks) {
    if (slot == 1U)
        return r7;
    if (slot == 3U)
        return static_cast<Byte>(physical_banks - 1U);
    if ((slot == 0U) == mode)
        return static_cast<Byte>(physical_banks - 2U);
    return r6;
}

Byte expected_chr_bank(unsigned slot, bool inversion, const std::array<Byte, 8>& registers) {
    const unsigned mapped = slot ^ (inversion ? 4U : 0U);
    if (mapped < 4U)
        return static_cast<Byte>((registers[mapped / 2U] & 0xFEU) | (mapped & 1U));
    return registers[mapped - 2U];
}

TEST_CASE("Mapper 325 validates mapper, submapper, ROM and memory population", "[mapper325]") {
    for (auto population : {RamPopulation::None, RamPopulation::Volatile, RamPopulation::Battery}) {
        for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (std::size_t prg : {32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ}) {
                for (std::size_t chr : {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ})
                    REQUIRE(Mapper325::validate_geometry(header(population, mirror), prg, chr));
            }
        }
    }

    for (unsigned mapper : {4U, 324U, 326U, 65535U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(mapper);
        REQUIRE_FALSE(Mapper325::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper325::validate_geometry(legacy, kPrgSize, kChrSize));
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper325::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (std::size_t size : {0UZ, 1UZ, 16384UZ, 32767UZ, 49152UZ, 1048576UZ})
        REQUIRE_FALSE(Mapper325::validate_geometry(header(), size, kChrSize));
    for (std::size_t size : {0UZ, 1UZ, 4096UZ, 8191UZ, 12288UZ, 524288UZ})
        REQUIRE_FALSE(Mapper325::validate_geometry(header(), kPrgSize, size));
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper325::validate_geometry(h, kPrgSize, kChrSize));
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
        case 11U:
            h.chr_ram_size = kRamSize;
            h.chr_nvram_size = kRamSize;
            break;
        }
        CAPTURE(kind);
        REQUIRE_FALSE(Mapper325::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 325 CPU A0 wiring selects the same register at every alias", "[mapper325]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        for (auto* mapper : {actual.get(), expected.get()}) {
            write_logical(*mapper, 0x8000U, 0xC5U);
            write_logical(*mapper, 0x8001U, 0xABU);
            write_logical(*mapper, 0x8000U, 6U);
            write_logical(*mapper, 0x8001U, 0x2AU);
            write_logical(*mapper, 0xA000U, 1U);
            write_logical(*mapper, 0xA001U, 0xC0U);
            write_logical(*mapper, 0xC000U, 0x5AU);
            write_logical(*mapper, 0xC001U, 0U);
            write_logical(*mapper, 0xE001U, 0U);
            mapper->clock_irq_counter();
        }

        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        write_logical(*expected, decoded_port(static_cast<Address>(raw)), value);
        CAPTURE(raw, value, decoded_port(static_cast<Address>(raw)));
        REQUIRE(state(*actual) == state(*expected));
    }

    const auto before = state(*actual);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw)
        actual->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
    REQUIRE(state(*actual) == before);
}

TEST_CASE("Mapper 325 swaps PRG bank bits two and three in both MMC3 modes", "[mapper325]") {
    auto mapper = board();
    constexpr std::array<Address, 4> addresses{0x8000U, 0xA000U, 0xC000U, 0xE000U};
    for (unsigned reg = 6U; reg <= 7U; ++reg) {
        for (bool mode : {false, true}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->reset();
                write_logical(*mapper, 0x8000U, static_cast<Byte>(reg | (mode ? 0x40U : 0U)));
                write_logical(*mapper, 0x8001U, static_cast<Byte>(value));
                const auto r6 = reg == 6U ? static_cast<Byte>(value & 0x3FU) : Byte{0U};
                const auto r7 = reg == 7U ? static_cast<Byte>(value & 0x3FU) : Byte{0U};
                for (unsigned slot = 0U; slot < addresses.size(); ++slot) {
                    const auto raw = expected_prg_bank(slot, mode, r6, r7, kPrgSize / kPrgBankSize);
                    const auto expected =
                        cell(static_cast<std::size_t>(swap_prg(raw)) * kPrgBankSize + 0x123U);
                    CAPTURE(reg, mode, value, slot, raw);
                    REQUIRE(mapper->read_prg(static_cast<Address>(addresses[slot] + 0x123U)) ==
                            expected);
                }
            }
        }
    }
}

TEST_CASE("Mapper 325 swaps CHR bank bits one and five after MMC3 pairing and inversion",
          "[mapper325]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 6U; ++reg) {
        for (bool inversion : {false, true}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->reset();
                write_logical(*mapper, 0x8000U, static_cast<Byte>(reg | (inversion ? 0x80U : 0U)));
                write_logical(*mapper, 0x8001U, static_cast<Byte>(value));
                std::array<Byte, 8> registers{};
                registers[reg] = static_cast<Byte>(value);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto raw = expected_chr_bank(slot, inversion, registers);
                    const auto address = static_cast<Address>(slot * kChrBankSize + 0x155U);
                    const auto expected =
                        cell(static_cast<std::size_t>(swap_chr(raw)) * kChrBankSize + 0x155U);
                    CAPTURE(reg, inversion, value, slot, raw);
                    REQUIRE(mapper->read_chr(address) == expected);
                    mapper->write_chr(address, static_cast<Byte>(expected ^ 0xFFU));
                    REQUIRE(mapper->read_chr(address) == expected);
                }
            }
        }
    }
}

TEST_CASE("Mapper 325 implements MMC3 RAM enables, protection and open-bus windows",
          "[mapper325]") {
    auto mapper = board(RamPopulation::Volatile);
    mapper->write_prg(0x6123U, 0xA7U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->maps_prg(0x7FFFU));
    REQUIRE(mapper->battery_ram().empty());

    write_logical(*mapper, 0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0x5BU);
    write_logical(*mapper, 0xA001U, 0x80U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);

    write_logical(*mapper, 0xA001U, 0xC0U);
    mapper->write_prg(0x6123U, 0x5BU);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    write_logical(*mapper, 0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x5BU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5BU);

    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    auto absent = board();
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->read_prg(0x6000U) == 0U);
    REQUIRE(absent->battery_ram().empty());
    REQUIRE_FALSE(absent->has_expansion_audio());
    REQUIRE(absent->audio_output() == 0.0F);
}

TEST_CASE("Mapper 325 battery imports are exact and rejection is atomic", "[mapper325]") {
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
    for (std::size_t offset = 0U; offset < replacement.size(); ++offset)
        replacement[offset] = static_cast<Byte>(offset * 17U + (offset >> 8U));
    mapper->set_battery_ram(replacement);
    for (std::size_t offset = 0U; offset < kRamSize; ++offset)
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) == replacement[offset]);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == replacement[0x123U]);
}

TEST_CASE("Mapper 325 clocks, reloads, acknowledges and disables MMC3 IRQs", "[mapper325]") {
    auto mapper = board();
    write_logical(*mapper, 0xC000U, 2U);
    write_logical(*mapper, 0xE001U, 0U);
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

    write_logical(*mapper, 0xC000U, 0U);
    write_logical(*mapper, 0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    write_logical(*mapper, 0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    write_logical(*mapper, 0xC000U, 1U);
    write_logical(*mapper, 0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    write_logical(*mapper, 0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 325 reset restores registers while power clears only volatile RAM",
          "[mapper325]") {
    auto mapper = board(RamPopulation::Volatile, kPrgSize, kChrSize, MirrorMode::Horizontal);
    mapper->write_prg(0x6123U, 0xA7U);
    write_logical(*mapper, 0x8000U, 0xC6U);
    write_logical(*mapper, 0x8001U, 0x2AU);
    write_logical(*mapper, 0xA000U, 0U);
    write_logical(*mapper, 0xA001U, 0xC0U);
    write_logical(*mapper, 0xC000U, 0U);
    write_logical(*mapper, 0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_chr(0U) == cell(0U));

    mapper->write_prg(0x6123U, 0x5BU);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 325 snapshots replay bounded mutable state without ROM", "[mapper325][state]") {
    auto mapper = board(RamPopulation::Battery);
    write_logical(*mapper, 0x8000U, 0xC6U);
    write_logical(*mapper, 0x8001U, 0x2AU);
    write_logical(*mapper, 0x8000U, 1U);
    write_logical(*mapper, 0x8001U, 0xA5U);
    write_logical(*mapper, 0xA000U, 1U);
    write_logical(*mapper, 0xA001U, 0x80U);
    write_logical(*mapper, 0xC000U, 0U);
    write_logical(*mapper, 0xE001U, 0U);
    mapper->clock_irq_counter();
    for (std::size_t offset = 0U; offset < kRamSize; ++offset)
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 23U));

    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateHeaderSize + kRamSize);
    auto copy = board(RamPopulation::Battery);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    for (unsigned slot = 0U; slot < 8U; ++slot)
        REQUIRE(copy->read_chr(static_cast<Address>(slot * kChrBankSize + 0x99U)) ==
                mapper->read_chr(static_cast<Address>(slot * kChrBankSize + 0x99U)));
    for (std::size_t offset = 0U; offset < kRamSize; ++offset)
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + offset)) ==
                static_cast<Byte>(offset * 23U));
}

TEST_CASE("Mapper 325 malformed states reject atomically with bounded RAM reads",
          "[mapper325][state]") {
    auto mapper = board(RamPopulation::Battery);
    write_logical(*mapper, 0x8000U, 0xC7U);
    write_logical(*mapper, 0x8001U, 0x3FU);
    write_logical(*mapper, 0xA000U, 1U);
    write_logical(*mapper, 0xA001U, 0x80U);
    write_logical(*mapper, 0xC000U, 0U);
    write_logical(*mapper, 0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->write_prg(0x6123U, 0xA7U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t size = 0U; size < saved.size(); ++size)
        reject(std::span(saved).first(size));
    for (std::size_t offset = 0U; offset < 12U; ++offset) {
        auto bad = saved;
        bad[offset] ^= 1U;
        reject(bad);
    }
    for (Byte reserved : {0x08U, 0x10U, 0x20U, 0x38U}) {
        auto bad = saved;
        bad[12U] |= reserved;
        reject(bad);
    }
    for (std::size_t offset : {19UZ, 20UZ}) {
        auto bad = saved;
        bad[offset] = 0x40U;
        reject(bad);
    }
    for (Byte value : {2U, 0x80U, 0xFFU}) {
        auto bad = saved;
        bad[21U] = value;
        reject(bad);
    }
    for (Byte value : {4U, 8U, 0xFFU}) {
        auto bad = saved;
        bad[24U] = value;
        reject(bad);
    }
    for (Byte value : {1U, 0x3FU, 0xFFU}) {
        auto bad = saved;
        bad[25U] = value;
        reject(bad);
    }
    auto oversized = saved;
    constexpr std::uint64_t kOversized = kRamSize + 1U;
    std::memcpy(oversized.data() + 26U, &kOversized, sizeof(kOversized));
    reject(oversized);

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
}

} // namespace
