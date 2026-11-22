#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/cartridge/ines_header.hpp"

using namespace mapperbus::core;

static std::vector<Byte> make_rom(Byte flags7_extra = 0, Byte byte9 = 0, Byte byte12 = 0) {
    std::vector<Byte> rom(16 + 16384, 0);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 1;
    rom[5] = 0;
    rom[6] = 0;
    rom[7] = flags7_extra;
    rom[9] = byte9;
    rom[12] = byte12;
    return rom;
}

TEST_CASE("NES 2.0 mapper extension and submapper parsing", "[region][ines]") {
    auto rom = make_rom(0x08, 0, 0);
    rom[6] = 0x70;
    rom[8] = 0x21;

    auto r = parse_ines_header(rom);

    REQUIRE(r.has_value());
    REQUIRE(r->is_nes2);
    REQUIRE(r->mapper_number == 0x107);
    REQUIRE(r->submapper == 2);
}

TEST_CASE("NES 2.0 extended and exponent ROM sizes", "[region][ines]") {
    SECTION("linear high bits") {
        auto rom = make_rom(0x08, 0x21, 0);
        rom[4] = 3;
        rom[5] = 2;
        const auto parsed = parse_ines_header(rom);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->prg_rom_banks == 0x0103);
        REQUIRE(parsed->chr_rom_banks == 0x0202);
        REQUIRE(parsed->prg_rom_size == 0x0103 * 16384);
        REQUIRE(parsed->chr_rom_size == 0x0202 * 8192);
    }

    SECTION("exponent multiplier") {
        auto rom = make_rom(0x08, 0xFF, 0);
        rom[4] = (4 << 2) | 1;
        rom[5] = (3 << 2) | 2;
        const auto parsed = parse_ines_header(rom);
        REQUIRE(parsed.has_value());
        REQUIRE(parsed->prg_rom_banks == 0);
        REQUIRE(parsed->chr_rom_banks == 0);
        REQUIRE(parsed->prg_rom_size == 48);
        REQUIRE(parsed->chr_rom_size == 40);
    }

    SECTION("exponent overflow") {
        auto rom = make_rom(0x08, 0x0F, 0);
        rom[4] = 0xFF;
        const auto parsed = parse_ines_header(rom);
        REQUIRE_FALSE(parsed.has_value());
    }
}

TEST_CASE("NES 2.0 region detection", "[region]") {
    SECTION("NTSC") {
        auto r = parse_ines_header(make_rom(0x08, 0, 0x00));
        REQUIRE(r.has_value());
        REQUIRE(r->is_nes2);
        REQUIRE(r->region == Region::NTSC);
    }
    SECTION("PAL") {
        auto r = parse_ines_header(make_rom(0x08, 0, 0x01));
        REQUIRE(r->region == Region::PAL);
    }
    SECTION("Multi") {
        auto r = parse_ines_header(make_rom(0x08, 0, 0x02));
        REQUIRE(r->region == Region::Multi);
    }
    SECTION("Dendy") {
        auto r = parse_ines_header(make_rom(0x08, 0, 0x03));
        REQUIRE(r->region == Region::Dendy);
    }
}

TEST_CASE("iNES 1.0 region detection", "[region]") {
    SECTION("default NTSC") {
        auto r = parse_ines_header(make_rom(0x00, 0, 0));
        REQUIRE(r.has_value());
        REQUIRE_FALSE(r->is_nes2);
        REQUIRE(r->region == Region::NTSC);
    }
    SECTION("byte 9 PAL") {
        auto r = parse_ines_header(make_rom(0x00, 0x01, 0));
        REQUIRE(r->region == Region::PAL);
    }
}

