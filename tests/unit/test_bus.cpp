#include <catch2/catch_test_macros.hpp>

#include "core/bus/memory_bus.hpp"

using namespace mapperbus::core;

TEST_CASE("MemoryBus RAM read/write", "[bus]") {
    MemoryBus bus;

    SECTION("basic read and write") {
        bus.write(0x0000, 0x42);
        REQUIRE(bus.read(0x0000) == 0x42);

        bus.write(0x07FF, 0xAB);
        REQUIRE(bus.read(0x07FF) == 0xAB);
    }

    SECTION("RAM mirroring reads") {
        bus.write(0x0000, 0xCD);
        REQUIRE(bus.read(0x0800) == 0xCD);
        REQUIRE(bus.read(0x1000) == 0xCD);
        REQUIRE(bus.read(0x1800) == 0xCD);
    }

    SECTION("RAM mirroring writes") {
        bus.write(0x0800, 0xEF);
        REQUIRE(bus.read(0x0000) == 0xEF);
    }

    SECTION("unconnected cartridge reads zero") {
        REQUIRE(bus.read(0x8000) == 0);
        REQUIRE(bus.read(0xFFFF) == 0);
    }
}
