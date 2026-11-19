#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper445.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x100000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::size_t kOuterPrgOffset = 12UZ;
constexpr std::size_t kOuterChrOffset = 13UZ;
constexpr std::size_t kOuterModeOffset = 14UZ;
constexpr std::size_t kLockOffset = 15UZ;
constexpr std::size_t kBankSelectOffset = 16UZ;
constexpr std::size_t kBankModeOffset = 17UZ;
constexpr std::size_t kBankRegistersOffset = 18UZ;
constexpr std::size_t kMirrorOffset = 26UZ;
constexpr std::size_t kIrqReloadOffset = 28UZ;
constexpr std::size_t kIrqFlagsOffset = 29UZ;
constexpr std::size_t kRamFlagsOffset = 30UZ;
constexpr std::size_t kRamOffset = 31UZ;
constexpr std::size_t kStateSize = kRamOffset + kWramSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

enum class RamKind { Absent, Volatile, Battery };

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, RamKind ram = RamKind::Volatile) {
    INesHeader result{};
    result.mapper_number = 445U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = ram == RamKind::Battery;
    result.prg_ram_size = ram == RamKind::Volatile ? kWramSize : 0U;
    result.prg_nvram_size = ram == RamKind::Battery ? kWramSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 15U) * 43U +
                             (offset >> 16U) * 59U + (offset >> 17U) * 61U + (offset >> 18U) * 71U +
                             (offset >> 19U) * 37U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper445> board(std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 RamKind ram = RamKind::Volatile,
                                 Byte salt = 0U) {
    CAPTURE(prg_size, chr_size, mirror, ram, salt);
    const auto h = header(mirror, ram);
    REQUIRE(Mapper445::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper445>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper445& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper445& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper445& mapper, Byte prg, Byte chr, Byte mode) {
    mapper.write_expansion(0x5000U, prg);
    mapper.write_expansion(0x5001U, chr);
    mapper.write_expansion(0x5002U, mode);
}

std::size_t expected_prg(
    Address addr, Byte outer_prg, Byte outer_mode, Byte mmc3_mode, Byte r6, Byte r7) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned inner =
        slot == 1U ? r7 & 0x3FU
        : (((mmc3_mode & 0x40U) == 0U && slot == 0U) || ((mmc3_mode & 0x40U) != 0U && slot == 2U))
            ? r6 & 0x3FU
        : slot == 3U ? 63U
                     : 62U;
    const unsigned a17 = (outer_mode & 1U) != 0U ? outer_prg & 0x10U : inner & 0x10U;
    const unsigned physical = (inner & 0x0FU) | a17 | (outer_prg & 0x60U);
    return physical * kPrgBankSize + (addr & 0x1FFFU);
}

std::size_t expected_chr(Address addr,
                         Byte outer_chr,
                         Byte outer_mode,
                         Byte mmc3_mode,
                         const std::array<Byte, 6>& values) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((mmc3_mode & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner =
        slot < 4U ? (values[slot / 2U] & 0xFEU) | (slot & 1U) : values[slot - 2U];
    const unsigned a17 = (outer_mode & 4U) != 0U ? (outer_chr & 0x10U) << 3U : inner & 0x80U;
    const unsigned physical = (inner & 0x7FU) | a17 | ((outer_chr & 0x60U) << 3U);
    return physical * kChrBankSize + (addr & 0x3FFU);
}

TEST_CASE("Mapper 445 validates decoded geometry memory and CIRAM metadata",
          "[mapper445][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const auto ram : {RamKind::Absent, RamKind::Volatile, RamKind::Battery}) {
            CAPTURE(mirror, ram);
            auto mapper = board(kMaxPrgSize, kMaxChrSize, mirror, ram);
            auto different_rom = board(kMaxPrgSize, kMaxChrSize, mirror, ram, 0x80U);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == (ram == RamKind::Absent ? kRamOffset : kStateSize));
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().size() == (ram == RamKind::Battery ? kWramSize : 0U));
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper445::validate_geometry(header(), size, kMaxChrSize));
    }
    for (std::size_t size = kMinChrSize; size <= kMaxChrSize; size += kChrBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper445::validate_geometry(header(), kMaxPrgSize, size));
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
        REQUIRE_FALSE(Mapper445::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 441U, 443U, 444U, 446U, 4095U}) {
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
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x100001UZ, 0x200000UZ}) {
        reject(header(), size, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2200UZ, 0x100001UZ, 0x200000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, kMinChrSize);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x1000U;
            break;
        case 1U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 2U:
            h.has_battery = false;
            h.prg_ram_size = 0U;
            h.prg_nvram_size = 0x2000U;
            break;
        case 3U:
            h.has_battery = true;
            break;
        case 4U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 5U:
            h.chr_ram_size = 0x2000U;
            break;
        case 6U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, kMinChrSize);
    }
}

