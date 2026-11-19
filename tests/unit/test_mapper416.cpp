#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper416.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x010000UZ;
constexpr std::size_t kMaxPrgSize = 0x020000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x008000UZ;
constexpr std::size_t kBankSize = 0x002000UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kDataLatchOffset = 14UZ;
constexpr std::size_t kPrgBankOffset = 15UZ;
constexpr std::size_t kIrqCounterOffset = 16UZ;
constexpr std::size_t kIrqEnabledOffset = 18UZ;
constexpr std::size_t kStateSize = 19UZ;
constexpr std::array<Address, 10> kPrgAddresses{
    0x6000U, 0x7123U, 0x7FFFU, 0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 416U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 47U +
                             (offset >> 16U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper416> board(std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper416::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper416>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x35U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper416& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void write_u16(std::span<Byte> bytes, std::size_t offset, std::uint16_t value) {
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

unsigned normal_bank(Address addr, Byte latch) {
    unsigned bank = 16U | ((latch & 0x80U) >> 4U) | ((latch & 0x20U) >> 4U);
    switch ((latch >> 1U) & 3U) {
    case 0U:
        break;
    case 1U:
        bank |= (addr >> 13U) & 1U;
        break;
    default:
        bank = (bank & ~3U) | ((addr >> 13U) & 3U);
        break;
    }
    return bank;
}

unsigned smb2j_bank(Address addr, Byte value) {
    if (addr < 0xA000U) {
        return 0U;
    }
    if (addr < 0xC000U) {
        return 1U;
    }
    if (addr >= 0xE000U) {
        return 3U;
    }
    return ((value & 4U) << 1U) | (value & 3U);
}

std::size_t prg_offset(Address addr, unsigned bank, std::size_t size = kMaxPrgSize) {
    return (static_cast<std::size_t>(bank) * kBankSize + (addr & 0x1FFFU)) % size;
}

TEST_CASE("Mapper 416 validates decoder geometry and deterministic power-on",
          "[mapper416][geometry][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize, kMinChrSize, mirror);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->read_prg(0x6000U) == cell(prg_offset(0x6000U, 7U, kMinPrgSize)));
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xA000U) == cell(prg_offset(0xA000U, 1U, kMinPrgSize)));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xE000U) == cell(prg_offset(0xE000U, 3U, kMinPrgSize)));
        REQUIRE(mapper->read_chr(0U) == cell(0U, 0x35U));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        auto other_rom = board(kMinPrgSize, kMinChrSize, mirror, 0x80U);
        REQUIRE(state(*other_rom) == state(*mapper));
    }
    for (const std::size_t prg : {kMinPrgSize, 0x12000UZ, 0x1C000UZ, kMaxPrgSize}) {
        for (const std::size_t chr : {kMinChrSize, 0x4000UZ, 0x6000UZ, kMaxChrSize}) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper416::validate_geometry(header(), prg, chr));
        }
    }

    for (const unsigned id : {0U, 50U, 256U, 414U, 415U, 417U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper416::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper416::validate_geometry(legacy, kMinPrgSize, kMinChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        CAPTURE(submapper);
        REQUIRE_FALSE(Mapper416::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0xE000UZ, 0x10001UZ, 0x11000UZ, kMaxPrgSize + kBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper416::validate_geometry(header(), size, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x3000UZ, kMaxChrSize + kBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper416::validate_geometry(header(), kMinPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper416::validate_geometry(header(mirror), kMinPrgSize, kMinChrSize));
    }
    for (unsigned field = 0U; field < 6U; ++field) {
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
        case 4U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper416::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
}

TEST_CASE("Mapper 416 data latch decodes every A15 write without bus conflicts",
          "[mapper416][decoding][bus]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        auto wanted = initial;
        if (raw >= 0x8000U) {
            wanted[kDataLatchOffset] = value & 0xEEU;
        }
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == wanted);
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xA5A5U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kDataLatchOffset] == (value & 0xEEU));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x08U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    mapper->reset();
    const auto before = state(*mapper);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU}) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    for (const auto addr : kPrgAddresses) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA7U));
}

