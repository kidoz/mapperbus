#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
Cartridge board(unsigned id, Byte prg, Byte chr, Byte wram = 0, Byte vram = 0) {
    register_builtin_mappers();
    std::vector<Byte> image(16U + prg * 16384U + chr * 8192U);
    const std::array<Byte, 12> h{'N',
                                 'E',
                                 'S',
                                 0x1A,
                                 prg,
                                 chr,
                                 static_cast<Byte>((id << 4U) | ((wram & 0xF0U) ? 2U : 0U)),
                                 static_cast<Byte>((id & 0xF0U) | 8U),
                                 static_cast<Byte>(id >> 8U),
                                 0,
                                 wram,
                                 vram};
    std::ranges::copy(h, image.begin());
    for (unsigned i = 0; i < prg * 16384U; ++i)
        image[16U + i] = static_cast<Byte>(i / 8192U);
    for (unsigned i = 0; i < chr * 8192U; ++i)
        image[16U + prg * 16384U + i] = static_cast<Byte>(i / 2048U);
    if (id == 533U) {
        image[16] = 0x9F;
        image[16 + 0x6000U] = 0xB4;
    }
    auto cart = Cartridge::from_data(image);
    INFO((cart ? "" : cart.error()));
    REQUIRE(cart);
    image[8] |= 0xF0U;
    CHECK_FALSE(Cartridge::from_data(image));
    return std::move(*cart);
}
void replay(Cartridge& cart) {
    StateWriter w;
    cart.save_state(w);
    cart.reset();
    StateReader r(w.data());
    cart.load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    StateWriter restored;
    cart.save_state(restored);
    REQUIRE(restored.data() == w.data());
    auto short_state = w.take();
    short_state.pop_back();
    StateReader bad(short_state);
    cart.load_state(bad);
    REQUIRE_FALSE(bad.ok());
    StateWriter after;
    cart.save_state(after);
    REQUIRE(after.data() == restored.data());
}

TEST_CASE("BlazePro reads packed disk sides through the FPGA and arbitrates its two IRQs",
          "[mapper469]") {
    constexpr unsigned prg_size = 8U * 1024U * 1024U;
    std::vector<Byte> image(16U + prg_size);
    const std::array<Byte, 12> header{'N', 'E', 'S', 0x1A, 0, 0, 0x50, 0xD8, 1, 2, 9, 7};
    std::ranges::copy(header, image.begin());
    for (unsigned offset = 0; offset < prg_size; ++offset)
        image[16U + offset] = static_cast<Byte>((offset >> 18U) * 7U + (offset >> 13U));
    register_builtin_mappers();
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    auto& c = *result;
    CHECK(c.read_prg(0x8000) == static_cast<Byte>(31U * 7U + 1020U));
    c.write_expansion(0x5701, 2);
    CHECK(c.read_prg(0x8000) == 8);
    c.write_expansion(0x5700, 0x40);
    CHECK(c.read_prg(0xE000) == 85);
    c.write_prg(0xDFFF, 0x6A);
    CHECK(c.read_prg(0xDFFF) == 0x6A);
    c.write_expansion(0x5110, 3);
    c.write_expansion(0x5100, 0);
    CHECK(c.read_expansion(0x5103) == static_cast<Byte>(3U * 7U + 127U));
    for (unsigned i = 0; i < 5; ++i)
        c.write_expansion(0x5102, 0);
    CHECK(c.read_expansion(0x5103) == static_cast<Byte>(2U * 7U + 92U));
    c.write_expansion(0x5025, 0x88);
    CHECK(c.mirror_mode() == MirrorMode::Horizontal);
    for (unsigned i = 0; i < 149; ++i)
        c.clock_cpu();
    CHECK_FALSE(c.irq_pending());
    c.clock_cpu();
    CHECK(c.irq_pending());
    c.write_expansion(0x5020, 1);
    c.write_expansion(0x5022, 2);
    c.clock_cpu();
    c.clock_cpu();
    CHECK(c.read_expansion(0x5030) == 1);
    CHECK_FALSE(c.irq_pending());
    replay(c);
    image[10] = 0;
    CHECK_FALSE(Cartridge::from_data(image));
}

TEST_CASE("Mapper 204 routes address pins to ROM, mirroring and menu straps", "[mapper204]") {
    for (auto size : {4, 8}) {
        auto c = board(204, size, size == 4 ? 4 : 7);
        REQUIRE(c.configure_board_profile("menu-4660"));
        c.write_prg(0xF004, 0);
        CHECK(c.read_prg(0xC000) == (size == 4 ? 0x34 : 8));
        c.write_prg(0xF008, 0);
        CHECK(c.read_prg(0x8000) == 0x12);
        c.write_prg(0x8033, 0);
        CHECK(c.read_prg(0x8000) == 4);
        CHECK(c.read_prg(0xC000) == 6);
        CHECK(c.read_chr(0) == 8);
        CHECK(c.mirror_mode() == MirrorMode::Horizontal);
        c.write_prg(0x8003, 0x10);
        CHECK(c.read_prg(0x8000) == 6);
        CHECK(c.read_prg(0xC000) == 6);
        CHECK(c.mirror_mode() == MirrorMode::Vertical);
        CHECK(c.read_chr(0) == 12);
        replay(c);
    }
}

