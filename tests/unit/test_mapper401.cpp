#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper401.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kWramSize = 0x2000UZ;
constexpr std::size_t kStateSize = 30UZ;
constexpr std::size_t kWramStateSize = kStateSize + kWramSize;

enum class Wram { None, Volatile, Persistent };

INesHeader header(Wram wram = Wram::None,
                  bool legacy = false,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 401U;
    result.is_nes2 = !legacy;
    result.has_battery = wram == Wram::Persistent;
    result.prg_ram_size = legacy ? kWramSize : (wram == Wram::Volatile ? kWramSize : 0U);
    result.prg_nvram_size = !legacy && wram == Wram::Persistent ? kWramSize : 0U;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 20U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper401> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 Wram wram = Wram::None,
                                 bool legacy = false,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(wram, legacy, mirror);
    REQUIRE(Mapper401::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper401>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper401& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void outer(Mapper401& mapper, Byte r0, Byte r1, Byte r2, Byte r3) {
    mapper.write_prg(0x6001U, 0U);
    for (const Byte value : {r0, r1, r2, r3}) {
        mapper.write_prg(0x6000U, value);
    }
}

void bank(Mapper401& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned expected_prg_bank(
    Address addr, Byte pads, Byte r1, Byte r2, Byte r3, Byte r6, Byte r7, Byte mode) {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    const unsigned inner = slot == 1U                               ? r7 & 0x3FU
                           : slot == 3U                             ? 63U
                           : (slot == 0U) == ((mode & 0x40U) != 0U) ? 62U
                                                                    : r6 & 0x3FU;
    unsigned result = (inner & (0x3FU ^ r3)) | (r1 & 0x1FU);
    result |= (pads & 1U) == 0U ? r2 & 0x20U : (r1 & 0x40U) >> 1U;
    result |= (pads & 2U) == 0U ? r2 & 0x40U : (r1 & 0x20U) << 1U;
    return result;
}

std::size_t expected_prg(
    Address addr, Byte pads, Byte r1, Byte r2, Byte r3, Byte r6, Byte r7, Byte mode = 0U) {
    return expected_prg_bank(addr, pads, r1, r2, r3, r6, r7, mode) * kPrgBankSize +
           (addr & 0x1FFFU);
}

TEST_CASE("Mapper 401 validates KC885 ROM and optional WRAM geometry", "[mapper401][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, kChrSize, Wram::None, false, mirror);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    for (const auto [prg, chr] :
         std::array<std::pair<std::size_t, std::size_t>, 5>{std::pair{0x8000UZ, 0x2000UZ},
                                                            std::pair{0x18000UZ, 0x6000UZ},
                                                            std::pair{0x40000UZ, 0x10000UZ},
                                                            std::pair{kPrgSize, kChrSize},
                                                            std::pair{kPrgSize, 0x400000UZ}}) {
        CAPTURE(prg, chr);
        REQUIRE(Mapper401::validate_geometry(header(), prg, chr));
    }
    for (const auto wram : {Wram::None, Wram::Volatile, Wram::Persistent}) {
        for (const bool legacy : {false, true}) {
            auto mapper = board(0x8000U, 0x2000U, wram, legacy);
            const bool populated = wram != Wram::None && (wram == Wram::Persistent || !legacy);
            CAPTURE(static_cast<int>(wram), legacy, populated);
            REQUIRE(mapper->maps_prg(0x6000U) == populated);
            REQUIRE(mapper->battery_ram().size() == (wram == Wram::Persistent ? kWramSize : 0U));
            REQUIRE(state(*mapper).size() == (populated ? kWramStateSize : kStateSize));
        }
    }

    for (const unsigned id : {0U, 4U, 45U, 400U, 402U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper401::validate_geometry(h, 0x8000U, 0x2000U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper401::validate_geometry(h, 0x8000U, 0x2000U));
    }
    auto legacy_submapper = header(Wram::None, true);
    legacy_submapper.submapper = 15U;
    REQUIRE(Mapper401::validate_geometry(legacy_submapper, 0x8000U, 0x2000U));
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, kPrgSize + 1U, kPrgSize + 0x2000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper401::validate_geometry(header(), size, 0x2000U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x400001UZ, 0x401000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper401::validate_geometry(header(), 0x8000U, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(
            Mapper401::validate_geometry(header(Wram::None, false, mirror), 0x8000U, 0x2000U));
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.chr_ram_size = 0x2000U;
            break;
        case 1U:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 0x1000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x1000U;
            h.has_battery = true;
            break;
        case 4U:
            h.prg_ram_size = h.prg_nvram_size = kWramSize;
            h.has_battery = true;
            break;
        case 5U:
            h.has_battery = true;
            break;
        case 6U:
            h.prg_nvram_size = kWramSize;
            break;
        case 7U:
            h.prg_ram_size = 0x4000U;
            break;
        default:
            h.prg_nvram_size = 0x4000U;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper401::validate_geometry(h, 0x8000U, 0x2000U));
    }
}

TEST_CASE("Mapper 401 decodes every outer and MMC3 register alias", "[mapper401][decoding]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
        } else if (raw >= 0x6000U) {
            expected->write_prg(static_cast<Address>(0x6000U | (raw & 1U)), value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
    }
}

TEST_CASE("Mapper 401 applies all PRG pin sources masks and chip enables",
          "[mapper401][decoding][banking][bus]") {
    auto mapper = board();
    constexpr std::array<Address, 4> addresses{0x8123U, 0xA123U, 0xC123U, 0xE123U};
    for (Byte pads = 0U; pads < 8U; ++pads) {
        REQUIRE(mapper->configure_board_jumpers(pads));
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto r1 = static_cast<Byte>(raw);
            const auto r2 = static_cast<Byte>(raw ^ 0xFFU);
            const auto r3 = static_cast<Byte>(raw & 0x3FU);
            const auto r6 = static_cast<Byte>(raw * 37U);
            const auto r7 = static_cast<Byte>(raw ^ 0xA5U);
            outer(*mapper, 0U, r1, r2, r3);
            const bool disabled = (pads & 4U) != 0U && (r1 & 0x80U) != 0U;
            for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
                bank(*mapper, 6U, r6, mode);
                bank(*mapper, 7U, r7, mode);
                for (const auto addr : addresses) {
                    const auto physical = expected_prg(addr, pads, r1, r2, r3, r6, r7, mode);
                    CAPTURE(pads, raw, mode, addr, physical, disabled);
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == (disabled ? 0U : 0xFFU));
                    REQUIRE(mapper->read_prg(addr) == (disabled ? 0U : cell(physical)));
                }
            }
        }
    }
    const auto before = state(*mapper);
    for (const Byte invalid : {8U, 9U, 0x80U, 0xFFU}) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(invalid));
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 401 retains mapper 45 CHR AND OR pairing and inversion", "[mapper401][banking]") {
    auto mapper = board();
    for (unsigned width = 0U; width < 16U; ++width) {
        outer(*mapper, 0xA5U, 0U, static_cast<Byte>(0xD0U | width), 0U);
        const unsigned and_mask = width < 8U ? 0U : (1U << (width - 7U)) - 1U;
        constexpr unsigned kOr = 0xA5U | 0xD00U;
        for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
            std::array<Byte, 6> values{};
            for (Byte index = 0U; index < 6U; ++index) {
                values[index] = static_cast<Byte>(index * 37U + 0x83U);
                bank(*mapper, index, values[index], mode);
            }
            const std::array<unsigned, 8> inner{values[0] & 0xFEU,
                                                values[0] | 1U,
                                                values[1] & 0xFEU,
                                                values[1] | 1U,
                                                values[2],
                                                values[3],
                                                values[4],
                                                values[5]};
            for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
                const auto slot = ((addr / kChrBankSize) ^ (mode == 0U ? 0U : 4U));
                const auto physical =
                    (((inner[slot] & and_mask) | kOr) % (kChrSize / kChrBankSize)) * kChrBankSize +
                    (addr & (kChrBankSize - 1U));
                CAPTURE(width, mode, addr, physical);
                mapper->write_chr(static_cast<Address>(addr), 0U);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == cell(physical, 0x35U));
            }
        }
    }
}

