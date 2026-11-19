#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper441.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kRamOffset = 28UZ;
constexpr std::size_t kStateSize = kRamOffset + kWramSize;

enum class RamKind { Absent, Volatile, Battery };

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, RamKind ram = RamKind::Volatile) {
    INesHeader result{};
    result.mapper_number = 441U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = ram == RamKind::Battery;
    result.prg_ram_size = ram == RamKind::Volatile ? kWramSize : 0U;
    result.prg_nvram_size = ram == RamKind::Battery ? kWramSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 14U) * 37U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper441> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 RamKind ram = RamKind::Volatile,
                                 Byte salt = 0U) {
    const auto h = header(mirror, ram);
    REQUIRE(Mapper441::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper441>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper441& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper441& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper441& mapper, Byte value, Address addr = 0x6000U) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(addr, value);
}

unsigned expected_inner_prg(Address addr, Byte outer, Byte mode, Byte r6, Byte r7) {
    unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if ((outer & 0x08U) != 0U) {
        slot &= 1U;
    }
    if (slot == 1U) {
        return r7 & 0x1FU;
    }
    if (((mode & 0x40U) == 0U && slot == 0U) || ((mode & 0x40U) != 0U && slot == 2U)) {
        return r6 & 0x1FU;
    }
    return slot == 3U ? 31U : 30U;
}

std::size_t expected_prg(Address addr, Byte outer, Byte mode, Byte r6, Byte r7) {
    unsigned inner = expected_inner_prg(addr, outer, mode, r6, r7);
    if ((outer & 0x08U) != 0U) {
        inner = (inner & ~3U) | ((addr >> 13U) & 3U);
    }
    const unsigned a17 = (outer & 4U) != 0U ? outer & 1U : (inner >> 4U) & 1U;
    const unsigned bank = (inner & 15U) | (a17 << 4U) | (((outer >> 1U) & 1U) << 5U);
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

unsigned expected_inner_chr(Address addr, Byte mode, const std::array<Byte, 6>& values) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (values[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return values[slot - 2U];
}

std::size_t expected_chr(Address addr, Byte outer, Byte mode, const std::array<Byte, 6>& values) {
    const unsigned inner = expected_inner_chr(addr, mode, values);
    const unsigned a17 = (outer & 0x40U) != 0U ? (outer >> 4U) & 1U : (inner >> 4U) & 1U;
    const unsigned bank = (inner & 15U) | (a17 << 4U) | (((outer >> 5U) & 1U) << 5U);
    return bank * kChrBankSize + (addr & 0x3FFU);
}

TEST_CASE("Mapper 441 validates decoded ROM bounds memory and CIRAM metadata", "[mapper441]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const auto ram : {RamKind::Absent, RamKind::Volatile, RamKind::Battery}) {
            CAPTURE(mirror, ram);
            REQUIRE(Mapper441::validate_geometry(header(mirror, ram), kPrgSize, kChrSize));
            auto mapper = board(kPrgSize, kChrSize, mirror, ram);
            REQUIRE(state(*mapper).size() == (ram == RamKind::Absent ? kRamOffset : kStateSize));
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().size() == (ram == RamKind::Battery ? kWramSize : 0U));
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper441::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size = 0x2000UZ; size <= kChrSize; size += kChrBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper441::validate_geometry(header(), kPrgSize, size));
    }

    for (const unsigned id : {0U, 4U, 391U, 440U, 442U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper441::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper441::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper441::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper441::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2200UZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper441::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper441::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
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
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper441::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 441 decodes every outer alias and all data bits", "[mapper441][bus][banking]") {
    auto mapper = board();
    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), 0x6BU);
        CAPTURE(raw);
        REQUIRE(state(*mapper)[kOuterOffset] == 0x6BU);
        REQUIRE(mapper->read_prg(0x8123U) == cell(expected_prg(0x8123U, 0x6BU, 0U, 0U, 0U)));
        REQUIRE(mapper->read_chr(0x0155U) == cell(expected_chr(0x0155U, 0x6BU, 0U, {})));
        REQUIRE(mapper->read_prg(static_cast<Address>(raw)) == 0x6BU);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0x7123U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kOuterOffset] == value);
        REQUIRE(mapper->read_prg(0x7123U) == value);
    }
}

TEST_CASE("Mapper 441 outer lock and MMC3 WRAM gates are independent of RAM writes",
          "[mapper441][bus][ram][reset]") {
    auto mapper = board();
    outer(*mapper, 0x6BU);
    outer(*mapper, 0xEBU, 0x7000U);
    const auto locked = state(*mapper);
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0x7FFFU, 0x7FU);
    REQUIRE(state(*mapper)[kOuterOffset] == locked[kOuterOffset]);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0x7FU);

    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0xA5U);
    REQUIRE(state(*mapper)[kOuterOffset] == locked[kOuterOffset]);
    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->maps_prg(0x6123U));
    mapper->write_prg(0x6123U, 0xA5U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(state(*mapper)[kOuterOffset] == locked[kOuterOffset]);
    mapper->reset();
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x11U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x11U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x11U);

    outer(*mapper, 0x80U);
    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    mapper->write_prg(0x6000U, 0x22U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x22U);
}

