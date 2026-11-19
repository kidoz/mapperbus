#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper394.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kModeOffset = 11UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kMmc3SelectOffset = 13UZ;
constexpr std::size_t kMmc3BanksOffset = 14UZ;
constexpr std::size_t kMmc3MirrorOffset = 22UZ;
constexpr std::size_t kMmc3CounterOffset = 23UZ;
constexpr std::size_t kMmc3ReloadOffset = 24UZ;
constexpr std::size_t kMmc3IrqOffset = 25UZ;
constexpr std::size_t kJyLatchesOffset = 59UZ;
constexpr std::size_t kJyJumpersOffset = 61UZ;
constexpr std::size_t kJyMultiplyOffset = 68UZ;
constexpr std::size_t kJyPendingOffset = 77UZ;
constexpr std::size_t kJyA12Offset = 78UZ;
constexpr std::size_t kStateSize = 79UZ;

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 394U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 14U) * 37U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 79U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper394> board(Byte submapper = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 Byte salt = 0U) {
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper394::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper394>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper394& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void mode(Mapper394& mapper, unsigned value, Byte pins = 0U) {
    mapper.write_expansion(0x5009U, static_cast<Byte>((value << 3U) | pins));
}

void outer(Mapper394& mapper, Byte value) {
    mapper.write_expansion(0x500BU, value);
}

void mmc3_bank(Mapper394& mapper, Byte index, Byte value, Byte control = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | control));
    mapper.write_prg(0x8001U, value);
}

std::size_t mode_prg_base(Byte pins) {
    return static_cast<std::size_t>(pins & 3U) << 18U;
}

std::size_t mode_chr_base(Byte submapper, Byte pins) {
    const unsigned a18 = submapper == 0U ? pins & 1U : (pins >> 2U) & 1U;
    return (static_cast<std::size_t>(pins & 2U) << 18U) | (static_cast<std::size_t>(a18) << 18U);
}

std::size_t expected_bnrom_prg(Address addr, Byte pins, Byte control) {
    return mode_prg_base(pins) | (static_cast<std::size_t>(control & 8U) << 14U) |
           (static_cast<std::size_t>((control >> 1U) & 3U) << 15U) | (addr & 0x7FFFU);
}

std::size_t expected_bnrom_chr(Address addr, Byte submapper, Byte pins, Byte control) {
    return mode_chr_base(submapper, pins) | (static_cast<std::size_t>(control & 0x40U) << 11U) |
           (addr & 0x1FFFU);
}

std::size_t expected_mmc3_prg(
    Address addr, Byte pins, Byte control, Byte bank_control, Byte r6, Byte r7) {
    const unsigned count = (control & 0x10U) != 0U ? 32U : 16U;
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const bool swapped = (bank_control & 0x40U) != 0U;
    const unsigned bank = slot == 1U                ? r7 % count
                          : slot == 3U              ? count - 1U
                          : (slot == 0U) == swapped ? count - 2U
                                                    : r6 % count;
    std::size_t offset = bank * kPrgBankSize + (addr & 0x1FFFU);
    if ((control & 0x10U) == 0U) {
        offset |= static_cast<std::size_t>(control & 8U) << 14U;
    }
    return mode_prg_base(pins) | offset;
}

std::size_t expected_mmc3_chr(Address addr,
                              Byte submapper,
                              Byte pins,
                              Byte control,
                              Byte bank_control,
                              const std::array<Byte, 6>& banks) {
    const unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((bank_control & 0x80U) != 0U ? 4U : 0U);
    const unsigned bank = slot < 4U ? (banks[slot / 2U] & 0xFEU) | (slot & 1U) : banks[slot - 2U];
    const std::size_t inner =
        (control & 0x80U) != 0U
            ? bank * kChrBankSize
            : ((bank & 0x7FU) * kChrBankSize) | (static_cast<std::size_t>(control & 0x40U) << 11U);
    return mode_chr_base(submapper, pins) | inner | (addr & 0x03FFU);
}