TEST_CASE("Mapper 401 wraps fitted ROMs only after pin decoding", "[mapper401][banking]") {
    for (const std::size_t prg_size : {0x8000UZ, 0x18000UZ}) {
        for (const std::size_t chr_size : {0x2000UZ, 0x6000UZ}) {
            auto mapper = board(prg_size, chr_size);
            REQUIRE(mapper->configure_board_jumpers(3U));
            outer(*mapper, 0xF3U, 0xD7U, 0xFFU, 0x08U);
            bank(*mapper, 6U, 0x2AU);
            bank(*mapper, 2U, 0xB4U);
            const auto prg_bank =
                expected_prg_bank(0x8123U, 3U, 0xD7U, 0xFFU, 0x08U, 0x2AU, 0U, 0U);
            const auto fixed_bank =
                expected_prg_bank(0xF123U, 3U, 0xD7U, 0xFFU, 0x08U, 0x2AU, 0U, 0U);
            const auto chr_bank = (0xB4U | 0xF3U | 0xF00U) % (chr_size / kChrBankSize);
            CAPTURE(prg_size, chr_size, prg_bank, fixed_bank, chr_bank);
            REQUIRE(mapper->read_prg(0x8123U) ==
                    cell((prg_bank % (prg_size / kPrgBankSize)) * kPrgBankSize + 0x123U));
            REQUIRE(mapper->read_prg(0xF123U) ==
                    cell((fixed_bank % (prg_size / kPrgBankSize)) * kPrgBankSize + 0x1123U));
            REQUIRE(mapper->read_chr(0x1123U) == cell(chr_bank * kChrBankSize + 0x123U, 0x35U));
        }
    }
}

