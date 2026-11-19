#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/mappers/mapper373.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

enum class Wram { None, Volatile, Persistent };

constexpr std::size_t kPrgSize = 0x800000UZ;
constexpr std::size_t kChrSize = 0x400000UZ;
constexpr std::size_t kWramSize = 0x2000UZ;
constexpr std::size_t kOuterOffset = 25UZ;
constexpr std::size_t kOuterIndexOffset = 29UZ;
constexpr std::size_t kStateSize = 32UZ;
constexpr std::size_t kWramStateSize = kStateSize + kWramSize;
constexpr std::array<Address, 4> kPrgSlots{0x8123U, 0xA123U, 0xC123U, 0xE123U};

INesHeader header(Wram wram = Wram::None, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 373U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = wram == Wram::Persistent;
    result.prg_ram_size = wram == Wram::Volatile ? kWramSize : 0U;
    result.prg_nvram_size = wram == Wram::Persistent ? kWramSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 17U) * 43U + (offset >> 19U) * 13U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper373> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 Wram wram = Wram::None,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(wram, mirror);
    REQUIRE(Mapper373::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper373>(h, rom(prg_size, salt), rom(chr_size, salt + 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void outer(Mapper373& mapper, Byte r0, Byte r1, Byte r2, Byte r3) {
    mapper.write_prg(0x6001U, 0U);
    for (const Byte value : {r0, r1, r2, r3}) {
        mapper.write_prg(0x6000U, value);
    }
}

void bank(Mapper373& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void set_chr_banks(Mapper373& mapper, const std::array<Byte, 8>& banks, Byte mode = 0U) {
    for (Byte index = 0U; index < 6U; ++index) {
        bank(mapper, index, banks[index], mode);
    }
}

Byte expected_raw_chr_bank(const std::array<Byte, 8>& banks, Byte mode, Address addr) {
    unsigned slot = (static_cast<unsigned>(addr) >> 10U) & 7U;
    if ((mode & 0x80U) != 0U) {
        slot ^= 4U;
    }
    return slot < 4U ? static_cast<Byte>((banks[slot / 2U] & 0xFEU) | (slot & 1U))
                     : banks[slot - 2U];
}

unsigned expected_gnrom_inner(unsigned slot, Byte r6, Byte r7, Byte mode) {
    const unsigned mmc3_slot = slot & 1U;
    const bool swapped = (mode & 0x40U) != 0U;
    unsigned inner = mmc3_slot == 1U ? r7 & 0x3FU : (mmc3_slot == 0U) == swapped ? 62U : r6 & 0x3FU;
    inner = slot < 2U ? inner & ~2U : inner | 2U;
    return inner;
}

std::size_t register_offset(Wram wram) {
    return 10U + (wram == Wram::None ? 0U : kWramSize);
}

TEST_CASE("Mapper 373 validates NES 2.0 ROM RAM and CIRAM geometry", "[mapper373]") {
    for (const auto wram : {Wram::None, Wram::Volatile, Wram::Persistent}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(wram, mirror);
            REQUIRE(Mapper373::validate_geometry(header(wram, mirror), 0x8000U, 0x2000U));
            REQUIRE(Mapper373::validate_geometry(header(wram, mirror), 0xA000U, 0x2400U));
            REQUIRE(Mapper373::validate_geometry(header(wram, mirror), kPrgSize, kChrSize));
        }
        auto mapper = board(0x8000U, 0x2000U, wram);
        REQUIRE(state(*mapper).size() == (wram == Wram::None ? kStateSize : kWramStateSize));
        REQUIRE(mapper->battery_ram().size() == (wram == Wram::Persistent ? kWramSize : 0U));
        REQUIRE(mapper->maps_prg(0x6000U) == (wram != Wram::None));
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const auto id : {0U, 4U, 45U, 356U, 372U, 374U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper373::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper373::validate_geometry(h, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper373::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x2000UZ,
                                   0x4000UZ,
                                   0x6000UZ,
                                   0x7FFFUZ,
                                   0x8001UZ,
                                   0xA001UZ,
                                   kPrgSize + 0x2000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper373::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x400UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, kChrSize + 0x400UZ, 0x800000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper373::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper373::validate_geometry(header(Wram::None, mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 10U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x1000U;
            break;
        case 2U:
            h.prg_ram_size = 0x4000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x1000U;
            break;
        case 4U:
            h.prg_nvram_size = kWramSize;
            break;
        case 5U:
            h.prg_ram_size = h.prg_nvram_size = kWramSize;
            break;
        case 6U:
            h.chr_ram_size = 0x2000U;
            break;
        case 7U:
            h.chr_ram_size = 0x1000U;
            break;
        case 8U:
            h.chr_ram_size = 0x3000U;
            break;
        default:
            h.chr_nvram_size = kWramSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper373::validate_geometry(h, kPrgSize, kChrSize));
    }
    h = header(Wram::Persistent);
    h.prg_ram_size = kWramSize;
    REQUIRE_FALSE(Mapper373::validate_geometry(h, kPrgSize, kChrSize));
}

TEST_CASE("Mapper 373 decodes every sequential outer and MMC3 alias", "[mapper373][bus]") {
    auto mapper = board(0x8000U, 0x2000U);
    auto expected = board(0x8000U, 0x2000U);
    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        mapper->reset();
        expected->reset();
        auto bytes = state(*expected);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        if ((raw & 1U) == 0U) {
            bytes[kOuterOffset] = value;
            bytes[kOuterIndexOffset] = 1U;
        }
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == bytes);
    }

    outer(*mapper, 0x12U, 0x34U, 0xF6U, 0xD0U);
    auto locked = state(*mapper);
    REQUIRE(locked[kOuterOffset] == 0x12U);
    REQUIRE(locked[kOuterOffset + 1U] == 0x34U);
    REQUIRE(locked[kOuterOffset + 2U] == 0xF6U);
    REQUIRE(locked[kOuterOffset + 3U] == 0x50U);
    REQUIRE(locked[kOuterIndexOffset] == 0U);
    for (Address addr = 0x6000U; addr < 0x8000U; addr = static_cast<Address>(addr + 2U)) {
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == locked);
    mapper->write_prg(0x6001U, 0U);
    mapper->write_prg(0x6000U, 0xA6U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0xA6U);
    REQUIRE(state(*mapper)[kOuterOffset + 1U] == 0U);
    REQUIRE(state(*mapper)[kOuterIndexOffset] == 1U);

    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        expected->write_prg(static_cast<Address>(0x8000U | (raw & 0xE001U)), value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == state(*expected));
    }
    mapper->reset();
    mapper->write_prg_cpu(0x8000U, 0xC6U, true);
    mapper->write_prg_cpu(0x8001U, 0xFFU, false);
    REQUIRE(state(*mapper)[12U + 6U] == 0x3FU);
}

TEST_CASE("Mapper 373 preserves Mapper 045 PRG banking outside GNROM mode",
          "[mapper373][banking]") {
    auto mapper = board();
    for (const Byte control : {0x00U, 0x15U, 0x20U, 0x3FU, 0x55U}) {
        outer(*mapper, 0U, 0xA5U, 0xE0U, control);
        const unsigned mask = 0x3FU ^ static_cast<unsigned>(control);
        constexpr unsigned kOuter = 0xA5U | 0x300U;
        for (const Byte mode : {0U, 0x40U}) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                bank(*mapper, 6U, static_cast<Byte>(raw), mode);
                bank(*mapper, 7U, static_cast<Byte>(raw ^ 0x35U), mode);
                const std::array<unsigned, 4> inner = {
                    mode == 0U ? raw & 63U : 62U,
                    (raw ^ 0x35U) & 63U,
                    mode == 0U ? 62U : raw & 63U,
                    63U,
                };
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto expected = ((inner[slot] & mask) | kOuter) % (kPrgSize / 0x2000U);
                    CAPTURE(control, mode, raw, slot, expected);
                    REQUIRE(mapper->read_prg(kPrgSlots[slot]) == cell(expected * 0x2000U + 0x123U));
                }
            }
        }
    }
}

