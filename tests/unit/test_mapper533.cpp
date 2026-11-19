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