TEST_CASE("Mapper 401 overlays WRAM on outer writes and honors RAM gates and reset",
          "[mapper401][ram][reset]") {
    for (const auto wram : {Wram::Volatile, Wram::Persistent}) {
        auto mapper = board(kPrgSize, kChrSize, wram, false, MirrorMode::Horizontal);
        REQUIRE(mapper->configure_board_jumpers(6U));
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(0x6123U, 0xA7U);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
        REQUIRE(mapper->maps_prg(0x6123U));
        REQUIRE(mapper->prg_read_mask(0x6123U) == 0xFFU);
        mapper->write_prg(0xA001U, 0U);
        mapper->write_prg(0x6123U, 0x5AU);
        REQUIRE(mapper->read_prg(0x6123U) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(0x6123U));
        REQUIRE(mapper->prg_read_mask(0x6123U) == 0U);
        mapper->write_prg(0xA001U, 0x80U);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
        mapper->write_prg(0xA001U, 0xC0U);
        mapper->write_prg(0x6123U, 0x5AU);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
        mapper->write_prg(0xA001U, 0x80U);
        outer(*mapper, 1U, 2U, 3U, 4U);
        bank(*mapper, 6U, 9U, 0xC0U);
        mapper->write_prg(0xA000U, 1U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        REQUIRE(mapper->battery_ram().size() == (wram == Wram::Persistent ? kWramSize : 0U));

        mapper->reset();
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_chr(0U) == cell(0U, 0x35U));
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
        mapper->power_on();
        REQUIRE(mapper->read_prg(0x6123U) == (wram == Wram::Persistent ? 0xA7U : 0U));
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    }

    auto persistent = board(kPrgSize, kChrSize, Wram::Persistent);
    const std::array<Byte, 4> imported{1U, 2U, 3U, 4U};
    persistent->set_battery_ram(imported);
    persistent->write_prg(0xA001U, 0x80U);
    REQUIRE(persistent->read_prg(0x6000U) == 1U);
    REQUIRE(persistent->read_prg(0x6003U) == 4U);
    persistent->set_battery_ram(std::array<Byte, 2>{9U, 8U});
    REQUIRE(persistent->read_prg(0x6000U) == 9U);
    REQUIRE(persistent->read_prg(0x6001U) == 8U);
    REQUIRE(persistent->read_prg(0x6002U) == 3U);
    auto volatile_ram = board(kPrgSize, kChrSize, Wram::Volatile);
    volatile_ram->set_battery_ram(imported);
    REQUIRE(volatile_ram->read_prg(0x6000U) == 0U);
}

