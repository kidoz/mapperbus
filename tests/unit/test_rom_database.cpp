#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "core/cartridge/rom_crc32.hpp"
#include "core/cartridge/rom_database.hpp"

using namespace mapperbus::core;

TEST_CASE("CRC32 computation", "[crc32]") {
    SECTION("empty data") {
        REQUIRE(crc32(std::vector<Byte>{}) == 0x00000000);
    }
    SECTION("standard test vector '123456789'") {
        std::vector<Byte> data = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
        REQUIRE(crc32(data) == 0xCBF43926);
    }
    SECTION("single zero byte") {
        REQUIRE(crc32(std::vector<Byte>{0x00}) == 0xD202EF8D);
    }
}

TEST_CASE("ROM CRC32 skips 16-byte header", "[crc32]") {
    std::vector<Byte> rom(20, 0);
    rom[16] = 'T';
    rom[17] = 'E';
    rom[18] = 'S';
    rom[19] = 'T';

    std::vector<Byte> data_only = {'T', 'E', 'S', 'T'};
    REQUIRE(rom_crc32(rom) == crc32(data_only));
}

TEST_CASE("ROM CRC32 returns 0 for too-small data", "[crc32]") {
    REQUIRE(rom_crc32(std::vector<Byte>(10, 0)) == 0);
}

TEST_CASE("ROM database lookup", "[rom-database]") {
    SECTION("known PAL game: Super Mario Bros.") {
        auto r = lookup_region_by_crc32(0x4E74BEF0);
        REQUIRE(r.has_value());
        REQUIRE(*r == Region::PAL);
    }
    SECTION("known PAL game: Mega Man 2 Europe") {
        auto r = lookup_region_by_crc32(0x718BAD86);
        REQUIRE(r.has_value());
        REQUIRE(*r == Region::PAL);
    }
    SECTION("unknown CRC32 returns nullopt") {
        REQUIRE_FALSE(lookup_region_by_crc32(0xDEADBEEF).has_value());
        REQUIRE_FALSE(lookup_region_by_crc32(0x00000000).has_value());
        REQUIRE_FALSE(lookup_region_by_crc32(0xFFFFFFFF).has_value());
    }
}
