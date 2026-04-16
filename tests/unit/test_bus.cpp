#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"

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

    SECTION("unmapped cartridge space returns CPU open bus") {
        bus.write(0x0000, 0x60);
        REQUIRE(bus.read(0x6000) == 0x60);

        bus.write(0x0000, 0x7F);
        REQUIRE(bus.read(0x7FFF) == 0x7F);
    }

    SECTION("AxROM without WRAM leaves $6000-$7FFF open") {
        register_builtin_mappers();

        std::vector<Byte> rom_data(16 + 8 * 16384, 0);
        rom_data[0] = 'N';
        rom_data[1] = 'E';
        rom_data[2] = 'S';
        rom_data[3] = 0x1A;
        rom_data[4] = 8;    // 128 KiB PRG ROM
        rom_data[5] = 0;    // CHR RAM
        rom_data[6] = 0x70; // Mapper 7, horizontal mirroring bit clear

        auto cartridge_result = Cartridge::from_data(rom_data);
        REQUIRE(cartridge_result);
        auto cartridge = std::move(*cartridge_result);
        bus.connect_cartridge(&cartridge);

        bus.write(0x0000, 0x60);
        REQUIRE(bus.read(0x6000) == 0x60);

        bus.write(0x0000, 0x7F);
        REQUIRE(bus.read(0x7FFF) == 0x7F);
    }

    SECTION("unmapped cartridge expansion reads preserve CPU open bus") {
        register_builtin_mappers();

        std::vector<Byte> rom_data(16 + 2 * 16384 + 8192, 0);
        rom_data[0] = 'N';
        rom_data[1] = 'E';
        rom_data[2] = 'S';
        rom_data[3] = 0x1A;
        rom_data[4] = 2; // 32 KiB PRG ROM
        rom_data[5] = 1; // 8 KiB CHR ROM

        auto cartridge_result = Cartridge::from_data(rom_data);
        REQUIRE(cartridge_result);
        auto cartridge = std::move(*cartridge_result);
        bus.connect_cartridge(&cartridge);

        bus.write(0x0000, 0x5A);
        REQUIRE(bus.read(0x5000) == 0x5A);
    }

    SECTION("OAM DMA timing") {
        // Needs PPU connected to trigger DMA properly
        mapperbus::core::Ppu ppu;
        bus.connect_ppu(&ppu);

        bus.write(0x4014, 0x02);
        REQUIRE(bus.take_dma_cycles() == 513);
        // The +1 alignment cycle is modeled in Cpu::step, this confirms Bus returns base cost
    }
}
