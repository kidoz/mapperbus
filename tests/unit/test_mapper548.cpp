#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

// PRG bytes identify their 8 KiB bank; CHR bytes identify their 1 KiB bank.
Cartridge cartridge(Word mapper,
                    unsigned prg_kib,
                    unsigned chr_kib,
                    Byte flags,
                    Byte cpu_ram,
                    Byte ppu_ram,
                    Byte submapper = 0,
                    std::span<const Byte> misc = {}) {
    register_builtin_mappers();
    std::vector<Byte> image(16U + (prg_kib + chr_kib) * 1024U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1A;
    image[4] = prg_kib / 16;
    image[5] = chr_kib / 8;
    image[6] = static_cast<Byte>((mapper << 4U) | flags);
    image[7] = (mapper & 0xF0U) | 8U;
    image[8] = (submapper << 4U) | (mapper >> 8U);
    image[10] = cpu_ram;
    image[11] = ppu_ram;
    for (unsigned i = 0; i < prg_kib * 1024U; ++i)
        image[16 + i] = static_cast<Byte>(i / 8192U);
    for (unsigned i = 0; i < chr_kib * 1024U; ++i)
        image[16 + prg_kib * 1024U + i] = static_cast<Byte>(i / 1024U);
    if (!misc.empty()) {
        image[14] = 1;
        image.insert(image.end(), misc.begin(), misc.end());
    }
    auto result = Cartridge::from_data(image);
    INFO((result ? "" : result.error()));
    REQUIRE(result);
    image[8] |= 0xF0U;
    CHECK_FALSE(Cartridge::from_data(image));
    return std::move(*result);
}

void snapshot(Cartridge& board) {
    StateWriter writer;
    board.save_state(writer);
    StateReader truncated(std::span{writer.data()}.first(writer.data().size() - 1));
    board.load_state(truncated);
    CHECK_FALSE(truncated.ok());
    StateWriter after;
    board.save_state(after);
    CHECK(std::ranges::equal(writer.data(), after.data()));
    board.reset();
    StateReader reader(writer.data());
    board.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0);
    StateWriter restored;
    board.save_state(restored);
    CHECK(std::ranges::equal(writer.data(), restored.data()));
}