TEST_CASE("Mapper 445 decodes only F003 outer aliases and masks every data byte",
          "[mapper445][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const auto decoded = raw & 0xF003U;
        const bool mapped = decoded >= 0x5000U && decoded <= 0x5003U;
        mapper->write_expansion(addr, value);
        auto expected = cold;
        if (mapped) {
            switch (decoded) {
            case 0x5000U:
                expected[kOuterPrgOffset] = value & 0x70U;
                break;
            case 0x5001U:
                expected[kOuterChrOffset] = value & 0x70U;
                break;
            case 0x5002U:
                expected[kOuterModeOffset] = value & 0x05U;
                break;
            default:
                expected[kLockOffset] = (value & 0x20U) != 0U ? 1U : 0U;
                break;
            }
        }
        CAPTURE(raw, value, decoded, mapped);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_expansion(addr) == mapped);
        REQUIRE(mapper->expansion_read_mask(addr) == (mapped ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->reset();
        mapper->write_prg(addr, value);
        REQUIRE(state(*mapper) == cold);
    }

    for (unsigned reg = 0U; reg < 4U; ++reg) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_expansion(static_cast<Address>(0x5000U + reg), static_cast<Byte>(value));
            const auto current = state(*mapper);
            CAPTURE(reg, value);
            REQUIRE(current[kOuterPrgOffset] == (reg == 0U ? value & 0x70U : 0U));
            REQUIRE(current[kOuterChrOffset] == (reg == 1U ? value & 0x70U : 0U));
            REQUIRE(current[kOuterModeOffset] == (reg == 2U ? value & 0x05U : 0U));
            REQUIRE(current[kLockOffset] == (reg == 3U && (value & 0x20U) != 0U ? 1U : 0U));
        }
    }
}

TEST_CASE("Mapper 445 outer lock ignores all outer writes but not MMC3 or RAM",
          "[mapper445][decoding][bus][ram][reset]") {
    auto mapper = board();
    outer(*mapper, 0x70U, 0x60U, 0x05U);
    mapper->write_expansion(0x5FF3U, 0x20U);
    for (const auto addr : {0x5000U, 0x5001U, 0x5002U, 0x5003U, 0x5123U, 0x5FFFU}) {
        mapper->write_expansion(addr, 0U);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper)[kOuterPrgOffset] == 0x70U);
    REQUIRE(state(*mapper)[kOuterChrOffset] == 0x60U);
    REQUIRE(state(*mapper)[kOuterModeOffset] == 0x05U);
    REQUIRE(state(*mapper)[kLockOffset] == 1U);
    bank(*mapper, 6U, 0x12U, 0x40U);
    REQUIRE(state(*mapper)[kBankRegistersOffset + 6U] == 0x12U);
    mapper->write_prg(0x6123U, 0xA5U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);

    mapper->write_prg(0xA001U, 0U);
    mapper->write_expansion(0x5000U, 0U);
    REQUIRE(state(*mapper)[kOuterPrgOffset] == 0x70U);
    mapper->reset();
    REQUIRE(state(*mapper)[kOuterPrgOffset] == 0U);
    REQUIRE(state(*mapper)[kOuterChrOffset] == 0U);
    REQUIRE(state(*mapper)[kOuterModeOffset] == 0U);
    REQUIRE(state(*mapper)[kLockOffset] == 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->write_prg(0xA001U, 0U);
    mapper->write_expansion(0x5000U, 0x50U);
    mapper->write_expansion(0x5003U, 0x20U);
    REQUIRE(state(*mapper)[kOuterPrgOffset] == 0x50U);
    REQUIRE(state(*mapper)[kLockOffset] == 1U);

    auto absent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    absent->write_prg(0xA001U, 0U);
    outer(*absent, 0x40U, 0x30U, 0x05U);
    REQUIRE(state(*absent)[kOuterPrgOffset] == 0x40U);
    REQUIRE(state(*absent)[kOuterChrOffset] == 0x30U);
    REQUIRE(state(*absent)[kOuterModeOffset] == 0x05U);
}

