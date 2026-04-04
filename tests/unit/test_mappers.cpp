#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/axrom.hpp"
#include "core/mappers/cnrom.hpp"
#include "core/mappers/color_dreams.hpp"
#include "core/mappers/mmc1.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/uxrom.hpp"

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

// Helper: perform 5 serial writes to MMC1
static void mmc1_serial_write(Mmc1& mapper, Address addr, Byte value) {
    for (int i = 0; i < 5; ++i) {
        mapper.write_prg(addr, (value >> i) & 0x01);
    }
}

// === UxROM ===

TEST_CASE("UxROM fixed last bank at $C000", "[uxrom]") {
    auto header = make_header(8, 0, MirrorMode::Vertical);
    std::vector<Byte> prg(128 * 1024, 0);
    prg[7 * 0x4000] = 0xAA;
    prg[7 * 0x4000 + 0x3FFF] = 0xBB;

    Uxrom mapper(header, std::move(prg), {});

    REQUIRE(mapper.read_prg(0xC000) == 0xAA);
    REQUIRE(mapper.read_prg(0xFFFF) == 0xBB);
}

TEST_CASE("UxROM bank switching", "[uxrom]") {
    auto header = make_header(8, 0, MirrorMode::Vertical);
    std::vector<Byte> prg(128 * 1024, 0);
    prg[0 * 0x4000] = 0x11;
    prg[3 * 0x4000] = 0x33;

    Uxrom mapper(header, std::move(prg), {});

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    mapper.write_prg(0x8000, 0x03);
    REQUIRE(mapper.read_prg(0x8000) == 0x33);
}

TEST_CASE("UxROM CHR RAM", "[uxrom]") {
    Uxrom mapper(make_header(4, 0, MirrorMode::Horizontal), std::vector<Byte>(64 * 1024, 0), {});

    mapper.write_chr(0x0000, 0x42);
    REQUIRE(mapper.read_chr(0x0000) == 0x42);
    mapper.write_chr(0x1FFF, 0x99);
    REQUIRE(mapper.read_chr(0x1FFF) == 0x99);
}

// === CNROM ===

TEST_CASE("CNROM fixed PRG", "[cnrom]") {
    std::vector<Byte> prg(32 * 1024, 0);
    prg[0] = 0xAA;
    prg[0x7FFF] = 0xBB;

    Cnrom mapper(
        make_header(2, 4, MirrorMode::Horizontal), std::move(prg), std::vector<Byte>(32 * 1024, 0));

    REQUIRE(mapper.read_prg(0x8000) == 0xAA);
    REQUIRE(mapper.read_prg(0xFFFF) == 0xBB);
}

TEST_CASE("CNROM CHR bank switching", "[cnrom]") {
    std::vector<Byte> chr(32 * 1024, 0);
    chr[0 * 0x2000] = 0x11;
    chr[2 * 0x2000] = 0x33;
    chr[3 * 0x2000] = 0x44;

    Cnrom mapper(
        make_header(2, 4, MirrorMode::Horizontal), std::vector<Byte>(32 * 1024, 0), std::move(chr));

    REQUIRE(mapper.read_chr(0x0000) == 0x11);
    mapper.write_prg(0x8000, 0x02);
    REQUIRE(mapper.read_chr(0x0000) == 0x33);
    mapper.write_prg(0xFFFF, 0x03);
    REQUIRE(mapper.read_chr(0x0000) == 0x44);
}

TEST_CASE("CNROM CHR ROM is read-only", "[cnrom]") {
    Cnrom mapper(make_header(1, 1, MirrorMode::Vertical),
                 std::vector<Byte>(16 * 1024, 0),
                 std::vector<Byte>(8 * 1024, 0xAA));

    mapper.write_chr(0x0000, 0xFF);
    REQUIRE(mapper.read_chr(0x0000) == 0xAA);
}

// === AxROM ===

TEST_CASE("AxROM 32KB bank switching", "[axrom]") {
    std::vector<Byte> prg(128 * 1024, 0);
    prg[0 * 0x8000] = 0x11;
    prg[2 * 0x8000] = 0x22;
    prg[3 * 0x8000] = 0x33;

    Axrom mapper(make_header(8, 0, MirrorMode::Horizontal), std::move(prg), {});

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    mapper.write_prg(0x8000, 0x02);
    REQUIRE(mapper.read_prg(0x8000) == 0x22);
    mapper.write_prg(0x8000, 0x03);
    REQUIRE(mapper.read_prg(0x8000) == 0x33);
}

