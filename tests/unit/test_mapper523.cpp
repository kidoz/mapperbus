#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "core/mappers/mapper523.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kDefaultPrgSize = 0x200000UZ;
constexpr std::size_t kMaxPrgSize = 0x1000000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kDefaultChrSize = 0x200000UZ;
constexpr std::size_t kMaxChrSize = 0x1000000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kWramSize = 0x4000UZ;
constexpr std::size_t kStatePrefixSize = 16UZ;
constexpr std::size_t kCoreStateSize = 60UZ;
constexpr std::size_t kEmptyStateSize = kStatePrefixSize + kCoreStateSize;
constexpr std::size_t kWramStateSize = kEmptyStateSize + 2UZ * kWramSize;
constexpr std::size_t kInnerModeOffset = 10UZ;
constexpr std::size_t kInnerChrBaseOffset = 12UZ;
constexpr std::size_t kInnerExtendedOffset = 15UZ;
constexpr std::size_t kInnerBankSelectOffset = 18UZ;
constexpr std::size_t kInnerBanksOffset = 19UZ;
constexpr std::size_t kInnerVolatileLengthOffset = 44UZ;
constexpr std::size_t kInnerVolatileOffset = 52UZ;
constexpr std::size_t kInnerNvramLengthOffset = kInnerVolatileOffset + kWramSize;
constexpr std::size_t kInnerNvramOffset = kInnerNvramLengthOffset + 8UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, bool wram = true) {
    INesHeader result{};
    result.mapper_number = 523U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kDefaultPrgSize;
    result.chr_rom_size = kDefaultChrSize;
    if (wram) {
        result.has_battery = true;
        result.prg_ram_size = kWramSize;
        result.prg_nvram_size = kWramSize;
    }
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 11U) * 37U +
                             (offset >> 13U) * 47U + (offset >> 16U) * 59U + (offset >> 20U) * 83U +
                             (offset >> 24U) * 101U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper523> board(std::size_t prg_size = kDefaultPrgSize,
                                 std::size_t chr_size = kDefaultChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 bool wram = true,
                                 Byte salt = 0U) {
    auto h = header(mirror, wram);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper523::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper523>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x5AU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper523& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper523& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0234U);
    const auto wram = mapper.read_prg(0x6123U);
    const auto irq = mapper.irq_pending();
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0234U) == chr);
    REQUIRE(mapper.read_prg(0x6123U) == wram);
    REQUIRE(mapper.irq_pending() == irq);
}

void outer(Mapper523& mapper, unsigned index, Byte value, Address base = 0x5010U) {
    mapper.write_expansion(static_cast<Address>(base + index), value);
}

void reg(Mapper523& mapper, Byte index, Byte value, Byte select = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | select));
    mapper.write_prg(0x8001U, value);
}

void require_chr(
    Mapper523& mapper, Address addr, std::uint32_t physical, std::size_t chr_size, Byte salt) {
    const auto offset = physical % chr_size;
    CAPTURE(addr, physical, offset, chr_size, salt);
    REQUIRE(mapper.read_chr(addr) == cell(offset, salt ^ 0x5AU));
}

