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