TEST_CASE(
    "Historical mapper IDs select a circuit only for a catalogued chip pair",
    "[mapper39][mapper54][mapper179][mapper181][mapper220][mapper223][mapper248][mapper251]") {
    register_builtin_mappers();
    struct Sample {
        Word id;
        unsigned prg_kib, chr_kib;
        std::uint32_t prg_suffix, chr_suffix;
    };
    // Each test chip contains its bank number throughout, then four generated
    // CRC check bytes. These fixtures exercise the real identification path
    // without shipping any of the games' program or character data.
    constexpr Sample samples[] = {
        {39, 1024, 0, 0xD88F3E28, 0x00000000},
        {39, 1024, 0, 0x7DC74A4F, 0x00000000},
        {54, 128, 64, 0x0B179769, 0xEEE56ECA},
        {54, 128, 64, 0x979B263A, 0xF6380F16},
        {179, 512, 0, 0x4AF28D05, 0x00000000},
        {181, 32, 8, 0xB05BCACD, 0x089695B6},
        {220, 128, 0, 0xFD5FA86B, 0x00000000},
        {220, 256, 128, 0x99EAB7D3, 0x559A3D9D},
        {223, 512, 0, 0xA6B2D3D0, 0x00000000},
        {248, 256, 256, 0xD9064407, 0x1DCA1A22},
        {251, 256, 512, 0x5E495096, 0x870514C2},
    };
    for (const auto& s : samples) {
        CAPTURE(s.id, s.prg_suffix);
        std::vector<Byte> image(16 + (s.prg_kib + s.chr_kib) * 1024U);
        image[0] = 'N';
        image[1] = 'E';
        image[2] = 'S';
        image[3] = 0x1a;
        image[4] = static_cast<Byte>(s.prg_kib / 16);
        image[5] = static_cast<Byte>(s.chr_kib / 8);
        image[6] = static_cast<Byte>(s.id << 4);
        if (s.id == 179 || s.id == 223 || s.id == 251)
            image[6] |= 2;
        image[7] = s.id & 0xf0;
        const auto chip = [](std::span<Byte> data, unsigned granularity, std::uint32_t suffix) {
            if (data.empty())
                return;
            for (unsigned i = 0; i < data.size(); ++i)
                data[i] = static_cast<Byte>(i / granularity);
            for (unsigned i = 0; i < 4; ++i)
                data[data.size() - 4 + i] = static_cast<Byte>(suffix >> (8 * i));
        };
        const auto prg = std::span{image}.subspan(16, s.prg_kib * 1024U);
        const auto chr = std::span{image}.subspan(16 + s.prg_kib * 1024U);
        chip(prg, 8192, s.prg_suffix);
        chip(chr, 1024, s.chr_suffix);
        auto board = Cartridge::from_data(image);
        INFO((board ? "" : board.error()));
        REQUIRE(board);
        CHECK(board->header().mapper_number == s.id);
        board->power_on();
        if (s.id == 39) {
            board->write_prg(0x8000, 17);
            CHECK(board->read_prg(0x8000) == 68);
            CHECK(board->mirror_mode() == MirrorMode::Vertical);
        } else if (s.id == 54) {
            board->write_prg(0x8043, 0);
            if (s.prg_suffix == samples[2].prg_suffix) {
                CHECK(board->read_prg(0x8000) == 6); // 58: 16 KiB bank 3 mirrored
                CHECK(board->read_prg(0xC000) == 6);
            } else {
                CHECK(board->read_prg(0x8000) == 12); // 201: 32 KiB bank 3
                CHECK(board->read_prg(0xC000) == 14);
            }
        } else if (s.id == 179) {
            REQUIRE(board->battery_ram().size() == 8192U);
            board->write_expansion(0x5010, 0x24);
            board->write_expansion(0x5011, 10);
            CHECK(board->read_prg(0x8000) == 20);
            board->write_prg(0x6000, 0xe3);
            CHECK(board->battery_ram()[0] == 0xe3);
            board->write_prg(0xA001, 0xC0);
            board->write_prg(0x6000, 1);
            CHECK(board->read_prg(0x6000) == 0xe3);
        } else if (s.id == 181) {
            board->write_prg(0xA000, 1); // ROM bank 1 supplies D0 for the bus conflict
            CHECK(board->read_chr(0x42) == 0x42);
            board->write_prg(0xA000, 0);
            CHECK(board->read_chr(0x42) == 0);
        } else if (s.id == 220) {
            if (s.chr_kib == 0) {
                board->write_chr(0x1500, 0xA1);
                CHECK(board->read_chr(0x1500) == 0xA1);
            } else {
                CHECK(board->read_prg(0xE000) == 31);
                board->write_chr(0x1500, 0xA1);
                CHECK(board->read_chr(0x1500) != 0xA1);
            }
        } else if (s.id == 223) {
            CHECK(board->battery_ram().size() == 32768U);
            board->write_prg(0x8000, 6);
            board->write_prg(0x8001, 24);
            CHECK(board->read_prg(0x8000) == 24);
        } else if (s.id == 251) {
            CHECK(board->battery_ram().size() == 8192U);
            board->write_prg(0x8010, 12);
            CHECK(board->read_prg(0x8000) == 12);
        } else {
            board->write_prg(0x8000, 6);
            board->write_prg(0x8001, 18);
            CHECK(board->read_prg(0x8000) == 18);
        }
        snapshot(*board);
        Emulator emu;
        REQUIRE(emu.load_cartridge(std::move(*board), {}));
        emu.power_on();
        CHECK(emu.load_state(emu.save_state()));
        // A near match on either chip must not select the board.
        image.back() ^= 1;
        CHECK_FALSE(Cartridge::from_data(image));
        image.back() ^= 1;
        image[16] ^= 1;
        CHECK_FALSE(Cartridge::from_data(image));
    }
}