TEST_CASE("Mapper 401 preserves mapper 45 IRQ progress acknowledgement and zero repeats",
          "[mapper401][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto partial = state(*mapper);
    outer(*mapper, 1U, 2U, 3U, 4U);
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
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 401 exposes console CIRAM and no expansion device or audio",
          "[mapper401][bus][ciram][audio]") {
    auto mapper = board(kPrgSize, kChrSize, Wram::None, false, MirrorMode::Horizontal);
    for (const Address addr : {0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xFFU);
    }
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xBFFEU, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 401 state replays bounded mutable data without ROM", "[mapper401][state]") {
    auto mapper = board(kPrgSize, kChrSize, Wram::Persistent);
    REQUIRE(mapper->configure_board_jumpers(5U));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA7U);
    outer(*mapper, 1U, 2U, 0x4FU, 3U);
    bank(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kWramStateSize);
    REQUIRE(saved.size() < kPrgSize);

    auto copy = board(kPrgSize, kChrSize, Wram::Persistent, false, MirrorMode::Vertical, 0x80U);
    REQUIRE(copy->configure_board_jumpers(5U));
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6123U) == 0xA7U);
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    REQUIRE(copy->read_chr(0x123U) == static_cast<Byte>(mapper->read_chr(0x123U) + 0x80U));
    mapper->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 401 rejects malformed and incompatible states atomically", "[mapper401][state]") {
    auto mapper = board(kPrgSize, kChrSize, Wram::Persistent);
    REQUIRE(mapper->configure_board_jumpers(7U));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA7U);
    outer(*mapper, 1U, 2U, 0x4FU, 3U);
    bank(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
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
    for (std::size_t offset = 0U; offset < 10U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    constexpr std::size_t kBase = kWramSize + 10U;
    for (const auto [offset, value] :
         std::array<std::pair<std::size_t, Byte>, 10>{std::pair{kBase, Byte{8U}},
                                                      std::pair{kBase + 1U, Byte{1U}},
                                                      std::pair{kBase + 8U, Byte{64U}},
                                                      std::pair{kBase + 10U, Byte{2U}},
                                                      std::pair{kBase + 13U, Byte{8U}},
                                                      std::pair{kBase + 13U, Byte{4U}},
                                                      std::pair{kBase + 14U, Byte{0xFFU}},
                                                      std::pair{kBase + 18U, Byte{0x80U}},
                                                      std::pair{kBase + 19U, Byte{4U}},
                                                      std::pair{kBase + 19U, Byte{0xFFU}}}) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_pads = board();
    REQUIRE(wrong_pads->configure_board_jumpers(6U));
    const auto wrong_pads_before = state(*wrong_pads);
    StateReader pads_reader(saved);
    wrong_pads->load_state(pads_reader);
    REQUIRE_FALSE(pads_reader.ok());
    REQUIRE(state(*wrong_pads) == wrong_pads_before);

    auto wrong_mirror = board(kPrgSize, kChrSize, Wram::None, false, MirrorMode::Horizontal);
    REQUIRE(wrong_mirror->configure_board_jumpers(7U));
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader mirror_reader(saved);
    wrong_mirror->load_state(mirror_reader);
    REQUIRE_FALSE(mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto wrong_geometry = board(kPrgSize / 2U, kChrSize);
    REQUIRE(wrong_geometry->configure_board_jumpers(7U));
    const auto wrong_geometry_before = state(*wrong_geometry);
    StateReader geometry_reader(saved);
    wrong_geometry->load_state(geometry_reader);
    REQUIRE_FALSE(geometry_reader.ok());
    REQUIRE(state(*wrong_geometry) == wrong_geometry_before);
}

} // namespace
