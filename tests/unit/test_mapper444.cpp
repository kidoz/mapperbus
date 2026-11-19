#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper444.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::size_t kMemoryOffset = 10UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kRamOffset = 28UZ;
constexpr std::size_t kStateSize = kRamOffset + kWramSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

enum class RamKind { Absent, Volatile, Battery };

INesHeader header(Byte submapper = 0U,
                  MirrorMode mirror = MirrorMode::Vertical,
                  RamKind ram = RamKind::Volatile) {
    INesHeader result{};
    result.mapper_number = 444U;
    result.submapper = submapper;
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
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper444> board(Byte submapper = 0U,
                                 std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 RamKind ram = RamKind::Volatile,
                                 Byte salt = 0U) {
    CAPTURE(submapper, prg_size, chr_size, mirror, ram, salt);
    const auto h = header(submapper, mirror, ram);
    REQUIRE(Mapper444::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper444>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper444& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper444& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper444& mapper, Byte value, Address addr = 0x6000U) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(addr, value);
}

std::size_t expected_prg(Address addr, Byte outer, Byte mode, Byte r6, Byte r7) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    unsigned inner =
        slot == 1U ? r7 & 0x0FU
        : (((mode & 0x40U) == 0U && slot == 0U) || ((mode & 0x40U) != 0U && slot == 2U))
            ? r6 & 0x0FU
        : slot == 3U ? 15U
                     : 14U;
    if ((outer & 0x0CU) == 0x04U) {
        inner = (r6 & 0x0CU) | ((addr >> 13U) & 3U);
    } else if ((outer & 0x0CU) == 0x0CU) {
        inner = (r6 & 0x0EU) | ((addr >> 13U) & 1U);
    }
    const unsigned physical = inner | ((outer & 3U) << 4U);
    return physical * kPrgBankSize + (addr & 0x1FFFU);
}

std::size_t expected_chr(
    Address addr, Byte outer, Byte mode, Byte submapper, const std::array<Byte, 6>& values) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner =
        slot < 4U ? (values[slot / 2U] & 0xFEU) | (slot & 1U) : values[slot - 2U];
    const unsigned a17 = (submapper & 1U) == 0U ? outer & 1U : (inner >> 7U) & 1U;
    const unsigned a18 = (submapper & 2U) == 0U ? (outer >> 1U) & 1U : (outer >> 4U) & 1U;
    const unsigned bank = (inner & 0x7FU) | (a17 << 7U) | (a18 << 8U);
    return bank * kChrBankSize + (addr & 0x3FFU);
}

TEST_CASE("Mapper 444 validates ROM bounds submappers memory and CIRAM metadata",
          "[mapper444][geometry][ram]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (const auto ram : {RamKind::Absent, RamKind::Volatile, RamKind::Battery}) {
                CAPTURE(submapper, mirror, ram);
                auto mapper = board(submapper, kMaxPrgSize, kMaxChrSize, mirror, ram);
                auto different_rom = board(submapper, kMaxPrgSize, kMaxChrSize, mirror, ram, 0x80U);
                REQUIRE(state(*mapper) == state(*different_rom));
                REQUIRE(state(*mapper).size() ==
                        (ram == RamKind::Absent ? kRamOffset : kStateSize));
                REQUIRE(mapper->mirror_mode() == mirror);
                REQUIRE(mapper->battery_ram().size() == (ram == RamKind::Battery ? kWramSize : 0U));
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
            }
        }
    }
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
            CAPTURE(submapper, size);
            REQUIRE(Mapper444::validate_geometry(header(submapper), size, kMaxChrSize));
        }
        for (std::size_t size = kMinChrSize; size <= kMaxChrSize; size += kChrBankSize) {
            CAPTURE(submapper, size);
            REQUIRE(Mapper444::validate_geometry(header(submapper), kMaxPrgSize, size));
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
        REQUIRE_FALSE(Mapper444::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 441U, 443U, 445U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, kMinChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, kMinChrSize);
    for (Byte submapper = 4U; submapper < 16U; ++submapper) {
        reject(header(submapper), kMinPrgSize, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x80001UZ, 0x100000UZ}) {
        reject(header(), size, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2200UZ, 0x40001UZ, 0x80000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(0U, mirror), kMinPrgSize, kMinChrSize);
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

TEST_CASE("Mapper 444 decodes every outer alias and all data bits",
          "[mapper444][decoding][bus][banking]") {
    auto mapper = board();
    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        mapper->write_prg(static_cast<Address>(raw), value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x1FU));
        REQUIRE(mapper->read_prg(0x8123U) ==
                cell(expected_prg(0x8123U, value & 0x1FU, 0U, 0U, 0U)));
        REQUIRE(mapper->read_chr(0x0155U) ==
                cell(expected_chr(0x0155U, value & 0x1FU, 0U, 0U, {}) % kMaxChrSize, 0x55U));
        if ((value & 0x0CU) == 0x08U) {
            REQUIRE(mapper->read_prg(static_cast<Address>(raw)) == 0U);
            REQUIRE(mapper->prg_read_mask(static_cast<Address>(raw)) == 0x03U);
        } else {
            REQUIRE(mapper->read_prg(static_cast<Address>(raw)) == value);
            REQUIRE(mapper->prg_read_mask(static_cast<Address>(raw)) == 0xFFU);
        }
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0x7123U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x1FU));
        if ((value & 0x0CU) == 0x08U) {
            REQUIRE(mapper->read_prg(0x7123U) == 0U);
        } else {
            REQUIRE(mapper->read_prg(0x7123U) == value);
        }
    }
}

