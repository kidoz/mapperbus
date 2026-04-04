#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/nrom.hpp"

using namespace mapperbus::core;

static INesHeader make_header(uint8_t prg_banks, uint8_t chr_banks, MirrorMode mirror) {
    return INesHeader{
        .prg_rom_banks = prg_banks,
        .chr_rom_banks = chr_banks,
        .mapper_number = 0,
        .mirror_mode = mirror,
        .region = Region::NTSC,
        .has_battery = false,
        .has_trainer = false,
        .is_nes2 = false,
    };
}

TEST_CASE("NROM-128 PRG mirroring", "[nrom]") {
    auto header = make_header(1, 1, MirrorMode::Horizontal);
    std::vector<Byte> prg(16384, 0);
    prg[0] = 0xAA;
    prg[16383] = 0xBB;

    Nrom mapper(header, std::move(prg), std::vector<Byte>(8192, 0));

    REQUIRE(mapper.read_prg(0x8000) == 0xAA);
    REQUIRE(mapper.read_prg(0xBFFF) == 0xBB);
    REQUIRE(mapper.read_prg(0xC000) == 0xAA);
    REQUIRE(mapper.read_prg(0xFFFF) == 0xBB);
}

TEST_CASE("NROM-256 PRG", "[nrom]") {
    auto header = make_header(2, 1, MirrorMode::Vertical);
    std::vector<Byte> prg(32768, 0);
    prg[0] = 0x11;
    prg[16384] = 0x22;

    Nrom mapper(header, std::move(prg), std::vector<Byte>(8192, 0));

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    REQUIRE(mapper.read_prg(0xC000) == 0x22);
}

TEST_CASE("NROM CHR ROM read", "[nrom]") {
    auto header = make_header(1, 1, MirrorMode::Horizontal);
    std::vector<Byte> chr(8192, 0);
    chr[0] = 0x55;
    chr[0x1FFF] = 0x66;

    Nrom mapper(header, std::vector<Byte>(16384, 0), std::move(chr));

    REQUIRE(mapper.read_chr(0x0000) == 0x55);
    REQUIRE(mapper.read_chr(0x1FFF) == 0x66);
}

TEST_CASE("NROM CHR RAM when no CHR ROM", "[nrom]") {
    auto header = make_header(1, 0, MirrorMode::Horizontal);

    Nrom mapper(header, std::vector<Byte>(16384, 0), {});

    mapper.write_chr(0x0000, 0x77);
    REQUIRE(mapper.read_chr(0x0000) == 0x77);

    mapper.write_chr(0x1FFF, 0x88);
    REQUIRE(mapper.read_chr(0x1FFF) == 0x88);
}

TEST_CASE("NROM PRG write is noop", "[nrom]") {
    auto header = make_header(1, 1, MirrorMode::Horizontal);

    Nrom mapper(header, std::vector<Byte>(16384, 0xAA), std::vector<Byte>(8192, 0));

    mapper.write_prg(0x8000, 0xFF);
    REQUIRE(mapper.read_prg(0x8000) == 0xAA);
}

TEST_CASE("NROM mirror mode", "[nrom]") {
    auto header = make_header(1, 1, MirrorMode::Vertical);

    Nrom mapper(header, std::vector<Byte>(16384, 0), std::vector<Byte>(8192, 0));

    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
}
