#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper447.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kPrgRamSize = 0x000800UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kPrgBanksOffset = 13UZ;
constexpr std::size_t kChrBanksOffset = 15UZ;
constexpr std::size_t kMirrorOffset = 31UZ;
constexpr std::size_t kModeOffset = 32UZ;
constexpr std::size_t kIrqLatchOffset = 33UZ;
constexpr std::size_t kIrqCounterOffset = 34UZ;
constexpr std::size_t kIrqPrescalerOffset = 35UZ;
constexpr std::size_t kIrqControlOffset = 37UZ;
constexpr std::size_t kIrqFlagsOffset = 38UZ;
constexpr std::size_t kRamOffset = 39UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0xA123U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

enum class RamKind { Absent, Volatile, Battery };

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, RamKind ram = RamKind::Volatile) {
    INesHeader result{};
    result.mapper_number = 447U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = ram == RamKind::Battery;
    result.prg_ram_size = ram == RamKind::Volatile ? kPrgRamSize : 0U;
    result.prg_nvram_size = ram == RamKind::Battery ? kPrgRamSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 15U) * 43U +
                             (offset >> 16U) * 59U + (offset >> 17U) * 61U + (offset >> 18U) * 71U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper447> board(std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 RamKind ram = RamKind::Volatile,
                                 Byte salt = 0U) {
    CAPTURE(prg_size, chr_size, mirror, ram, salt);
    const auto h = header(mirror, ram);
    REQUIRE(Mapper447::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper447>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper447& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void set_chr_bank(Mapper447& mapper, unsigned index, std::uint16_t value) {
    const auto base = static_cast<Address>(0xB000U + (index / 2U) * 0x1000U + (index % 2U) * 8U);
    mapper.write_prg(base, static_cast<Byte>(value & 0x0FU));
    mapper.write_prg(static_cast<Address>(base + 4U), static_cast<Byte>((value >> 4U) & 0x1FU));
}

std::size_t expected_prg(Address addr, Byte outer, Byte mode, Byte prg0, Byte prg1) {
    unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if ((outer & 8U) != 0U) {
        slot &= 1U;
    }
    unsigned inner = 15U;
    if (slot == 1U) {
        inner = prg1;
    } else if (slot != 3U) {
        inner = (slot == 0U) != ((mode & 2U) != 0U) ? prg0 : 14U;
    }
    if ((outer & 0x0CU) == 8U) {
        inner = (inner & ~2U) | ((addr >> 13U) & 2U);
    }
    const unsigned bank = (inner & 0x0FU) | ((outer & 3U) << 4U);
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

std::size_t expected_chr(Address addr, Byte outer, const std::array<std::uint16_t, 8>& banks) {
    const unsigned inner = banks[(addr >> 10U) & 7U];
    const unsigned bank = (inner & 0x7FU) | ((outer & 3U) << 7U);
    return bank * 0x400UZ + (addr & 0x03FFU);
}

TEST_CASE("Mapper 447 validates VRC4 geometry memory and CIRAM metadata",
          "[mapper447][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const auto ram : {RamKind::Absent, RamKind::Volatile, RamKind::Battery}) {
            auto mapper = board(kMinPrgSize, kMinChrSize, mirror, ram);
            auto different = board(kMinPrgSize, kMinChrSize, mirror, ram, 0x80U);
            CAPTURE(mirror, ram);
            REQUIRE(state(*mapper) == state(*different));
            REQUIRE(state(*mapper).size() ==
                    kRamOffset + (ram == RamKind::Absent ? 0U : kPrgRamSize));
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().size() == (ram == RamKind::Battery ? kPrgRamSize : 0U));
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper447::validate_geometry(header(), size, kMaxChrSize));
    }
    for (std::size_t size = kMinChrSize; size <= kMaxChrSize; size += 0x400UZ) {
        CAPTURE(size);
        REQUIRE(Mapper447::validate_geometry(header(), kMaxPrgSize, size));
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
        REQUIRE_FALSE(Mapper447::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 25U, 384U, 441U, 446U, 448U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, kMinChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, kMinChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x80001UZ, 0x100000UZ}) {
        reject(header(), size, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2200UZ, 0x80001UZ, 0x100000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, kMinChrSize);
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x1000U;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = kPrgRamSize;
            break;
        case 3U:
            h.has_battery = false;
            h.prg_ram_size = 0U;
            h.prg_nvram_size = kPrgRamSize;
            break;
        case 4U:
            h.has_battery = true;
            break;
        case 5U:
            h.prg_nvram_size = kPrgRamSize;
            break;
        case 6U:
            h.chr_ram_size = kMinChrSize;
            break;
        case 7U:
            h.chr_nvram_size = kMinChrSize;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, kMinChrSize);
    }
}