TEST_CASE("ST-80 routes MMC3 pins to the menu, the large chip and the small chip", "[mapper545]") {
    auto c = cartridge(545, 640, 512, 0, 0, 0);
    CHECK(c.read_prg(0x8000) == 0);
    CHECK(c.read_prg(0xE000) == 15);
    CHECK(c.read_chr(0) == 64);
    CHECK_FALSE(c.maps_prg(0x6000));
    c.write_prg(0xF120, 15);
    CHECK(c.read_prg(0x8000) == 64);
    CHECK(c.read_prg(0xE000) == 63);
    CHECK(c.read_chr(0) == 128);
    c.write_prg(0x8000, 6);
    c.write_prg(0x8001, 0x12);
    CHECK(c.read_prg(0x8000) == 50);
    c.write_prg(0xC000, 1);
    c.write_prg(0xC001, 0);
    c.write_prg(0xE001, 0);
    c.clock_irq_counter();
    c.clock_irq_counter();
    CHECK(c.irq_pending());
    snapshot(c);
}

TEST_CASE("Co Tung CTC-15 latches address pins separately from bank selection and IRQ",
          "[mapper548]") {
    auto c = cartridge(548, 128, 0, 0, 7, 7);
    CHECK(c.read_prg(0x8000) == 14);
    CHECK(c.read_prg(0xC000) == 6);
    c.write_expansion(0x4828, 0);
    CHECK(c.read_prg(0x8000) == 14);
    c.write_expansion(0x57FF, 0xFF);
    CHECK(c.read_prg(0x8000) == 6);
    for (unsigned i = 0; i < 23679; ++i)
        c.clock_cpu_falling_edge();
    CHECK_FALSE(c.irq_pending());
    c.clock_cpu_falling_edge();
    CHECK(c.irq_pending());
    c.write_prg(0x6000, 0x37);
    c.write_chr(0x1FFF, 0x91);
    snapshot(c);
    CHECK(c.read_prg(0x6000) == 0x37);
    CHECK(c.read_chr(0x1FFF) == 0x91);
    for (unsigned i = 0; i < 640; ++i)
        c.clock_cpu_falling_edge();
    CHECK_FALSE(c.irq_pending());
    c.write_expansion(0x4804, 0);
    for (unsigned i = 0; i < 23680; ++i)
        c.clock_cpu_falling_edge();
    CHECK_FALSE(c.irq_pending());
}

TEST_CASE("JY820845C observes MMC1 writes during MHROM banking and locks its game selection",
          "[mapper550]") {
    auto c = cartridge(550, 512, 128, 0, 7, 0);
    c.write_prg(0x7003, 0xCA);
    CHECK(c.read_prg(0x7003) == 0xCA);
    c.write_prg(0x8000, 0x13);
    CHECK(c.read_prg(0x8000) == 28);
    CHECK(c.read_chr(0) == 56);
    c.write_prg(0x8000, 0x80);
    for (auto bit : {0, 1, 0, 0, 0})
        c.write_prg(0x8000, bit);
    CHECK(c.mirror_mode() == MirrorMode::Vertical);
    c.write_prg(0x700E, 0);
    c.write_prg(0x7000, 0xDF);
    CHECK(c.read_prg(0x8000) == 48);
    CHECK(c.read_prg(0x7000) == 0xDF);
    for (auto bit : {0, 1, 1, 0, 0})
        c.write_prg(0xE000, bit);
    CHECK(c.read_prg(0x8000) == 60);
    CHECK(c.read_chr(0) == 96);
    snapshot(c);
}

TEST_CASE("Sachen 3013 drives a constant lower PRG window", "[mapper553]") {
    auto c = cartridge(553, 16, 8, 1, 0, 0);
    CHECK_FALSE(c.maps_prg(0x7FFF));
    CHECK(c.read_prg(0x8000) == 0x3A);
    CHECK(c.read_prg(0xBFFF) == 0x3A);
    CHECK(c.read_prg(0xC000) == 0);
    CHECK(c.read_prg(0xFFFF) == 1);
    c.write_chr(0x1FFF, 0x80);
    CHECK(c.read_chr(0x1FFF) == 7);
    snapshot(c);
}