TEST_CASE("Mapper 373 GNROM mode holds MMC3 A14 low and forces PRG bank bit 1",
          "[mapper373][banking]") {
    auto mapper = board();
    for (const Byte control : {0x00U, 0x15U, 0x20U, 0x3FU, 0x55U}) {
        outer(*mapper, 0U, 0xA5U, 0xF0U, control);
        const unsigned mask = 0x3FU ^ static_cast<unsigned>(control);
        constexpr unsigned kOuter = 0xA5U | 0x300U;
        for (const Byte mode : {0U, 0x40U}) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto r6 = static_cast<Byte>(raw);
                const auto r7 = static_cast<Byte>(raw ^ 0x35U);
                bank(*mapper, 6U, r6, mode);
                bank(*mapper, 7U, r7, mode);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto inner = expected_gnrom_inner(slot, r6, r7, mode);
                    const auto expected = ((inner & mask) | kOuter) % (kPrgSize / 0x2000U);
                    CAPTURE(control, mode, raw, slot, inner, expected);
                    REQUIRE(mapper->read_prg(kPrgSlots[slot]) == cell(expected * 0x2000U + 0x123U));
                }
            }
        }
    }

    outer(*mapper, 0U, 0U, 0x10U, 0U);
    bank(*mapper, 6U, 0x15U);
    bank(*mapper, 7U, 0x26U);
    REQUIRE(mapper->read_prg(0x8123U) == cell(0x15U * 0x2000U + 0x123U));
    REQUIRE(mapper->read_prg(0xA123U) == cell(0x24U * 0x2000U + 0x123U));
    REQUIRE(mapper->read_prg(0xC123U) == cell(0x17U * 0x2000U + 0x123U));
    REQUIRE(mapper->read_prg(0xE123U) == cell(0x26U * 0x2000U + 0x123U));
}