TEST_CASE("Mapper 447 decodes VRC4 registers through CPU A2 and A3 only",
          "[mapper447][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (const Address addr : {0x8000U, 0x8001U, 0x8002U, 0x8003U, 0x8FFCU}) {
        mapper->write_prg(addr, 0xF5U);
        REQUIRE(state(*mapper)[kPrgBanksOffset] == 0x15U);
    }
    for (const Address addr : {0xA000U, 0xA001U, 0xA002U, 0xA003U, 0xAFFCU}) {
        mapper->write_prg(addr, 0xEAU);
        REQUIRE(state(*mapper)[kPrgBanksOffset + 1U] == 0x0AU);
    }

    mapper->reset();
    mapper->write_prg(0x9004U, 3U);
    mapper->write_prg(0x900CU, 3U);
    REQUIRE(state(*mapper) == cold);
    constexpr std::array modes{MirrorMode::Vertical,
                               MirrorMode::Horizontal,
                               MirrorMode::SingleLower,
                               MirrorMode::SingleUpper};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x9000U | (value & 3U), static_cast<Byte>(value));
        REQUIRE(mapper->mirror_mode() == modes[value & 3U]);
        mapper->write_prg(0x9008U | (value & 3U), static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kModeOffset] == (value & 3U));
    }

    mapper->reset();
    for (unsigned index = 0U; index < 8U; ++index) {
        CAPTURE(index);
        set_chr_bank(*mapper, index, 0x1A5U);
        const auto current = state(*mapper);
        REQUIRE(current[kChrBanksOffset + index * 2U] == 0xA5U);
        REQUIRE(current[kChrBanksOffset + index * 2U + 1U] == 1U);
        mapper->write_prg(
            static_cast<Address>(0xB000U + (index / 2U) * 0x1000U + (index % 2U) * 8U + 3U), 0x0AU);
        REQUIRE(state(*mapper)[kChrBanksOffset + index * 2U] == 0xAAU);
    }
    REQUIRE(state(*mapper)[kIrqLatchOffset] == 0U);
    mapper->write_prg(0xF000U, 0xAEU);
    mapper->write_prg(0xF004U, 0xBBU);
    REQUIRE(state(*mapper)[kIrqLatchOffset] == 0xBEU);
    mapper->write_prg(0xF008U, 0xFFU);
    REQUIRE(state(*mapper)[kIrqControlOffset] == 7U);
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 1U);
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xBEU);
}

TEST_CASE("Mapper 447 outer writes share RAM decoding and lock independently",
          "[mapper447][decoding][bus][ram][reset]") {
    auto mapper = board();
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x6000U, 0xF3U);
    REQUIRE(state(*mapper)[kOuterOffset] == 3U);
    mapper->write_prg(0x9008U, 1U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->maps_prg(0x6FFFU));
    REQUIRE_FALSE(mapper->maps_prg(0x7000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
    REQUIRE(mapper->prg_read_mask(0x7000U) == 0U);

    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    REQUIRE(mapper->read_prg(0x6923U) == 0x5AU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0AU);
    mapper->write_prg(0x7FFFU, 0xFFU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0FU);
    mapper->write_prg(0x6123U, 0xA5U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0FU);
    mapper->write_prg(0x9008U, 0U);
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0FU);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_prg(0x9008U, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);

    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    mapper->write_prg(0x9008U, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->power_on();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    mapper->write_prg(0x9008U, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);

    auto absent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    absent->write_prg(0x9008U, 1U);
    absent->write_prg(0x6000U, 3U);
    REQUIRE(state(*absent)[kOuterOffset] == 3U);
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->read_prg(0x6000U) == 0U);
    REQUIRE(absent->battery_ram().empty());

    auto persistent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Battery);
    std::array<Byte, kPrgRamSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0xA5U);
    }
    persistent->set_battery_ram(bytes);
    persistent->write_prg(0x9008U, 1U);
    REQUIRE(persistent->read_prg(0x6123U) == bytes[0x123U]);
    persistent->power_on();
    persistent->write_prg(0x9008U, 1U);
    REQUIRE(persistent->read_prg(0x6123U) == bytes[0x123U]);
    persistent->set_battery_ram(std::array<Byte, 4>{9U, 8U, 7U, 6U});
    REQUIRE(persistent->read_prg(0x6003U) == 6U);
    REQUIRE(persistent->read_prg(0x6004U) == bytes[4U]);
}