TEST_CASE("Mapper 444 outer writes require enable while WRAM writes also honor protection",
          "[mapper444][decoding][bus][ram]") {
    auto mapper = board();
    outer(*mapper, 0x15U);
    mapper->write_prg(0x6123U, 0x5AU);
    outer(*mapper, 0x15U);
    mapper->write_prg(0xA001U, 0U);
    mapper->write_prg(0x6123U, 0x1EU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x15U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0U);

    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6123U, 0x1AU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x1AU);
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0x03U);
    outer(*mapper, 0x01U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);

    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x07U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x07U);
    outer(*mapper, 0x00U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x07U);

    auto absent = board(0U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    outer(*absent, 0x08U);
    REQUIRE(absent->maps_prg(0x6000U));
    REQUIRE(absent->prg_read_mask(0x7FFFU) == 0x03U);
    REQUIRE(absent->read_prg(0x6000U) == 0U);
}

TEST_CASE("Mapper 444 decodes every MMC3 alias without unrelated side effects",
          "[mapper444][decoding][bus]") {
    auto mapper = board(0U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    mapper->reset();
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
                expected[13U] = value & 7U;
                expected[14U] = value & 0xC0U;
            } else {
                expected[15U] = value;
            }
        } else if (raw < 0xC000U) {
            if (even) {
                expected[23U] = (value & 1U) ^ 1U;
            } else {
                expected[27U] = value & 0xC0U;
            }
        } else if (raw < 0xE000U) {
            if (even) {
                expected[25U] = value;
            } else {
                expected[26U] = 1U;
            }
        } else if (!even) {
            expected[26U] = 2U;
        }
        CAPTURE(raw, value, even);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 444 combines outer PRG pins with MMC3 NROM-256 and NROM-128 wiring",
          "[mapper444][decoding][banking][prg]") {
    auto mapper = board();
    constexpr Byte kR6 = 0x0BU;
    constexpr Byte kR7 = 0x07U;
    for (const Byte mode : {Byte{0U}, Byte{0x40U}, Byte{0xC0U}}) {
        bank(*mapper, 6U, 0xFBU, mode);
        bank(*mapper, 7U, 0xF7U, mode);
        for (unsigned value = 0U; value < 32U; ++value) {
            const auto control = static_cast<Byte>(value);
            outer(*mapper, control);
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, control, mode, kR6, kR7);
                CAPTURE(mode, control, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            }
        }
    }

    for (const Byte control :
         {Byte{0x00U}, Byte{0x04U}, Byte{0x08U}, Byte{0x0CU}, Byte{0x15U}, Byte{0x1BU}}) {
        outer(*mapper, control);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const auto physical = expected_prg(addr, control, 0xC0U, kR6, kR7);
            CAPTURE(control, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, 0x018000UZ, 0x048000UZ}) {
        auto fitted = board(0U, size, kMaxChrSize);
        bank(*fitted, 6U, 0xFFU, 0x40U);
        bank(*fitted, 7U, 0xFFU, 0x40U);
        for (const Byte control : {Byte{0x1FU}, Byte{0x1BU}, Byte{0x17U}, Byte{0x13U}}) {
            outer(*fitted, control);
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, control, 0x40U, 0x0FU, 0x0FU) % size;
                CAPTURE(size, control, addr, physical);
                REQUIRE(fitted->read_prg(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 444 routes CHR A17 A18 for every submapper and MMC3 inversion",
          "[mapper444][decoding][banking][chr][ram]") {
    constexpr std::array<Byte, 6> values = {0xA2U, 0x8BU, 0x93U, 0xFCU, 0xB1U, 0xC7U};
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto mapper = board(submapper);
        for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
            for (Byte reg = 0U; reg < 6U; ++reg) {
                bank(*mapper, reg, values[reg], mode);
            }
            for (unsigned value = 0U; value < 32U; ++value) {
                const auto control = static_cast<Byte>(value);
                outer(*mapper, control);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                    const auto physical =
                        expected_chr(addr, control, mode, submapper, values) % kMaxChrSize;
                    CAPTURE(submapper, mode, control, addr, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                    mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                }
            }
        }
    }

    for (const std::size_t size : {kMinChrSize, 0x003000UZ, 0x018000UZ, 0x028000UZ}) {
        auto fitted = board(3U, kMaxPrgSize, size);
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*fitted, reg, values[reg]);
        }
        outer(*fitted, 0x1FU);
        for (const Address addr : {0x0000U, 0x03FFU, 0x0800U, 0x1000U, 0x1555U, 0x1FFFU, 0x3FFFU}) {
            const auto physical = expected_chr(addr, 0x1FU, 0U, 3U, values) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_chr(addr) == cell(physical, 0x55U));
        }
    }
}

