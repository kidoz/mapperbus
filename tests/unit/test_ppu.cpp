// PPU unit tests — exercise the Loopy $2005/$2006/$2007 register model,
// fine-X scroll, and VRAM access patterns documented in ADR-001.

#include <catch2/catch_test_macros.hpp>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"
#include "core/types.hpp"

using namespace mapperbus::core;

namespace {

std::pair<uint16_t, uint16_t> ppu_position(const Ppu& ppu) {
    StateWriter writer;
    ppu.save_state(writer);
    StateReader reader(writer.data());
    const auto line = reader.read<uint16_t>();
    const auto dot = reader.read<uint16_t>();
    return {line, dot};
}

// PPUSTATUS read resets the write latch, per hardware.
void reset_latch(Ppu& ppu) {
    (void)ppu.read_register(0x02);
}

} // namespace

TEST_CASE("Only rendered odd NTSC frames omit a dot", "[ppu][timing]") {
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        for (const Byte mask : {0, 0x08, 0x10, 0x18}) {
            CAPTURE(region, mask);
            Ppu ppu;
            ppu.set_region(region);
            ppu.reset();
            ppu.write_register(1, mask);
            const auto full =
                static_cast<uint32_t>(timing_for_region(region).scanlines_per_frame) * 341;
            ppu.step_dots(full);
            REQUIRE(ppu_position(ppu) == std::pair<uint16_t, uint16_t>{0, 0});
            const bool skip = region == Region::NTSC && mask != 0;
            ppu.step_dots(full - (skip ? 1U : 0U));
            CHECK(ppu_position(ppu) == std::pair<uint16_t, uint16_t>{0, 0});
        }
    }
}

TEST_CASE("Frame parity continues while rendering is disabled", "[ppu][timing]") {
    Ppu ppu;
    ppu.reset();
    ppu.step_dots(341 * 262);
    ppu.write_register(1, 0x08);
    ppu.step_dots(341 * 262 - 1);
    CHECK(ppu_position(ppu) == std::pair<uint16_t, uint16_t>{0, 0});
    ppu.reset();
    ppu.write_register(1, 8);
    ppu.step_dots(341 * 262); // reset restores a full first frame
    CHECK(ppu_position(ppu) == std::pair<uint16_t, uint16_t>{0, 0});
}

TEST_CASE("PPUSTATUS suppression and CPU NMI latching agree around vblank", "[ppu][timing][nmi]") {
    for (const Address address : {0x2002, 0x200A, 0x3FFA}) {
        for (int dot = -2; dot <= 4; ++dot) {
            CAPTURE(address, dot);
            Ppu ppu;
            ppu.reset();
            ppu.write_register(0, 0x80);
            MemoryBus bus;
            bus.connect_ppu(&ppu);
            bus.reset_interrupts();
            ppu.step_dots(static_cast<uint32_t>(241 * 341 + dot - 2));
            bus.begin_cpu_cycle(address);
            const auto status = bus.read(address);
            bus.end_cpu_cycle();
            CHECK(((status & 0x80U) != 0) == (dot >= 1));
            bus.clock_cpu_bus(0, 2);
            const bool expected_nmi = dot < 0 || dot >= 3;
            CHECK(((bus.interrupt_sample() & 2U) != 0) == expected_nmi);
            CHECK(((ppu.read_register(2) & 0x80U) != 0) == (dot < 0));
        }
    }
}

TEST_CASE("Odd-frame skip decisions survive snapshots and ignore later mask writes",
          "[ppu][timing][state]") {
    for (const uint32_t dot : {337U, 338U, 339U}) {
        CAPTURE(dot);
        Ppu ppu;
        ppu.reset();
        ppu.step_dots(341 * 262); // parity toggles on the fully disabled frame
        ppu.write_register(1, 0x08);
        ppu.step_dots(261 * 341 + dot);
        StateWriter writer;
        ppu.save_state(writer);
        ppu.write_register(1, 0); // at 338 or 339 the decision is already retained
        ppu.step_dots(340 - dot);
        const bool skipped = dot >= 338;
        CHECK(ppu_position(ppu) == (skipped ? std::pair<uint16_t, uint16_t>{0, 0}
                                            : std::pair<uint16_t, uint16_t>{261, 340}));
        StateWriter expected;
        ppu.save_state(expected);
        StateReader reader(writer.data());
        ppu.load_state(reader);
        REQUIRE(reader.ok());
        ppu.write_register(1, 0);
        ppu.step_dots(340 - dot);
        StateWriter replay;
        ppu.save_state(replay);
        CHECK(replay.data() == expected.data());
    }
}

