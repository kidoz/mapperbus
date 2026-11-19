#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper404.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::size_t kStateSize = 17UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kShiftOffset = 11UZ;
constexpr std::size_t kCountOffset = 12UZ;
constexpr std::size_t kControlOffset = 13UZ;
constexpr std::size_t kChr0Offset = 14UZ;
constexpr std::size_t kChr1Offset = 15UZ;
constexpr std::size_t kPrgOffset = 16UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 404U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper404> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper404::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper404>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x35U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper404& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void write_serial(Mapper404& mapper, Address addr, Byte value, bool consecutive = false) {
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper.write_prg_cpu(addr, static_cast<Byte>((value >> bit) & 1U), consecutive);
    }
}

std::size_t expected_prg(Address addr, Byte outer, Byte control, Byte prg_bank) {
    const auto mode = (control >> 2U) & 3U;
    const auto selected = prg_bank & 0x0FU;
    const bool upper = addr >= 0xC000U;
    unsigned inner = 0U;
    if (mode <= 1U) {
        inner = (selected & 0x0EU) | static_cast<unsigned>(upper);
    } else if (mode == 2U) {
        inner = upper ? selected : 0U;
    } else {
        inner = upper ? 0x0FU : selected;
    }
    const unsigned bank = (outer & 0x40U) != 0U
                              ? (inner & 7U) | (static_cast<unsigned>(outer & 7U) << 3U)
                              : inner | (static_cast<unsigned>(outer & 6U) << 3U);
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
}

std::size_t expected_chr(Address addr, Byte outer, Byte control, Byte chr0, Byte chr1) {
    const bool upper = (addr & 0x1000U) != 0U;
    const unsigned inner = (control & 0x10U) != 0U ? (upper ? chr1 : chr0) & 0x1FU
                                                   : (chr0 & 0x1EU) | static_cast<unsigned>(upper);
    const unsigned bank = inner | (static_cast<unsigned>(outer & 7U) << 5U);
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x0FFFU);
}

TEST_CASE("Mapper 404 validates bounded ROM-only JY012005 geometry", "[mapper404][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, kChrSize, mirror);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper)[kOuterOffset] == 0U);
        REQUIRE(state(*mapper)[kControlOffset] == 0x0CU);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(15UZ * kPrgBankSize));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper404::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size = 0x2000UZ; size <= kChrSize; size += kChrBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper404::validate_geometry(header(), kPrgSize, size));
    }

    for (const unsigned id : {0U, 1U, 105U, 374U, 403U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper404::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper404::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper404::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, kPrgSize + 1U, kPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper404::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, kChrSize + 1U, kChrSize + kChrBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper404::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper404::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
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
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 5U:
            h.prg_ram_size = 0x2000U;
            h.prg_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper404::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 404 decodes every outer alias and preserves its sticky lock",
          "[mapper404][decoding][bus]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x6000U) {
            expected->write_prg(0x6000U, value);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE_FALSE(actual->maps_prg(static_cast<Address>(raw)));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == 0U);
        REQUIRE(actual->read_prg(static_cast<Address>(raw)) == 0U);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        actual->reset();
        actual->write_prg(0x6000U, static_cast<Byte>(value));
        REQUIRE(state(*actual)[kOuterOffset] == (value & 0xC7U));
        const auto before = state(*actual);
        actual->write_prg(0x7FFFU, static_cast<Byte>(value ^ 0xFFU));
        const auto expected_outer =
            (value & 0x80U) != 0U ? before[kOuterOffset] : static_cast<Byte>(~value & 0xC7U);
        CAPTURE(value, expected_outer);
        REQUIRE(state(*actual)[kOuterOffset] == expected_outer);
    }

    actual->reset();
    actual->write_prg_cpu(0x6AAAU, 0x45U, true);
    REQUIRE(state(*actual)[kOuterOffset] == 0x45U);
    actual->write_prg_cpu(0x7FFFU, 0xC2U, true);
    REQUIRE(state(*actual)[kOuterOffset] == 0xC2U);
    const auto locked = state(*actual);
    for (const Address addr : {0x6000U, 0x6001U, 0x6FFFU, 0x7000U, 0x7FFFU}) {
        actual->write_prg(addr, 0U);
        actual->write_prg_cpu(addr, 0xFFU, false);
    }
    REQUIRE(state(*actual) == locked);
}