TEST_CASE("Mapper 447 combines VRC4 PRG banking outer pins and fitted ROM wrapping",
          "[mapper447][banking][prg]") {
    auto mapper = board();
    constexpr Byte kPrg0 = 0x15U;
    constexpr Byte kPrg1 = 0x0AU;
    mapper->write_prg(0x8000U, kPrg0);
    mapper->write_prg(0xA000U, kPrg1);
    for (const Byte mode : {Byte{0U}, Byte{2U}}) {
        mapper->write_prg(0x9008U, mode);
        for (Byte outer = 0U; outer < 4U; ++outer) {
            mapper->write_prg(0x6000U, outer);
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, outer, mode, kPrg0, kPrg1);
                CAPTURE(mode, outer, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            }
        }
    }
    for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, 0x018000UZ, 0x048000UZ}) {
        auto fitted = board(size, kMinChrSize);
        fitted->write_prg(0x8000U, 0xFFU);
        fitted->write_prg(0xA000U, 0xFFU);
        fitted->write_prg(0x9008U, 2U);
        fitted->write_prg(0x6000U, 3U);
        for (const auto addr : kPrgAddresses) {
            const auto physical = expected_prg(addr, 3U, 2U, 0x1FU, 0x1FU) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 447 NROM modes duplicate VRC4 registers and select PRG A14",
          "[mapper447][banking][prg][decoding]") {
    auto mapper = board();
    constexpr Byte kPrg0 = 0x09U;
    constexpr Byte kPrg1 = 0x12U;
    for (Byte outer = 8U; outer < 16U; ++outer) {
        for (const Byte mode : {Byte{0U}, Byte{2U}}) {
            mapper->reset();
            mapper->write_prg(0x8000U, kPrg0);
            mapper->write_prg(0xA000U, kPrg1);
            mapper->write_prg(0x9008U, mode);
            mapper->write_prg(0x6000U, outer);
            for (Address addr = 0x8000U; addr != 0U; ++addr) {
                if ((addr & 0x0123U) != 0x0123U) {
                    continue;
                }
                const auto physical = expected_prg(addr, outer, mode, kPrg0, kPrg1);
                CAPTURE(outer, mode, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }

    mapper->reset();
    mapper->write_prg(0x8000U, 4U);
    mapper->write_prg(0xA000U, 8U);
    mapper->write_prg(0x6000U, 8U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, 8U, 0U, 4U, 8U)));
    REQUIRE(mapper->read_prg(0xA000U) == cell(expected_prg(0xA000U, 8U, 0U, 4U, 8U)));
    REQUIRE(mapper->read_prg(0xC000U) != mapper->read_prg(0x8000U));
    REQUIRE(mapper->read_prg(0xE000U) != mapper->read_prg(0xA000U));
    mapper->reset();
    mapper->write_prg(0x8000U, 4U);
    mapper->write_prg(0xA000U, 8U);
    mapper->write_prg(0x6000U, 0x0CU);
    REQUIRE(mapper->read_prg(0xC000U) == mapper->read_prg(0x8000U));
    REQUIRE(mapper->read_prg(0xE000U) == mapper->read_prg(0xA000U));
}

TEST_CASE("Mapper 447 combines nine-bit VRC4 CHR registers with outer pins",
          "[mapper447][banking][chr][decoding]") {
    auto mapper = board();
    constexpr std::array<std::uint16_t, 8> banks{
        0x1A2U, 0x08BU, 0x193U, 0x0FCU, 0x1B1U, 0x0C7U, 0x155U, 0x046U};
    for (unsigned index = 0U; index < banks.size(); ++index) {
        set_chr_bank(*mapper, index, banks[index]);
    }
    for (Byte outer = 0U; outer < 4U; ++outer) {
        mapper->write_prg(0x6000U, outer);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * 0x400U + 0x155U);
            const auto physical = expected_chr(addr, outer, banks);
            CAPTURE(outer, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
            mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
        }
    }
    for (const std::size_t size : {kMinChrSize, 0x003000UZ, 0x018000UZ, 0x048000UZ}) {
        auto fitted = board(kMinPrgSize, size);
        for (unsigned index = 0U; index < banks.size(); ++index) {
            set_chr_bank(*fitted, index, banks[index]);
        }
        fitted->write_prg(0x6000U, 3U);
        for (const Address addr : {0x0000U, 0x03FFU, 0x0800U, 0x1000U, 0x1555U, 0x1FFFU}) {
            const auto physical = expected_chr(addr, 3U, banks) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_chr(addr) == cell(physical, 0x55U));
        }
    }
}