TEST_CASE("KS-7010 read strobes switch the low PRG window and CHR together", "[mapper554]") {
    auto c = cartridge(554, 128, 128, 1, 0, 0);
    for (Address addr : {0xCAB6, 0xCAD6, 0xEE32, 0xEBE2, 0xFFFC}) {
        c.observe_cpu_read(addr, c.read_prg(addr));
        auto bank = (addr >> 2U) & 15U;
        CHECK(c.read_prg(0x6000) == bank);
        CHECK(c.read_chr(0) == bank * 8);
        CHECK(c.read_prg(0x8000) == 10);
        CHECK(c.read_prg(0xA000) == 11);
        CHECK(c.read_prg(0xC000) == 6);
        CHECK(c.read_prg(0xE000) == 7);
    }
    c.write_prg(0xEE32, 0);
    c.observe_cpu_read(0xCAD7, 0);
    CHECK(c.read_prg(0x6000) == 15);
    snapshot(c);
}

TEST_CASE("YC-03-09 scrambles bank writes and selects EEPROM or persistent WRAM", "[mapper558]") {
    for (Byte nvram : {0x30, 0x70}) {
        auto c = cartridge(558, 2048, 0, 3, nvram, 7);
        CHECK(c.read_prg(0x8000) == 12);
        CHECK(c.mirror_mode() == MirrorMode::Vertical);
        c.write_expansion(0x5300, 4);
        c.write_expansion(0x5000, 1);
        CHECK(c.read_prg(0x8000) == 8);
        c.write_expansion(0x5300, 5);
        CHECK(c.read_prg(0x8000) == 8);
        c.write_expansion(0x55FF, 1);
        CHECK(c.read_prg(0x8000) == 72);
        if (nvram == 0x70) {
            c.write_prg(0x6000, 0xB2);
            c.power_on();
            CHECK(c.read_prg(0x6000) == 0xB2);
            CHECK(c.battery_ram().size() == 8192);
            CHECK(c.expansion_read_mask(0x5500) == 0);
        } else {
            CHECK(c.battery_ram().size() == 512);
            CHECK(c.expansion_read_mask(0x5500) == 4);
            CHECK(c.read_expansion(0x5500) == 4);
        }
        c.write_chr(0x06AA, 0x3F);
        CHECK(c.read_chr(0x06AA) == 0x3F);
        snapshot(c);
    }
}

TEST_CASE(
    "FS306 controls individual CIRAM quadrants, the third PRG slot and a shared CHR RAM window",
    "[mapper544]") {
    auto c = cartridge(544, 256, 512, 2, 0x70, 5);
    c.write_prg(0x9C00, 9);
    CHECK(c.read_prg(0xC000) == 9);
    CHECK(c.read_prg(0xE000) == 31);
    for (unsigned i = 0; i < 4; ++i) {
        c.write_prg(0x9C0C + i, i == 2);
        CHECK(c.nametable_address(0x2007 + i * 1024U) == (i == 2 ? 1031 : 7));
    }
    const auto bank = [&](unsigned value) {
        c.write_prg(0xB000, value & 15U);
        c.write_prg(0xB400, value >> 4U);
    };
    bank(0x28);
    c.write_chr(5, 0xBC);
    bank(0x12A);
    CHECK(c.read_chr(5) == 0xBC);
    bank(0xC8);
    c.write_chr(0, 0);
    bank(4);
    CHECK(c.read_chr(5) == 0xBC);
    bank(0x28);
    CHECK(c.read_chr(5) == 0x28);
    bank(0xD8);
    c.write_chr(0, 0);
    bank(4);
    CHECK(c.read_chr(5) == 4);
    c.write_prg(0xF000, 15);
    c.write_prg(0xF400, 15);
    c.write_prg(0xF800, 6);
    c.clock_cpu();
    CHECK_FALSE(c.irq_pending());
    snapshot(c);
    c.clock_cpu();
    CHECK(c.irq_pending());
    c.write_prg(0xFC00, 0);
    CHECK_FALSE(c.irq_pending());
}

