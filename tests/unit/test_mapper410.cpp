#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "core/mappers/mapper410.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x800000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x400000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kRamSize = 0x002000UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kRegisterOffset = kGeometrySize + kRamSize;
constexpr std::size_t kStateSize = kRegisterOffset + 22UZ;
constexpr std::size_t kWramStateSize = kStateSize + kRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

enum class Wram { None, Volatile, Persistent };

std::size_t wram_size(Wram wram) {
    return wram == Wram::None ? 0U : kRamSize;
}

INesHeader header(Wram wram = Wram::None, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 410U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = wram == Wram::Persistent;
    result.prg_ram_size = wram == Wram::Volatile ? kRamSize : 0U;
    result.prg_nvram_size = wram == Wram::Persistent ? kRamSize : 0U;
    result.chr_ram_size = kRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + (offset >> 22U) * 97U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper410> board(std::size_t prg_size = 0x20000UZ,
                                 std::size_t chr_size = 0x20000UZ,
                                 Wram wram = Wram::None,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(wram, mirror);
    REQUIRE(Mapper410::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper410>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x35U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper, Wram wram = Wram::None) {
    const auto bytes = state(mapper);
    const auto first =
        bytes.begin() + static_cast<std::ptrdiff_t>(kRegisterOffset + wram_size(wram));
    return {first, bytes.end()};
}

void load(Mapper410& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void outer(Mapper410& mapper, Byte r0, Byte r1, Byte r2, Byte r3) {
    mapper.write_prg(0x6001U, 0U);
    for (const Byte value : {r0, r1, r2, r3}) {
        mapper.write_prg(0x6000U, value);
    }
}

void bank(Mapper410& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned expected_prg_bank(
    Address addr, Byte r6, Byte r7, Byte mode, Byte outer1, Byte outer2, Byte outer3) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned inner = slot == 1U                               ? r7 & 0x3FU
                           : slot == 3U                             ? 63U
                           : (slot == 0U) == ((mode & 0x40U) != 0U) ? 62U
                                                                    : r6 & 0x3FU;
    const unsigned mask = 0x3FU ^ static_cast<unsigned>(outer3);
    const unsigned high = outer1 | (static_cast<unsigned>(outer2 & 0xC0U) << 2U);
    return (inner & mask) | high;
}

unsigned expected_chr_bank(
    Address addr, const std::array<Byte, 8>& banks, Byte mode, Byte outer0, Byte outer2) {
    const unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    const unsigned raw = slot < 4U ? (banks[slot / 2U] & 0xFEU) | (slot & 1U) : banks[slot - 2U];
    const unsigned width = outer2 & 0x0FU;
    const unsigned mask = width < 8U ? 0U : (1U << (width - 7U)) - 1U;
    const unsigned high = outer0 | (static_cast<unsigned>(outer2 & 0xF0U) << 4U);
    return (raw & mask) | high;
}

TEST_CASE("Mapper 410 validates NES 2.0 geometry and exact memory populations",
          "[mapper410][geometry]") {
    for (const auto wram : {Wram::None, Wram::Volatile, Wram::Persistent}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(kMinPrgSize, kMinChrSize, wram, mirror);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(state(*mapper).size() == (wram == Wram::None ? kStateSize : kWramStateSize));
            REQUIRE(state(*mapper).size() == (wram == Wram::None ? 8228U : 16420U));
            REQUIRE(register_state(*mapper, wram).size() == 22U);
            REQUIRE(mapper->battery_ram().size() == (wram == Wram::Persistent ? kRamSize : 0U));
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
            REQUIRE(mapper->read_prg(0xC000U) == cell(2UZ * kPrgBankSize));
            REQUIRE(mapper->read_prg(0xE000U) == cell(3UZ * kPrgBankSize));
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (const std::size_t prg : {kMinPrgSize, 0xA000UZ, 0x18000UZ, 0x80000UZ, kMaxPrgSize}) {
        for (const std::size_t chr : {kMinChrSize, 0x2400UZ, 0x6000UZ, 0x80000UZ, kMaxChrSize}) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper410::validate_geometry(header(), prg, chr));
        }
    }

    for (const unsigned id : {0U, 4U, 45U, 256U, 409U, 411U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper410::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper410::validate_geometry(legacy, kMinPrgSize, kMinChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        CAPTURE(submapper);
        REQUIRE_FALSE(Mapper410::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, kMaxPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper410::validate_geometry(header(), size, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x400UZ, 0x1FFFUZ, 0x2001UZ, kMaxChrSize + kChrBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper410::validate_geometry(header(), kMinPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(
            Mapper410::validate_geometry(header(Wram::None, mirror), kMinPrgSize, kMinChrSize));
    }
    for (unsigned field = 0U; field < 12U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_nvram_size = kRamSize;
            break;
        case 2U:
            h.prg_ram_size = 0x1000U;
            break;
        case 3U:
            h.prg_ram_size = 2UZ * kRamSize;
            break;
        case 4U:
            h.prg_ram_size = h.prg_nvram_size = kRamSize;
            break;
        case 5U:
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 0x1000U;
            break;
        case 7U:
            h.chr_ram_size = 2UZ * kRamSize;
            break;
        case 8U:
            h.chr_nvram_size = kRamSize;
            break;
        case 9U:
            h.prg_nvram_size = 0x1000U;
            break;
        case 10U:
            h.prg_nvram_size = 2UZ * kRamSize;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper410::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    auto missing_nvram = header(Wram::Volatile);
    missing_nvram.has_battery = true;
    REQUIRE_FALSE(Mapper410::validate_geometry(missing_nvram, kMinPrgSize, kMinChrSize));
}

TEST_CASE("Mapper 410 sequences outer writes locks and independent WRAM accesses",
          "[mapper410][decoding][banking][ram]") {
    auto mapper = board(0x80000UZ, 0x80000UZ, Wram::Persistent);
    mapper->write_prg(0xA001U, 0x80U);
    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        outer(*mapper, 0U, 0U, 0U, 0U);
        mapper->write_prg(static_cast<Address>(raw), 1U);
        if ((raw & 1U) != 0U) {
            mapper->write_prg(0x6000U, 1U);
        }
        CAPTURE(raw);
        REQUIRE(mapper->read_chr(0U) == cell(kChrBankSize, 0x35U));
        REQUIRE(mapper->read_prg(static_cast<Address>(raw)) == 1U);
    }

    outer(*mapper, 0x12U, 0x34U, 0x06U, 0xD0U);
    const auto locked = register_state(*mapper, Wram::Persistent);
    for (unsigned raw = 0x6000U; raw < 0x8000U; raw += 2U) {
        mapper->write_prg(static_cast<Address>(raw), 0xFFU);
    }
    REQUIRE(register_state(*mapper, Wram::Persistent) == locked);
    REQUIRE(mapper->read_chr(0U) == cell(0x12UZ * kChrBankSize, 0x35U));
    mapper->write_prg(0x6001U, 0U);
    mapper->write_prg(0x6000U, 1U);
    REQUIRE(mapper->read_chr(0U) == cell(kChrBankSize, 0x35U));

    mapper->write_prg(0x6001U, 0U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6122U, 0xA7U);
    mapper->write_prg(0xA001U, 0U);
    mapper->write_prg(0x6122U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6122U) == 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6122U));
    REQUIRE(mapper->prg_read_mask(0x6122U) == 0U);
    mapper->write_prg(0xA001U, 0x80U);
    REQUIRE(mapper->read_prg(0x6122U) == 0xA7U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6122U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6122U) == 0xA7U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6122U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6122U) == 0x5AU);

    const auto saved = state(*mapper);
    std::array<Byte, kRamSize> imported{};
    imported.fill(0x6BU);
    mapper->set_battery_ram(imported);
    REQUIRE(mapper->read_prg(0x6122U) == 0x6BU);
    REQUIRE(state(*mapper) != saved);
    auto volatile_mapper = board(0x20000UZ, 0x20000UZ, Wram::Volatile);
    const auto volatile_before = state(*volatile_mapper);
    volatile_mapper->set_battery_ram(imported);
    REQUIRE(state(*volatile_mapper) == volatile_before);
}

TEST_CASE("Mapper 410 composes PRG inner fixed outer and A21 banks before wrapping",
          "[mapper410][decoding][banking][bus]") {
    auto mapper = board(kMaxPrgSize, 0x80000UZ);
    for (const Byte control : {Byte{0x00U}, Byte{0x15U}, Byte{0x20U}, Byte{0x3FU}, Byte{0x55U}}) {
        outer(*mapper, 0U, 0xA5U, 0xC0U, control);
        for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto r6 = static_cast<Byte>(raw);
                const auto r7 = static_cast<Byte>(raw ^ 0x35U);
                bank(*mapper, 6U, r6, mode);
                bank(*mapper, 7U, r7, mode);
                for (const auto addr : kPrgAddresses) {
                    const auto selected =
                        expected_prg_bank(addr, r6, r7, mode, 0xA5U, 0xC0U, control);
                    const auto physical = selected * kPrgBankSize + (addr & (kPrgBankSize - 1U));
                    CAPTURE(control, mode, raw, addr, selected, physical);
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x18000UZ}) {
        auto fitted = board(size, kMinChrSize);
        outer(*fitted, 0U, 0xD7U, 0U, 0x08U);
        bank(*fitted, 6U, 0x2AU);
        for (const auto addr : kPrgAddresses) {
            const auto selected = expected_prg_bank(addr, 0x2AU, 0U, 0U, 0xD7U, 0U, 0x08U);
            const auto physical = (selected * kPrgBankSize + (addr & (kPrgBankSize - 1U))) % size;
            CAPTURE(size, addr, selected, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }

    auto no_wram = board();
    for (const Address addr :
         {0x0000U, 0x401FU, 0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(no_wram->maps_prg(addr));
        REQUIRE(no_wram->prg_read_mask(addr) == 0U);
        REQUIRE(no_wram->read_prg(addr) == 0U);
    }
    REQUIRE(no_wram->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 410 applies CHR pairing inversion AND widths and OR pins to ROM",
          "[mapper410][decoding][banking][chr]") {
    auto mapper = board(0x20000UZ, kMaxChrSize);
    for (const Byte high : {Byte{0x00U}, Byte{0x10U}, Byte{0x20U}, Byte{0x80U}, Byte{0xB0U}}) {
        for (unsigned width = 0U; width < 16U; ++width) {
            const auto control = static_cast<Byte>(high | width);
            outer(*mapper, 0xA5U, 0U, control, 0U);
            for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
                std::array<Byte, 8> banks{};
                for (Byte index = 0U; index < 6U; ++index) {
                    banks[index] = static_cast<Byte>(index * 37U + 0x83U);
                    bank(*mapper, index, banks[index], mode);
                }
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
                    const auto selected = expected_chr_bank(addr, banks, mode, 0xA5U, control);
                    const auto physical = selected * kChrBankSize + 0x123U;
                    CAPTURE(high, width, mode, slot, selected, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                    mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                }
            }
        }
    }

    for (const std::size_t size : {kMinChrSize, 0x6000UZ}) {
        auto fitted = board(kMinPrgSize, size);
        outer(*fitted, 0xF3U, 0U, 0x8FU, 0U);
        bank(*fitted, 2U, 0xB4U);
        const auto selected = expected_chr_bank(0x1123U, {0U, 0U, 0xB4U}, 0U, 0xF3U, 0x8FU);
        const auto physical = (selected * kChrBankSize + 0x123U) % size;
        CAPTURE(size, selected, physical);
        REQUIRE(fitted->read_chr(0x1123U) == cell(physical, 0x35U));
    }
}

TEST_CASE("Mapper 410 bit 6 selects flat CHR RAM without changing PRG A21",
          "[mapper410][decoding][banking][ram][chr]") {
    auto mapper = board(kMaxPrgSize, 0x80000UZ);
    std::array<Byte, 8> banks{0x12U, 0x34U, 0x56U, 0x78U, 0x9AU, 0xBCU, 0U, 0U};
    for (Byte index = 0U; index < 6U; ++index) {
        bank(*mapper, index, banks[index], 0x80U);
    }
    bank(*mapper, 6U, 0U, 0x80U);
    outer(*mapper, 0U, 0U, 0U, 0U);
    REQUIRE(mapper->read_prg(0x8123U) == cell(0x123U));
    outer(*mapper, 0U, 0U, 0x40U, 0U);
    REQUIRE(mapper->read_prg(0x8123U) == cell(0x100UZ * kPrgBankSize + 0x123U));

    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto value = static_cast<Byte>(addr * 73U + (addr >> 8U));
        mapper->write_chr(addr, value);
        CAPTURE(addr, value);
        REQUIRE(mapper->read_chr(addr) == value);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == value);
    }
    for (const Byte control :
         {Byte{0x40U}, Byte{0x50U}, Byte{0x60U}, Byte{0x70U}, Byte{0xC0U}, Byte{0xFFU}}) {
        outer(*mapper, 0xA5U, 0U, control, 0U);
        for (Byte index = 0U; index < 6U; ++index) {
            bank(*mapper,
                 index,
                 static_cast<Byte>(banks[index] ^ 0xFFU),
                 (control & 1U) != 0U ? 0x80U : 0U);
        }
        for (const Address addr : {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1123U, 0x1FFFU}) {
            CAPTURE(control, addr);
            REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(addr * 73U + (addr >> 8U)));
        }
    }

    outer(*mapper, 0xA5U, 0U, 0x0FU, 0U);
    const auto rom_value = mapper->read_chr(0x1123U);
    mapper->write_chr(0x1123U, 0xFFU);
    REQUIRE(mapper->read_chr(0x1123U) == rom_value);
    outer(*mapper, 0xA5U, 0U, 0x4FU, 0U);
    REQUIRE(mapper->read_chr(0x1123U) == static_cast<Byte>(0x1123U * 73U + (0x1123U >> 8U)));
}

TEST_CASE("Mapper 410 decodes every MMC3 alias and controls H/V CIRAM",
          "[mapper410][decoding][ciram][irq][ram]") {
    auto mapper = board(0x20000UZ, 0x20000UZ, Wram::Persistent);
    const auto initial = register_state(*mapper, Wram::Persistent);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        auto wanted = initial;
        switch ((raw >> 13U) & 3U) {
        case 0U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xC6U);
                wanted[0U] = 6U;
                wanted[1U] = 0xC0U;
            } else {
                mapper->write_prg(addr, 0xFFU);
                wanted[2U] = 0xFFU;
            }
            break;
        case 1U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 1U);
                wanted[10U] = 1U;
            } else {
                mapper->write_prg(addr, 0xC0U);
                wanted[14U] = 0xC0U;
            }
            break;
        case 2U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xA5U);
                wanted[12U] = 0xA5U;
            } else {
                mapper->write_prg(addr, 0U);
                wanted[13U] = 1U;
            }
            break;
        default:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(0xE001U, 0U);
                mapper->write_prg(0xC000U, 0U);
                mapper->write_prg(0xC001U, 0U);
                mapper->clock_irq_counter();
                mapper->write_prg(addr, 0U);
            } else {
                mapper->write_prg(addr, 0U);
                wanted[13U] = 2U;
            }
            break;
        }
        CAPTURE(raw);
        REQUIRE(register_state(*mapper, Wram::Persistent) == wanted);
    }

    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_prg(0xA000U, static_cast<Byte>(raw));
        CAPTURE(raw);
        REQUIRE(mapper->mirror_mode() ==
                ((raw & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x2000U, 0x2ABCU, 0x3000U, 0x3EFFU}) {
            REQUIRE_FALSE(mapper->ciram_address(addr));
            if (addr >= 0x2000U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
            }
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 410 preserves DIP open-bus masks and explicit PRG chip-enable profiles",
          "[mapper410][decoding][bus]") {
    auto mapper = board();
    constexpr std::array<Address, 8> kPins = {
        0x10U, 0x20U, 0x40U, 0x80U, 0x100U, 0x200U, 0x400U, 0x800U};
    const auto before = register_state(*mapper);
    for (Byte dip = 0U; dip < 8U; ++dip) {
        REQUIRE(mapper->configure_board_jumpers(dip));
        for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const bool decoded = raw >= 0x5000U;
            CAPTURE(dip, raw, decoded);
            REQUIRE(mapper->maps_expansion(addr) == decoded);
            REQUIRE(mapper->expansion_read_mask(addr) == (decoded ? 1U : 0xFFU));
            REQUIRE(mapper->read_expansion(addr) ==
                    (decoded && (addr & kPins[dip]) != 0U ? 1U : 0U));
            mapper->write_expansion(addr, 0xFFU);
        }
    }
    REQUIRE(register_state(*mapper) == before);
    const auto dip_before = state(*mapper);
    REQUIRE_FALSE(mapper->configure_board_jumpers(8U));
    REQUIRE(state(*mapper) == dip_before);

    constexpr std::array<std::string_view, 5> kProfiles = {"menu-selection-0",
                                                           "menu-selection-1",
                                                           "menu-selection-2",
                                                           "menu-selection-3",
                                                           "menu-selection-4"};
    for (Byte selection = 0U; selection < 5U; ++selection) {
        auto profile = board();
        REQUIRE(profile->configure_board_profile(kProfiles[selection]));
        REQUIRE(profile->observes_ppu_address() == (selection == 2U || selection == 4U));
        outer(*profile,
              0U,
              selection == 1U ? 0x80U : (selection == 3U ? 0x40U : 0U),
              selection == 2U ? 0x40U : (selection == 4U ? 0x20U : 0U),
              0U);
        profile->observe_ppu_address(0U);
        REQUIRE(profile->prg_read_mask(0x8000U) == (selection == 0U ? 0xFFU : 0U));
        REQUIRE(profile->read_prg(0x8000U) == (selection == 0U ? cell(0U) : 0U));
        outer(*profile, 0U, 0U, 0U, 0U);
        REQUIRE(profile->prg_read_mask(0x8000U) == 0xFFU);
        REQUIRE(profile->read_prg(0x8000U) == cell(0U));
        for (const std::string_view invalid :
             {"", "menu-selection-", "menu-selection-5", "menu-selection-x", "menu-0"}) {
            const auto configured = state(*profile);
            REQUIRE_FALSE(profile->configure_board_profile(invalid));
            REQUIRE(state(*profile) == configured);
        }
    }
    auto large = board(0x100000UZ, 0x80000UZ);
    const auto large_before = state(*large);
    REQUIRE_FALSE(large->configure_board_profile("menu-selection-1"));
    REQUIRE(state(*large) == large_before);
    REQUIRE(large->configure_board_profile("menu-selection-0"));

    REQUIRE_FALSE(mapper->write_ppu_register(0x2012U, 0xA5U));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2012U, 0xA7U));
}

