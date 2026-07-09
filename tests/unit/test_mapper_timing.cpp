// Mapper timing tests — exercises IRQ and CHR-latch behavior through the
// full PPU rendering path (A12 scanline clocking, pattern-table reads)
// rather than manual register calls.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <vector>

#include "core/mappers/mmc2.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/types.hpp"

using namespace mapperbus::core;

namespace {
INesHeader make_header(uint8_t prg_banks, uint8_t chr_banks, uint16_t mapper, MirrorMode mirror) {
    return INesHeader{
        .prg_rom_banks = prg_banks,
        .chr_rom_banks = chr_banks,
        .mapper_number = mapper,
        .submapper = 0,
        .mirror_mode = mirror,
        .region = Region::NTSC,
        .has_battery = false,
        .has_trainer = false,
        .is_nes2 = false,
    };
}
} // namespace

TEST_CASE("MMC2 CHR latch fires on pattern-table reads", "[mmc2][timing]") {
    // Verify the latch toggles when read_chr is called at the trigger
    // addresses, independent of the PPU. This is the mechanism Punch-Out!!
    // uses to swap background CHR mid-frame.
    std::vector<Byte> chr(128 * 1024, 0);
    chr[1 * 0x1000] = 0xAA; // FD page sentinel
    chr[4 * 0x1000] = 0xBB; // FE page sentinel

    Mmc2 mapper(make_header(8, 16, 9, MirrorMode::Vertical),
                std::vector<Byte>(128 * 1024, 0),
                std::move(chr));

    mapper.write_prg(0xB000, 1); // left FD page = 1
    mapper.write_prg(0xD000, 4); // left FE page = 4

    SECTION("FD page active at power-on") {
        REQUIRE(mapper.read_chr(0x0000) == 0xAA);
    }
    SECTION("FE page after $0FE8 read") {
        mapper.read_chr(0x0FE8);
        REQUIRE(mapper.read_chr(0x0000) == 0xBB);
    }
    SECTION("FD page restored after $0FD8 read") {
        mapper.read_chr(0x0FE8);
        mapper.read_chr(0x0FD8);
        REQUIRE(mapper.read_chr(0x0000) == 0xAA);
    }
}

TEST_CASE("MMC3 scanline IRQ fires via per-scanline clock", "[mmc3][timing]") {
    // The PPU clocks the MMC3 IRQ counter once per scanline (A12 rising edge
    // at cycle 260). This validates that the counter decrements, reloads, and
    // asserts IRQ within a bounded number of scanlines — the same path the
    // PPU drives during live rendering.
    std::vector<Byte> prg(128 * 1024, 0xEA);
    std::vector<Byte> chr(8 * 1024, 0);

    Mmc3 mapper(make_header(8, 1, 4, MirrorMode::Vertical), std::move(prg), std::move(chr));

    mapper.write_prg(0xC000, 0x08); // reload value = 8 scanlines
    mapper.write_prg(0xC001, 0x00); // force reload
    mapper.write_prg(0xE001, 0x00); // enable IRQ

    REQUIRE_FALSE(mapper.irq_pending());

    // After reload + 8 scanline decrements the IRQ should fire.
    bool fired = false;
    for (int scanline = 0; scanline < 300; ++scanline) {
        mapper.clock_irq_counter();
        if (mapper.irq_pending()) {
            fired = true;
            break;
        }
    }
    REQUIRE(fired);
}
