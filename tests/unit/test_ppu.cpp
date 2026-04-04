// PPU unit tests — exercise the Loopy $2005/$2006/$2007 register model,
// fine-X scroll, and VRAM access patterns documented in ADR-001.

#include <catch2/catch_test_macros.hpp>

#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/types.hpp"

using namespace mapperbus::core;

namespace {

// PPUSTATUS read resets the write latch, per hardware.
void reset_latch(Ppu& ppu) {
    (void)ppu.read_register(0x02);
}

} // namespace

TEST_CASE("PPU $2005 / $2006 updates reg_t and fine_x correctly", "[ppu][loopy]") {
    Ppu ppu;
    ppu.reset();

    SECTION("$2005 first write sets coarse X and fine X") {
        reset_latch(ppu);
        ppu.write_register(0x05, 0x7D); // 0111 1101 -> coarse X = 0x0F, fine X = 5
        REQUIRE((ppu.loopy_t() & 0x001F) == 0x0F);
        REQUIRE(ppu.fine_x() == 0x05);
        REQUIRE(ppu.write_latch()); // latch flipped after first write
    }

    SECTION("$2005 second write sets fine Y + coarse Y") {
        reset_latch(ppu);
        ppu.write_register(0x05, 0x00); // first write (horizontal)
        ppu.write_register(0x05, 0x5E); // 0101 1110 -> fine Y = 6, coarse Y = 0x0B
        REQUIRE(((ppu.loopy_t() >> 12) & 0x07) == 0x06);
        REQUIRE(((ppu.loopy_t() >> 5) & 0x1F) == 0x0B);
        REQUIRE_FALSE(ppu.write_latch());
    }

    SECTION("$2006 second write copies t into v") {
        reset_latch(ppu);
        ppu.write_register(0x06, 0x24); // high byte: 0x24 -> v-to-be = 0x2400
        ppu.write_register(0x06, 0x00); // low byte
        REQUIRE(ppu.loopy_v() == 0x2400);
        REQUIRE(ppu.loopy_t() == 0x2400);
    }

    SECTION("$2002 read resets the write latch") {
        reset_latch(ppu);
        ppu.write_register(0x06, 0xAB); // latch now true
        REQUIRE(ppu.write_latch());
        (void)ppu.read_register(0x02); // reads $2002, resetting latch
        REQUIRE_FALSE(ppu.write_latch());
    }
}

TEST_CASE("PPU $2007 auto-increments reg_v", "[ppu][vram][ppudata]") {
    Ppu ppu;
    ppu.reset();

    SECTION("PPUCTRL bit 2 = 0 increments by 1") {
        ppu.write_register(0x00, 0x00); // increment = 1
        reset_latch(ppu);
        ppu.write_register(0x06, 0x20); // high = 0x20
        ppu.write_register(0x06, 0x00); // low = 0x00 -> v = 0x2000
        ppu.write_register(0x07, 0x11); // write and auto-increment
        REQUIRE(ppu.loopy_v() == 0x2001);
    }

    SECTION("PPUCTRL bit 2 = 1 increments by 32") {
        ppu.write_register(0x00, 0x04); // increment = 32
        reset_latch(ppu);
        ppu.write_register(0x06, 0x20);
        ppu.write_register(0x06, 0x00);
        ppu.write_register(0x07, 0x22);
        REQUIRE(ppu.loopy_v() == 0x2020);
    }
}

TEST_CASE("PPU VRAM round-trip via $2006 / $2007", "[ppu][vram]") {
    Ppu ppu;
    ppu.reset();
    ppu.write_register(0x00, 0x00); // increment = 1
    reset_latch(ppu);

    // Point v at $2000 and write a pattern.
    ppu.write_register(0x06, 0x20);
    ppu.write_register(0x06, 0x00);
    for (Byte b : {Byte{0x11}, Byte{0x22}, Byte{0x33}, Byte{0x44}}) {
        ppu.write_register(0x07, b);
    }

    // Point v back at $2000 and read. First $2007 read returns the
    // stale buffer byte; subsequent reads return the actual data
    // (this is the documented PPU read-buffer latch).
    ppu.write_register(0x06, 0x20);
    ppu.write_register(0x06, 0x00);
    (void)ppu.read_register(0x07); // prime the buffer
    REQUIRE(ppu.read_register(0x07) == 0x11);
    REQUIRE(ppu.read_register(0x07) == 0x22);
    REQUIRE(ppu.read_register(0x07) == 0x33);
    REQUIRE(ppu.read_register(0x07) == 0x44);
}