TEST_CASE("Mapper 373 wraps smaller PRG ROMs only after outer decoding", "[mapper373][banking]") {
    for (const std::size_t prg_size : {0x8000UZ, 0xA000UZ, 0x18000UZ, 0x20000UZ}) {
        auto mapper = board(prg_size, 0x2000U);
        outer(*mapper, 0U, 0xD7U, 0xE0U, 0x08U);
        bank(*mapper, 6U, 0x2AU);
        const auto offset =
            static_cast<std::size_t>(((0x2AU & 0x37U) | 0xD7U | 0x300U) * 0x2000U + 0x123U) %
            prg_size;
        CAPTURE(prg_size, offset);
        REQUIRE(mapper->read_prg(0x8123U) == cell(offset));
        REQUIRE(mapper->maps_prg(0x8000U));
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    }
}

TEST_CASE("Mapper 373 applies CHR AND and OR pins after pairing and inversion",
          "[mapper373][banking]") {
    auto mapper = board();
    for (unsigned width = 0U; width < 16U; ++width) {
        outer(*mapper, 0xA5U, 0U, static_cast<Byte>(0xD0U | width), 0U);
        const unsigned and_mask = width < 8U ? 0U : (1U << (width - 7U)) - 1U;
        constexpr unsigned kOuter = 0xA5U | 0xC00U;
        for (const Byte mode : {0U, 0x80U}) {
            std::array<Byte, 8> banks{};
            for (Byte index = 0U; index < 6U; ++index) {
                banks[index] = static_cast<Byte>(index * 37U + 0x83U);
            }
            set_chr_banks(*mapper, banks, mode);
            for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
                const auto raw = static_cast<Address>(addr);
                const auto inner = expected_raw_chr_bank(banks, mode, raw);
                const auto expected = ((inner & and_mask) | kOuter) % (kChrSize / 0x400U);
                CAPTURE(width, mode, addr, inner, expected);
                REQUIRE(mapper->read_chr(raw) == cell(expected * 0x400U + (addr & 0x3FFU), 0x55U));
                mapper->write_chr(raw, 0U);
            }
        }
    }

    std::array<Byte, 8> banks{0x12U, 0x34U, 0x56U, 0x78U, 0x9AU, 0xBCU, 0U, 0U};
    set_chr_banks(*mapper, banks, 0x80U);
    outer(*mapper, 0xA5U, 0U, 0xC0U, 0U);
    std::array<Byte, 32> without_gnrom{};
    for (unsigned addr = 0U; addr < 0x2000U; addr += 0x101U) {
        without_gnrom[addr / 0x101U] = mapper->read_chr(static_cast<Address>(addr));
    }
    outer(*mapper, 0xA5U, 0U, 0xD0U, 0U);
    for (unsigned addr = 0U; addr < 0x2000U; addr += 0x101U) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == without_gnrom[addr / 0x101U]);
    }
}