TEST_CASE("Mapper 404 decodes every MMC1 serial alias and filters consecutive writes",
          "[mapper404][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        write_serial(*mapper, static_cast<Address>(raw), 0x15U);
        const auto saved = state(*mapper);
        CAPTURE(raw);
        REQUIRE(saved[kOuterOffset] == 0U);
        REQUIRE(saved[kShiftOffset] == 0x10U);
        REQUIRE(saved[kCountOffset] == 0U);
        REQUIRE(saved[kControlOffset] == (raw < 0xA000U ? 0x15U : 0x0CU));
        REQUIRE(saved[kChr0Offset] == (raw >= 0xA000U && raw < 0xC000U ? 0x15U : 0U));
        REQUIRE(saved[kChr1Offset] == (raw >= 0xC000U && raw < 0xE000U ? 0x15U : 0U));
        REQUIRE(saved[kPrgOffset] == (raw >= 0xE000U ? 0x15U : 0U));
    }

    mapper->write_prg(0x6000U, 0xC5U);
    write_serial(*mapper, 0xA000U, 0x0AU);
    write_serial(*mapper, 0xC000U, 0x12U);
    write_serial(*mapper, 0xE000U, 0x03U);
    mapper->write_prg_cpu(0x8000U, 1U, false);
    const auto partial = state(*mapper);
    REQUIRE(partial[kShiftOffset] == 0x18U);
    REQUIRE(partial[kCountOffset] == 1U);
    mapper->write_prg_cpu(0x8000U, 1U, true);
    REQUIRE(state(*mapper) == partial);
    mapper->write_prg_cpu(0x8000U, 0x80U, true);
    auto saved = state(*mapper);
    REQUIRE(saved[kOuterOffset] == 0xC5U);
    REQUIRE(saved[kShiftOffset] == 0x10U);
    REQUIRE(saved[kCountOffset] == 0U);
    REQUIRE(saved[kControlOffset] == 0x0CU);
    REQUIRE(saved[kChr0Offset] == 0x0AU);
    REQUIRE(saved[kChr1Offset] == 0x12U);
    REQUIRE(saved[kPrgOffset] == 0x03U);

    write_serial(*mapper, 0x8000U, 0x10U);
    mapper->write_prg(0xFFFFU, 0x80U);
    saved = state(*mapper);
    REQUIRE(saved[kControlOffset] == 0x1CU);
    REQUIRE(saved[kOuterOffset] == 0xC5U);
}

TEST_CASE("Mapper 404 applies both outer PRG A17 policies after MMC1 mode decoding",
          "[mapper404][banking][bus]") {
    auto mapper = board();
    for (Byte mode_bit = 0U; mode_bit < 2U; ++mode_bit) {
        for (Byte outer = 0U; outer < 8U; ++outer) {
            const auto outer_value = static_cast<Byte>(outer | (mode_bit << 6U));
            for (Byte mode = 0U; mode < 4U; ++mode) {
                write_serial(*mapper, 0x8000U, static_cast<Byte>((mode << 2U) | 2U));
                for (unsigned raw = 0U; raw < 256U; ++raw) {
                    mapper->write_prg(0x6000U, outer_value);
                    write_serial(*mapper, 0xE000U, static_cast<Byte>(raw));
                    const auto control = static_cast<Byte>((mode << 2U) | 2U);
                    const auto prg_bank = static_cast<Byte>(raw & 0x1FU);
                    for (const auto addr : kPrgAddresses) {
                        const auto physical = expected_prg(addr, outer_value, control, prg_bank);
                        CAPTURE(mode_bit, outer, mode, raw, addr, physical);
                        REQUIRE(mapper->maps_prg(addr));
                        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                        REQUIRE(mapper->read_prg(addr) == cell(physical));
                    }
                }
            }
        }
    }

    auto fitted = board(0x18000UZ, 0x6000UZ);
    fitted->write_prg(0x6000U, 0x47U);
    write_serial(*fitted, 0xE000U, 0xFFU);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0x47U, 0x0CU, 0x1FU) % 0x18000UZ;
        CAPTURE(addr, physical);
        REQUIRE(fitted->read_prg(addr) == cell(physical));
    }
}

TEST_CASE("Mapper 404 shares outer A19 through A17 with both MMC1 CHR modes",
          "[mapper404][banking][bus]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 8U; ++outer) {
        mapper->write_prg(0x6000U, outer);
        for (Byte chr_mode = 0U; chr_mode < 2U; ++chr_mode) {
            const auto control = static_cast<Byte>(0x0CU | (chr_mode << 4U));
            write_serial(*mapper, 0x8000U, control);
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto chr0 = static_cast<Byte>(raw & 0x1FU);
                const auto chr1 = static_cast<Byte>((raw ^ 0x1FU) & 0x1FU);
                write_serial(*mapper, 0xA000U, static_cast<Byte>(raw));
                write_serial(*mapper, 0xC000U, static_cast<Byte>(raw ^ 0x1FU));
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * 0x400U + 0x123U);
                    const auto physical = expected_chr(addr, outer, control, chr0, chr1);
                    const auto expected = cell(physical, 0x35U);
                    CAPTURE(outer, chr_mode, raw, slot, physical);
                    REQUIRE(mapper->read_chr(addr) == expected);
                    mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == expected);
                }
            }
        }
    }

    auto fitted = board(0x8000UZ, 0x6000UZ);
    fitted->write_prg(0x6000U, 7U);
    write_serial(*fitted, 0x8000U, 0x1CU);
    write_serial(*fitted, 0xA000U, 0x1FU);
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        const auto physical = expected_chr(addr, 7U, 0x1CU, 0x1FU, 0U) % 0x6000UZ;
        CAPTURE(addr, physical);
        REQUIRE(fitted->read_chr(addr) == cell(physical, 0x35U));
    }
}