TEST_CASE("QTa selects adapter and cartridge chips, translates JIS and renders independent glyph "
          "bitplanes",
          "[mapper547]") {
    auto c = cartridge(547, 640, 128, 2, 0x77, 7);
    CHECK(c.read_prg(0xE000) == 79);
    c.write_prg(0xD234, 0x42);
    CHECK(c.read_prg(0x8000) == 18);
    c.write_prg(0x6000, 0xA6);
    c.write_prg(0xD000, 8);
    CHECK(c.read_prg(0x6000) == 0);
    CHECK(c.battery_ram()[0] == 0xA6);
    c.write_prg(0xD000, 0);
    CHECK(c.read_prg(0x6000) == 0xA6);
    c.write_prg(0xDC00, 0x21);
    c.write_prg(0xDD00, 0x21);
    c.write_prg(0xDB00, 5);
    CHECK(c.read_prg(0xDC00) == 0x85);
    CHECK(c.read_prg(0xDD00) == 0xC0);
    c.write_prg(0xDA00, 1);
    REQUIRE(c.write_nametable(0x2005, 0xC2));
    auto planes = c.background_pattern(0x2005, 0);
    REQUIRE(planes);
    CHECK((*planes)[0] == 4);
    CHECK((*planes)[1] == 255);
    c.write_chr(0x1000, 0x32);
    REQUIRE(c.write_nametable(0x2005, 1));
    CHECK((*c.background_pattern(0x2005, 0))[0] == 0x32);
    c.write_prg(0xD600, 0xFE);
    c.write_prg(0xD700, 0xFF);
    c.write_prg(0xD900, 2);
    c.clock_cpu();
    CHECK_FALSE(c.irq_pending());
    c.clock_cpu();
    CHECK(c.irq_pending());
    snapshot(c);
    c.write_prg(0xD800, 0);
    CHECK_FALSE(c.irq_pending());
}

TEST_CASE("Kaiser KS-7016B remaps address-latched PRG pins against fixed banks 8-11",
          "[mapper549]") {
    auto c = cartridge(549, 128, 0, 1, 0, 7);
    for (unsigned i = 0; i < 16; ++i) {
        c.write_prg(0x8000U + i * 4U, 0);
        CHECK(c.read_prg(0x6000) == (i >= 8U ? i | 4U : i));
    }
    CHECK(c.read_prg(0x8000) == 8);
    CHECK(c.read_prg(0xFFFF) == 11);
    c.write_chr(0x1FFF, 0x65);
    snapshot(c);
    CHECK(c.read_chr(0x1FFF) == 0x65);
}

TEST_CASE("NTDEC 2718 keeps CHR RAM fixed while N118 bank 5 changes CIRAM", "[mapper557]") {
    auto c = cartridge(557, 128, 0, 0, 7, 7);
    c.write_chr(0x1B23, 0xAC);
    c.write_prg(0x8000, 5);
    c.write_prg(0x8001, 0x20);
    CHECK(c.mirror_mode() == MirrorMode::Horizontal);
    CHECK(c.read_chr(0x1B23) == 0xAC);
    c.write_prg(0x8001, 5);
    CHECK(c.mirror_mode() == MirrorMode::Vertical);
    c.write_prg(0x8000, 6);
    c.write_prg(0x8001, 10);
    CHECK(c.read_prg(0x8000) == 10);
    c.write_prg(0x7000, 0xB7);
    snapshot(c);
    CHECK(c.read_prg(0x7000) == 0xB7);
}

TEST_CASE("C/E BASIC maps tile shadows and toggles pattern and PRG modes in one write",
          "[mapper560]") {
    for (Byte sub : {0, 1}) {
        auto c = cartridge(560, 64, 128, 1, 7, 0, sub);
        CHECK(c.read_prg(0x8000) == 4);
        CHECK_FALSE(c.write_nametable(0x2403, 0x82));
        auto pair = c.background_pattern(0x2003, 0);
        REQUIRE(pair);
        CHECK((*pair)[0] == (sub ? 8 : 4));
        CHECK((*pair)[1] == (sub ? 0 : 4));
        c.observe_ppu_read(0x2003);
        c.observe_ppu_read(0x23C0);
        CHECK(c.read_chr(0) == (*pair)[0]);
        c.write_prg(0xFFFF, 99);
        CHECK(c.read_prg(0x8000) == 0);
        CHECK(c.read_chr(8) == (sub ? 0 : 64));
        snapshot(c);
    }
}