TEST_CASE("Old snapshots reconstruct parity from their unshortened frame clock",
          "[ppu][timing][state]") {
    for (const bool odd : {false, true}) {
        Ppu ppu;
        ppu.reset();
        StateWriter writer;
        ppu.save_state(writer);
        auto legacy = writer.take();
        legacy.resize(legacy.size() - 3);
        StateReader reader(legacy);
        ppu.load_state(reader, true, false, odd ? 341 * 262 : 0, false);
        REQUIRE(reader.ok());
        ppu.write_register(1, 8);
        ppu.step_dots(341 * 262 - (odd ? 1 : 0));
        CHECK(ppu_position(ppu) == std::pair<uint16_t, uint16_t>{0, 0});
    }
}

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

TEST_CASE("PPUCTRL only retriggers vblank NMI on an enable edge", "[ppu][nmi]") {
    Ppu ppu;
    ppu.reset();
    ppu.write_register(0x00, 0x80);
    ppu.step(28000);
    REQUIRE(ppu.consume_nmi());
    REQUIRE_FALSE(ppu.consume_nmi());

    // An NMI handler may update the pattern table or nametable selection.
    // Keeping NMI enabled must not recursively interrupt the handler.
    ppu.write_register(0x00, 0x90);
    REQUIRE_FALSE(ppu.consume_nmi());
    ppu.write_register(0x00, 0x80);
    REQUIRE_FALSE(ppu.consume_nmi());

    ppu.write_register(0x00, 0x00);
    ppu.write_register(0x00, 0x80);
    REQUIRE(ppu.consume_nmi());
    (void)ppu.read_register(0x02);
    ppu.write_register(0x00, 0x00);
    ppu.write_register(0x00, 0x80);
    REQUIRE_FALSE(ppu.consume_nmi());
}

TEST_CASE("PPUSTATUS at the dot before vblank suppresses its flag and NMI", "[ppu][nmi]") {
    Ppu ppu;
    ppu.reset();
    ppu.write_register(0x00, 0x80);
    ppu.step_dots(241 * 341); // scanline 241, dot 0
    REQUIRE_FALSE(ppu.consume_nmi());
    REQUIRE((ppu.read_register(0x02) & 0x80U) == 0);
    ppu.step_dots(1);
    REQUIRE_FALSE(ppu.nmi_line());
    REQUIRE((ppu.read_register(0x02) & 0x80U) == 0);
    REQUIRE_FALSE(ppu.consume_nmi());

    ppu.reset();
    ppu.write_register(0x00, 0x80);
    ppu.step(27396);
    (void)ppu.read_register(0x02);
    REQUIRE(ppu.consume_nmi());
}

TEST_CASE("Vblank suppression survives a snapshot taken before the next PPU dot",
          "[ppu][nmi][state]") {
    Ppu ppu;
    ppu.reset();
    ppu.write_register(0, 0x80);
    ppu.step_dots(241 * 341);
    REQUIRE((ppu.read_register(2) & 0x80U) == 0);
    StateWriter writer;
    ppu.save_state(writer);
    Ppu restored;
    StateReader reader(writer.data());
    restored.load_state(reader);
    REQUIRE(reader.ok());
    restored.step_dots(1);
    CHECK_FALSE(restored.nmi_line());
    CHECK((restored.read_register(2) & 0x80U) == 0);
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
    REQUIRE(ppu.read_register(0x04) == 0xC0); // attribute bits 2-4 are unimplemented
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
    ppu.step(6);                    // Let the delayed PPUMASK rendering bits become effective.

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
    // Rendering consumes the logical Loopy scroll address. Use v=0 here so
    // fine Y remains 0 while tile fetches still resolve through $2000.
    ppu.write_register(0x06, 0x00);
    ppu.write_register(0x06, 0x00);

    ppu.step(1); // 3 PPU dots => pixels x = 18, 19, 20 after the delay warmup above.

    const auto& frame = ppu.frame_buffer();
    REQUIRE(frame.pixels[18] == 0xFFFFFEFF);
    REQUIRE(frame.pixels[19] == 0xFFFFFEFF);
    REQUIRE(frame.pixels[20] == 0xFFFFFEFF);
}

TEST_CASE("Dendy PPU starts VBlank at scanline 291, not 241", "[ppu][region][dendy]") {
    Ppu ppu;
    ppu.set_region(Region::Dendy);
    ppu.reset();

    // 29000 CPU cycles = 87000 dots ≈ mid-scanline 255 — past the NTSC VBlank
    // line (241) but still short of the Dendy one (291).
    ppu.step(29000);
    REQUIRE_FALSE(ppu.frame_ready());
    REQUIRE((ppu.read_register(0x02) & 0x80) == 0);

    // Dendy VBlank starts at scanline 291, dot 1 (99232 dots ≈ 33078 CPU
    // cycles); the pre-render line (311) does not clear it before ~35337.
    ppu.step(33100 - 29000);
    REQUIRE(ppu.frame_ready());
    REQUIRE((ppu.read_register(0x02) & 0x80) != 0);
}