TEST_CASE("Mapper 404 exposes all MMC1 CIRAM arrangements and no board VRAM",
          "[mapper404][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, kChrSize, initial);
        constexpr std::array<MirrorMode, 4> kModes{MirrorMode::SingleLower,
                                                   MirrorMode::SingleUpper,
                                                   MirrorMode::Vertical,
                                                   MirrorMode::Horizontal};
        for (unsigned value = 0U; value < 256U; ++value) {
            write_serial(*mapper, 0x8000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() == kModes[value & 3U]);
            REQUIRE(state(*mapper)[kControlOffset] == (value & 0x1FU));
        }
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr = static_cast<Address>(addr + 0x100U)) {
            CAPTURE(addr);
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    }
}

TEST_CASE("Mapper 404 has write-only outer ports and no RAM IRQ or expansion audio",
          "[mapper404][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0xC7U);
    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, static_cast<Byte>(addr));
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_irq_counter();
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
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
}

TEST_CASE("Mapper 404 reset restores power-on registers without ROM mutation",
          "[mapper404][reset]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    mapper->write_prg(0x6000U, 0xC7U);
    write_serial(*mapper, 0x8000U, 0x1BU);
    write_serial(*mapper, 0xA000U, 0x13U);
    write_serial(*mapper, 0xC000U, 0x07U);
    write_serial(*mapper, 0xE000U, 0x11U);
    mapper->write_prg(0x8000U, 1U);
    REQUIRE(state(*mapper) != initial);
    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(15UZ * kPrgBankSize));
    REQUIRE(mapper->read_chr(0U) == cell(0U, 0x35U));

    mapper->write_prg(0x6000U, 0x47U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    mapper->write_prg(0x6000U, 0xC5U);
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0xC5U);
    mapper->reset();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 404 bounded snapshots replay outer and partial MMC1 state without ROM",
          "[mapper404][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0xC5U);
    write_serial(*mapper, 0x8000U, 0x1BU);
    write_serial(*mapper, 0xA000U, 0x13U);
    write_serial(*mapper, 0xC000U, 0x07U);
    write_serial(*mapper, 0xE000U, 0x11U);
    for (const Byte bit : {1U, 0U, 1U}) {
        mapper->write_prg(0xA000U, bit);
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x8000UZ);
    REQUIRE(saved[kOuterOffset] == 0xC5U);
    REQUIRE(saved[kCountOffset] == 3U);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0xC5U, 0x1BU, 0x11U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (const Address addr : {0x0000U, 0x0123U, 0x1000U, 0x1123U, 0x1FFFU}) {
        const auto physical = expected_chr(addr, 0xC5U, 0x1BU, 0x13U, 0x07U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0x80U ^ 0x35U));
    }
    for (const Byte bit : {0U, 1U}) {
        mapper->write_prg(0xA000U, bit);
        copy->write_prg(0xA000U, bit);
    }
    mapper->write_prg(0x6000U, 0U);
    copy->write_prg(0x6000U, 0U);
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 404 rejects damaged truncated and mismatched states atomically",
          "[mapper404][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0xC5U);
    write_serial(*mapper, 0x8000U, 0x1BU);
    write_serial(*mapper, 0xA000U, 0x13U);
    write_serial(*mapper, 0xC000U, 0x07U);
    write_serial(*mapper, 0xE000U, 0x11U);
    for (const Byte bit : {1U, 0U, 1U}) {
        mapper->write_prg(0xA000U, bit);
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
    for (unsigned bit = 0U; bit < 80U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 16> kInvalidFields{{
        {kOuterOffset, 0x08U},
        {kOuterOffset, 0x20U},
        {kOuterOffset, 0x38U},
        {kOuterOffset, 0xFFU},
        {kShiftOffset, 0U},
        {kShiftOffset, 0x20U},
        {kShiftOffset, 0xFFU},
        {kCountOffset, 0U},
        {kCountOffset, 1U},
        {kCountOffset, 4U},
        {kCountOffset, 5U},
        {kControlOffset, 0x20U},
        {kChr0Offset, 0x20U},
        {kChr1Offset, 0x20U},
        {kPrgOffset, 0x20U},
        {kPrgOffset, 0xFFU},
    }};
    for (const auto [offset, value] : kInvalidFields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_geometry = board(kPrgSize / 2U, kChrSize);
    const auto wrong_prg_before = state(*wrong_geometry);
    StateReader wrong_prg_reader(saved);
    wrong_geometry->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_geometry) == wrong_prg_before);

    wrong_geometry = board(kPrgSize, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_geometry);
    StateReader wrong_chr_reader(saved);
    wrong_geometry->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_geometry) == wrong_chr_before);
}

} // namespace