TEST_CASE("Mapper 373 CHR ROM is read-only and GNROM does not select CHR RAM",
          "[mapper373][ram][banking]") {
    auto mapper = board(0x8000U, 0x2000U);
    constexpr std::array<Byte, 8> kIdentity{0U, 2U, 4U, 5U, 6U, 7U, 0U, 0U};
    for (const Byte control : {0x0FU, 0x1FU, 0x3FU, 0x7FU, 0xFFU}) {
        outer(*mapper, 0U, 0U, control, 0U);
        set_chr_banks(*mapper, kIdentity, control & 0x80U);
        for (unsigned addr = 0U; addr < 0x2000U; addr += 0x101U) {
            const auto port = static_cast<Address>(addr);
            const auto previous = mapper->read_chr(port);
            mapper->write_chr(port, static_cast<Byte>(previous ^ 0xFFU));
            CAPTURE(control, addr, previous);
            REQUIRE(mapper->read_chr(port) == previous);
        }
    }
}

TEST_CASE("Mapper 373 uses ordinary software-controlled CIRAM mirroring", "[mapper373][ciram]") {
    auto mapper = board(0x8000U, 0x2000U, Wram::None, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xA000U, static_cast<Byte>(value));
        const auto expected = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        CAPTURE(value, expected);
        REQUIRE(mapper->mirror_mode() == expected);
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr = static_cast<Address>(addr + 0x100U)) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 373 overlays sequential writes on gated WRAM and bounds CPU claims",
          "[mapper373][ram][bus]") {
    auto mapper = board(0x8000U, 0x2000U, Wram::Persistent);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA7U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0xFFU);
    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
    mapper->write_prg(0x6001U, 0U);
    mapper->write_prg(0x6000U, 0x5AU);
    REQUIRE(state(*mapper)[register_offset(Wram::Persistent) + 15U] == 0x5AU);

    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->battery_ram().size() == kWramSize);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    for (Address addr = 0x6000U; addr < 0x6004U; ++addr) {
        REQUIRE(mapper->read_prg(addr) == addr - 0x6000U + 1U);
    }
    std::vector<Byte> oversized(kWramSize + 1U, 0xA6U);
    mapper->set_battery_ram(oversized);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA6U);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0xA6U);

    auto volatile_mapper = board(0x8000U, 0x2000U, Wram::Volatile);
    REQUIRE(volatile_mapper->battery_ram().empty());
    const auto before = state(*volatile_mapper);
    volatile_mapper->set_battery_ram(oversized);
    REQUIRE(state(*volatile_mapper) == before);
}