TEST_CASE("Mapper 444 uses MMC3 horizontal vertical console CIRAM only", "[mapper444][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(0U, kMaxPrgSize, kMaxChrSize, initial);
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

TEST_CASE("Mapper 444 solder pads drive only D0 D1 and retain ordinary MMC3 banking",
          "[mapper444][decoding][bus][banking][reset]") {
    auto mapper = board(3U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    for (Byte pads = 0U; pads < 4U; ++pads) {
        bank(*mapper, 6U, 0x0DU, 0xC0U);
        bank(*mapper, 7U, 0x06U, 0xC0U);
        outer(*mapper, 0x1AU);
        REQUIRE(mapper->configure_board_jumpers(pads));
        for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x0101U) {
            CAPTURE(pads, addr);
            REQUIRE(mapper->read_prg(addr) == pads);
            REQUIRE(mapper->prg_read_mask(addr) == 0x03U);
            REQUIRE(mapper->maps_prg(addr));
        }
        for (const auto addr : kPrgAddresses) {
            REQUIRE(mapper->read_prg(addr) == cell(expected_prg(addr, 0x1AU, 0xC0U, 0x0DU, 0x06U)));
        }
        mapper->reset();
        REQUIRE(state(*mapper)[kMemoryOffset] == (0x19U | (pads << 5U)));
        outer(*mapper, 0x08U);
        REQUIRE(mapper->read_prg(0x6000U) == pads);
        mapper->power_on();
        outer(*mapper, 0x08U);
        REQUIRE(mapper->read_prg(0x7FFFU) == pads);
    }
    REQUIRE_FALSE(mapper->configure_board_jumpers(4U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0xFFU));
    outer(*mapper, 0x08U);
    REQUIRE(mapper->read_prg(0x6000U) == 3U);
    outer(*mapper, 0x00U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
}

TEST_CASE("Mapper 444 exposes optional WRAM but no expansion or audio device",
          "[mapper444][bus][ram][irq][audio]") {
    auto absent = board(0U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->prg_read_mask(0x6000U) == 0U);
    REQUIRE(absent->battery_ram().empty());
    outer(*absent, 0x1BU);
    REQUIRE(absent->read_prg(0xE000U) == cell(expected_prg(0xE000U, 0x1BU, 0U, 0U, 0U)));

    auto mapper = board();
    for (Address addr = 0U; addr < 0x6000U; ++addr) {
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
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    mapper->write_prg_cpu(0x8000U, 0x80U, true);
    mapper->write_prg_cpu(0x8001U, 0xFEU, false);
    REQUIRE(state(*mapper)[15U] == 0xFEU);
}

TEST_CASE("Mapper 444 MMC3 IRQ latch reload acknowledge disable and zero repeats",
          "[mapper444][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    outer(*mapper, 0x1BU);
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

TEST_CASE("Mapper 444 reset restores registers and cold power clears only volatile WRAM",
          "[mapper444][reset][ram]") {
    auto mapper = board(2U, kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    REQUIRE(mapper->configure_board_jumpers(2U));
    const auto initial = state(*mapper);
    outer(*mapper, 0x1BU);
    bank(*mapper, 6U, 0x09U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0x6123U, 0x07U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0x07U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    mapper->write_prg(0x6123U, 0x05U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);

    auto persistent = board(2U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Battery);
    std::array<Byte, kWramSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0xA5U);
    }
    persistent->set_battery_ram(bytes);
    REQUIRE(persistent->battery_ram().size() == kWramSize);
    persistent->write_prg(0x6000U, 0x1BU);
    persistent->power_on();
    REQUIRE(persistent->read_prg(0x6123U) == bytes[0x123U]);
    REQUIRE(state(*persistent)[kOuterOffset] == 0U);
    persistent->set_battery_ram(std::span(bytes).first(0x123U));
    REQUIRE(persistent->read_prg(0x6122U) == bytes[0x122U]);
    persistent->set_battery_ram(std::array<Byte, 4>{9U, 8U, 7U, 6U});
    REQUIRE(persistent->read_prg(0x6003U) == 6U);
    REQUIRE(persistent->read_prg(0x6004U) == bytes[4U]);
}

TEST_CASE("Mapper 444 bounded snapshots replay without ROM and preserve board configuration",
          "[mapper444][state][banking][ram][irq]") {
    auto mapper = board(3U);
    REQUIRE(mapper->configure_board_jumpers(3U));
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        mapper->write_prg(addr, static_cast<Byte>(cell(addr) & 0x7FU));
    }
    outer(*mapper, 0x1BU);
    bank(*mapper, 6U, 0x09U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    bank(*mapper, 0U, 0xA2U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8220U);
    REQUIRE(saved[kOuterOffset] == 0x1BU);

    auto copy = board(3U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Volatile, 0x80U);
    REQUIRE(copy->configure_board_jumpers(3U));
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0x1BU, 0xC0U, 0x09U, 0x07U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    REQUIRE(copy->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0x1BU, 0xC0U, 3U, {0xA2U, 0U, 0U, 0U, 0U, 0U}) % kMaxChrSize,
                 0xD5U));
    REQUIRE(copy->read_prg(0x6000U) == 3U);
    REQUIRE(copy->prg_read_mask(0x6000U) == 0x03U);
    copy->write_prg(0xA001U, 0x80U);
    copy->write_prg(0x6000U, 0x01U);
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        const auto expected = addr == 0x6000U ? Byte{0x01U} : static_cast<Byte>(cell(addr) & 0x7FU);
        REQUIRE(copy->read_prg(addr) == expected);
    }
    load(*copy, saved);
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

    auto absent = board(0U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    REQUIRE(state(*absent).size() == kRamOffset);
    REQUIRE(
        state(*absent) ==
        state(*board(0U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent, 0x35U)));
    REQUIRE(state(*absent) !=
            state(*board(0U, kMinPrgSize, kMinChrSize, MirrorMode::Vertical, RamKind::Absent)));
}

TEST_CASE("Mapper 444 rejects truncated damaged and mismatched states atomically",
          "[mapper444][state][ram]") {
    auto mapper = board(3U);
    REQUIRE(mapper->configure_board_jumpers(1U));
    outer(*mapper, 0x1BU);
    bank(*mapper, 6U, 0x09U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    mapper->write_prg(0xA001U, 0x80U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 15> invalid_fields = {{{0U, 0U},
                                                                              {2U, 1U},
                                                                              {6U, 1U},
                                                                              {10U, 0U},
                                                                              {12U, 0x20U},
                                                                              {13U, 8U},
                                                                              {14U, 1U},
                                                                              {21U, 0x10U},
                                                                              {22U, 0x10U},
                                                                              {23U, 2U},
                                                                              {26U, 8U},
                                                                              {26U, 4U},
                                                                              {27U, 1U},
                                                                              {27U, 0x3FU},
                                                                              {11U, 0x40U}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        invalid[kRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(offset, value);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper444>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg = board(3U, kMinPrgSize, kMaxChrSize);
    mismatch(wrong_prg);
    auto wrong_chr = board(3U, kMaxPrgSize, kMinChrSize);
    mismatch(wrong_chr);
    auto no_ram = board(3U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    mismatch(no_ram);
    auto battery = board(3U, kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Battery);
    mismatch(battery);
    auto wrong_mirror = board(3U, kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    mismatch(wrong_mirror);
    auto wrong_pads = board(3U);
    REQUIRE(wrong_pads->configure_board_jumpers(2U));
    mismatch(wrong_pads);
    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        auto wrong_submapper = board(submapper);
        REQUIRE(wrong_submapper->configure_board_jumpers(1U));
        mismatch(wrong_submapper);
    }

    auto changed = saved;
    changed[kOuterOffset] = 0x17U;
    changed[15U] = 0x5AU;
    changed[21U] = 0x0EU;
    changed[kRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0x17U, 0xC0U, 3U, {0x5AU, 0U, 0U, 0U, 0U, 0U}) % kMaxChrSize,
                 0x55U));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6000U, 0x01U);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x05U ^ 0xFFU));
}

} // namespace