void require_prg(
    Mapper523& mapper, Address addr, std::uint32_t physical, std::size_t prg_size, Byte salt) {
    const auto offset = physical % prg_size;
    CAPTURE(addr, physical, offset, prg_size, salt);
    REQUIRE(mapper.read_prg(addr) == cell(offset, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

std::uint32_t normal_chr_physical(
    const std::array<Byte, 12>& banks, Byte bank_select, Byte mode, Byte chr_base, Address addr) {
    if ((mode & 0x40U) != 0U) {
        return (static_cast<std::uint32_t>(chr_base) << 14U) | ((addr & 0x1800U) << 1U) |
               (addr & 0x07FFU);
    }
    const unsigned slot = ((addr >> 10U) & 6U) ^ ((bank_select & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner = slot < 4U ? (banks[slot >> 1U] & 0xFEU) | (slot & 1U) : banks[slot - 2U];
    const std::uint32_t mask = (mode & 0x10U) != 0U ? 0x7FU : 0xFFU;
    const std::uint32_t bank =
        (inner & mask) | ((static_cast<std::uint32_t>(chr_base) << 3U) & ~mask);
    return (bank << 11U) | (addr & 0x07FFU);
}

std::uint32_t extended_chr_physical(const std::array<Byte, 12>& banks,
                                    Byte bank_select,
                                    Byte chr_base,
                                    Address addr) {
    constexpr std::array<Byte, 8> kSelectors = {0U, 10U, 1U, 11U, 2U, 3U, 4U, 5U};
    const unsigned slot = ((addr >> 10U) & 6U) ^ ((bank_select & 0x80U) != 0U ? 4U : 0U);
    const std::uint32_t bank =
        banks[kSelectors[slot]] | ((static_cast<std::uint32_t>(chr_base) << 3U) & ~0xFFU);
    return (bank << 11U) | (addr & 0x07FFU);
}

TEST_CASE("Mapper 523 validates hard-wired ROM and WRAM geometry",
          "[mapper523][geometry][ram][ciram][state]") {
    for (const auto prg : {kMinPrgSize, kDefaultPrgSize, kMaxPrgSize}) {
        for (const auto chr : {kMinChrSize, kDefaultChrSize, kMaxChrSize}) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                for (const bool wram : {false, true}) {
                    auto h = header(mirror, wram);
                    CAPTURE(prg, chr, mirror, wram);
                    REQUIRE(Mapper523::validate_geometry(h, prg, chr));
                    auto mapper = board(prg, chr, mirror, wram, 0x35U);
                    auto different_rom = board(prg, chr, mirror, wram, 0xA0U);
                    REQUIRE(state(*mapper) == state(*different_rom));
                    REQUIRE(state(*mapper).size() == (wram ? kWramStateSize : kEmptyStateSize));
                    REQUIRE(mapper->mirror_mode() == mirror);
                    REQUIRE(mapper->battery_ram().size() == (wram ? kWramSize : 0U));
                    REQUIRE(mapper->shared_nvram().empty());
                    REQUIRE_FALSE(mapper->has_flash_storage());
                    REQUIRE_FALSE(mapper->irq_pending());
                    REQUIRE_FALSE(mapper->has_expansion_audio());
                    REQUIRE(mapper->audio_output() == 0.0F);
                    require_prg(*mapper, 0x8000U, 0U, prg, 0x35U);
                    require_prg(*mapper, 0xA000U, kPrgBankSize, prg, 0x35U);
                    require_prg(*mapper, 0xC000U, 0x3EU * kPrgBankSize, prg, 0x35U);
                    require_prg(*mapper, 0xE000U, 0x3FU * kPrgBankSize, prg, 0x35U);
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const auto addr = static_cast<Address>(slot * 0x400U + 0x200U);
                        require_chr(
                            *mapper, addr, (slot & 6U) * 0x800U + (addr & 0x7FFU), chr, 0x35U);
                    }
                }
            }
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
        REQUIRE_FALSE(Mapper523::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 176U, 197U, 522U, 524U, 4095U}) {
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
    for (const std::size_t size : {0UZ, 1UZ, 0x4000UZ, 0x8001UZ, 0xC000UZ, kMaxPrgSize * 2UZ}) {
        reject(header(), size, kDefaultChrSize);
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x1000UZ, 0x2001UZ, 0x3000UZ, kMaxChrSize * 2UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, kDefaultChrSize);
    }
    for (unsigned field = 0U; field < 12U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_trainer = true;
            break;
        case 1U:
            h.has_battery = false;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 0x2000U;
            break;
        case 4U:
            h.prg_nvram_size = 0U;
            break;
        case 5U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 6U:
            h.chr_ram_size = 0x2000U;
            break;
        case 7U:
            h.chr_nvram_size = 0x2000U;
            break;
        case 8U:
            h = header(MirrorMode::Vertical, false);
            h.has_battery = true;
            break;
        case 9U:
            h = header(MirrorMode::Vertical, false);
            h.prg_ram_size = kWramSize;
            break;
        case 10U:
            h = header(MirrorMode::Vertical, false);
            h.prg_nvram_size = kWramSize;
            break;
        default:
            h = header(MirrorMode::Vertical, false);
            h.prg_ram_size = kWramSize;
            h.prg_nvram_size = kWramSize;
            break;
        }
        CAPTURE(field);
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
}

TEST_CASE("Mapper 523 decodes every FS005 outer-register alias",
          "[mapper523][decoding][banking][bus]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, false);
    for (Byte pad = 0U; pad <= 7U; ++pad) {
        REQUIRE(mapper->configure_board_jumpers(pad));
        const Address selected = static_cast<Address>(0x10U << pad);
        for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
            mapper->reset();
            const auto addr = static_cast<Address>(raw);
            mapper->write_expansion(addr, 0x40U);
            const Address mask = static_cast<Address>(0x5003U | selected);
            const bool decoded = raw >= 0x5000U && raw < 0x6000U && (raw & 3U) == 1U &&
                                 (raw & mask) == (0x5000U | selected | 1U);
            CAPTURE(pad, raw, decoded);
            require_prg(
                *mapper, 0x8000U, (decoded ? 0x80U : 0U) * kPrgBankSize, kDefaultPrgSize, 0U);
        }
        outer(*mapper, 1U, 0U, 0x5FF0U);
        require_prg(*mapper, 0x8000U, 0U, kDefaultPrgSize, 0U);
        REQUIRE(mapper->configure_board_profile("menu-" + std::to_string(pad)));
    }
    REQUIRE_FALSE(mapper->configure_board_jumpers(8U));
    for (const auto* profile : {"menu-8", "menu--1", "menu-x", "menu-1x", "other", ""}) {
        REQUIRE_FALSE(mapper->configure_board_profile(profile));
    }
}

TEST_CASE("Mapper 523 exhaustively decodes MMC3 bank-select and data aliases",
          "[mapper523][decoding][banking][chr][bus]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, false);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        mapper->write_prg(0x8000U, 0U);
        const auto addr = static_cast<Address>(raw);
        mapper->write_prg(addr, 0xA5U);
        const bool select_decoded = (raw & 0xE003U) == 0x8000U;
        const bool data_decoded = (raw & 0xE003U) == 0x8001U;
        const auto saved = state(*mapper);
        CAPTURE(raw, select_decoded, data_decoded);
        REQUIRE(saved[kStatePrefixSize + kInnerBankSelectOffset] == (select_decoded ? 0x85U : 0U));
        const unsigned bank = data_decoded ? 0xA4U : select_decoded ? 4U : 0U;
        require_chr(*mapper, 0U, bank * 0x800U, kDefaultChrSize, 0U);
    }
}

TEST_CASE("Mapper 523 preserves FS005 PRG modes and extended banks",
          "[mapper523][banking][prg][decoding]") {
    auto normal = board();
    outer(*normal, 1U, 0x35U);
    for (Byte mode : {0U, 1U, 2U}) {
        outer(*normal, 0U, mode);
        const std::uint32_t mask = mode == 0U ? 0x3FU : mode == 1U ? 0x1FU : 0x0FU;
        const std::uint32_t base = 0x35U * 2U;
        for (Byte select : {0U, 0x40U}) {
            reg(*normal, 6U, 0x97U, select);
            reg(*normal, 7U, 0x63U, select);
            const std::array<std::uint32_t, 4> banks = {
                select == 0U ? (0x17U & mask) | (base & ~mask) : ((base & ~mask) | mask) - 1U,
                (0x23U & mask) | (base & ~mask),
                select == 0U ? ((base & ~mask) | mask) - 1U : (0x17U & mask) | (base & ~mask),
                (base & ~mask) | mask};
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                require_prg(*normal,
                            static_cast<Address>(0x8000U + slot * kPrgBankSize + 0x123U),
                            banks[slot] * kPrgBankSize + 0x123U,
                            kDefaultPrgSize,
                            0U);
            }
        }
    }

    auto mapper = board();
    outer(*mapper, 1U, 0x15U);
    outer(*mapper, 0U, 3U);
    for (Address addr = 0x8000U;; ++addr) {
        require_prg(*mapper, addr, (0x15U << 14U) | (addr & 0x3FFFU), kDefaultPrgSize, 0U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
    outer(*mapper, 0U, 4U);
    require_prg(*mapper, 0x8123U, (0x14U << 14U) | 0x123U, kDefaultPrgSize, 0U);
    require_prg(*mapper, 0xF123U, (0x14U << 14U) | 0x7123U, kDefaultPrgSize, 0U);
    outer(*mapper, 0U, 5U);
    for (Byte latch = 0U; latch < 8U; ++latch) {
        mapper->write_prg(0x9FFFU, latch);
        require_prg(*mapper, 0x8123U, ((0x10U | latch) << 14U) | 0x123U, kDefaultPrgSize, 0U);
        require_prg(*mapper, 0xF123U, (0x17U << 14U) | 0x3123U, kDefaultPrgSize, 0U);
    }

    outer(*mapper, 3U, 2U);
    for (Byte select : {0U, 0x40U}) {
        reg(*mapper, 6U, 0x81U, select);
        reg(*mapper, 7U, 0x42U, select);
        reg(*mapper, 8U, 0x73U, select);
        reg(*mapper, 9U, 0x24U, select);
        const std::array<std::uint32_t, 4> banks = {
            select == 0U ? 0x81U : 0x73U, 0x42U, select == 0U ? 0x73U : 0x81U, 0x24U};
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            require_prg(*mapper,
                        static_cast<Address>(0x8000U + slot * kPrgBankSize + 0x123U),
                        banks[slot] * kPrgBankSize + 0x123U,
                        kDefaultPrgSize,
                        0U);
        }
    }
}

TEST_CASE("Mapper 523 shifts normal CHR outputs and preserves physical PPU A10",
          "[mapper523][banking][chr][bus]") {
    for (const std::size_t size : {kMinChrSize, 0x40000UZ, kDefaultChrSize}) {
        auto mapper = board(kDefaultPrgSize, size, MirrorMode::Vertical, false, 0x35U);
        outer(*mapper, 2U, 0x15U);
        std::array<Byte, 12> banks = {0U, 2U, 4U, 5U, 6U, 7U, 0U, 1U, 0xFEU, 0xFFU, 0xFFU, 0xFFU};
        for (const Byte mode : {Byte{0U}, Byte{0x10U}}) {
            outer(*mapper, 0U, mode);
            for (const Byte inversion : {Byte{0U}, Byte{0x80U}}) {
                for (Byte index = 0U; index < 6U; ++index) {
                    for (unsigned value = 0U; value < 256U; ++value) {
                        banks[index] = static_cast<Byte>(value);
                        reg(*mapper, index, static_cast<Byte>(value), inversion);
                        CAPTURE(mode, inversion, index, value);
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            for (const Address offset :
                                 {Address{0U}, Address{0x200U}, Address{0x3FFU}}) {
                                const auto addr = static_cast<Address>(slot * 0x400U + offset);
                                require_chr(
                                    *mapper,
                                    addr,
                                    normal_chr_physical(banks, inversion, mode, 0x15U, addr),
                                    size,
                                    0x35U);
                                mapper->write_chr(addr, static_cast<Byte>(value ^ 0xFFU));
                                require_chr(
                                    *mapper,
                                    addr,
                                    normal_chr_physical(banks, inversion, mode, 0x15U, addr),
                                    size,
                                    0x35U);
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 523 extended CHR uses documented 2 KiB registers and ignores R3 R5",
          "[mapper523][banking][chr][decoding]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, false, 0xA0U);
    outer(*mapper, 2U, 0x15U);
    outer(*mapper, 3U, 2U);
    std::array<Byte, 12> banks = {0U, 2U, 4U, 5U, 6U, 7U, 0U, 1U, 0xFEU, 0xFFU, 0xFFU, 0xFFU};
    for (const Byte inversion : {Byte{0U}, Byte{0x80U}}) {
        for (Byte index = 0U; index < 12U; ++index) {
            for (unsigned value = 0U; value < 256U; ++value) {
                banks[index] = static_cast<Byte>(value);
                reg(*mapper, index, static_cast<Byte>(value), inversion);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * 0x400U + 0x234U);
                    require_chr(*mapper,
                                addr,
                                extended_chr_physical(banks, inversion, 0x15U, addr),
                                kDefaultChrSize,
                                0xA0U);
                }
            }
        }
    }
}

TEST_CASE("Mapper 523 direct CHR mode keeps 8 KiB outer pages and PPU A10",
          "[mapper523][banking][chr]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, false, 0x35U);
    outer(*mapper, 0U, 0x40U);
    for (unsigned base = 0U; base < 256U; ++base) {
        outer(*mapper, 2U, static_cast<Byte>(base));
        for (Address addr = 0U; addr < 0x2000U; addr += 0x200U) {
            require_chr(*mapper,
                        addr,
                        normal_chr_physical({}, 0U, 0x40U, static_cast<Byte>(base), addr),
                        kDefaultChrSize,
                        0x35U);
        }
    }
}

TEST_CASE("Mapper 523 hard-wires CIRAM and exposes only documented CPU buses",
          "[mapper523][ciram][bus][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, kDefaultChrSize, mirror, false);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0xA000U, static_cast<Byte>(value));
            mapper->write_prg(0xBFFFU, static_cast<Byte>(value ^ 0xFFU));
            REQUIRE(mapper->mirror_mode() == mirror);
        }
        for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        for (Address addr = 0U; addr < 0x8000U; ++addr) {
            const auto before = state(*mapper);
            mapper->write_prg(addr, static_cast<Byte>(addr));
            mapper->write_expansion(addr, static_cast<Byte>(addr ^ 0xFFU));
            REQUIRE(mapper->read_prg(addr) == 0U);
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
            REQUIRE(mapper->read_expansion(addr) == 0U);
            if (addr < 0x5000U || addr >= 0x6000U) {
                REQUIRE(state(*mapper) == before);
            }
        }
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
        REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    }
}

TEST_CASE("Mapper 523 retains FS005 banked WRAM protection and expansion overlay",
          "[mapper523][ram][bus][decoding]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xA001U, static_cast<Byte>(0xE0U | bank));
        mapper->write_prg(0x6123U, static_cast<Byte>(0x40U + bank));
        REQUIRE(mapper->read_prg(0x6123U) == 0x40U + bank);
        REQUIRE(mapper->maps_prg(0x6123U));
    }
    REQUIRE(mapper->battery_ram()[0x123U] == 0x41U);
    REQUIRE(mapper->battery_ram()[0x2123U] == 0x43U);

    mapper->write_prg(0xA001U, 0xA0U);
    for (Address addr = 0x5000U; addr < 0x6000U; ++addr) {
        REQUIRE(mapper->maps_expansion(addr));
        mapper->write_expansion(addr, static_cast<Byte>(addr));
        REQUIRE(mapper->read_expansion(addr) == static_cast<Byte>(addr));
    }
    mapper->write_prg(0xA001U, 0xE2U);
    REQUIRE(mapper->read_prg(0x7000U) == 0U);
    REQUIRE(mapper->read_prg(0x7010U) == 0x10U);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0xFFU);
    REQUIRE_FALSE(mapper->maps_expansion(0x5000U));

    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x66U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6123U, 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x66U);
    mapper->write_prg(0xA001U, 0x20U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->read_prg(0x6123U) == 0U);

    std::array<Byte, kWramSize> imported{};
    imported[7U] = 0x99U;
    mapper->set_battery_ram(imported);
    REQUIRE(mapper->battery_ram()[7U] == 0x99U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::span(imported).first(1U));
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 523 retains MMC3 IRQ progress and repeating zero-latch IRQs",
          "[mapper523][irq][state]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto partial = state(*mapper);
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

    load(*mapper, partial);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 3U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
    }
}