TEST_CASE("DiskDude! tag corruption is repaired", "[region][ines][cartridge]") {
    constexpr char kTag[] = "DiskDude!";

    SECTION("tag over flags7 masks mapper, PRG-RAM, and PAL fields") {
        // Jackal (U) layout: the credit overwrites bytes 7-15, so the UNROM
        // low nibble in flags6 pairs with the tag's 'D' to read as mapper 66,
        // and the tag characters light up the PRG-RAM byte and the PAL bit.
        auto rom = make_rom(0x00, 0, 0);
        rom[4] = 8;
        rom[6] = 0x21;
        std::copy(kTag, kTag + 9, rom.begin() + 7);

        const auto r = parse_ines_header(rom);

        REQUIRE(r.has_value());
        CHECK(r->mapper_number == 2);
        CHECK_FALSE(r->is_nes2);
        CHECK(r->region == Region::NTSC);
        CHECK(r->prg_ram_size == 8192);
    }

    SECTION("tag at byte 12 masks only the mapper nibble") {
        // Classic layout: bytes 12-15 hold "Disk" and "Dude!" spills into PRG
        // data, leaving byte 8 and byte 9 genuine.
        auto rom = make_rom(0x40, 0x01, 0);
        rom[6] = 0x20;
        std::copy(kTag, kTag + 9, rom.begin() + 12);

        const auto r = parse_ines_header(rom);

        REQUIRE(r.has_value());
        CHECK(r->mapper_number == 2);
        CHECK_FALSE(r->is_nes2);
        CHECK(r->region == Region::PAL);
        CHECK(r->prg_ram_size == 8192);
    }

    SECTION("partial tag without PRG spill leaves the header alone") {
        // A 16-byte image cannot confirm the classic tag because its tail
        // lives in PRG data, so the extended mapper nibble stays.
        std::vector<Byte> rom(16, 0);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1A;
        rom[4] = 1;
        rom[6] = 0x20;
        rom[7] = 0x40;
        constexpr char kDisk[] = "Disk";
        std::copy(kDisk, kDisk + 4, rom.begin() + 12);

        const auto r = parse_ines_header(rom);

        REQUIRE(r.has_value());
        CHECK(r->mapper_number == 66);
    }

    SECTION("clean headers keep the extended mapper nibble") {
        auto rom = make_rom(0x40, 0, 0);
        rom[6] = 0x20;

        const auto r = parse_ines_header(rom);

        REQUIRE(r.has_value());
        CHECK(r->mapper_number == 66);
    }
}

TEST_CASE("Filename region detection", "[region]") {
    SECTION("USA/Japan → NTSC") {
        REQUIRE(detect_region_from_filename("Super Mario Bros. (USA).nes") == Region::NTSC);
        REQUIRE(detect_region_from_filename("/roms/BattleCity (J).nes") == Region::NTSC);
        REQUIRE(detect_region_from_filename("Game (U).nes") == Region::NTSC);
        REQUIRE(detect_region_from_filename("Game (usa).nes") == Region::NTSC);
    }
    SECTION("Europe → PAL") {
        REQUIRE(detect_region_from_filename("Game (Europe).nes") == Region::PAL);
        REQUIRE(detect_region_from_filename("Game (EUR).nes") == Region::PAL);
        REQUIRE(detect_region_from_filename("/path/to/Game (E).nes") == Region::PAL);
        REQUIRE(detect_region_from_filename("Game (PAL).nes") == Region::PAL);
    }
    SECTION("World → Multi") {
        REQUIRE(detect_region_from_filename("Game (W).nes") == Region::Multi);
    }
    SECTION("no tag → nullopt") {
        REQUIRE_FALSE(detect_region_from_filename("BattleCity.nes").has_value());
    }
}

TEST_CASE("Region timing constants", "[region]") {
    REQUIRE(kNtscTiming.scanlines_per_frame == 262);
    REQUIRE(kNtscTiming.vblank_scanline == 241);
    REQUIRE(kNtscTiming.pre_render_scanline == 261);
    REQUIRE(kPalTiming.scanlines_per_frame == 312);
    REQUIRE(kPalTiming.pre_render_scanline == 311);

    REQUIRE(&timing_for_region(Region::NTSC) == &kNtscTiming);
    REQUIRE(&timing_for_region(Region::PAL) == &kPalTiming);
    REQUIRE(&timing_for_region(Region::Dendy) == &kDendyTiming);
    REQUIRE(&timing_for_region(Region::Multi) == &kNtscTiming);
}

TEST_CASE("Hybrid clock and host pacing use the same nominal crystal", "[region][hybrid]") {
    CHECK(kDendyTiming.cpu_clock_hz() == 1'773'447.5);
    CHECK(kDendyTiming.cpu_clock_divider == 15);
    CHECK(kDendyTiming.ppu_clock_divider == 5);
    CHECK(kDendyTiming.frame_duration_ns() == 19'997'209);
    CHECK(kPalTiming.frame_duration_ns() == kDendyTiming.frame_duration_ns());
    CHECK(kNtscTiming.frame_duration_ns() == 16'639'263);
    CHECK(kDendyTiming.scanlines_per_frame * 341 * kDendyTiming.ppu_clock_divider /
              kDendyTiming.cpu_clock_divider ==
          35'464);
}