TEST_CASE("Mapper 373 preserves Mapper 045 DIP and explicit menu chip-enable wiring",
          "[mapper373][bus]") {
    auto mapper = board(0x80000U, 0x80000U);
    constexpr std::array<Address, 8> kPins = {
        0x10U, 0x20U, 0x40U, 0x80U, 0x100U, 0x200U, 0x400U, 0x800U};
    for (Byte dip = 0U; dip < 8U; ++dip) {
        REQUIRE(mapper->configure_board_jumpers(dip));
        const auto before = state(*mapper);
        for (unsigned addr = 0x5000U; addr < 0x6000U; ++addr) {
            const auto port = static_cast<Address>(addr);
            CAPTURE(dip, addr);
            REQUIRE(mapper->maps_expansion(port));
            REQUIRE(mapper->expansion_read_mask(port) == 1U);
            REQUIRE(mapper->read_expansion(port) == ((addr & kPins[dip]) != 0U ? 1U : 0U));
            mapper->write_expansion(port, 0xFFU);
        }
        REQUIRE(state(*mapper) == before);
    }
    REQUIRE_FALSE(mapper->maps_expansion(0x4FFFU));
    REQUIRE_FALSE(mapper->maps_expansion(0x6000U));
    REQUIRE(mapper->read_expansion(0x4FFFU) == 0U);
    const auto configured = state(*mapper);
    REQUIRE_FALSE(mapper->configure_board_jumpers(8U));
    REQUIRE(state(*mapper) == configured);

    constexpr std::array<std::string_view, 5> kProfiles = {"menu-selection-0",
                                                           "menu-selection-1",
                                                           "menu-selection-2",
                                                           "menu-selection-3",
                                                           "menu-selection-4"};
    for (Byte selection = 0U; selection < 5U; ++selection) {
        REQUIRE(mapper->configure_board_profile(kProfiles[selection]));
        REQUIRE(mapper->observes_ppu_address() == (selection == 2U || selection == 4U));
        outer(*mapper,
              0U,
              selection == 1U ? 0x80U : (selection == 3U ? 0x40U : 0U),
              selection == 2U ? 0x40U : (selection == 4U ? 0x20U : 0U),
              0U);
        mapper->observe_ppu_address(0U);
        const bool disabled = selection != 0U;
        CAPTURE(selection, disabled);
        REQUIRE(mapper->prg_read_mask(0x8000U) == (disabled ? 0U : 0xFFU));
        REQUIRE(mapper->read_prg(0x8000U) == (disabled ? 0U : cell(0U)));
        outer(*mapper, 0U, 0U, 0U, 0U);
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    }
    const auto before = state(*mapper);
    for (const auto profile : {"", "menu-0", "menu-selection-5", "menu-selection-x"}) {
        REQUIRE_FALSE(mapper->configure_board_profile(profile));
        REQUIRE(state(*mapper) == before);
    }
    auto large = board();
    const auto large_before = state(*large);
    REQUIRE_FALSE(large->configure_board_profile("menu-selection-1"));
    REQUIRE(state(*large) == large_before);
    REQUIRE(large->configure_board_profile("menu-selection-0"));
}

