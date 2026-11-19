#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper360.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kChrRamOffset = 10UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 360U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 17U) * 43U + salt);
}

std::vector<Byte> rom(Byte salt = 0U) {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper360> board(Byte dip = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper360::validate_geometry(h, kPrgSize, 0U));
    auto mapper = std::make_unique<Mapper360>(h, rom(salt), std::vector<Byte>{});
    REQUIRE(mapper->configure_board_jumpers(dip));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 360 validates NES 2.0 ROM RAM and CIRAM geometry", "[mapper360]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper360::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (const auto id : {0U, 2U, 357U, 359U, 361U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper360::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper360::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper360::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x4000UZ, 0x7FFFUZ, 0x7FFFFUZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper360::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper360::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper360::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = kChrRamSize;
            break;
        case 2U:
            h.prg_nvram_size = kChrRamSize;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper360::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 360 DIP zero selects 32 KiB and other values select outer 16 KiB",
          "[mapper360][banking]") {
    for (Byte dip = 0U; dip < 32U; ++dip) {
        auto mapper = board(dip, MirrorMode::Horizontal);
        REQUIRE(mapper->mirror_mode() ==
                ((dip & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (const Address addr : {0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xC123U, 0xE123U, 0xFFFFU}) {
            const auto offset = dip == 0U
                                    ? static_cast<std::size_t>(addr & 0x7FFFU)
                                    : static_cast<std::size_t>(dip) * 0x4000UZ + (addr & 0x3FFFU);
            CAPTURE(dip, addr, offset);
            REQUIRE(mapper->read_prg(addr) == cell(offset));
        }
    }
}

TEST_CASE("Mapper 360 claims only PRG ROM and has no registers RAM IRQ or audio",
          "[mapper360][bus][ram][irq][audio]") {
    auto mapper = board(0x15U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());

    for (unsigned raw = 0U; raw <= 0xFFFFU; raw += 0x101U) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE(mapper->maps_prg(addr) == (addr >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (addr >= 0x8000U ? 0xFFU : 0U));
        if (addr < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
    }

    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(addr, value);
        mapper->write_expansion(addr, value);
    }
    mapper->write_prg_cpu(0xFFFFU, 0xFFU, true);
    mapper->observe_cpu_read(0xFFFFU, 0xA5U);
    mapper->observe_cpu_write(0xFFFFU, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 360 uses ordinary CIRAM routing controlled by the fifth DIP",
          "[mapper360][ciram]") {
    for (Byte dip = 0U; dip < 32U; ++dip) {
        auto mapper = board(dip, dip < 16U ? MirrorMode::Horizontal : MirrorMode::Vertical);
        CAPTURE(dip);
        REQUIRE(mapper->mirror_mode() ==
                (dip < 16U ? MirrorMode::Vertical : MirrorMode::Horizontal));
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

TEST_CASE("Mapper 360 CHR RAM is flat writable and retained across soft reset",
          "[mapper360][ram][reset]") {
    auto mapper = board(0x1FU);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0xA5U));
    }
    const auto programmed = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == programmed);
    REQUIRE(mapper->read_prg(0x8123U) == cell(31UZ * 0x4000UZ + 0x123U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    mapper->power_on();
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(state(*mapper) == state(*board(0x1FU)));
}

TEST_CASE("Mapper 360 validates all DIP values and rejects invalid ones atomically",
          "[mapper360][bus][state]") {
    auto mapper = board();
    for (Byte dip = 0U; dip < 32U; ++dip) {
        REQUIRE(mapper->configure_board_jumpers(dip));
        REQUIRE(state(*mapper)[0] == dip);
        REQUIRE(mapper->mirror_mode() ==
                ((dip & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    mapper->write_chr(0x0321U, 0xA7U);
    const auto before = state(*mapper);
    for (const Byte dip : {32U, 33U, 63U, 127U, 128U, 255U}) {
        CAPTURE(dip);
        REQUIRE_FALSE(mapper->configure_board_jumpers(dip));
        REQUIRE(state(*mapper) == before);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
}

TEST_CASE("Mapper 360 bounded snapshots replay CHR RAM without ROM", "[mapper360][state]") {
    auto mapper = board(0x12U);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_chr(0x0123U, 0x67U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8202U);

    auto copy = board(0x12U, MirrorMode::Horizontal, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_chr(0x0123U) == 0x67U);
    REQUIRE(copy->read_prg(0x8123U) == static_cast<Byte>(mapper->read_prg(0x8123U) + 0x80U));
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 360 rejects malformed and incompatible states atomically", "[mapper360][state]") {
    auto mapper = board(0x0FU);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
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
    for (const std::size_t offset : {0UZ, 1UZ, 2UZ, 3UZ, 6UZ, 7UZ}) {
        auto invalid = saved;
        invalid[offset] ^= 0xFFU;
        CAPTURE(offset);
        reject(invalid);
    }

    auto other_dip = board(0x10U);
    const auto other_before = state(*other_dip);
    StateReader wrong_dip(saved);
    other_dip->load_state(wrong_dip);
    REQUIRE_FALSE(wrong_dip.ok());
    REQUIRE(state(*other_dip) == other_before);
    REQUIRE(other_dip->mirror_mode() == MirrorMode::Horizontal);
}

} // namespace
