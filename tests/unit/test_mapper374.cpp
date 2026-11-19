#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper374.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kRomSize = 0x20000UZ;
constexpr std::size_t kGameSize = 0x08000UZ;
constexpr std::size_t kStateSize = 17UZ;
constexpr std::size_t kGameOffset = 10UZ;
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
    result.mapper_number = 374U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper374> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper374::validate_geometry(h, kRomSize, kRomSize));
    return std::make_unique<Mapper374>(h, rom(kRomSize, salt), rom(kRomSize, salt + 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_serial(Mapper374& mapper, Address addr, Byte value, bool consecutive = false) {
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper.write_prg_cpu(addr, static_cast<Byte>((value >> bit) & 1U), consecutive);
    }
}

void select_game(Mapper374& mapper, Byte game) {
    mapper.power_on();
    for (Byte current = 0U; current < game; ++current) {
        mapper.reset();
    }
}

TEST_CASE("Mapper 374 validates the documented ROM-only geometry", "[mapper374]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        CAPTURE(mirror);
        REQUIRE(Mapper374::validate_geometry(header(mirror), kRomSize, kRomSize));
        auto mapper = board(mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const auto id : {0U, 1U, 105U, 313U, 373U, 375U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper374::validate_geometry(h, kRomSize, kRomSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper374::validate_geometry(h, kRomSize, kRomSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper374::validate_geometry(h, kRomSize, kRomSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x8000UZ, 0x10000UZ, kRomSize - 1UZ, kRomSize + 1UZ, kRomSize * 2UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper374::validate_geometry(header(), size, kRomSize));
        REQUIRE_FALSE(Mapper374::validate_geometry(header(), kRomSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper374::validate_geometry(header(mirror), kRomSize, kRomSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        h = header();
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
        CAPTURE(field);
        REQUIRE_FALSE(Mapper374::validate_geometry(h, kRomSize, kRomSize));
    }
}

TEST_CASE("Mapper 374 decodes every MMC1 register alias through the serial port",
          "[mapper374][bus]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        const auto addr = static_cast<Address>(raw);
        write_serial(*mapper, addr, 0x15U);
        const auto saved = state(*mapper);
        CAPTURE(raw);
        REQUIRE(saved.size() == kStateSize);
        REQUIRE(saved[kGameOffset] == 0U);
        REQUIRE(saved[kShiftOffset] == 0x10U);
        REQUIRE(saved[kCountOffset] == 0U);
        REQUIRE(saved[kControlOffset] == (raw < 0xA000U ? 0x15U : 0x0CU));
        REQUIRE(saved[kChr0Offset] == (raw >= 0xA000U && raw < 0xC000U ? 0x15U : 0U));
        REQUIRE(saved[kChr1Offset] == (raw >= 0xC000U && raw < 0xE000U ? 0x15U : 0U));
        REQUIRE(saved[kPrgOffset] == (raw >= 0xE000U ? 0x15U : 0U));
    }

    mapper->power_on();
    mapper->write_prg_cpu(0x8000U, 1U, false);
    const auto partial = state(*mapper);
    REQUIRE(partial[kShiftOffset] == 0x18U);
    REQUIRE(partial[kCountOffset] == 1U);
    mapper->write_prg_cpu(0x8000U, 1U, true);
    REQUIRE(state(*mapper) == partial);
    mapper->write_prg_cpu(0x8000U, 0x80U, true);
    REQUIRE(state(*mapper)[kShiftOffset] == 0x10U);
    REQUIRE(state(*mapper)[kCountOffset] == 0U);
    REQUIRE(state(*mapper)[kControlOffset] == 0x0CU);
    REQUIRE(state(*mapper)[kGameOffset] == 0U);

    write_serial(*mapper, 0x8000U, 0x10U);
    mapper->write_prg(0x8000U, 0x80U);
    REQUIRE(state(*mapper)[kControlOffset] == 0x1CU);
    REQUIRE(state(*mapper)[kGameOffset] == 0U);
}

TEST_CASE("Mapper 374 applies every PRG mode inside the selected 32 KiB game",
          "[mapper374][banking]") {
    auto mapper = board();
    for (Byte game = 0U; game < 4U; ++game) {
        select_game(*mapper, game);
        for (Byte mode = 0U; mode < 4U; ++mode) {
            write_serial(*mapper, 0x8000U, static_cast<Byte>((mode << 2U) | 2U));
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                write_serial(*mapper, 0xE000U, static_cast<Byte>(raw));
                const unsigned selected = raw & 0x0FU;
                for (const auto addr : kPrgAddresses) {
                    const bool upper = addr >= 0xC000U;
                    unsigned bank = 0U;
                    if (mode <= 1U) {
                        bank = (selected & 0x0EU) | static_cast<unsigned>(upper);
                    } else if (mode == 2U) {
                        bank = upper ? selected : 0U;
                    } else {
                        bank = upper ? 1U : selected;
                    }
                    const auto offset = static_cast<std::size_t>(game) * kGameSize +
                                        (bank & 1U) * 0x4000U + (addr & 0x3FFFU);
                    CAPTURE(game, mode, raw, addr, bank, offset);
                    REQUIRE(mapper->read_prg(addr) == cell(offset));
                }
            }
        }
    }
}

TEST_CASE("Mapper 374 applies both CHR modes inside each game and protects ROM",
          "[mapper374][banking][bus]") {
    auto mapper = board();
    for (Byte game = 0U; game < 4U; ++game) {
        select_game(*mapper, game);
        for (Byte chr_mode = 0U; chr_mode < 2U; ++chr_mode) {
            write_serial(*mapper, 0x8000U, static_cast<Byte>(0x0CU | (chr_mode << 4U)));
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                write_serial(*mapper, 0xA000U, static_cast<Byte>(raw));
                write_serial(*mapper, 0xC000U, static_cast<Byte>(raw ^ 0x1FU));
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const bool upper = slot >= 4U;
                    const unsigned selected = upper ? (raw ^ 0x1FU) & 0x1FU : raw & 0x1FU;
                    const unsigned bank =
                        chr_mode != 0U ? selected : (raw & 0x1EU) | static_cast<unsigned>(upper);
                    const auto addr = static_cast<Address>(slot * 0x400U + 0x123U);
                    const auto offset = static_cast<std::size_t>(game) * kGameSize +
                                        (bank & 7U) * 0x1000U + (addr & 0x0FFFU);
                    const auto expected = cell(offset, 0x55U);
                    CAPTURE(game, chr_mode, raw, slot, bank, offset);
                    REQUIRE(mapper->read_chr(addr) == expected);
                    mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == expected);
                }
            }
        }
    }
}

TEST_CASE("Mapper 374 exposes all MMC1 CIRAM arrangements", "[mapper374][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
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
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    }
}

TEST_CASE("Mapper 374 reset advances four games and power-on selects game zero",
          "[mapper374][reset]") {
    auto mapper = board();
    for (unsigned selection = 0U; selection < 12U; ++selection) {
        const auto game = static_cast<Byte>(selection % 4U);
        CAPTURE(selection, game);
        REQUIRE(state(*mapper)[kGameOffset] == game);
        REQUIRE(mapper->read_prg(0x8000U) == cell(game * kGameSize));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(game * kGameSize + 0x7FFFU));
        REQUIRE(mapper->read_chr(0x0000U) == cell(game * kGameSize, 0x55U));
        REQUIRE(mapper->read_chr(0x1FFFU) == cell(game * kGameSize + 0x1FFFU, 0x55U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);

        write_serial(*mapper, 0x8000U, 0x1FU);
        write_serial(*mapper, 0xA000U, 0x1FU);
        write_serial(*mapper, 0xC000U, 0x1FU);
        write_serial(*mapper, 0xE000U, 0x1FU);
        mapper->write_prg(0x8000U, 1U);
        mapper->clock_cpu();
        mapper->clock_cpu_falling_edge();
        REQUIRE(state(*mapper)[kGameOffset] == game);
        mapper->reset();
    }
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board()));
}

