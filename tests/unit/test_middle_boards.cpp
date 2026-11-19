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

TEST_CASE("09-078 locks the NROM/UNROM selection until reset", "[mapper277][cartridge]") {
    register_builtin_mappers();
    auto cart = Cartridge::from_data(image(277, 16, 0, 0, 7));
    REQUIRE(cart);
    CHECK(cart->read_prg(0x8000) == 8);
    CHECK(cart->read_prg(0xFFFF) == 9);
    cart->write_prg(0x8000, 0x13);
    CHECK(cart->read_prg(0x8000) == 3);
    CHECK(cart->read_prg(0xFFFF) == 7);
    CHECK(cart->mirror_mode() == MirrorMode::Horizontal);
    cart->write_prg(0xFFE0, 0x2B);
    cart->write_prg(0x8000, 0);
    CHECK(cart->read_prg(0x8000) == 11);
    CHECK(cart->read_prg(0xFFFF) == 11);
    cart->reset();
    CHECK(cart->read_prg(0xFFFF) == 9);
}
TEST_CASE("K-3017 resets to the extra chip and distinguishes its two bank wirings",
          "[mapper280][cartridge]") {
    register_builtin_mappers();
    auto rom = image(280, 40, 0, 0, 7);
    for (Byte sub : {0U, 1U}) {
        rom[8] = static_cast<Byte>(1U | sub << 4);
        auto cart = Cartridge::from_data(rom);
        REQUIRE(cart);
        cart->write_chr(1, 5);
        cart->write_prg(0x808B, 0xFF);
        CHECK(cart->read_prg(0x8000) == 2);
        CHECK(cart->read_prg(0xFFFF) == 3);
        cart->write_chr(1, 6);
        CHECK(cart->read_chr(1) == 5);
        cart->reset();
        CHECK(cart->read_prg(0x8000) == 32);
        CHECK(cart->read_prg(0xFFFF) == 39);
        // D0..2 at $C00C are 111 from fixed bank 39, so the conflict passes 5.
        cart->write_prg(0xC00C, 5);
        CHECK(cart->read_prg(0x8000) == (sub ? 35 : 37));
        cart->write_chr(1, 6);
        CHECK(cart->read_chr(1) == 6);
        cart->reset();
        CHECK(cart->read_prg(0x8000) == 0);
    }
}
TEST_CASE("Sheng Tian data latch selects either a mirrored page or an entire PRG pair",
          "[mapper343][cartridge]") {
    register_builtin_mappers();
    for (Byte sub : {0U, 1U}) {
        auto rom = image(343, sub ? 64 : 8, 0, 0, 7);
        rom[8] = static_cast<Byte>(1U | sub << 4);
        auto cart = Cartridge::from_data(rom);
        REQUIRE(cart);
        CHECK(cart->read_prg(0xFFFF) == (sub ? 63 : 7));
        cart->write_prg(0x8000, 2);
        CHECK(cart->read_prg(0x8000) == (sub ? 4 : 2));
        CHECK(cart->read_prg(0xFFFF) == (sub ? 5 : 2));
        cart->reset();
        CHECK(cart->read_prg(0xFFFF) == (sub ? 5 : 2));
    }
}
TEST_CASE("Croaky Karaoke expansion registers control PRG page size and mirroring",
          "[mapper476][cartridge]") {
    register_builtin_mappers();
    auto cart = Cartridge::from_data(image(476, 32, 0, 0, 7));
    REQUIRE(cart);
    cart->write_expansion(0x54FF, 3);
    CHECK(cart->read_prg(0xFFFF) == 3);
    cart->write_expansion(0x5EFF, 5);
    CHECK(cart->read_prg(0x8000) == 6);
    CHECK(cart->read_prg(0xFFFF) == 7);
    CHECK(cart->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("158B protects its lookup port while switching MMC3 and NROM banks",
          "[mapper258][cartridge]") {
    register_builtin_mappers();
    auto cart = Cartridge::from_data(image(258, 8, 8));
    REQUIRE(cart);
    CHECK(cart->read_expansion(0x5006) == 15);
    CHECK(cart->expansion_read_mask(0x5F06) == 15);
    cart->write_expansion(0x5FFA, 1);
    CHECK(cart->read_expansion(0x5006) == 3);
    CHECK(cart->read_expansion(0x5005) == 0);
    cart->write_expansion(0x5000, 0x83);
    CHECK(cart->read_prg(0x8000) == 3);
    CHECK(cart->read_prg(0xFFFF) == 3);
    cart->write_expansion(0x5000, 0xA3);
    CHECK(cart->read_prg(0x8000) == 2);
    CHECK(cart->read_prg(0xFFFF) == 3);
}
TEST_CASE("F-15 gates its discrete PRG latch with the MMC3 WRAM control",
          "[mapper259][cartridge]") {
    register_builtin_mappers();
    auto cart = Cartridge::from_data(image(259, 16, 32));
    REQUIRE(cart);
    cart->write_prg(0xA001, 0x80);
    cart->write_prg(0x7FFF, 3);
    CHECK(cart->read_prg(0x8000) == 3);
    CHECK(cart->read_prg(0xFFFF) == 3);
    cart->write_prg(0x6000, 13);
    CHECK(cart->read_prg(0x8000) == 12);
    CHECK(cart->read_prg(0xFFFF) == 13);
    cart->write_prg(0xA001, 0);
    cart->write_prg(0x7FFF, 4);
    CHECK(cart->read_prg(0x8000) == 12);
    cart->write_prg(0x8000, 2);
    cart->write_prg(0x8001, 16);
    CHECK(cart->read_chr(0x1000) == 2);
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
TEST_CASE("8157 decodes its inverted upper window and menu pad", "[mapper301][cartridge]") {
    register_builtin_mappers();
    auto rom = image(301, 32, 0, 0, 7);
    rom[16 + 1] = 0x75;
    auto c = Cartridge::from_data(rom);
    REQUIRE(c);
    c->write_prg(0x82B6, 0);
    CHECK(c->mirror_mode() == MirrorMode::Horizontal);
    CHECK(c->read_prg(0x8000) == 13);
    CHECK(c->read_prg(0xC000) == 15);
    c->write_prg(0x80B4, 0);
    CHECK(c->read_prg(0xC000) == 8);
    c->write_prg(0x8100, 0);
    REQUIRE(c->configure_board_jumpers(1));
    CHECK(c->read_prg(0x8000) == 0x75);
}
TEST_CASE("5069 locks its outer PRG and mirroring while retaining an inner register",
          "[mapper363][cartridge]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(363, 64, 0, 0, 7));
    REQUIRE(c);
    c->write_prg(0x8035, 6);
    CHECK(c->read_prg(0x8000) == 46);
    CHECK(c->read_prg(0xFFFF) == 47);
    CHECK(c->mirror_mode() == MirrorMode::Horizontal);
    c->write_prg(0x8012, 3);
    CHECK(c->read_prg(0x8000) == 43);
    c->reset();
    CHECK(c->read_prg(0x8000) == 0);
    CHECK(c->mirror_mode() == MirrorMode::Vertical);
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