TEST_CASE("A971210 decodes both MMC3 and NROM outer banking", "[mapper420]") {
    auto c = board(420, 32, 64);
    c.write_prg(0x6000, 0x86);
    c.write_prg(0x7FFF, 0x20);
    CHECK(c.read_prg(0x8000) == 44);
    CHECK(c.read_prg(0xE000) == 47);
    c.write_prg(0x6000, 0x20);
    c.write_prg(0x6003, 4);
    c.write_prg(0x8000, 6);
    c.write_prg(0x8001, 0x15);
    CHECK(c.read_prg(0x8000) == 37);
    CHECK(c.read_prg(0xE000) == 47);
    c.write_prg(0x6001, 0x84);
    CHECK(c.read_chr(0) == 192);
    replay(c);
}
TEST_CASE("SC871115C wires outer banks to both pattern and nametable ROM", "[mapper421]") {
    auto c = board(421, 64, 128, 7);
    c.write_prg(0xD003, 2);
    CHECK(c.read_prg(0x8000) == 60); // Last 32 KiB of the selected 256 KiB block.
    c.write_prg(0xD000, 0x60);
    c.write_prg(0xB000, 16);
    CHECK(c.read_nametable(0x2000) == 8);
    c.write_prg(0xD003, 1);
    CHECK(c.read_chr(0) == 128);
    CHECK(c.read_nametable(0x2000) == 136);
    replay(c);
}
TEST_CASE("KB-46 menu jumpers and CNROM latch share the low register window", "[mapper428]") {
    auto c = board(428, 8, 8);
    REQUIRE(c.configure_board_jumpers(3));
    CHECK(c.prg_read_mask(0x6000) == 3);
    CHECK(c.read_prg(0x7FFF) == 3);
    c.write_prg(0x7FFD, 0xBF);
    c.write_prg(0x6002, 0xC0);
    c.write_prg(0xFFFF, 2);
    CHECK(c.read_chr(0) == 24);
    CHECK(c.read_prg(0x8000) == 8);
    CHECK(c.read_prg(0xC000) == 10);
    CHECK(c.mirror_mode() == MirrorMode::Horizontal);
    replay(c);
}
TEST_CASE("VQ-168 switches between NROM and fixed-upper PRG", "[mapper459]") {
    auto c = board(459, 16, 16);
    c.write_prg(0x8191, 0);
    CHECK(c.read_prg(0x8000) == 16);
    CHECK(c.read_prg(0xC000) == 30);
    CHECK(c.read_chr(0) == 52);
    CHECK(c.mirror_mode() == MirrorMode::Horizontal);
    c.write_prg(0x8095, 0);
    CHECK(c.read_prg(0xC000) == 18);
    replay(c);
}
TEST_CASE("971107-00G alternates between UNROM and AxROM", "[mapper462]") {
    auto c = board(462, 32, 0, 0, 7);
    c.write_prg(0xA000, 0x30);
    c.write_prg(0xC123, 3);
    CHECK(c.read_prg(0x8000) == 22);
    CHECK(c.read_prg(0xC000) == 30);
    CHECK(c.mirror_mode() == MirrorMode::Vertical);
    c.write_prg(0xBFFF, 0x40);
    c.write_prg(0x8000, 0x11);
    CHECK(c.read_prg(0x8000) == 36);
    CHECK(c.read_prg(0xC000) == 38);
    CHECK(c.mirror_mode() == MirrorMode::SingleUpper);
    replay(c);
}
TEST_CASE("LittleCom PC-95 exposes two outer bank ports and active-low keyboard rows",
          "[mapper531]") {
    auto c = board(531, 64, 0, 7, 7);
    c.write_prg(0x8000, 6);
    c.write_prg(0x8001, 9);
    CHECK(c.read_prg(0x8000) == 105);
    c.write_expansion(0x4C00, 0x40);
    CHECK(c.read_prg(0x8000) == 73);
    c.set_keyboard_row(3, 0x81);
    c.write_expansion(0x4904, 3);
    CHECK(c.read_expansion(0x4906) == 0x7E);
    CHECK(c.read_expansion(0x4C03) == 0);
    replay(c);
    CHECK(c.read_expansion(0x4906) == 0x7E);
}
TEST_CASE("LittleCom 160 uses address pins for PRG and CIRAM", "[mapper541]") {
    auto c = board(541, 64, 0, 0, 7);
    c.write_prg(0xBFFF, 255);
    CHECK(c.read_prg(0x8000) == 0);
    CHECK(c.read_prg(0xC000) == 2);
    c.write_prg(0xC0FF, 0);
    CHECK(c.read_prg(0x8000) == 126);
    CHECK(c.read_prg(0xC000) == 126);
    CHECK(c.mirror_mode() == MirrorMode::Vertical);
    c.write_prg(0xD0FC, 0);
    CHECK(c.read_prg(0x8000) == 124);
    CHECK(c.read_prg(0xC000) == 126);
    CHECK(c.mirror_mode() == MirrorMode::Horizontal);
    replay(c);
}