TEST_CASE("PPU palette reads skip the buffer latch", "[ppu][palette]") {
    Ppu ppu;
    ppu.reset();
    ppu.write_register(0x00, 0x00);
    reset_latch(ppu);

    // Write palette entry $3F01 = $2A directly.
    ppu.write_register(0x06, 0x3F);
    ppu.write_register(0x06, 0x01);
    ppu.write_register(0x07, 0x2A);

    // Read back — palette reads return immediately (no buffer delay).
    ppu.write_register(0x06, 0x3F);
    ppu.write_register(0x06, 0x01);
    REQUIRE((ppu.read_register(0x07) & 0x3F) == 0x2A);
}

TEST_CASE("PPUSTATUS read clears VBlank flag and nmi state", "[ppu][status]") {
    Ppu ppu;
    ppu.reset();

    // Enable NMI on VBlank.
    ppu.write_register(0x00, 0x80);
    // VBlank starts at NTSC scanline 241, dot 1 (82182 PPU dots = 27394 CPU cycles).
    // Pre-render (which clears VBlank) is at scanline 261, dot 1.
    // Step enough to enter VBlank but not exit it.
    ppu.step(28000);

    Byte status = ppu.read_register(0x02);
    REQUIRE((status & 0x80) != 0);                  // vblank was set
    REQUIRE((ppu.read_register(0x02) & 0x80) == 0); // cleared on subsequent read
}

TEST_CASE("PPU OAM DMA writes through oam_addr", "[ppu][oam]") {
    Ppu ppu;
    ppu.reset();

    // Pre-set OAMADDR to 0.
    ppu.write_register(0x03, 0x00);
    // Write 4 bytes via the DMA path.
    ppu.write_oam_dma(0xAA);
    ppu.write_oam_dma(0xBB);
    ppu.write_oam_dma(0xCC);
    ppu.write_oam_dma(0xDD);

    // Read them back via OAMDATA ($2004) — reset OAMADDR first.
    ppu.write_register(0x03, 0x00);
    REQUIRE(ppu.read_register(0x04) == 0xAA);
    ppu.write_register(0x03, 0x01);
    REQUIRE(ppu.read_register(0x04) == 0xBB);
    ppu.write_register(0x03, 0x02);
    REQUIRE(ppu.read_register(0x04) == 0xCC);
    ppu.write_register(0x03, 0x03);
    REQUIRE(ppu.read_register(0x04) == 0xDD);
}

TEST_CASE("PPU frame pacing: frame_ready toggles once per frame", "[ppu][timing]") {
    Ppu ppu;
    ppu.reset();

    REQUIRE_FALSE(ppu.frame_ready());
    // frame_ready is set at VBlank start (scanline 241) and cleared again at
    // pre-render (scanline 261). Step into VBlank but not past pre-render.
    ppu.step(28000);
    REQUIRE(ppu.frame_ready());
    ppu.clear_frame_ready();
    REQUIRE_FALSE(ppu.frame_ready());
}

TEST_CASE("PPU background fetch crosses into the next tile when fine_x overflows",
          "[ppu][loopy][render]") {
    register_builtin_mappers();

    std::vector<Byte> rom_data(16 + 16384 + 8192, 0);
    rom_data[0] = 'N';
    rom_data[1] = 'E';
    rom_data[2] = 'S';
    rom_data[3] = 0x1A;
    rom_data[4] = 1; // 16 KB PRG
    rom_data[5] = 1; // 8 KB CHR

    const std::size_t chr_offset = 16 + 16384;
    rom_data[chr_offset + 16] = 0xFF; // tile 1, row 0, plane 0 => opaque across the whole row

    auto cartridge_result = Cartridge::from_data(rom_data);
    REQUIRE(cartridge_result);
    auto cartridge = std::move(*cartridge_result);

    Ppu ppu;
    ppu.reset();
    ppu.connect_cartridge(&cartridge);

    ppu.write_register(0x01, 0x0A); // Show background, including the leftmost 8 pixels

    reset_latch(ppu);
    ppu.write_register(0x06, 0x20);
    ppu.write_register(0x06, 0x00);
    ppu.write_register(0x07, 0x00); // tile 0: transparent
    ppu.write_register(0x07, 0x01); // tile 1: opaque

    reset_latch(ppu);
    ppu.write_register(0x06, 0x3F);
    ppu.write_register(0x06, 0x00);
    ppu.write_register(0x07, 0x0F); // universal background = black
    ppu.write_register(0x07, 0x30); // palette color 1 = white

    reset_latch(ppu);
    ppu.write_register(0x05, 0x07); // fine_x = 7, coarse_x = 0
    ppu.write_register(0x05, 0x00);

    reset_latch(ppu);
    ppu.write_register(0x06, 0x20);
    ppu.write_register(0x06, 0x00);

    ppu.step(1); // 3 PPU dots => pixels x = 0, 1, 2

    const auto& frame = ppu.frame_buffer();
    REQUIRE(frame.pixels[0] == 0xFF000000);
    REQUIRE(frame.pixels[1] == 0xFFFFFEFF);
    REQUIRE(frame.pixels[2] == 0xFFFFFEFF);
}
