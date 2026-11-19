#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

std::vector<Byte> image(unsigned mapper, Byte prg, Byte chr, Byte prg_ram = 0U, Byte chr_ram = 0U) {
    std::vector<Byte> data(16U + prg * 16384U + chr * 8192U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = prg;
    data[5] = chr;
    data[6] = static_cast<Byte>((mapper << 4U) | 1U);
    data[7] = static_cast<Byte>((mapper & 0xF0U) | 8U);
    data[8] = static_cast<Byte>(mapper >> 8U);
    data[10] = prg_ram;
    data[11] = chr_ram;
    for (unsigned i = 0; i < prg; ++i)
        std::fill_n(data.begin() + 16 + 16384 * i, 16384, static_cast<Byte>(i));
    for (unsigned i = 0; i < chr; ++i)
        std::fill_n(data.begin() + 16 + 16384 * prg + 8192 * i, 8192, static_cast<Byte>(i));
    return data;
}

TEST_CASE("Asder PC-95 returns active-low key rows through the cartridge port",
          "[mapper365][cartridge]") {
    register_builtin_mappers();
    auto cart = Cartridge::from_data(image(365, 32, 0, 7, 7));
    REQUIRE(cart);
    for (Byte row = 0; row < 13; ++row)
        cart->set_keyboard_row(row, static_cast<Byte>(row * 17));
    for (unsigned row = 0; row < 256; ++row) {
        cart->write_expansion(0x4904, static_cast<Byte>(row));
        CHECK(cart->read_expansion(0x4906) == (row < 13 ? static_cast<Byte>(~(row * 17)) : 0xFFU));
    }
    cart->write_expansion(0x4904, 5);
    StateWriter writer;
    cart->save_state(writer);
    auto saved = writer.take();
    cart->set_keyboard_row(5, 0);
    StateReader reader(saved);
    cart->load_state(reader);
    REQUIRE(reader.ok());
    CHECK(cart->read_expansion(0x4906) == 0xAA);
}

TEST_CASE("Computer keyboards multiplex serial keys onto D1 and D6 alongside port two",
          "[mapper371][mapper440][mapper442][cartridge]") {
    register_builtin_mappers();
    for (unsigned id : {371, 440, 442}) {
        const unsigned rows = id == 442 ? 11 : 13;
        auto cart = Cartridge::from_data(image(id, id == 371 ? 36 : id == 440 ? 8 : 32, 0, 7, 7));
        REQUIRE(cart);
        Controller pad;
        pad.set_button_state(1, Button::A, true);
        MemoryBus bus;
        bus.connect_controller(&pad);
        bus.connect_cartridge(&*cart);
        cart->set_keyboard_row(0, 0x81);
        cart->set_keyboard_row(1, 0x40);
        bus.write(0x4016, 3);
        bus.write(0x4016, 2);
        REQUIRE((bus.read(0x4017) & 0x43) == 0x43);
        for (unsigned i = 1; i < 7; i++)
            REQUIRE((bus.read(0x4017) & 0x42) == 0);
        CHECK((bus.read(0x4017) & 0x42) == 0x42);
        bus.write(0x4016, 4);
        bus.write(0x4016, 0);
        CHECK((bus.read(0x4017) & 0x42) == 0);
        StateWriter w;
        cart->save_state(w);
        CHECK((bus.read(0x4017) & 0x42) == 0x42);
        StateReader r(w.data());
        cart->load_state(r);
        REQUIRE(r.ok());
        CHECK((bus.read(0x4017) & 0x42) == 0x42);
        for (unsigned i = 1; i < rows; i++) {
            bus.write(0x4016, 5);
            bus.write(0x4016, 0);
        }
        CHECK((bus.read(0x4017) & 0x42) == 0x42);
    }
}
TEST_CASE("Sonic REC-9388 decrypts program lines and substitutes pattern-read pins",
          "[mapper440][cartridge]") {
    register_builtin_mappers();
    auto rom = image(440, 8, 0, 7, 7);
    rom[16 + 0x10000 + 0x800] = 0x96;
    auto c = Cartridge::from_data(rom);
    REQUIRE(c);
    c->write_expansion(0x5800, 0xC1);
    CHECK(c->read_prg(0x8001) == 0x69);
    c->write_expansion(0x5100, 2);
    CHECK(c->mirror_mode() == MirrorMode::Vertical);
    c->write_chr(0x1008, 0x5C);
    c->observe_ppu_read(0x2201);
    c->observe_ppu_read(0x23C0);
    CHECK(c->read_chr(0) == 0x5C);
    CHECK(c->read_expansion(0x5300) == 4);
    CHECK(c->read_expansion(0x5300) == 0);
    c->write_expansion(0x5400, 0x10);
    CHECK(c->expansion_read_mask(0x5500) == 0x2F);
    CHECK(c->read_expansion(0x5500) == 9);
}
TEST_CASE("Golden Key latches pattern pins at rising A13 and decodes PRG A18",
          "[mapper442][cartridge]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(442, 32, 0, 7, 7));
    REQUIRE(c);
    c->write_expansion(0x5000, 0x92);
    CHECK(c->read_prg(0x8000) == 20);
    CHECK(c->read_prg(0xFFFF) == 21);
    c->write_chr(0x1008, 0x35);
    c->observe_ppu_address(0x2201);
    c->observe_ppu_address(0x2000);
    CHECK(c->read_chr(0) == 0x35);
    c->observe_ppu_address(0);
    c->observe_ppu_address(0x2000);
    CHECK(c->read_chr(0) == 0);
    c->write_prg(0x6123, 0x78);
    c->reset();
    CHECK(c->read_prg(0x6123) == 0x78);
    CHECK(c->read_prg(0x8000) == 0);
}
TEST_CASE("Keyboard Transformer latches make and break codes and acknowledges read IRQs",
          "[mapper329][cartridge]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(329, 2, 1, 7));
    REQUIRE(c);
    MemoryBus bus;
    bus.connect_cartridge(&*c);
    c->send_keyboard_scancode(0x9E);
    REQUIRE(c->irq_pending());
    CHECK(bus.read(0x5005) == 0);
    CHECK(c->irq_pending());
    CHECK(bus.read(0x5001) == 9);
    CHECK_FALSE(c->irq_pending());
    CHECK(bus.read(0x5004) == 14);
    c->write_prg(0x6123, 42);
    c->send_keyboard_scancode(0x1E);
    StateWriter w;
    c->save_state(w);
    c->reset();
    StateReader r(w.data());
    c->load_state(r);
    REQUIRE(r.ok());
    CHECK(c->irq_pending());
    CHECK(bus.read(0x5000) == 14);
    CHECK(bus.read(0x5001) == 1);
    CHECK(c->read_prg(0x6123) == 42);
}
TEST_CASE("Spanish PEC system-ROM-only population leaves the empty game socket undriven",
          "[mapper371][cartridge]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(371, 4, 0, 7, 7));
    REQUIRE(c);
    CHECK(c->read_prg(0xC000) == 3);
    c->write_expansion(0x5000, 0x53);
    CHECK_FALSE(c->maps_prg(0x8000));
    CHECK(c->prg_read_mask(0xD100) == 0);
}