TEST_CASE("Hybrid NMI asserts at line 291 dot 1 and clears at line 311 dot 1",
          "[ppu][region][hybrid][nmi]") {
    Ppu ppu;
    ppu.set_region(Region::Dendy);
    ppu.reset();
    ppu.write_register(0, 0x80);
    ppu.step_dots(291 * 341);
    CHECK_FALSE(ppu.nmi_line());
    CHECK_FALSE(ppu.consume_nmi());
    ppu.step_dots(1);
    CHECK(ppu.nmi_line());
    CHECK(ppu.consume_nmi());
    ppu.step_dots(20 * 341 - 1);
    CHECK(ppu.nmi_line());
    ppu.step_dots(1);
    CHECK_FALSE(ppu.nmi_line());
    ppu.step_dots(292 * 341 - 1);
    CHECK_FALSE(ppu.nmi_line());
    ppu.step_dots(1);
    CHECK(ppu.nmi_line());
    CHECK(ppu.consume_nmi());
}

TEST_CASE("Rendered emphasis swaps red and green for PAL and Hybrid", "[ppu][palette][hybrid]") {
    const auto pixel = [](Region region, Byte mask, Byte color) {
        Ppu ppu;
        ppu.set_region(region);
        ppu.reset();
        ppu.write_register(6, 0x3F);
        ppu.write_register(6, 0);
        ppu.write_register(7, color);
        ppu.write_register(6, 0x20);
        ppu.write_register(6, 0);
        ppu.write_register(1, mask);
        ppu.step_dots(1);
        return ppu.frame_buffer().pixels[0];
    };
    // Fixed rendered outputs from the documented 3/4 RGB attenuation model.
    constexpr std::array<std::uint32_t, 8> kNtscWhite = {
        0xFFFFFEFF,
        0xFFFFBEBF,
        0xFFBFFEBF,
        0xFFBFBE8F,
        0xFFBFBEFF,
        0xFFBF8EBF,
        0xFF8FBEBF,
        0xFF8F8E8F,
    };
    constexpr std::array<unsigned, 8> kPalOrder = {0, 2, 1, 3, 4, 6, 5, 7};
    for (unsigned emphasis = 0; emphasis < 8; ++emphasis) {
        const auto mask = static_cast<Byte>(emphasis << 5);
        CAPTURE(emphasis);
        CHECK(pixel(Region::NTSC, mask, 0x20) == kNtscWhite[emphasis]);
        CHECK(pixel(Region::Multi, mask, 0x20) == kNtscWhite[emphasis]);
        CHECK(pixel(Region::PAL, mask, 0x20) == kNtscWhite[kPalOrder[emphasis]]);
        CHECK(pixel(Region::Dendy, mask, 0x20) == kNtscWhite[kPalOrder[emphasis]]);
    }
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        CHECK(pixel(region, 0, 0x16) == 0xFFB53120);
        CHECK(pixel(region, 1, 0x16) == 0xFFADADAD);    // grayscale masks the palette index
        CHECK(pixel(region, 0x81, 0x16) == 0xFF8181AD); // tint still applies to gray
    }
}

TEST_CASE("PPU I/O latch is separate from its VRAM read buffer", "[ppu][bus]") {
    Ppu ppu;
    ppu.write_register(6, 0x20);
    ppu.write_register(6, 0);
    ppu.write_register(7, 0x7F);
    ppu.write_register(6, 0x20);
    ppu.write_register(6, 0);
    CHECK(ppu.read_register(7) == 0);
    ppu.write_register(2, 0xA5); // even a read-only port loads the latch
    CHECK(ppu.read_register(0) == 0xA5);
    CHECK((ppu.read_register(2) & 0x1FU) == 5); // not the read buffer's $1F
    CHECK(ppu.read_register(5) == 5);
    CHECK(ppu.read_register(7) == 0x7F);
    CHECK(ppu.read_register(6) == 0x7F);
    StateWriter writer;
    ppu.save_state(writer);
    ppu.write_register(0, 0);
    StateReader reader(writer.data());
    ppu.load_state(reader);
    REQUIRE(reader.ok());
    CHECK(ppu.read_register(0) == 0x7F);
}

TEST_CASE("Palette reads merge latch high bits and apply the greyscale mask", "[ppu][bus]") {
    Ppu ppu;
    ppu.write_register(6, 0x3F);
    ppu.write_register(6, 0);
    ppu.write_register(7, 0x2F);
    ppu.write_register(6, 0x3F);
    ppu.write_register(6, 0);
    ppu.write_register(2, 0xC0);
    CHECK(ppu.read_register(7) == 0xEF);
    ppu.write_register(1, 1);
    ppu.write_register(6, 0x3F);
    ppu.write_register(6, 0);
    ppu.write_register(2, 0x80);
    CHECK(ppu.read_register(7) == 0xA0);
}