std::size_t expected_jy_prg(Address addr,
                            Byte pins,
                            Byte config,
                            const std::array<Byte, 4>& banks) {
    const unsigned bank_mode = config & 3U;
    const unsigned shift = bank_mode < 2U ? 2U - bank_mode : 0U;
    const unsigned unit = kPrgBankSize << shift;
    unsigned bank = 0U;
    unsigned offset = addr & (unit - 1U);
    if (addr < 0x8000U) {
        bank = (static_cast<unsigned>(banks[3]) << shift) | ((1U << shift) - 1U);
        offset = addr & 0x1FFFU;
    } else {
        const unsigned slot = (addr - 0x8000U) / unit;
        const unsigned reg = bank_mode == 0U ? 3U : (bank_mode == 1U ? slot * 2U + 1U : slot);
        bank = reg == 3U && (config & 4U) == 0U ? 0x7FU : banks[reg];
    }
    if (bank_mode == 3U) {
        unsigned reversed = 0U;
        for (unsigned bit = 0U; bit < 7U; ++bit) {
            reversed |= ((bank >> bit) & 1U) << (6U - bit);
        }
        bank = reversed;
    }
    const unsigned bank_unit = addr < 0x8000U ? kPrgBankSize : unit;
    return mode_prg_base(pins) | ((bank * bank_unit + offset) & 0x1FFFFU);
}

std::size_t expected_jy_chr(Address addr,
                            Byte submapper,
                            Byte pins,
                            Byte config,
                            Byte outer_config,
                            const std::array<std::uint16_t, 8>& banks,
                            const std::array<Byte, 2>& latches = {0U, 0U}) {
    const unsigned chr_mode = (config >> 3U) & 3U;
    const unsigned unit = 0x2000U >> chr_mode;
    const unsigned slot = (addr & 0x1FFFU) / unit;
    unsigned reg = slot << (3U - chr_mode);
    if (chr_mode == 1U && (outer_config & 0x80U) != 0U) {
        reg += 2U * latches[slot];
    }
    const std::size_t inner = static_cast<std::size_t>(banks[reg]) * unit + (addr & (unit - 1U));
    return mode_chr_base(submapper, pins) | (inner & 0x1FFFFU);
}

void arm_jy_irq(Mapper394& mapper, Byte irq_mode, Byte prescaler, Byte counter) {
    mapper.write_prg(0xC002U, 0U);
    mapper.write_prg(0xC001U, irq_mode);
    mapper.write_prg(0xC006U, 0U);
    mapper.write_prg(0xC004U, prescaler);
    mapper.write_prg(0xC005U, counter);
    mapper.write_prg(0xC003U, 0U);
}