TEST_CASE("Mapper 441 applies outer PRG pins and NROM-256 wiring in both MMC3 modes",
          "[mapper441][banking]") {
    auto mapper = board();
    constexpr Byte kR6 = 0x35U;
    constexpr Byte kR7 = 0x47U;
    for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
        bank(*mapper, 6U, kR6, mode);
        bank(*mapper, 7U, kR7, mode);
        for (unsigned value = 0U; value < 128U; ++value) {
            const auto control = static_cast<Byte>(value);
            outer(*mapper, control);
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto addr = static_cast<Address>(0x8123U + slot * kPrgBankSize);
                const auto physical = expected_prg(addr, control, mode, kR6, kR7);
                CAPTURE(mode, control, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }

    for (const std::size_t size : {0x8000UZ, 0x18000UZ, 0x48000UZ}) {
        auto fitted = board(size, kChrSize);
        bank(*fitted, 6U, 0x3FU, 0x40U);
        bank(*fitted, 7U, 0x3FU, 0x40U);
        outer(*fitted, 0x6FU);
        for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
            const auto physical = expected_prg(addr, 0x6FU, 0x40U, 0x3FU, 0x3FU) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 441 combines outer CHR pins with paired and inverted MMC3 banks",
          "[mapper441][banking][ram]") {
    auto mapper = board();
    constexpr std::array<Byte, 6> values = {0x22U, 0x0BU, 0x13U, 0x2CU, 0x31U, 0x07U};
    for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*mapper, reg, values[reg], mode);
        }
        for (unsigned value = 0U; value < 128U; ++value) {
            const auto control = static_cast<Byte>(value);
            outer(*mapper, control);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                const auto physical = expected_chr(addr, control, mode, values);
                CAPTURE(mode, control, addr, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical));
                mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                REQUIRE(mapper->read_chr(addr) == cell(physical));
            }
        }
    }

    for (const std::size_t size : {0x2000UZ, 0x3000UZ, 0x48000UZ}) {
        auto fitted = board(kPrgSize, size);
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*fitted, reg, values[reg]);
        }
        outer(*fitted, 0x7FU);
        for (const Address addr : {0x0000U, 0x03FFU, 0x0800U, 0x1000U, 0x1555U, 0x1FFFU}) {
            const auto physical = expected_chr(addr, 0x7FU, 0U, values) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_chr(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 441 uses MMC3 horizontal vertical console CIRAM only", "[mapper441][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, kChrSize, initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg((value & 1U) != 0U ? 0xBFFEU : 0xA000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == initial);
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
    }
}

TEST_CASE("Mapper 441 exposes optional WRAM but no expansion or audio device",
          "[mapper441][bus][ram][irq][audio]") {
    auto absent = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Absent);
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->prg_read_mask(0x6000U) == 0U);
    REQUIRE(absent->battery_ram().empty());
    absent->write_prg(0x6000U, 0x0BU);
    REQUIRE(absent->read_prg(0xE000U) == cell(expected_prg(0xE000U, 0x0BU, 0U, 0U, 0U)));

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
    mapper->clock_irq_counter();
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
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 441 MMC3 IRQ latch reload acknowledge disable and zero repeats",
          "[mapper441][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    outer(*mapper, 0x6BU);
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

TEST_CASE("Mapper 441 reset restores registers and cold power clears only volatile WRAM",
          "[mapper441][reset][ram]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    outer(*mapper, 0xEBU);
    bank(*mapper, 6U, 19U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);

    auto persistent = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Battery);
    std::array<Byte, kWramSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0xA5U);
    }
    persistent->set_battery_ram(bytes);
    REQUIRE(persistent->battery_ram().size() == kWramSize);
    persistent->write_prg(0x6000U, 0x6BU);
    persistent->power_on();
    REQUIRE(persistent->read_prg(0x6123U) == bytes[0x123U]);
    REQUIRE(state(*persistent)[kOuterOffset] == 0U);
}

TEST_CASE("Mapper 441 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper441][state]") {
    auto mapper = board();
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        mapper->write_prg(addr, static_cast<Byte>(cell(addr) & 0x7FU));
    }
    outer(*mapper, 0x6BU);
    bank(*mapper, 6U, 19U, 0xC0U);
    bank(*mapper, 7U, 7U, 0xC0U);
    bank(*mapper, 0U, 0x22U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8220U);
    REQUIRE(saved[kOuterOffset] == 0x6BU);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Volatile, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
        const auto physical = expected_prg(addr, 0x6BU, 0xC0U, 19U, 7U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        const auto expected = addr == 0x6000U ? Byte{0x6BU} : static_cast<Byte>(cell(addr) & 0x7FU);
        REQUIRE(copy->read_prg(addr) == expected);
    }
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

    mapper->write_prg(0x6123U, 0x55U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 13> invalid_fields = {{{0U, 0U},
                                                                              {2U, 1U},
                                                                              {6U, 1U},
                                                                              {10U, 0U},
                                                                              {13U, 8U},
                                                                              {14U, 1U},
                                                                              {21U, 0x40U},
                                                                              {22U, 0x40U},
                                                                              {23U, 0x40U},
                                                                              {26U, 8U},
                                                                              {26U, 4U},
                                                                              {27U, 1U},
                                                                              {27U, 0x3FU}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = board(kPrgSize / 2U, kChrSize);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(kPrgSize, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto no_ram = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Absent);
    const auto no_ram_before = state(*no_ram);
    StateReader no_ram_reader(saved);
    no_ram->load_state(no_ram_reader);
    REQUIRE_FALSE(no_ram_reader.ok());
    REQUIRE(state(*no_ram) == no_ram_before);
    REQUIRE(no_ram_before.size() == kRamOffset);

    auto battery = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Battery);
    const auto battery_before = state(*battery);
    StateReader battery_reader(saved);
    battery->load_state(battery_reader);
    REQUIRE_FALSE(battery_reader.ok());
    REQUIRE(state(*battery) == battery_before);
}

} // namespace