TEST_CASE("Mapper 523 has no expansion audio or unrelated observer side effects",
          "[mapper523][audio][bus][irq]") {
    auto mapper = board();
    outer(*mapper, 3U, 2U);
    reg(*mapper, 0U, 0xA5U, 0xC0U);
    mapper->write_prg(0xA001U, 0xE0U);
    mapper->write_prg(0x6123U, 0x5AU);
    const auto before = state(*mapper);
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
    mapper->write_chr(0x0123U, 0xFFU);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->led_outputs() == std::nullopt);
}

TEST_CASE("Mapper 523 soft reset restores registers and cold power clears volatile WRAM",
          "[mapper523][reset][banking][ram][ciram][irq]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, kDefaultChrSize, mirror, true, 0x35U);
        const auto cold = state(*mapper);
        mapper->write_prg(0xA001U, 0xE1U);
        mapper->write_prg(0x6123U, 0x66U);
        mapper->write_prg(0xA001U, 0xE0U);
        mapper->write_prg(0x6123U, 0x77U);
        outer(*mapper, 0U, 0U);
        outer(*mapper, 1U, 0x12U);
        outer(*mapper, 2U, 0x45U);
        outer(*mapper, 3U, 2U);
        reg(*mapper, 0U, 0xA5U, 0xC0U);
        reg(*mapper, 6U, 9U, 0xC0U);
        mapper->write_prg(0xA000U, 3U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());

        mapper->reset();
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        require_prg(*mapper, 0x8000U, 0U, kDefaultPrgSize, 0x35U);
        require_prg(*mapper, 0xE000U, 0x3FU * kPrgBankSize, kDefaultPrgSize, 0x35U);
        require_chr(*mapper, 0x0200U, 0x0200U, kDefaultChrSize, 0x35U);
        mapper->write_prg(0xA001U, 0xE0U);
        REQUIRE(mapper->read_prg(0x6123U) == 0x77U);
        mapper->write_prg(0xA001U, 0xE1U);
        REQUIRE(mapper->read_prg(0x6123U) == 0x66U);

        mapper->power_on();
        REQUIRE(state(*mapper) != cold);
        REQUIRE(mapper->battery_ram()[0x123U] == 0x66U);
        mapper->write_prg(0xA001U, 0xE0U);
        REQUIRE(mapper->read_prg(0x6123U) == 0U);
        mapper->write_prg(0xA001U, 0xE1U);
        REQUIRE(mapper->read_prg(0x6123U) == 0x66U);
    }
}

