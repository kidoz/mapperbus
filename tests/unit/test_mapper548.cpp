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