TEST_CASE("Mapper 447 uses VRC4 horizontal vertical and single-screen CIRAM",
          "[mapper447][ciram][decoding]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize, kMinChrSize, initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x9000U, static_cast<Byte>(value));
            constexpr std::array modes{MirrorMode::Vertical,
                                       MirrorMode::Horizontal,
                                       MirrorMode::SingleLower,
                                       MirrorMode::SingleUpper};
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() == modes[value & 3U]);
        }
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
}

TEST_CASE("Mapper 447 VRC4 IRQ supports cycle and prescaled modes with acknowledgement",
          "[mapper447][irq][decoding]") {
    auto mapper = board();
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFFU);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFEU);
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    mapper->write_prg(0xF00CU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 0U);
    mapper->clock_cpu();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFFU);

    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 2U);
    for (unsigned clock = 0U; clock < 113U; ++clock) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqPrescalerOffset] == 2U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFFU);
    mapper->write_prg(0xF008U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 0U);

    mapper->write_prg(0xF008U, 7U);
    mapper->clock_cpu();
    mapper->write_prg(0xF00CU, 0U);
    REQUIRE(state(*mapper)[kIrqFlagsOffset] == 1U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF008U, 0U);
    const auto disabled = state(*mapper);
    mapper->clock_irq_counter();
    mapper->clock_audio();
    REQUIRE(state(*mapper) == disabled);
}

TEST_CASE("Mapper 447 reset clears registers but not RAM and has no other bus or audio device",
          "[mapper447][reset][bus][ram][audio]") {
    auto mapper = board(kMinPrgSize, kMinChrSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    mapper->write_prg(0x9008U, 1U);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_prg(0x8000U, 9U);
    mapper->write_prg(0xA000U, 7U);
    set_chr_bank(*mapper, 3U, 0x1A5U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF008U, 6U);
    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    const auto reset_state = state(*mapper);
    REQUIRE(std::equal(reset_state.begin(), reset_state.begin() + kRamOffset, initial.begin()));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0x9008U, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);

    const auto before = state(*mapper);
    for (Address addr = 0x4020U; addr < 0x6000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
    }
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_irq_counter();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));

    mapper->write_prg(0x6123U, 0x5AU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 447 bounded snapshots replay registers IRQ and RAM without ROM",
          "[mapper447][state][banking][ram][irq]") {
    auto mapper = board();
    mapper->write_prg(0x9008U, 1U);
    for (Address addr = 0x6000U; addr < 0x7000U; addr += 0x101U) {
        mapper->write_prg(addr, static_cast<Byte>(cell(addr) & 0x7FU));
    }
    mapper->reset();
    mapper->write_prg(0x9008U, 1U);
    mapper->write_prg(0x8000U, 0x15U);
    mapper->write_prg(0xA000U, 0x0AU);
    constexpr std::array<std::uint16_t, 8> banks{
        0x1A2U, 0x08BU, 0x193U, 0x0FCU, 0x1B1U, 0x0C7U, 0x155U, 0x046U};
    for (unsigned index = 0U; index < banks.size(); ++index) {
        set_chr_bank(*mapper, index, banks[index]);
    }
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    mapper->write_prg(0x7000U, 3U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kRamOffset + kPrgRamSize);
    REQUIRE(saved.size() == 2087U);
    REQUIRE(saved[kOuterOffset] == 3U);
    REQUIRE(saved[kIrqCounterOffset] == 0xFFU);
    REQUIRE(saved[kIrqFlagsOffset] == 1U);

    auto copy = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Volatile, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleUpper);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 3U, 1U, 0x15U, 0x0AU);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    REQUIRE(copy->read_chr(0x1155U) == cell(expected_chr(0x1155U, 3U, banks), 0xD5U));
    for (Address addr = 0x6000U; addr < 0x7000U; addr += 0x101U) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(cell(addr) & 0x7FU));
    }
    copy->clock_cpu();
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

    auto absent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    REQUIRE(state(*absent).size() == kRamOffset);
    REQUIRE(state(*absent) ==
            state(*board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent, 0x35U)));
    REQUIRE(state(*absent) !=
            state(*board(kMinPrgSize, kMinChrSize, MirrorMode::Vertical, RamKind::Absent)));
}