TEST_CASE("Mapper 410 retains MMC3 IRQ timing across outer writes and acknowledgement",
          "[mapper410][irq][audio]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xC000U, static_cast<Byte>(latch));
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        CAPTURE(latch);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        for (unsigned count = 0U; count < latch; ++count) {
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->reset();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    outer(*mapper, 1U, 2U, 3U, 4U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned repeat = 0U; repeat < 3U; ++repeat) {
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    load(*mapper, state(*board()));

    const auto before = register_state(*mapper);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_read(0x1234U);
    REQUIRE(register_state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 410 reset restores registers and cold power clears only volatile RAM",
          "[mapper410][reset][ram][irq]") {
    for (const auto wram : {Wram::Volatile, Wram::Persistent}) {
        auto mapper = board(0x20000UZ, 0x20000UZ, wram, MirrorMode::Horizontal);
        REQUIRE(mapper->configure_board_jumpers(6U));
        REQUIRE(mapper->configure_board_profile("menu-selection-1"));
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(0x6123U, 0xA7U);
        outer(*mapper, 1U, 2U, 0x40U, 3U);
        mapper->write_chr(0x1123U, 0x5CU);
        bank(*mapper, 6U, 9U, 0xC0U);
        mapper->write_prg(0xA000U, 1U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());

        mapper->reset();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_expansion(0x5400U) == 1U);
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        outer(*mapper, 0U, 0U, 0x40U, 0U);
        REQUIRE(mapper->read_chr(0x1123U) == 0x5CU);

        mapper->power_on();
        REQUIRE(mapper->read_prg(0x6123U) == (wram == Wram::Persistent ? 0xA7U : 0U));
        REQUIRE(mapper->read_expansion(0x5400U) == 1U);
        outer(*mapper, 0U, 0U, 0x40U, 0U);
        for (Address addr = 0U; addr < 0x2000U; addr = static_cast<Address>(addr + 0x101U)) {
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 410 bounded snapshots replay all RAM and registers without either ROM",
          "[mapper410][state][ram][irq]") {
    auto mapper = board(0x20000UZ, 0x20000UZ, Wram::Persistent);
    REQUIRE(mapper->configure_board_jumpers(5U));
    REQUIRE(mapper->configure_board_profile("menu-selection-2"));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA7U);
    outer(*mapper, 1U, 2U, 0x4FU, 3U);
    mapper->write_chr(0x1123U, 0x5CU);
    bank(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->observe_ppu_address(0x1000U);
    const auto saved = state(*mapper);
    constexpr std::size_t chr_offset = kGeometrySize + kRamSize;
    constexpr std::size_t registers = chr_offset + kRamSize;
    REQUIRE(saved.size() == kWramStateSize);
    REQUIRE(saved[kGeometrySize + 0x123U] == 0xA7U);
    REQUIRE(saved[chr_offset + 0x1123U] == 0x5CU);
    REQUIRE(saved[registers + 15U + 2U] == 0x4FU);
    REQUIRE((saved[registers + 13U] & 4U) != 0U);

    auto copy = board(0x20000UZ, 0x20000UZ, Wram::Persistent, MirrorMode::Vertical, 0x80U);
    REQUIRE(copy->configure_board_jumpers(5U));
    REQUIRE(copy->configure_board_profile("menu-selection-2"));
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->read_prg(0x6123U) == 0xA7U);
    REQUIRE(copy->read_chr(0x1123U) == 0x5CU);
    REQUIRE(copy->irq_pending());
    REQUIRE(copy->prg_read_mask(0x8000U) == mapper->prg_read_mask(0x8000U));

    outer(*copy, 1U, 2U, 0x0FU, 3U);
    outer(*mapper, 1U, 2U, 0x0FU, 3U);
    for (const auto addr : kPrgAddresses) {
        const auto selected = expected_prg_bank(addr, 9U, 0U, 0xC0U, 2U, 0x0FU, 3U);
        const auto physical = (selected * kPrgBankSize + (addr & (kPrgBankSize - 1U))) % 0x20000UZ;
        CAPTURE(addr, selected, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    const auto chr_selected =
        expected_chr_bank(0x1123U, {0U, 0U, 0U, 0U, 0U, 0U, 9U, 0U}, 0xC0U, 1U, 0x0FU);
    REQUIRE(copy->read_chr(0x1123U) == cell(chr_selected * kChrBankSize + 0x123U, 0xB5U));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 410 rejects damaged truncated and mismatched states atomically",
          "[mapper410][state][ram]") {
    auto mapper = board(0x20000UZ, 0x20000UZ, Wram::Persistent);
    REQUIRE(mapper->configure_board_jumpers(5U));
    REQUIRE(mapper->configure_board_profile("menu-selection-2"));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA7U);
    outer(*mapper, 1U, 2U, 0x4FU, 3U);
    mapper->write_chr(0x1123U, 0x5AU);
    bank(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
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
    for (std::size_t offset = 0U; offset < kGeometrySize; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    constexpr std::size_t registers = kRegisterOffset + kRamSize;
    constexpr std::array<std::pair<std::size_t, Byte>, 12> invalid_fields{{
        {0U, 8U},
        {1U, 1U},
        {8U, 64U},
        {9U, 64U},
        {10U, 2U},
        {13U, 8U},
        {13U, 4U},
        {14U, 1U},
        {14U, 0x3FU},
        {18U, 0x80U},
        {19U, 4U},
        {21U, 0xFFU},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[registers + offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = board(0x10000UZ, 0x20000UZ, Wram::Persistent);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(0x20000UZ, 0x10000UZ, Wram::Persistent);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_mirror = board(0x20000UZ, 0x20000UZ, Wram::Persistent, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto wrong_wram = board();
    const auto wrong_wram_before = state(*wrong_wram);
    StateReader wrong_wram_reader(saved);
    wrong_wram->load_state(wrong_wram_reader);
    REQUIRE_FALSE(wrong_wram_reader.ok());
    REQUIRE(state(*wrong_wram) == wrong_wram_before);

    auto wrong_config = board(0x20000UZ, 0x20000UZ, Wram::Persistent);
    const auto wrong_config_before = state(*wrong_config);
    StateReader wrong_config_reader(saved);
    wrong_config->load_state(wrong_config_reader);
    REQUIRE_FALSE(wrong_config_reader.ok());
    REQUIRE(state(*wrong_config) == wrong_config_before);

    auto changed_ram = saved;
    changed_ram[kGeometrySize + kRamSize + 0x123U] ^= 0xFFU;
    load(*mapper, changed_ram);
    REQUIRE(state(*mapper) == changed_ram);
}

} // namespace