TEST_CASE("AxROM mirroring control", "[axrom]") {
    Axrom mapper(make_header(4, 0, MirrorMode::Horizontal), std::vector<Byte>(64 * 1024, 0), {});

    REQUIRE(mapper.mirror_mode() == MirrorMode::SingleLower);
    mapper.write_prg(0x8000, 0x10);
    REQUIRE(mapper.mirror_mode() == MirrorMode::SingleUpper);
    mapper.write_prg(0x8000, 0x01);
    REQUIRE(mapper.mirror_mode() == MirrorMode::SingleLower);
}

TEST_CASE("AxROM CHR RAM", "[axrom]") {
    Axrom mapper(make_header(4, 0, MirrorMode::Horizontal), std::vector<Byte>(64 * 1024, 0), {});

    mapper.write_chr(0x0000, 0x55);
    REQUIRE(mapper.read_chr(0x0000) == 0x55);
}

// === Color Dreams ===

TEST_CASE("Color Dreams PRG bank", "[color-dreams]") {
    std::vector<Byte> prg(64 * 1024, 0);
    prg[0 * 0x8000] = 0x11;
    prg[1 * 0x8000] = 0x22;

    ColorDreams mapper(
        make_header(4, 4, MirrorMode::Vertical), std::move(prg), std::vector<Byte>(32 * 1024, 0));

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    mapper.write_prg(0x8000, 0x01);
    REQUIRE(mapper.read_prg(0x8000) == 0x22);
}

TEST_CASE("Color Dreams CHR bank", "[color-dreams]") {
    std::vector<Byte> chr(32 * 1024, 0);
    chr[0 * 0x2000] = 0xAA;
    chr[2 * 0x2000] = 0xCC;

    ColorDreams mapper(
        make_header(2, 4, MirrorMode::Vertical), std::vector<Byte>(32 * 1024, 0), std::move(chr));

    REQUIRE(mapper.read_chr(0x0000) == 0xAA);
    mapper.write_prg(0x8000, 0x20);
    REQUIRE(mapper.read_chr(0x0000) == 0xCC);
}

TEST_CASE("Color Dreams combined PRG+CHR", "[color-dreams]") {
    std::vector<Byte> prg(64 * 1024, 0);
    prg[1 * 0x8000] = 0x77;
    std::vector<Byte> chr(64 * 1024, 0);
    chr[3 * 0x2000] = 0x88;

    ColorDreams mapper(make_header(4, 8, MirrorMode::Horizontal), std::move(prg), std::move(chr));

    mapper.write_prg(0x8000, 0x31);
    REQUIRE(mapper.read_prg(0x8000) == 0x77);
    REQUIRE(mapper.read_chr(0x0000) == 0x88);
}

// === MMC1 ===

TEST_CASE("MMC1 default fixed last bank", "[mmc1]") {
    std::vector<Byte> prg(256 * 1024, 0);
    prg[15 * 0x4000] = 0xFF;

    Mmc1 mapper(make_header(16, 0, MirrorMode::Horizontal), std::move(prg), {});

    REQUIRE(mapper.read_prg(0xC000) == 0xFF);
}

TEST_CASE("MMC1 PRG bank switching", "[mmc1]") {
    std::vector<Byte> prg(256 * 1024, 0);
    prg[0 * 0x4000] = 0x11;
    prg[5 * 0x4000] = 0x55;

    Mmc1 mapper(make_header(16, 0, MirrorMode::Horizontal), std::move(prg), {});

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    mmc1_serial_write(mapper, 0xE000, 0x05);
    REQUIRE(mapper.read_prg(0x8000) == 0x55);
}

TEST_CASE("MMC1 reset on bit 7", "[mmc1]") {
    Mmc1 mapper(make_header(16, 0, MirrorMode::Horizontal), std::vector<Byte>(256 * 1024, 0), {});

    mapper.write_prg(0x8000, 0x01);
    mapper.write_prg(0x8000, 0x00);
    mapper.write_prg(0x8000, 0x01);
    mapper.write_prg(0x8000, 0x80); // reset
    // Should not crash
    REQUIRE(true);
}