TEST_CASE("Mapper 394 validates both submappers decoded sizes and ROM-only metadata",
          "[mapper394][geometry]") {
    for (Byte submapper : {Byte{0U}, Byte{1U}}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(submapper, mirror);
            const auto initial = state(*mapper);
            REQUIRE(initial.size() == kStateSize);
            REQUIRE(initial[kModeOffset] == 0x0FU);
            REQUIRE(initial[kOuterOffset] == 0x90U);
            REQUIRE(initial[kMmc3MirrorOffset] == static_cast<Byte>(mirror));
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE(mapper->battery_ram().empty());
        }
    }
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size += kPrgBankSize) {
        REQUIRE(Mapper394::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size = 0x2000UZ; size <= kChrSize; size += kChrBankSize) {
        REQUIRE(Mapper394::validate_geometry(header(), kPrgSize, size));
    }
    for (const unsigned id : {0U, 4U, 90U, 393U, 395U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper394::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper394::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 2U; submapper < 16U; ++submapper) {
        REQUIRE_FALSE(Mapper394::validate_geometry(header(submapper), kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x80001UZ, 0x100000UZ}) {
        REQUIRE_FALSE(Mapper394::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2200UZ, 0x80001UZ, 0x100000UZ}) {
        REQUIRE_FALSE(Mapper394::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper394::validate_geometry(header(0U, mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
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
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        REQUIRE_FALSE(Mapper394::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 394 decodes only literal mode and outer write ports with open reads",
          "[mapper394][bus][decoding]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x5009U, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kModeOffset] == (value & 0x1FU));
        mapper->write_expansion(0x500BU, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0xDEU));
    }
    for (Address addr = 0x4020U; addr < 0x6000U; ++addr) {
        mapper->reset();
        mapper->write_expansion(addr, 0U);
        const auto current = state(*mapper);
        CAPTURE(addr);
        REQUIRE(current[kModeOffset] == (addr == 0x5009U ? 0U : 0x0FU));
        REQUIRE(current[kOuterOffset] == (addr == 0x500BU ? 0U : 0x90U));
        REQUIRE(mapper->maps_expansion(addr) == (addr == 0x5009U || addr == 0x500BU));
        if (addr == 0x5009U || addr == 0x500BU) {
            REQUIRE(mapper->expansion_read_mask(addr) == 0U);
            REQUIRE(mapper->read_expansion(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 394 BNROM combines outer 32 KiB pages with fixed 8 KiB CHR",
          "[mapper394][banking]") {
    for (Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (Byte pins = 0U; pins < 8U; ++pins) {
            for (Byte control = 0U; control < 128U; ++control) {
                mode(*mapper, 0U, pins);
                outer(*mapper, static_cast<Byte>(control | 0x90U));
                for (const Address addr : {0x8000U, 0x9123U, 0xBFFFU, 0xC000U, 0xE456U, 0xFFFFU}) {
                    const auto physical = expected_bnrom_prg(addr, pins, control);
                    CAPTURE(submapper, pins, control, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical % kPrgSize));
                }
                for (const Address addr : {0x0000U, 0x0123U, 0x1000U, 0x1FFFU}) {
                    const auto physical = expected_bnrom_chr(addr, submapper, pins, control);
                    REQUIRE(mapper->read_chr(addr) == cell(physical % kChrSize));
                    mapper->write_chr(addr, 0xA5U);
                    REQUIRE(mapper->read_chr(addr) == cell(physical % kChrSize));
                }
            }
        }
        const auto before = state(*mapper);
        for (Address addr = 0x8000U; addr != 0U; ++addr) {
            mapper->write_prg(addr, static_cast<Byte>(addr));
        }
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 394 MMC3 PRG uses local 128 or 256 KiB fixed banks and outer A17",
          "[mapper394][banking]") {
    auto mapper = board();
    mode(*mapper, 1U, 3U);
    for (Byte bank_control : {Byte{0U}, Byte{0x40U}}) {
        for (Byte r6 = 0U; r6 < 64U; r6 += 9U) {
            const Byte r7 = static_cast<Byte>(63U - r6);
            mmc3_bank(*mapper, 6U, r6, bank_control);
            mmc3_bank(*mapper, 7U, r7, bank_control);
            for (Byte control = 0U; control < 32U; ++control) {
                outer(*mapper, static_cast<Byte>(control | 0xC0U));
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto addr = static_cast<Address>(0x8123U + slot * kPrgBankSize);
                    const auto physical =
                        expected_mmc3_prg(addr, 3U, control, bank_control, r6, r7);
                    CAPTURE(bank_control, r6, r7, control, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical % kPrgSize));
                }
            }
        }
    }
    for (const std::size_t size : {0x8000UZ, 0x18000UZ, 0x48000UZ}) {
        auto fitted = board(0U, MirrorMode::Vertical, size);
        mode(*fitted, 1U, 3U);
        outer(*fitted, 0x18U);
        mmc3_bank(*fitted, 6U, 63U, 0x40U);
        for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xFFFFU}) {
            const auto physical = expected_mmc3_prg(addr, 3U, 0x18U, 0x40U, 63U, 0U) % size;
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 394 MMC3 CHR applies pairing inversion A17 source and submapper A18",
          "[mapper394][banking]") {
    constexpr std::array<Byte, 6> banks = {0xA2U, 0x8BU, 0x93U, 0xACU, 0xB1U, 0x87U};
    for (Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (Byte bank_control : {Byte{0U}, Byte{0x80U}}) {
            for (Byte reg = 0U; reg < 6U; ++reg) {
                mmc3_bank(*mapper, reg, banks[reg], bank_control);
            }
            for (Byte pins = 0U; pins < 8U; ++pins) {
                mode(*mapper, 1U, pins);
                for (Byte control = 0U; control < 4U; ++control) {
                    const Byte decoded = static_cast<Byte>(control << 6U);
                    outer(*mapper, decoded | 0x10U);
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                        const auto physical =
                            expected_mmc3_chr(addr, submapper, pins, decoded, bank_control, banks);
                        CAPTURE(submapper, bank_control, pins, decoded, addr, physical);
                        REQUIRE(mapper->read_chr(addr) == cell(physical % kChrSize));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 394 MMC3 register aliases preserve banking CIRAM and IRQ state",
          "[mapper394][bus][ciram][irq]") {
    auto mapper = board();
    mode(*mapper, 1U);
    for (unsigned addr = 0x8000U; addr < 0xA000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0xC5U);
        REQUIRE(state(*mapper)[kMmc3SelectOffset] == 0xC5U);
    }
    for (unsigned addr = 0x8001U; addr < 0xA000U; addr += 2U) {
        mapper->write_prg(0x8000U, 2U);
        mapper->write_prg(static_cast<Address>(addr), 0xA6U);
        REQUIRE(state(*mapper)[kMmc3BanksOffset + 2U] == 0xA6U);
    }
    for (unsigned addr = 0xA000U; addr < 0xC000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr >> 1U));
        REQUIRE(mapper->mirror_mode() ==
                ((addr & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE_FALSE(mapper->nametable_address(0x2000U));
        REQUIRE_FALSE(mapper->read_nametable(0x2000U));
        REQUIRE_FALSE(mapper->write_nametable(0x2000U, 0xA5U));
    }
    for (unsigned addr = 0xC000U; addr < 0xE000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        REQUIRE(state(*mapper)[kMmc3ReloadOffset] == static_cast<Byte>(addr));
    }
    for (unsigned addr = 0xC001U; addr < 0xE000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0U);
        REQUIRE(state(*mapper)[kMmc3CounterOffset] == 0U);
        REQUIRE((state(*mapper)[kMmc3IrqOffset] & 1U) != 0U);
    }
    mapper->write_prg(0xC000U, 0U);
    for (unsigned addr = 0xE000U; addr < 0x10000U; addr += 2U) {
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(static_cast<Address>(addr), 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 394 J.Y. PRG modes retain ASIC banks while mode pins replace outer pins",
          "[mapper394][banking]") {
    auto mapper = board();
    mode(*mapper, 2U, 5U);
    const std::array<Byte, 4> banks = {1U, 2U, 3U, 4U};
    for (unsigned reg = 0U; reg < 4U; ++reg) {
        mapper->write_prg(static_cast<Address>(0x8000U + reg), banks[reg]);
    }
    for (Byte bank_mode = 0U; bank_mode < 4U; ++bank_mode) {
        for (Byte switch_last : {Byte{0U}, Byte{4U}}) {
            const Byte config = static_cast<Byte>(bank_mode | switch_last);
            mapper->write_prg(0xD000U, config);
            for (Byte ignored_outer : {Byte{0U}, Byte{1U}, Byte{0x38U}}) {
                mapper->write_prg(0xD003U, ignored_outer);
                for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U, 0x9123U, 0xF456U}) {
                    const auto physical = expected_jy_prg(addr, 5U, config, banks);
                    CAPTURE(bank_mode, switch_last, ignored_outer, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical % kPrgSize));
                }
            }
        }
    }
    mapper->write_prg(0xD000U, 0x80U);
    for (const Address addr : {0x6000U, 0x6123U, 0x7FFFU}) {
        const auto physical = expected_jy_prg(addr, 5U, 0x80U, banks);
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_prg(addr) == cell(physical % kPrgSize));
        mapper->write_prg(addr, 0xA5U);
        REQUIRE(mapper->read_prg(addr) == cell(physical % kPrgSize));
    }
    mapper->write_prg(0xD000U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    mode(*mapper, 3U, 5U);
    REQUIRE(mapper->read_prg(0x9123U) == cell(expected_jy_prg(0x9123U, 5U, 0U, banks) % kPrgSize));
}

TEST_CASE("Mapper 394 J.Y. CHR modes high bytes and physical-read latches use a 128 KiB window",
          "[mapper394][banking]") {
    auto mapper = board(1U);
    mode(*mapper, 2U, 7U);
    std::array<std::uint16_t, 8> banks{};
    for (unsigned reg = 0U; reg < 8U; ++reg) {
        banks[reg] = 0x102U + reg;
        mapper->write_prg(static_cast<Address>(0x9000U + reg), static_cast<Byte>(banks[reg]));
        mapper->write_prg(static_cast<Address>(0xA000U + reg), static_cast<Byte>(banks[reg] >> 8U));
    }
    for (Byte config = 0U; config < 32U; config += 8U) {
        mapper->write_prg(0xD000U, config);
        for (const Address addr :
             {0x0000U, 0x0400U, 0x0800U, 0x0C00U, 0x1000U, 0x1400U, 0x1800U, 0x1C55U}) {
            const auto physical = expected_jy_chr(addr, 1U, 7U, config, 0U, banks);
            CAPTURE(config, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical % kChrSize));
        }
    }
    mapper->write_prg(0xD000U, 8U);
    mapper->write_prg(0xD003U, 0x80U);
    std::array<Byte, 2> latches = {0U, 0U};
    REQUIRE(mapper->read_chr(0U) ==
            cell(expected_jy_chr(0U, 1U, 7U, 8U, 0x80U, banks, latches) % kChrSize));
    static_cast<void>(mapper->read_chr(0x0FE8U));
    REQUIRE(state(*mapper)[kJyLatchesOffset] == 0U);
    mapper->observe_ppu_read(0x0FE8U);
    latches[0] = 1U;
    REQUIRE(state(*mapper)[kJyLatchesOffset] == 1U);
    REQUIRE(mapper->read_chr(0U) ==
            cell(expected_jy_chr(0U, 1U, 7U, 8U, 0x80U, banks, latches) % kChrSize));
    mapper->observe_ppu_read(0x1FE8U);
    latches[1] = 1U;
    REQUIRE(state(*mapper)[kJyLatchesOffset + 1U] == 1U);
    mapper->observe_ppu_read(0x0FD8U);
    latches[0] = 0U;
    REQUIRE(state(*mapper)[kJyLatchesOffset] == 0U);
}

TEST_CASE("Mapper 394 J.Y. CIRAM arrangements and ROM nametables share outer CHR pins",
          "[mapper394][ciram]") {
    auto mapper = board(1U);
    mode(*mapper, 2U, 7U);
    constexpr std::array modes = {MirrorMode::Vertical,
                                  MirrorMode::Horizontal,
                                  MirrorMode::SingleLower,
                                  MirrorMode::SingleUpper};
    for (Byte value = 0U; value < 4U; ++value) {
        mapper->write_prg(0xD001U, value);
        REQUIRE(mapper->mirror_mode() == modes[value]);
        REQUIRE_FALSE(mapper->nametable_address(0x2400U));
    }
    mapper->write_prg(0xD001U, 8U);
    mapper->write_prg(0xB000U, 0U);
    mapper->write_prg(0xB001U, 1U);
    mapper->write_prg(0xB002U, 0U);
    REQUIRE(mapper->nametable_address(0x2403U) == 0x403U);
    REQUIRE(mapper->nametable_address(0x2003U) == 0x003U);

    for (unsigned reg = 0U; reg < 4U; ++reg) {
        mapper->write_prg(static_cast<Address>(0xB000U + reg), static_cast<Byte>(0x80U + reg));
        mapper->write_prg(static_cast<Address>(0xB004U + reg), 1U);
    }
    mapper->write_prg(0xD000U, 0x60U);
    for (unsigned index = 0U; index < 4U; ++index) {
        const auto addr = static_cast<Address>(0x2000U + index * 0x400U);
        const auto physical =
            mode_chr_base(1U, 7U) | (((0x180U + index) * kChrBankSize) & 0x1FFFFU);
        CAPTURE(index, addr, physical);
        REQUIRE(mapper->read_nametable(addr) == cell(physical % kChrSize));
        REQUIRE(mapper->write_nametable(addr, 0xA5U));
        REQUIRE(mapper->read_nametable(addr) == cell(physical % kChrSize));
    }
    mapper->write_prg(0xD000U, 0x20U);
    mapper->write_prg(0xB001U, 3U);
    mapper->write_prg(0xD002U, 0x80U);
    REQUIRE(mapper->read_nametable(0x2400U));
    REQUIRE_FALSE(mapper->read_nametable(0x2000U));
    mode(*mapper, 1U, 7U);
    REQUIRE_FALSE(mapper->read_nametable(0x2400U));
    REQUIRE_FALSE(mapper->nametable_address(0x2400U));
}

TEST_CASE("Mapper 394 exposes no CPU or CHR RAM and no expansion audio",
          "[mapper394][bus][ram][audio]") {
    auto mapper = board();
    for (unsigned selected = 0U; selected < 3U; ++selected) {
        mode(*mapper, selected);
        for (const Address addr : {0x4020U, 0x5000U, 0x5800U, 0x6000U, 0x7FFFU}) {
            if (addr < 0x6000U) {
                REQUIRE(mapper->maps_expansion(addr) == (selected >= 2U && addr >= 0x5000U));
            } else {
                REQUIRE_FALSE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0U);
                REQUIRE(mapper->read_prg(addr) == 0U);
                mapper->write_prg(addr, 0xA5U);
                REQUIRE(mapper->read_prg(addr) == 0U);
            }
        }
        for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
            const auto before = mapper->read_chr(addr);
            mapper->write_chr(addr, static_cast<Byte>(before ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == before);
        }
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        mapper->clock_audio();
        mapper->set_region(Region::PAL);
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->prg_read_mask(0xFFFFU) == 0xFFU);
    REQUIRE(mapper->maps_expansion(0x5009U));
    REQUIRE(mapper->expansion_read_mask(0x5009U) == 0U);
    mode(*mapper, 0U);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 394 J.Y. math jumpers and mode-gated observers retain bounded timing",
          "[mapper394][bus][timing]") {
    auto mapper = board();
    for (Byte jumpers = 0U; jumpers < 4U; ++jumpers) {
        REQUIRE(mapper->configure_board_jumpers(jumpers));
        mode(*mapper, 2U);
        for (const Address addr : {0x5000U, 0x5400U, 0x5C00U}) {
            REQUIRE(mapper->maps_expansion(addr));
            REQUIRE(mapper->expansion_read_mask(addr) == 0xC0U);
            REQUIRE(mapper->read_expansion(addr) == jumpers * 64U);
        }
    }
    REQUIRE_FALSE(mapper->configure_board_jumpers(4U));
    REQUIRE(mapper->observes_ppu_a12());
    REQUIRE(mapper->observes_ppu_reads());
    mapper->write_expansion(0x5800U, 23U);
    mapper->write_expansion(0x5FFDU, 17U);
    for (unsigned cycle = 0U; cycle < 7U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE(static_cast<unsigned>(mapper->read_expansion(0x5800U) |
                                      (mapper->read_expansion(0x5801U) << 8U)) == 0U);
    }
    mapper->clock_cpu();
    REQUIRE(static_cast<unsigned>(mapper->read_expansion(0x5800U) |
                                  (mapper->read_expansion(0x5801U) << 8U)) == 391U);
    mapper->write_expansion(0x5802U, 250U);
    mapper->write_expansion(0x5FFEU, 11U);
    REQUIRE(mapper->read_expansion(0x5802U) == 5U);
    mapper->write_expansion(0x5FFFU, 0xA5U);
    REQUIRE(mapper->read_expansion(0x5802U) == 0U);
    REQUIRE(mapper->read_expansion(0x5803U) == 0xA5U);
    mapper->write_expansion(0x5801U, 2U);
    mode(*mapper, 1U);
    for (unsigned cycle = 0U; cycle < 8U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->maps_expansion(0x5800U));
    mode(*mapper, 2U);
    REQUIRE(mapper->read_expansion(0x5800U) == 0x87U);
    for (unsigned cycle = 0U; cycle < 8U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->read_expansion(0x5800U) == 46U);
}

TEST_CASE("Mapper 394 MMC3 IRQ reload zero repeats acknowledgement and active-mode gating",
          "[mapper394][irq]") {
    auto mapper = board();
    mode(*mapper, 1U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mode(*mapper, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mode(*mapper, 1U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    for (unsigned repeat = 0U; repeat < 3U; ++repeat) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
    }
    mapper->write_prg(0xE000U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 394 J.Y. IRQ sources directions prescalers and acknowledgement",
          "[mapper394][irq]") {
    auto mapper = board();
    mode(*mapper, 2U);
    for (Byte source = 0U; source < 4U; ++source) {
        for (Byte direction : {Byte{0x40U}, Byte{0x80U}}) {
            for (Byte narrow : {Byte{0U}, Byte{4U}}) {
                arm_jy_irq(*mapper,
                           static_cast<Byte>(direction | narrow | source),
                           direction == 0x40U ? 0xFEU : 1U,
                           direction == 0x40U ? 0xFFU : 0U);
                const auto tick = [&] {
                    switch (source) {
                    case 0U:
                        mapper->clock_cpu();
                        break;
                    case 1U:
                        mapper->observe_ppu_a12(false);
                        mapper->observe_ppu_a12(true);
                        break;
                    case 2U:
                        mapper->observe_ppu_read(0x2000U);
                        break;
                    default:
                        mapper->observe_cpu_write(0x10U, 0U);
                        break;
                    }
                };
                tick();
                REQUIRE_FALSE(mapper->irq_pending());
                tick();
                REQUIRE(mapper->irq_pending());
                tick();
                REQUIRE(mapper->irq_pending());
                mapper->acknowledge_irq();
                REQUIRE(mapper->irq_pending());
                mapper->write_prg(0xC802U, 0xFFU);
                REQUIRE_FALSE(mapper->irq_pending());
            }
        }
    }
    arm_jy_irq(*mapper, 0x40U, 0xFEU, 0xFFU);
    mapper->clock_cpu();
    mapper->clock_cpu();
    mode(*mapper, 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    mapper->observe_cpu_write(0x10U, 0U);
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    mode(*mapper, 2U);
    REQUIRE(mapper->irq_pending());
    arm_jy_irq(*mapper, 0x44U, 5U, 0xFFU);
    mapper->clock_cpu();
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC003U, 0U);
    for (unsigned cycle = 0U; cycle < 7U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 394 reset restores documented latches and clears devices but not jumpers",
          "[mapper394][reset]") {
    auto mapper = board(1U, MirrorMode::Horizontal);
    REQUIRE(mapper->configure_board_jumpers(3U));
    const auto initial = state(*mapper);
    mode(*mapper, 1U, 1U);
    mmc3_bank(*mapper, 6U, 19U, 0xC0U);
    mode(*mapper, 2U, 7U);
    outer(*mapper, 0x5EU);
    mapper->write_prg(0xD001U, 3U);
    mapper->write_prg(0x8003U, 9U);
    mapper->write_expansion(0x5800U, 4U);
    mapper->write_expansion(0x5801U, 5U);
    arm_jy_irq(*mapper, 0x40U, 0xFEU, 0xFFU);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper) != initial);

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kJyJumpersOffset] == 3U);
    mode(*mapper, 2U);
    REQUIRE(mapper->read_expansion(0x5000U) == 0xC0U);
    mapper->write_expansion(0x5800U, 7U);
    mapper->write_expansion(0x5801U, 8U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 394 fixed snapshots replay without ROM and reject every truncation atomically",
          "[mapper394][state]") {
    auto mapper = board(1U);
    REQUIRE(mapper->configure_board_jumpers(2U));
    mode(*mapper, 1U, 1U);
    mmc3_bank(*mapper, 6U, 19U, 0xC0U);
    mmc3_bank(*mapper, 2U, 0xA6U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mode(*mapper, 2U, 7U);
    outer(*mapper, 0x4EU);
    mapper->write_prg(0x8003U, 9U);
    mapper->write_prg(0x9001U, 0x55U);
    mapper->write_prg(0xA001U, 1U);
    mapper->write_prg(0xD003U, 0x80U);
    mapper->observe_ppu_read(0x1FE8U);
    mapper->write_expansion(0x5800U, 23U);
    mapper->write_expansion(0x5801U, 17U);
    arm_jy_irq(*mapper, 0x40U, 0xFEU, 0xFFU);
    mapper->clock_cpu();
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kJyMultiplyOffset] == 5U);
    REQUIRE(saved[kJyPendingOffset] == 1U);
    REQUIRE(saved[kJyA12Offset] == 1U);

    auto copy = board(1U, MirrorMode::Vertical, kPrgSize, kChrSize, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->irq_pending());
    for (unsigned cycle = 0U; cycle < 5U; ++cycle) {
        copy->clock_cpu();
    }
    REQUIRE(static_cast<unsigned>(copy->read_expansion(0x5800U) |
                                  (copy->read_expansion(0x5801U) << 8U)) == 391U);
    const std::array<Byte, 4> jy_banks = {0U, 0U, 0U, 9U};
    const auto expected = expected_jy_prg(0xE123U, 7U, 0U, jy_banks);
    REQUIRE(copy->read_prg(0xE123U) == cell(expected % kPrgSize, 0x80U));
    load(*copy, saved);
    REQUIRE(copy->irq_pending());
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail(with_tail);
    copy->load_state(tail);
    REQUIRE(tail.ok());
    REQUIRE(tail.remaining() == 3U);

    mapper->reset();
    mapper->write_expansion(0x5009U, 0x11U);
    mapper->write_expansion(0x5800U, 3U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 21> invalid_fields = {
        {{0U, 0U},     {2U, 1U},     {6U, 1U},  {10U, 0U}, {11U, 0x20U}, {12U, 1U}, {13U, 8U},
         {20U, 0x40U}, {21U, 0x40U}, {22U, 2U}, {25U, 8U}, {25U, 4U},    {26U, 1U}, {27U, 0x80U},
         {59U, 2U},    {61U, 4U},    {68U, 9U}, {76U, 2U}, {77U, 2U},    {78U, 2U}, {0U, 0xFFU}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_submapper = board(0U);
    const auto wrong_submapper_before = state(*wrong_submapper);
    StateReader wrong_submapper_reader(saved);
    wrong_submapper->load_state(wrong_submapper_reader);
    REQUIRE_FALSE(wrong_submapper_reader.ok());
    REQUIRE(state(*wrong_submapper) == wrong_submapper_before);

    auto wrong_mirror = board(1U, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto wrong_prg = board(1U, MirrorMode::Vertical, kPrgSize / 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(1U, MirrorMode::Vertical, kPrgSize, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);
}

} // namespace