TEST_CASE("Mapper 416 normal mode implements all NROM bank forms before ROM wrapping",
          "[mapper416][banking][bus]") {
    auto mapper = board();
    for (unsigned cc = 0U; cc < 4U; ++cc) {
        for (const Byte bpa : {0x40U, 0x60U, 0xC0U, 0xE0U}) {
            const auto latch = static_cast<Byte>(bpa | (cc << 1U));
            mapper->write_prg(0x8000U, latch);
            for (const auto addr : kPrgAddresses) {
                const unsigned bank = addr < 0x8000U ? 7U : normal_bank(addr, latch);
                const auto physical = prg_offset(addr, bank);
                CAPTURE(cc, bpa, addr, bank, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x12000UZ, 0x1C000UZ}) {
        auto fitted = board(size, kMinChrSize);
        constexpr Byte kLatch = 0xE7U;
        fitted->write_prg(0xFFFFU, kLatch);
        for (const auto addr : kPrgAddresses) {
            const unsigned bank = addr < 0x8000U ? 7U : normal_bank(addr, kLatch);
            const auto physical = prg_offset(addr, bank, size);
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 416 SMB2J mode retains N-32 fixed slots and switchable C000 bank",
          "[mapper416][banking][decoding][bus]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x4020U, static_cast<Byte>(value));
        for (const auto addr : kPrgAddresses) {
            const unsigned bank = addr < 0x8000U ? 7U : smb2j_bank(addr, value & 7U);
            const auto physical = prg_offset(addr, bank);
            CAPTURE(value, addr, bank, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
        REQUIRE(state(*mapper)[kPrgBankOffset] == (value & 7U));
    }

    for (Address addr = 0x4020U; addr <= 0x5FFFU; ++addr) {
        mapper->reset();
        mapper->write_expansion(addr, 0x07U);
        const bool selected = (addr & 0x4120U) == 0x4020U;
        CAPTURE(addr, selected);
        REQUIRE(state(*mapper)[kPrgBankOffset] == (selected ? 7U : 0U));
        REQUIRE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0U);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }

    mapper->write_prg(0x8000U, 0x40U);
    mapper->write_expansion(0x4020U, 0x07U);
    const auto normal = state(*mapper);
    REQUIRE(normal[kPrgBankOffset] == 0U);
    for (const Address addr : {0x0000U, 0x401FU, 0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
        mapper->write_expansion(addr, 0xFFU);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
    REQUIRE(state(*mapper) == normal);
}

TEST_CASE("Mapper 416 CHR uses the shared cc bank and stays read-only",
          "[mapper416][banking][chr][bus]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xFFFFU, static_cast<Byte>(value));
        const unsigned bank = (value >> 1U) & 3U;
        for (Address addr = 0U; addr < 0x2000U; addr += 0x0111U) {
            const auto physical = prg_offset(addr, bank);
            CAPTURE(value, bank, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
            mapper->write_chr(addr, static_cast<Byte>(value ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
        }
    }
    for (const std::size_t size : {kMinChrSize, 0x4000UZ, 0x6000UZ}) {
        auto fitted = board(kMinPrgSize, size);
        fitted->write_prg(0x8000U, 0x06U);
        for (const Address addr : {0x0000U, 0x0123U, 0x0800U, 0x1FFFU}) {
            const auto physical = (3UZ * kBankSize + (addr & 0x1FFFU)) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_chr(addr) == cell(physical, 0x35U));
        }
    }
}

TEST_CASE("Mapper 416 uses only latch M for horizontal vertical console CIRAM",
          "[mapper416][ciram][decoding]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x08U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
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

TEST_CASE("Mapper 416 has no RAM audio or unrelated observer side effects",
          "[mapper416][ram][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0xA5A5U, 0xAEU);
    mapper->write_expansion(0x4020U, 0x05U);
    const auto before = state(*mapper);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_irq_counter();
    mapper->acknowledge_irq();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("default"));
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 416 SMB2J IRQ counts 4096 CPU cycles and latches until disabled",
          "[mapper416][irq][decoding][reset]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x40U);
    mapper->write_expansion(0x4120U, 1U);
    for (unsigned cycle = 0U; cycle < 10000U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqEnabledOffset] == 0U);

    mapper->reset();
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_expansion(0x4120U, 0xFEU);
    mapper->write_expansion(0x4120U, 1U);
    for (unsigned cycle = 1U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        CAPTURE(cycle);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 10000U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4120U, 1U);
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4120U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqEnabledOffset] == 0U);

    mapper->write_expansion(0x4120U, 1U);
    for (unsigned cycle = 0U; cycle < 1234U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto partial = state(*mapper);
    mapper->write_prg(0x8000U, 0x40U);
    for (unsigned cycle = 0U; cycle < 10000U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    auto suspended = partial;
    suspended[kDataLatchOffset] = 0x40U;
    REQUIRE(state(*mapper) == suspended);
    mapper->write_prg(0x8000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 1234U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 416 decodes every SMB2J IRQ register alias", "[mapper416][irq][decoding]") {
    auto mapper = board();
    for (Address addr = 0x4020U; addr <= 0x5FFFU; ++addr) {
        mapper->reset();
        mapper->write_expansion(addr, 0xFFU);
        const bool selected = (addr & 0x4120U) == 0x4120U;
        for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(addr, selected);
        REQUIRE(mapper->irq_pending() == selected);
        mapper->write_expansion(addr, 0U);
        if ((addr & 0x4120U) == 0x4020U) {
            REQUIRE(state(*mapper)[kPrgBankOffset] == 0U);
        } else {
            REQUIRE_FALSE(mapper->irq_pending());
        }
    }
}

TEST_CASE("Mapper 416 soft and cold reset restore all registers without clearing ROM",
          "[mapper416][reset][ciram][irq]") {
    for (const bool cold : {false, true}) {
        auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
        mapper->write_prg(0xA5A5U, 0xAEU);
        mapper->write_expansion(0x4020U, 0x07U);
        mapper->write_expansion(0x4120U, 1U);
        for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE(mapper->irq_pending());
        if (cold) {
            mapper->power_on();
        } else {
            mapper->reset();
        }
        CAPTURE(cold);
        REQUIRE(state(*mapper) == state(*board()));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->read_prg(0x6000U) == cell(prg_offset(0x6000U, 7U)));
        REQUIRE(mapper->read_prg(0xC123U) == cell(0x0123U));
        REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U, 0x35U));
    }
}

TEST_CASE("Mapper 416 bounded snapshots replay banking and IRQ state without ROM",
          "[mapper416][state][banking][irq]") {
    auto mapper = board();
    mapper->write_prg(0xA5A5U, 0xAAU);
    mapper->write_expansion(0x4020U, 0x05U);
    mapper->write_expansion(0x4120U, 1U);
    for (unsigned cycle = 0U; cycle < 1234U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kMinChrSize);
    REQUIRE(saved[kDataLatchOffset] == 0xAAU);
    REQUIRE(saved[kPrgBankOffset] == 5U);

    auto copy = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        const unsigned bank = addr < 0x8000U ? 7U : smb2j_bank(addr, 5U);
        const auto physical = prg_offset(addr, bank);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    REQUIRE(copy->read_chr(0x0123U) == cell(0x2123U, 0xB5U));
    for (unsigned cycle = 1234U; cycle < 4096U; ++cycle) {
        copy->clock_cpu();
    }
    REQUIRE(copy->irq_pending());
    const auto pending = state(*copy);

    auto with_tail = pending;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->reset();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == pending);
    REQUIRE(copy->irq_pending());
}

TEST_CASE("Mapper 416 rejects damaged truncated and mismatched states atomically",
          "[mapper416][state]") {
    auto mapper = board();
    mapper->write_prg(0xA5A5U, 0xAAU);
    mapper->write_expansion(0x4020U, 0x05U);
    mapper->write_expansion(0x4120U, 1U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
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
    for (const Byte latch : {0x01U, 0x10U, 0x11U, 0xFFU}) {
        auto invalid = saved;
        invalid[kDataLatchOffset] = latch;
        CAPTURE(latch);
        reject(invalid);
    }
    for (const Byte bank : {8U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kPrgBankOffset] = bank;
        CAPTURE(bank);
        reject(invalid);
    }
    for (const std::uint16_t counter : {0x1001U, 0x1FFFU, 0xFFFFU}) {
        auto invalid = saved;
        write_u16(invalid, kIrqCounterOffset, counter);
        CAPTURE(counter);
        reject(invalid);
    }
    for (const Byte enabled : {2U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqEnabledOffset] = enabled;
        CAPTURE(enabled);
        reject(invalid);
    }
    auto inconsistent = saved;
    write_u16(inconsistent, kIrqCounterOffset, 1U);
    inconsistent[kIrqEnabledOffset] = 0U;
    reject(inconsistent);

    auto wrong_prg = board(0x10000UZ);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(kMaxPrgSize, 0x2000UZ);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto changed = saved;
    changed[kDataLatchOffset] = 0x40U;
    changed[kPrgBankOffset] = 2U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE_FALSE(mapper->irq_pending());
}

} // namespace