TEST_CASE("Mapper 445 decodes every MMC3 alias without unrelated side effects",
          "[mapper445][decoding][bus]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
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

TEST_CASE("Mapper 445 combines outer PRG pins and both A17 sources with MMC3 banking",
          "[mapper445][banking][prg]") {
    auto mapper = board();
    constexpr Byte kR6 = 0x35U;
    constexpr Byte kR7 = 0x27U;
    for (const Byte mmc3_mode : {Byte{0U}, Byte{0x40U}, Byte{0xC0U}}) {
        bank(*mapper, 6U, static_cast<Byte>(kR6 | 0x80U), mmc3_mode);
        bank(*mapper, 7U, static_cast<Byte>(kR7 | 0x80U), mmc3_mode);
        for (const Byte outer_mode : {Byte{0U}, Byte{1U}, Byte{4U}, Byte{5U}}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                const auto control = static_cast<Byte>(value & 0x70U);
                outer(*mapper, static_cast<Byte>(value), 0U, outer_mode);
                for (const auto addr : kPrgAddresses) {
                    const auto physical =
                        expected_prg(addr, control, outer_mode, mmc3_mode, kR6, kR7);
                    CAPTURE(mmc3_mode, outer_mode, value, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
            }
        }
    }

    for (const Byte outer_mode : {Byte{0U}, Byte{1U}}) {
        outer(*mapper, 0x70U, 0U, outer_mode);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const auto physical = expected_prg(addr, 0x70U, outer_mode, 0xC0U, kR6, kR7);
            CAPTURE(outer_mode, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, 0x018000UZ, 0x048000UZ}) {
        auto fitted = board(size, kMaxChrSize);
        bank(*fitted, 6U, 0xFFU, 0x40U);
        bank(*fitted, 7U, 0xFFU, 0x40U);
        for (const Byte outer_mode : {Byte{0U}, Byte{1U}}) {
            outer(*fitted, 0x70U, 0U, outer_mode);
            for (const auto addr : kPrgAddresses) {
                const auto physical =
                    expected_prg(addr, 0x70U, outer_mode, 0x40U, 0x3FU, 0x3FU) % size;
                CAPTURE(size, outer_mode, addr, physical);
                REQUIRE(fitted->read_prg(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 445 combines outer CHR pins and both A17 sources with MMC3 inversion",
          "[mapper445][banking][chr]") {
    auto mapper = board();
    constexpr std::array<Byte, 6> values = {0xA2U, 0x8BU, 0x93U, 0xFCU, 0xB1U, 0xC7U};
    for (const Byte mmc3_mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*mapper, reg, values[reg], mmc3_mode);
        }
        for (const Byte outer_mode : {Byte{0U}, Byte{1U}, Byte{4U}, Byte{5U}}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                const auto control = static_cast<Byte>(value & 0x70U);
                outer(*mapper, 0U, static_cast<Byte>(value), outer_mode);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                    const auto physical =
                        expected_chr(addr, control, outer_mode, mmc3_mode, values);
                    CAPTURE(mmc3_mode, outer_mode, value, addr, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                    mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                }
            }
        }
    }

    for (const std::size_t size : {kMinChrSize, 0x003000UZ, 0x018000UZ, 0x048000UZ}) {
        auto fitted = board(kMaxPrgSize, size);
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*fitted, reg, values[reg]);
        }
        for (const Byte outer_mode : {Byte{0U}, Byte{4U}}) {
            outer(*fitted, 0U, 0x70U, outer_mode);
            for (const Address addr :
                 {0x0000U, 0x03FFU, 0x0800U, 0x1000U, 0x1555U, 0x1FFFU, 0x3FFFU}) {
                const auto physical = expected_chr(addr, 0x70U, outer_mode, 0U, values) % size;
                CAPTURE(size, outer_mode, addr, physical);
                REQUIRE(fitted->read_chr(addr) == cell(physical, 0x55U));
            }
        }
    }
}

TEST_CASE("Mapper 445 uses MMC3 horizontal vertical console CIRAM only", "[mapper445][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, kMaxChrSize, initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg((value & 1U) != 0U ? 0xBFFEU : 0xA000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
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

TEST_CASE("Mapper 445 exposes optional WRAM and no other bus or audio device",
          "[mapper445][bus][ram][audio]") {
    auto absent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->prg_read_mask(0x6000U) == 0U);
    REQUIRE(absent->read_prg(0x6000U) == 0U);
    REQUIRE(absent->battery_ram().empty());
    outer(*absent, 0x70U, 0x70U, 0x05U);
    REQUIRE(absent->read_prg(0xE000U) == cell(expected_prg(0xE000U, 0x70U, 0x05U, 0U, 0U, 0U)));

    auto mapper = board();
    for (Address addr = 0U; addr < 0x4020U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->maps_prg(0x6000U));
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
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    mapper->write_prg_cpu(0x8000U, 0x80U, true);
    mapper->write_prg_cpu(0x8001U, 0xFEU, false);
    REQUIRE(state(*mapper)[kBankRegistersOffset] == 0xFEU);
}

TEST_CASE("Mapper 445 WRAM enable protection battery import and cold clearing",
          "[mapper445][bus][ram][reset]") {
    auto mapper = board();
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0x1EU);
    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    mapper->write_prg(0x6123U, 0x1EU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x1EU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x1EU);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6123U) == 0x1EU);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == 0U);

    auto persistent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Battery);
    std::array<Byte, kWramSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0xA5U);
    }
    persistent->set_battery_ram(bytes);
    REQUIRE(persistent->battery_ram().size() == kWramSize);
    persistent->write_prg(0x6000U, 0x77U);
    persistent->power_on();
    REQUIRE(persistent->read_prg(0x6123U) == bytes[0x123U]);
    persistent->set_battery_ram(std::span(bytes).first(0x123U));
    REQUIRE(persistent->read_prg(0x6122U) == bytes[0x122U]);
    persistent->set_battery_ram(std::array<Byte, 4>{9U, 8U, 7U, 6U});
    REQUIRE(persistent->read_prg(0x6003U) == 6U);
    REQUIRE(persistent->read_prg(0x6004U) == bytes[4U]);
}