TEST_CASE("MMC1 mirroring control", "[mmc1]") {
    Mmc1 mapper(make_header(8, 0, MirrorMode::Horizontal), std::vector<Byte>(128 * 1024, 0), {});

    mmc1_serial_write(mapper, 0x8000, 0x0E);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);

    mmc1_serial_write(mapper, 0x8000, 0x0F);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("MMC1 CHR 4KB mode", "[mmc1]") {
    std::vector<Byte> chr(128 * 1024, 0);
    chr[5 * 0x1000] = 0x55;

    Mmc1 mapper(make_header(8, 16, MirrorMode::Horizontal),
                std::vector<Byte>(128 * 1024, 0),
                std::move(chr));

    mmc1_serial_write(mapper, 0x8000, 0x1C);
    mmc1_serial_write(mapper, 0xA000, 0x05);
    REQUIRE(mapper.read_chr(0x0000) == 0x55);
}

TEST_CASE("MMC1 PRG RAM", "[mmc1]") {
    Mmc1 mapper(make_header(8, 0, MirrorMode::Horizontal), std::vector<Byte>(128 * 1024, 0), {});

    mapper.write_prg(0x6000, 0x42);
    REQUIRE(mapper.read_prg(0x6000) == 0x42);
}

// === MMC3 ===

TEST_CASE("MMC3 PRG bank switching", "[mmc3]") {
    std::vector<Byte> prg(256 * 1024, 0);
    prg[5 * 0x2000] = 0x55;
    prg[(32 - 1) * 0x2000] = 0xFF;

    Mmc3 mapper(make_header(16, 0, MirrorMode::Vertical), std::move(prg), {});

    REQUIRE(mapper.read_prg(0xE000) == 0xFF);
    mapper.write_prg(0x8000, 0x06);
    mapper.write_prg(0x8001, 0x05);
    REQUIRE(mapper.read_prg(0x8000) == 0x55);
}

TEST_CASE("MMC3 CHR bank switching", "[mmc3]") {
    std::vector<Byte> chr(256 * 1024, 0);
    chr[10 * 0x0400] = 0xAA;

    Mmc3 mapper(
        make_header(8, 32, MirrorMode::Vertical), std::vector<Byte>(128 * 1024, 0), std::move(chr));

    mapper.write_prg(0x8000, 0x02);
    mapper.write_prg(0x8001, 10);
    REQUIRE(mapper.read_chr(0x1000) == 0xAA);
}

TEST_CASE("MMC3 mirroring", "[mmc3]") {
    Mmc3 mapper(make_header(8, 8, MirrorMode::Vertical),
                std::vector<Byte>(128 * 1024, 0),
                std::vector<Byte>(64 * 1024, 0));

    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    mapper.write_prg(0xA000, 0x01);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Horizontal);
    mapper.write_prg(0xA000, 0x00);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("MMC3 IRQ counter", "[mmc3]") {
    Mmc3 mapper(make_header(8, 8, MirrorMode::Vertical),
                std::vector<Byte>(128 * 1024, 0),
                std::vector<Byte>(64 * 1024, 0));

    mapper.write_prg(0xC000, 0x03);
    mapper.write_prg(0xC001, 0x00);
    mapper.write_prg(0xE001, 0x00);

    REQUIRE_FALSE(mapper.irq_pending());

    mapper.clock_irq_counter(); // reload to 3
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.clock_irq_counter(); // 2
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.clock_irq_counter(); // 1
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.clock_irq_counter(); // 0 → IRQ
    REQUIRE(mapper.irq_pending());

    mapper.write_prg(0xE000, 0x00);
    REQUIRE_FALSE(mapper.irq_pending());
}

TEST_CASE("MMC3 PRG RAM", "[mmc3]") {
    Mmc3 mapper(make_header(8, 8, MirrorMode::Vertical),
                std::vector<Byte>(128 * 1024, 0),
                std::vector<Byte>(64 * 1024, 0));

    mapper.write_prg(0x7000, 0xAB);
    REQUIRE(mapper.read_prg(0x7000) == 0xAB);
}