TEST_CASE("03-101 selects discrete banks or MMC1 and gates WRAM and graphics writes",
          "[mapper546]") {
    auto c = cartridge(546, 512, 0, 2, 0x70, 7);
    c.write_prg(0x9003, 0x80);
    CHECK(c.read_prg(0x8000) == 6);
    CHECK(c.read_prg(0xC000) == 6);
    CHECK_FALSE(c.maps_prg(0x6000));
    c.write_chr(0x1FE1, 0xB5);
    c.write_prg(0x80A3, 0x80);
    CHECK(c.read_prg(0x8000) == 4);
    CHECK(c.read_prg(0xC000) == 6);
    c.write_chr(0x1FE1, 0);
    CHECK(c.read_chr(0x1FE1) == 0xB5);
    c.write_prg(0x9010, 0x80);
    CHECK(c.read_prg(0xC000) == 62);
    c.write_prg(0x6000, 0x7C);
    snapshot(c);
    CHECK(c.read_prg(0x6000) == 0x7C);
}

TEST_CASE("Campus Challenge RAM select and the timer are independent of MMC3 IRQs", "[mapper555]") {
    auto c = cartridge(555, 512, 256, 0, 8, 7);
    REQUIRE(c.configure_board_jumpers(0));
    CHECK_FALSE(c.configure_board_jumpers(16));
    c.write_expansion(0x5012, 0xB7);
    c.write_prg(0x6012, 0xC8);
    CHECK(c.read_prg(0x6012) == 0xC8);
    CHECK(c.read_expansion(0x5012) == 0xB7);
    c.write_expansion(0x5800, 2);
    c.write_prg(0x8000, 2);
    c.write_prg(0x8001, 0x43);
    c.write_chr(0x1007, 0xA1);
    CHECK(c.read_chr(0x1007) == 0xA1);
    c.write_expansion(0x5800, 4);
    CHECK(c.read_chr(0x1007) == 0xC3);
    CHECK(c.read_prg(0xE000) == 39);
    c.write_expansion(0x5800, 8);
    StateWriter saved;
    c.save_state(saved);
    auto before_timeout = saved.take();
    const std::uint32_t deadline = (16U << 25U) - 1;
    std::memcpy(before_timeout.data() + 4, &deadline, sizeof(deadline));
    StateReader state(before_timeout);
    c.load_state(state);
    REQUIRE(state.ok());
    CHECK(c.read_expansion(0x5FFF) == 0x5C);
    c.clock_cpu();
    CHECK(c.read_expansion(0x5800) == 0xDC);
    snapshot(c);
    c.write_expansion(0x5800, 0);
    CHECK(c.read_expansion(0x5800) == 0x5C);
}

TEST_CASE("JY-215 switches to VRC4e, locks the game and restores a running VRC IRQ",
          "[mapper556]") {
    auto c = cartridge(556, 1024, 1024, 0, 7, 0);
    for (Byte reg : {0x20, 0x40, 0x8F, 0xA0})
        c.write_expansion(0x5000, reg);
    c.write_prg(0x8000, 5);
    CHECK(c.read_prg(0x8000) == 69);
    CHECK(c.read_prg(0xC000) == 94);
    c.write_prg(0x9008, 2);
    CHECK(c.read_prg(0xC000) == 69);
    c.write_prg(0x9000, 3);
    CHECK(c.mirror_mode() == MirrorMode::SingleUpper);
    c.write_prg(0xB008, 9);
    c.write_prg(0xB00C, 1);
    CHECK(c.read_chr(0x0400) == 0x39);
    for (int i = 0; i < 4; ++i)
        c.write_expansion(0x5000, 0);
    CHECK(c.read_prg(0xC000) == 69);
    c.write_prg(0xF000, 14);
    c.write_prg(0xF004, 15);
    c.write_prg(0xF008, 6);
    c.clock_cpu();
    CHECK_FALSE(c.irq_pending());
    snapshot(c);
    c.clock_cpu();
    CHECK(c.irq_pending());
    c.write_prg(0xF00C, 0);
    CHECK_FALSE(c.irq_pending());
}