TEST_CASE("Mapper 445 MMC3 IRQ latch reload acknowledge disable and zero repeats",
          "[mapper445][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    outer(*mapper, 0x70U, 0x70U, 0x05U);
    mapper->write_expansion(0x5003U, 0x20U);
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

TEST_CASE("Mapper 445 reset restores all registers and header CIRAM",
          "[mapper445][reset][ram][irq]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    outer(*mapper, 0x70U, 0x60U, 0x05U);
    mapper->write_expansion(0x5003U, 0x20U);
    bank(*mapper, 6U, 0x29U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    const auto reset_state = state(*mapper);
    REQUIRE(std::equal(reset_state.begin(), reset_state.begin() + kRamOffset, initial.begin()));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    mapper->write_expansion(0x5000U, 0x70U);
    REQUIRE(state(*mapper)[kOuterPrgOffset] == 0x70U);
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
}

TEST_CASE("Mapper 445 bounded snapshots replay registers IRQ and RAM without ROM",
          "[mapper445][state][banking][ram][irq]") {
    auto mapper = board();
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        mapper->write_prg(addr, static_cast<Byte>(cell(addr) & 0x7FU));
    }
    outer(*mapper, 0x50U, 0x60U, 0x05U);
    mapper->write_expansion(0x5003U, 0x20U);
    bank(*mapper, 6U, 0x29U, 0xC0U);
    bank(*mapper, 7U, 0x17U, 0xC0U);
    bank(*mapper, 0U, 0xA2U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8223U);
    REQUIRE(saved[kOuterPrgOffset] == 0x50U);
    REQUIRE(saved[kOuterChrOffset] == 0x60U);
    REQUIRE(saved[kOuterModeOffset] == 0x05U);
    REQUIRE(saved[kLockOffset] == 1U);

    auto copy = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Volatile, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0x50U, 0x05U, 0xC0U, 0x29U, 0x17U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    REQUIRE(copy->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0x60U, 0x05U, 0xC0U, {0xA2U, 0U, 0U, 0U, 0U, 0U}), 0xD5U));
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(cell(addr) & 0x7FU));
    }
    copy->write_expansion(0x5000U, 0U);
    copy->write_expansion(0x5003U, 0U);
    REQUIRE(state(*copy) == saved);
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

    auto absent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    REQUIRE(state(*absent).size() == kRamOffset);
    REQUIRE(state(*absent) ==
            state(*board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent, 0x35U)));
    REQUIRE(state(*absent) !=
            state(*board(kMinPrgSize, kMinChrSize, MirrorMode::Vertical, RamKind::Absent)));
}