TEST_CASE("Mapper 447 rejects truncated damaged and mismatched states atomically",
          "[mapper447][state][ram][irq]") {
    auto mapper = board();
    mapper->write_prg(0x9008U, 1U);
    mapper->write_prg(0x6123U, 0x05U);
    mapper->write_prg(0x8000U, 0x15U);
    mapper->write_prg(0xA000U, 0x0AU);
    set_chr_bank(*mapper, 2U, 0x1A5U);
    mapper->write_prg(0x9000U, 2U);
    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF004U, 0x0FU);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    mapper->write_prg(0x6000U, 0x0FU);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };

    for (std::size_t length = 0U; length < active.size(); ++length) {
        CAPTURE(length);
        reject(std::span(active).first(length));
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 15> invalid_fields{{
        {0U, 0U},
        {2U, 1U},
        {6U, 1U},
        {10U, 0U},
        {kOuterOffset, 0x10U},
        {kPrgBanksOffset, 0x20U},
        {kPrgBanksOffset + 1U, 0x20U},
        {1U, 0U},
        {kChrBanksOffset + 5U, 2U},
        {kMirrorOffset, 2U},
        {kMirrorOffset, 5U},
        {kModeOffset, 4U},
        {kIrqControlOffset, 8U},
        {kIrqFlagsOffset, 4U},
        {kIrqFlagsOffset, 2U},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = active;
        invalid[offset] = value;
        invalid[kRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(offset, value);
        reject(invalid);
    }
    auto bad_prescaler = active;
    bad_prescaler[kIrqPrescalerOffset] = 0U;
    bad_prescaler[kIrqPrescalerOffset + 1U] = 0U;
    reject(bad_prescaler);
    bad_prescaler = active;
    bad_prescaler[kIrqPrescalerOffset] = 0x56U;
    bad_prescaler[kIrqPrescalerOffset + 1U] = 1U;
    reject(bad_prescaler);

    const auto mismatch = [&](std::unique_ptr<Mapper447>& other) {
        const auto before = state(*other);
        StateReader reader(active);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg = board(kMinPrgSize, kMaxChrSize);
    mismatch(wrong_prg);
    auto wrong_chr = board(kMaxPrgSize, kMinChrSize);
    mismatch(wrong_chr);
    auto no_ram = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    mismatch(no_ram);
    auto battery = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Battery);
    mismatch(battery);
    auto wrong_mirror = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    mismatch(wrong_mirror);

    auto changed = active;
    changed[kOuterOffset] = 3U;
    changed[kPrgBanksOffset] = 7U;
    changed[kMirrorOffset] = static_cast<Byte>(MirrorMode::Horizontal);
    changed[kModeOffset] = 1U;
    changed[kIrqFlagsOffset] = 0U;
    changed[kRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x05U ^ 0xFFU));
    mapper->write_prg(0x6000U, 1U);
    REQUIRE(state(*mapper)[kOuterOffset] == 1U);
}

} // namespace