TEST_CASE("Mapper 373 MMC3 IRQ reload acknowledge repeat and disable survive outer writes",
          "[mapper373][irq]") {
    auto mapper = board(0x8000U, 0x2000U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    outer(*mapper, 1U, 2U, 0x10U, 4U);
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

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned repeat = 0U; repeat < 3U; ++repeat) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 373 has no expansion audio or unrelated clock side effects",
          "[mapper373][audio][irq]") {
    auto mapper = board(0x8000U, 0x2000U);
    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 373 reset restores registers and cold power clears only volatile RAM",
          "[mapper373][reset]") {
    for (const auto wram : {Wram::Volatile, Wram::Persistent}) {
        auto mapper = board(0x8000U, 0x2000U, wram, MirrorMode::Horizontal);
        REQUIRE(mapper->configure_board_jumpers(6U));
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(0x6123U, 0xA7U);
        outer(*mapper, 1U, 2U, 0xD0U, 3U);
        bank(*mapper, 6U, 9U, 0xC0U);
        mapper->observe_ppu_address(0x1234U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());

        mapper->reset();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(state(*mapper)[30U + register_offset(wram) - 10U] == 0U);
        REQUIRE(mapper->read_expansion(0x5400U) == 1U);

        mapper->power_on();
        REQUIRE(mapper->read_prg(0x6123U) == (wram == Wram::Persistent ? 0xA7U : 0U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_expansion(0x5400U) == 1U);
    }
}

TEST_CASE("Mapper 373 fixed bounded snapshots replay registers and RAM without ROM",
          "[mapper373][state]") {
    auto mapper = board(0x80000U, 0x80000U, Wram::Persistent);
    REQUIRE(mapper->configure_board_jumpers(5U));
    REQUIRE(mapper->configure_board_profile("menu-selection-1"));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA7U);
    outer(*mapper, 1U, 2U, 0x5FU, 3U);
    bank(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->observe_ppu_address(0x1000U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kWramStateSize);
    REQUIRE(saved.size() == 8224U);
    REQUIRE(saved.size() < 0x80000U);

    auto copy = board(0x80000U, 0x80000U, Wram::Persistent, MirrorMode::Vertical, 0x80U);
    REQUIRE(copy->configure_board_jumpers(5U));
    REQUIRE(copy->configure_board_profile("menu-selection-1"));
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6123U) == 0xA7U);
    REQUIRE(copy->read_prg(0x8123U) == static_cast<Byte>(mapper->read_prg(0x8123U) + 0x80U));
    REQUIRE(copy->read_chr(0x0123U) == static_cast<Byte>(mapper->read_chr(0x0123U) + 0x80U));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 373 rejects truncated malformed and incompatible states atomically",
          "[mapper373][state]") {
    auto mapper = board(0x80000U, kChrSize, Wram::Persistent);
    mapper->write_prg(0x6123U, 0xA7U);
    outer(*mapper, 1U, 2U, 0x6FU, 3U);
    bank(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->observe_ppu_address(0x1000U);
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

    const std::size_t base = register_offset(Wram::Persistent);
    const std::array<std::pair<std::size_t, Byte>, 15> bad_fields = {{
        {0U, 0U},
        {4U, 0U},
        {8U, 0U},
        {base + 0U, 8U},
        {base + 1U, 1U},
        {base + 8U, 64U},
        {base + 9U, 64U},
        {base + 10U, 2U},
        {base + 13U, 8U},
        {base + 13U, 4U},
        {base + 14U, 1U},
        {base + 18U, 0x80U},
        {base + 19U, 4U},
        {base + 21U, 0x20U},
        {base + 21U, 0xFFU},
    }};
    for (const auto [offset, value] : bad_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto smaller = board(0x40000U, kChrSize, Wram::Persistent);
    const auto smaller_before = state(*smaller);
    StateReader wrong_prg(saved);
    smaller->load_state(wrong_prg);
    REQUIRE_FALSE(wrong_prg.ok());
    REQUIRE(state(*smaller) == smaller_before);

    auto other_mirror = board(0x80000U, kChrSize, Wram::Persistent, MirrorMode::Horizontal);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);

    auto no_wram = board(0x80000U, kChrSize);
    const auto no_wram_before = state(*no_wram);
    StateReader wrong_wram(saved);
    no_wram->load_state(wrong_wram);
    REQUIRE_FALSE(wrong_wram.ok());
    REQUIRE(state(*no_wram) == no_wram_before);

    REQUIRE(mapper->configure_board_jumpers(3U));
    const auto dip_before = state(*mapper);
    StateReader wrong_dip(saved);
    mapper->load_state(wrong_dip);
    REQUIRE_FALSE(wrong_dip.ok());
    REQUIRE(state(*mapper) == dip_before);
}

TEST_CASE("Mapper 373 state validation accepts every raw second outer register value",
          "[mapper373][state]") {
    auto mapper = board(0x8000U, 0x2000U);
    const auto saved = state(*mapper);
    for (unsigned control = 0U; control < 256U; ++control) {
        auto valid = saved;
        valid[kOuterOffset + 2U] = static_cast<Byte>(control);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == valid);
    }
}

} // namespace