TEST_CASE("Mapper 374 leaves RAM expansion IRQ and audio absent", "[mapper374][ram][irq][audio]") {
    auto mapper = board();
    write_serial(*mapper, 0xE000U, 0x10U);
    const auto before = state(*mapper);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->prg_read_mask(0xFFFFU) == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
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
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 374 snapshots replay all mutable state without ROM", "[mapper374][state]") {
    auto mapper = board();
    select_game(*mapper, 2U);
    write_serial(*mapper, 0x8000U, 0x1BU);
    write_serial(*mapper, 0xA000U, 0x13U);
    write_serial(*mapper, 0xC000U, 0x07U);
    write_serial(*mapper, 0xE000U, 0x11U);
    for (const Byte bit : {1U, 0U, 1U}) {
        mapper->write_prg(0x8000U, bit);
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kGameOffset] == 2U);
    REQUIRE(saved[kCountOffset] == 3U);
    REQUIRE(saved.size() < kGameSize);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (const Address addr : {0x0000U, 0x0123U, 0x1000U, 0x1123U, 0x1FFFU}) {
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(mapper->read_chr(addr) + 0x80U));
    }
    for (const Byte bit : {0U, 1U}) {
        mapper->write_prg(0x8000U, bit);
        copy->write_prg(0x8000U, bit);
    }
    REQUIRE(state(*copy) == state(*mapper));
    copy->reset();
    mapper->reset();
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

TEST_CASE("Mapper 374 rejects damaged truncated and mismatched states atomically",
          "[mapper374][state]") {
    auto mapper = board();
    select_game(*mapper, 2U);
    write_serial(*mapper, 0x8000U, 0x1BU);
    write_serial(*mapper, 0xA000U, 0x13U);
    write_serial(*mapper, 0xC000U, 0x07U);
    write_serial(*mapper, 0xE000U, 0x11U);
    for (const Byte bit : {1U, 0U, 1U}) {
        mapper->write_prg(0x8000U, bit);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 13> kInvalidFields{{
        {kGameOffset, 4U},
        {kGameOffset, 0xFFU},
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
    }};
    for (const auto [offset, value] : kInvalidFields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
}

} // namespace