TEST_CASE("Mapper 523 bounded snapshots replay without ROM bytes",
          "[mapper523][state][atomic][banking][ram][irq]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, true, 0x35U);
    REQUIRE(mapper->configure_board_jumpers(6U));
    mapper->write_prg(0xA001U, 0xE0U);
    mapper->write_prg(0x6123U, 0x66U);
    mapper->write_prg(0xA001U, 0xE1U);
    mapper->write_prg(0x6123U, 0x77U);
    mapper->write_prg(0xA001U, 0xE0U);
    outer(*mapper, 0U, 0U, 0x5400U);
    outer(*mapper, 1U, 0x23U, 0x5400U);
    outer(*mapper, 2U, 0x45U, 0x5400U);
    outer(*mapper, 3U, 2U, 0x5400U);
    reg(*mapper, 10U, 0xA7U, 0xC0U);
    reg(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kWramStateSize);
    REQUIRE(saved.size() == 32844U);
    REQUIRE(saved.size() < 0x8100UZ);
    REQUIRE(saved[0U] == 0x30U);
    REQUIRE(saved[1U] == 0x52U);
    REQUIRE(saved[2U] == 6U);
    REQUIRE(saved[3U] == 0U);
    REQUIRE(saved[kStatePrefixSize + kInnerModeOffset] == 0U);
    REQUIRE(saved[kStatePrefixSize + kInnerChrBaseOffset] == 0x45U);
    REQUIRE(saved[kStatePrefixSize + kInnerExtendedOffset] == 2U);
    REQUIRE(saved[kStatePrefixSize + kInnerBankSelectOffset] == 0xC6U);
    REQUIRE(saved[kStatePrefixSize + kInnerVolatileOffset + 0x123U] == 0x66U);
    REQUIRE(saved[kStatePrefixSize + kInnerNvramOffset + 0x123U] == 0x77U);

    auto copy = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, true, 0xA0U);
    REQUIRE(copy->configure_board_jumpers(6U));
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    require_chr(
        *copy,
        0x0234U,
        extended_chr_physical(
            {0U, 2U, 4U, 5U, 6U, 7U, 9U, 1U, 0xFEU, 0xFFU, 0xA7U, 0xFFU}, 0xC0U, 0x45U, 0x0234U),
        kDefaultChrSize,
        0xA0U);
    copy->write_prg(0xA001U, 0xE0U);
    REQUIRE(copy->read_prg(0x6123U) == 0x66U);
    copy->write_prg(0xA001U, 0xE1U);
    REQUIRE(copy->read_prg(0x6123U) == 0x77U);
    copy->clock_irq_counter();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());

    auto trailing = saved;
    trailing.push_back(0U);
    load_rejected(*copy, trailing);
    auto wrong_prg = board(kDefaultPrgSize / 2U);
    REQUIRE(wrong_prg->configure_board_jumpers(6U));
    load_rejected(*wrong_prg, saved);
    auto wrong_chr = board(kDefaultPrgSize, kDefaultChrSize / 2U);
    REQUIRE(wrong_chr->configure_board_jumpers(6U));
    load_rejected(*wrong_chr, saved);
    auto no_wram = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, false);
    REQUIRE(no_wram->configure_board_jumpers(6U));
    load_rejected(*no_wram, saved);
    auto wrong_mirror = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Horizontal);
    REQUIRE(wrong_mirror->configure_board_jumpers(6U));
    load_rejected(*wrong_mirror, saved);
    auto wrong_pad = board();
    REQUIRE(wrong_pad->configure_board_jumpers(5U));
    load_rejected(*wrong_pad, saved);
}

