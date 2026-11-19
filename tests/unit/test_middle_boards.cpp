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

TEST_CASE("Mapper 297 registry routes the expansion mode bit to both ROM chips",
          "[mapper297][cartridge]") {
    register_builtin_mappers();
    auto rom = image(297, 16, 32);
    rom[6] &= 0xFEU; // The known NES 2.0 dump uses H metadata on this fixed-V PCB.
    auto cart = Cartridge::from_data(rom);
    REQUIRE(cart);
    REQUIRE(cart->read_prg(0x8000) == 0);
    REQUIRE(cart->read_prg(0xFFFF) == 3);
    REQUIRE(cart->read_chr(0) == 0);
    cart->write_expansion(0x4120, 1);
    CHECK(cart->read_prg(0x8000) == 8);
    CHECK(cart->read_prg(0xFFFF) == 15);
    CHECK(cart->read_chr(0) == 16);
    CHECK(cart->mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("Mapper 352 reset clocks whole PRG/CHR games together and retains header CIRAM",
          "[mapper352][cartridge]") {
    register_builtin_mappers();
    auto rom = image(352, 8, 4);
    for (Byte sub : {0U, 1U}) {
        rom[8] = static_cast<Byte>((sub << 4U) | 1U);
        for (Byte mirror : {0U, 1U}) {
            rom[6] = static_cast<Byte>((rom[6] & 0xFEU) | mirror);
            auto cart = Cartridge::from_data(rom);
            REQUIRE(cart);
            for (unsigned i = 0; i < 8; ++i) {
                CAPTURE(i);
                CHECK(cart->read_prg(0x8000) == (i & 3U) * 2);
                CHECK(cart->read_prg(0xFFFF) == (i & 3U) * 2 + 1);
                CHECK(cart->read_chr(0x1FFF) == (i & 3U));
                CHECK(cart->mirror_mode() ==
                      ((sub ? mirror : (i & 1U)) ? MirrorMode::Vertical : MirrorMode::Horizontal));
                cart->reset();
            }
            cart->reset();
            StateWriter writer;
            cart->save_state(writer);
            auto saved = writer.take();
            cart->power_on();
            REQUIRE(cart->read_prg(0x8000) == 0);
            StateReader reader(saved);
            cart->load_state(reader);
            REQUIRE(reader.ok());
            CHECK(cart->read_prg(0x8000) == 2);
        }
    }
    rom[5] = 2;
    REQUIRE_FALSE(Cartridge::from_data(rom));
}

TEST_CASE("KS-7017 stages each PRG selector and clocks its one-shot IRQ",
          "[mapper303][cartridge]") {
    register_builtin_mappers();
    const auto rom = image(303, 8, 0, 7, 7);
    auto cart = Cartridge::from_data(rom);
    REQUIRE(cart);
    for (unsigned page = 0; page < 8; ++page) {
        cart->write_expansion(static_cast<Address>(0x4A00 | ((page & 4) << 4) | ((page & 3) << 2)),
                              0);
        if (page)
            REQUIRE(cart->read_prg(0x8000) == page - 1);
        cart->write_expansion(0x51FF, 0);
        CHECK(cart->read_prg(0xBFFF) == page);
        CHECK(cart->read_prg(0xFFFF) == 2);
    }
    cart->write_chr(0x1FFF, 78);
    cart->write_prg(0x7FFF, 31);
    cart->write_expansion(0x4025, 8);
    CHECK(cart->mirror_mode() == MirrorMode::Horizontal);
    cart->write_expansion(0x4020, 3);
    cart->write_expansion(0x4021, 0);
    cart->clock_cpu();
    StateWriter writer;
    cart->save_state(writer);
    auto saved = writer.take();
    for (int attempt = 0; attempt < 2; ++attempt) {
        cart->clock_cpu();
        REQUIRE_FALSE(cart->irq_pending());
        cart->clock_cpu();
        REQUIRE(cart->irq_pending());
        CHECK(cart->read_expansion(0x4030) == 1);
        CHECK_FALSE(cart->irq_pending());
        cart->power_on();
        StateReader reader(saved);
        cart->load_state(reader);
        REQUIRE(reader.ok());
        CHECK(cart->read_prg(0x7FFF) == 31);
        CHECK(cart->read_chr(0x1FFF) == 78);
        CHECK(cart->read_prg(0x8000) == 7);
    }
    saved.resize(saved.size() - 1);
    StateReader truncated(saved);
    cart->load_state(truncated);
    REQUIRE_FALSE(truncated.ok());
    CHECK(cart->read_prg(0xBFFF) == 7);
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

TEST_CASE("Haratyler accepts a 32 KiB CHR RAM chip and exposes the two wired banks",
          "[mapper451][cartridge]") {
    register_builtin_mappers();
    auto cart = Cartridge::from_data(image(451, 32, 0, 0, 9));
    REQUIRE(cart);
    REQUIRE(cart->has_battery());
    CHECK(cart->battery_ram().size() == 512U * 1024);
    cart->write_chr(0x1FFF, 0x6A);
    cart->write_prg(0xE001, 0);
    cart->write_chr(0x1FFF, 0xB8);
    cart->write_prg(0xE002, 0);
    CHECK(cart->read_chr(0x1FFF) == 0x6A);
    cart->write_prg(0xE003, 0);
    CHECK(cart->read_chr(0x1FFF) == 0xB8);
}
TEST_CASE("LH42 banks program ROM while R5 controls CIRAM independently of CHR RAM",
          "[mapper418][cartridge]") {
    register_builtin_mappers();
    auto cart = Cartridge::from_data(image(418, 8, 0, 0, 7));
    REQUIRE(cart);
    cart->write_chr(0x1FFF, 0xA5);
    for (Byte page = 0; page < 16; ++page) {
        cart->write_prg(0x9FFE, 6);
        cart->write_prg(0x9FFF, page);
        CHECK(cart->read_prg(0x8000) == page / 2U);
        CHECK(cart->read_prg(0xFFFF) == 7);
        cart->write_prg(0x8000, 5);
        cart->write_prg(0x8001, page);
        CHECK(cart->read_chr(0x1FFF) == 0xA5);
        CHECK(cart->mirror_mode() == ((page & 1U) ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
}

TEST_CASE("BATMAP reads and advances the serial ROM through both CPU windows",
          "[mapper413][cartridge]") {
    register_builtin_mappers();
    auto rom = image(413, 16, 32);
    const auto start = rom.size();
    rom[14] = 1;
    REQUIRE_FALSE(Cartridge::from_data(rom));
    rom.resize(start + (8U << 20));
    rom[start + 3] = 0xC2;
    rom[start + 4] = 0xB7;
    auto cart = Cartridge::from_data(rom);
    REQUIRE(cart);
    cart->write_prg(0xC000, 0x80);
    cart->write_prg(0xC000, 0x80);
    CHECK(cart->read_expansion(0x4ABC) == 0xC2);
    cart->write_prg(0xD000, 2);
    CHECK(cart->read_prg(0xCFFF) == 0xC2);
    CHECK(cart->read_expansion(0x4800) == 0xB7);
    cart->write_prg(0xE000, 10);
    CHECK(cart->read_prg(0x6000) == 5);
    cart->write_prg(0xE000, 0xC6);
    CHECK(cart->read_chr(0x0FFF) == 3);
    CHECK(cart->read_chr(0x1000) == 30);
    cart->write_prg(0x8000, 1);
    cart->write_prg(0xB000, 0);
    for (int rise = 0; rise < 2; ++rise) {
        cart->observe_ppu_a12(false);
        for (int cycle = 0; cycle < 5; ++cycle)
            cart->clock_cpu();
        cart->observe_ppu_a12(true);
        CHECK(cart->irq_pending() == (rise == 1));
    }
    cart->write_prg(0xA000, 0);
    CHECK_FALSE(cart->irq_pending());
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
TEST_CASE("Historical Realtec 8031 latches CHR and PRG independently", "[mapper390][cartridge]") {
    register_builtin_mappers();
    auto rom = image(390, 8, 8);
    rom[16 + 3] = 67;
    auto c = Cartridge::from_data(rom);
    REQUIRE(c);
    c->write_prg(0x8026, 0);
    CHECK(c->mirror_mode() == MirrorMode::Horizontal);
    CHECK(c->read_chr(0) == 6);
    c->write_prg(0xC025, 0);
    CHECK(c->read_prg(0x8000) == 4);
    CHECK(c->read_prg(0xC001) == 5);
    REQUIRE(c->configure_board_jumpers(1));
    c->write_prg(0xC010, 0);
    CHECK(c->read_prg(0x8002) == 67);
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
TEST_CASE("CoolX Lite board switches among MMC3, UNROM and NROM windows",
          "[mapper479][cartridge]") {
    register_builtin_mappers();
    auto rom = image(479, 16, 16, 7);
    rom[8] |= 0x10;
    auto c = Cartridge::from_data(rom);
    REQUIRE(c);
    c->write_prg(0x8000, 6);
    c->write_prg(0x8001, 4);
    CHECK(c->read_prg(0x8000) == 2);
    CHECK(c->read_prg(0xE000) == 15);
    c->write_prg(0xA000, 1);
    CHECK(c->mirror_mode() == MirrorMode::SingleUpper);
    c->write_prg(0xA000, 2);
    CHECK(c->mirror_mode() == MirrorMode::Vertical);
    c->write_prg(0x8000, 2);
    c->write_prg(0x8001, 24);
    CHECK(c->read_chr(0x1000) == 3);
    c->write_expansion(0x4FF3, 8);
    c->write_prg(0xE000, 5);
    CHECK(c->read_prg(0x8000) == 5);
    CHECK(c->read_prg(0xC000) == 15);
    c->write_expansion(0x4FF3, 6);
    c->write_prg(0xC000, 0x12);
    CHECK(c->read_prg(0x8000) == 4);
    CHECK(c->mirror_mode() == MirrorMode::SingleUpper);
    c->write_expansion(0x4FF3, 0);
    c->write_prg(0x8000, 0x11);
    CHECK(c->read_prg(0x8000) == 2);
    c->write_prg(0x6000, 71);
    StateWriter w;
    c->save_state(w);
    c->reset();
    StateReader r(w.data());
    c->load_state(r);
    REQUIRE(r.ok());
    CHECK(c->read_prg(0x8000) == 2);
    CHECK(c->read_prg(0x6000) == 71);
    rom = image(479, 16, 0, 0, 7);
    rom[8] |= 0x20;
    c = Cartridge::from_data(rom);
    REQUIRE(c);
    c->write_chr(0x1000, 0x5C);
    c->write_expansion(0x4FF3, 1);
    c->write_chr(0x1000, 0xAB);
    CHECK(c->read_chr(0x1000) == 0x5C);
}

TEST_CASE("BlazePro maps its ROM disk sides and clocks its FDS-compatible IRQs",
          "[mapper469][cartridge]") {
    register_builtin_mappers();
    auto rom = image(469, 0, 0, 9, 7);
    rom[9] = 2;
    rom.resize(16 + 8 * 1024 * 1024);
    rom[16 + 0x7F8000] = 0x85;
    rom[16 + 0x38000] = 0x41;
    rom[16 + 0x7FFFB] = 0x65;
    auto c = Cartridge::from_data(rom);
    REQUIRE(c);
    CHECK(c->read_prg(0x8000) == 0x85);
    CHECK_FALSE(c->maps_prg(0x6000));
    c->write_expansion(0x5700, 0x40);
    c->write_expansion(0x5110, 2);
    c->write_expansion(0x5100, 0);
    CHECK(c->read_expansion(0x5103) == 0x65);
    for (int i = 0; i < 5; ++i)
        c->write_expansion(0x5102, 0);
    CHECK(c->read_expansion(0x5103) == 0x41);
    c->write_prg(0xDFFF, 0x54);
    CHECK(c->read_prg(0xDFFF) == 0x54);
    c->write_expansion(0x5025, 0x88);
    CHECK(c->mirror_mode() == MirrorMode::Horizontal);
    for (int i = 0; i < 149; ++i)
        c->clock_cpu();
    CHECK_FALSE(c->irq_pending());
    c->write_expansion(0x5020, 0);
    c->write_expansion(0x5022, 2);
    c->clock_cpu();
    CHECK(c->irq_pending());
    CHECK(c->read_expansion(0x5030) == 1);
    CHECK_FALSE(c->irq_pending());
    for (int i = 0; i < 149; ++i)
        c->clock_cpu();
    CHECK(c->read_expansion(0x5030) == 2);
    c->write_expansion(0x5110, 255);
    CHECK(c->expansion_read_mask(0x5103) == 0);
}

TEST_CASE("SMB2JX preserves the historical 311 ROM packing and one-shot IRQ",
          "[mapper311][cartridge]") {
    register_builtin_mappers();
    auto rom = image(311, 5, 1);
    rom[16 + 17 * 4096] = 0x77;
    rom[16 + 9 * 8192] = 0x85;
    auto c = Cartridge::from_data(rom);
    REQUIRE(c);
    CHECK(c->read_expansion(0x5000) == 0x77);
    CHECK(c->read_prg(0x6000) == 0x85);
    CHECK(c->read_expansion(0x4042) == 0xFF);
    CHECK(c->expansion_read_mask(0x4056) == 0);
    c->write_expansion(0x4022, 1);
    CHECK(c->read_prg(0x8000) == 2);
    CHECK(c->read_prg(0xFFFF) == 3);
    c->write_expansion(0x4122, 1);
    for (unsigned i = 0; i < 4096; i++)
        c->clock_cpu();
    REQUIRE_FALSE(c->irq_pending());
    StateWriter w;
    c->save_state(w);
    c->clock_cpu();
    CHECK(c->irq_pending());
    StateReader r(w.data());
    c->load_state(r);
    REQUIRE(r.ok());
    c->clock_cpu();
    CHECK(c->irq_pending());
    c->write_expansion(0x4122, 0);
    CHECK_FALSE(c->irq_pending());
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

TEST_CASE("N42S-2 switches between 16K, 32K and MMC3 PRG while retaining flat CHR RAM",
          "[mapper536][cartridge]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(536, 64, 0, 0, 7));
    REQUIRE(c);
    CHECK(c->read_prg(0x8000) == 32);
    CHECK(c->read_prg(0xFFFF) == 33);
    c->write_prg(0x6000, 3);
    CHECK(c->read_prg(0x8000) == 32);
    c->write_prg(0xA001, 0x80);
    c->write_prg(0x7FFE, 3);
    CHECK(c->read_prg(0x8000) == 3);
    CHECK(c->read_prg(0xFFFF) == 3);
    c->write_prg(0x6000, 0x25);
    CHECK(c->read_prg(0x8000) == 4);
    CHECK(c->read_prg(0xC000) == 5);
    c->write_prg(0x6000, 0x81);
    c->write_prg(0x8000, 6);
    c->write_prg(0x8001, 2);
    CHECK(c->read_prg(0x8000) == 3);
    CHECK(c->read_prg(0xE000) == 7);
    c->write_prg(0xA001, 0xC0);
    c->write_prg(0x6000, 0);
    CHECK(c->read_prg(0x8000) == 3);
    CHECK(c->prg_read_mask(0x7000) == 0);
    c->write_chr(0x1000, 0x25);
    c->write_prg(0x8000, 2);
    c->write_prg(0x8001, 7);
    CHECK(c->read_chr(0x1000) == 0x25);
    c->write_prg(0xC000, 1);
    c->write_prg(0xE001, 0);
    c->clock_irq_counter();
    CHECK_FALSE(c->irq_pending());
    c->clock_irq_counter();
    CHECK(c->irq_pending());
    c->reset();
    CHECK(c->read_prg(0x8000) == 32);
    CHECK(c->read_chr(0x1000) == 0x25);
}
TEST_CASE("JY4M4 applies both outer registers to MMC3 switched and fixed slots",
          "[mapper537][cartridge]") {
    register_builtin_mappers();
    for (Byte prg : {24U, 32U}) {
        auto c = Cartridge::from_data(image(537, prg, 64));
        REQUIRE(c);
        CHECK(c->read_prg(0xE000) == 15);
        CHECK(c->read_chr(0) == 0);
        c->write_prg(0xA001, 0x80);
        c->write_prg(0x6000, 0xC0);
        c->write_prg(0x8000, 6);
        c->write_prg(0x8001, 4);
        CHECK(c->read_prg(0x8000) == 18);
        CHECK(c->read_prg(0xE000) == 23);
        c->write_prg(0x8000, 2);
        c->write_prg(0x8001, 16);
        CHECK(c->read_chr(0x1000) == 34);
        c->write_prg(0x6001, 1);
        if (prg == 32) {
            CHECK(c->read_prg(0x8000) == 26);
            CHECK(c->read_prg(0xE000) == 31);
        } else
            CHECK(c->prg_read_mask(0x8000) == 0);
        c->write_prg(0xA000, 1);
        CHECK(c->mirror_mode() == MirrorMode::Horizontal);
        StateWriter w;
        c->save_state(w);
        c->reset();
        CHECK(c->read_chr(0x1000) == 2);
        StateReader r(w.data());
        c->load_state(r);
        REQUIRE(r.ok());
        CHECK(c->read_chr(0x1000) == 34);
    }
    REQUIRE_FALSE(Cartridge::from_data(image(537, 16, 64)));
}

} // namespace