TEST_CASE("Mapper 445 rejects truncated damaged and mismatched states atomically",
          "[mapper445][state][ram]") {
    auto mapper = board();
    outer(*mapper, 0x50U, 0x60U, 0x05U);
    mapper->write_expansion(0x5003U, 0x20U);
    bank(*mapper, 6U, 0x29U, 0xC0U);
    bank(*mapper, 7U, 0x17U, 0xC0U);
    mapper->write_prg(0x6123U, 0x05U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    const auto saved = state(*mapper);
    const auto active = saved;
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
    constexpr std::array<std::pair<std::size_t, Byte>, 17> invalid_fields = {{{0U, 0U},
                                                                              {2U, 1U},
                                                                              {6U, 1U},
                                                                              {10U, 0U},
                                                                              {12U, 0x80U},
                                                                              {13U, 0x80U},
                                                                              {14U, 2U},
                                                                              {15U, 2U},
                                                                              {16U, 8U},
                                                                              {17U, 1U},
                                                                              {24U, 0x40U},
                                                                              {25U, 0x40U},
                                                                              {26U, 2U},
                                                                              {29U, 8U},
                                                                              {29U, 4U},
                                                                              {30U, 1U},
                                                                              {30U, 0x3FU}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        invalid[kRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(offset, value);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper445>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
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

    auto changed = saved;
    changed[kOuterPrgOffset] = 0x30U;
    changed[kOuterChrOffset] = 0x40U;
    changed[kOuterModeOffset] = 0U;
    changed[kLockOffset] = 0U;
    changed[kBankRegistersOffset] = 0x5AU;
    changed[kRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0x40U, 0U, 0xC0U, {0x5AU, 0U, 0U, 0U, 0U, 0U}), 0x55U));
    mapper->write_expansion(0x5000U, 0x70U);
    REQUIRE(state(*mapper)[kOuterPrgOffset] == 0x70U);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x05U ^ 0xFFU));
}

} // namespace