TEST_CASE("Mapper 523 rejects truncated damaged and mismatched states atomically",
          "[mapper523][state][atomic][ram][banking][irq]") {
    auto mapper = board();
    outer(*mapper, 3U, 2U);
    reg(*mapper, 0U, 0xA5U, 0xC0U);
    mapper->write_prg(0xA001U, 0xE0U);
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);

    std::vector<std::size_t> lengths;
    for (std::size_t length = 0U; length < 80U; ++length) {
        lengths.push_back(length);
    }
    for (const auto length : {kStatePrefixSize + kInnerVolatileLengthOffset,
                              kStatePrefixSize + kInnerVolatileOffset,
                              kStatePrefixSize + kInnerNvramLengthOffset,
                              kStatePrefixSize + kInnerNvramOffset,
                              saved.size() - 1U}) {
        lengths.push_back(length);
    }
    for (const auto length : lengths) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 4U * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        load_rejected(*mapper, invalid);
    }

    const std::array<std::pair<std::size_t, Byte>, 16> bad_fields = {{
        {0U, 0xFFU},
        {2U, 1U},
        {11U, 0x80U},
        {13U, 1U},
        {14U, 1U},
        {15U, 4U},
        {16U, 8U},
        {17U, 4U},
        {18U, 0x10U},
        {31U, 4U},
        {35U, 8U},
        {35U, 4U},
        {36U, 0xFFU},
        {44U, 1U},
        {kInnerVolatileLengthOffset, 0xFFU},
        {kInnerNvramLengthOffset, 0xFFU},
    }};
    for (const auto [offset, value] : bad_fields) {
        auto invalid = saved;
        invalid[kStatePrefixSize + offset] = value;
        CAPTURE(offset, value);
        load_rejected(*mapper, invalid);
    }

    auto changed = saved;
    changed[kStatePrefixSize + kInnerBanksOffset + 2U] = 0x3CU;
    changed[kStatePrefixSize + kInnerVolatileOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_chr(
        *mapper,
        0x0234U,
        extended_chr_physical(
            {0U, 2U, 0x3CU, 5U, 6U, 7U, 0U, 1U, 0xFEU, 0xFFU, 0xFFU, 0xFFU}, 0xC0U, 0U, 0x0234U),
        kDefaultChrSize,
        0U);
}

} // namespace
