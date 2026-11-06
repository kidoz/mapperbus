#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

#include "core/apu/apu.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

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

TEST_CASE("Write-only APU registers preserve CPU open bus", "[bus][bus-side-effects]") {
    MemoryBus bus;
    for (const Address address : {0x4000, 0x4013, 0x4014}) {
        bus.write(0, 0x5A);
        CHECK(bus.read(address) == 0x5A);
    }
}

namespace {
uint32_t ppu_dot_position(const Ppu& ppu) {
    StateWriter writer;
    ppu.save_state(writer);
    StateReader reader(writer.data());
    const auto line = reader.read<uint16_t>();
    const auto dot = reader.read<uint16_t>();
    return static_cast<uint32_t>(line) * 341U + dot;
}
} // namespace

TEST_CASE("CPU bus cycles preserve regional PPU clock ratios and access phases", "[bus][region]") {
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy, Region::Multi}) {
        INFO("region " << static_cast<int>(region));
        Ppu ppu;
        Apu apu;
        MemoryBus bus;
        ppu.set_region(region);
        apu.set_region(region);
        bus.set_region(region);
        bus.connect_ppu(&ppu);
        bus.connect_apu(&apu);
        for (uint32_t cycle = 0; cycle < 100; ++cycle) {
            // One selected alignment: PAL accesses at master clock 8 of 16;
            // the existing NTSC/Dendy two-dot/one-dot split stays unchanged.
            const auto before = region == Region::PAL ? (cycle * 16U + 8U) / 5U : cycle * 3U + 2U;
            const auto after = region == Region::PAL ? (cycle + 1U) * 16U / 5U : (cycle + 1U) * 3U;
            bus.begin_cpu_cycle(0x2002);
            CHECK(ppu_dot_position(ppu) == before);
            bus.end_cpu_cycle();
            CHECK(ppu_dot_position(ppu) == after);
            CHECK(apu.cpu_cycles() == cycle + 1U);
        }
        const auto before = ppu_dot_position(ppu);
        (void)bus.read(0x2002);
        bus.write(0x2000, 0);
        CHECK(ppu_dot_position(ppu) == before); // debugger accesses stay untimed
    }
}

TEST_CASE("PAL CPU clocks complete exactly two PPU frames without drift", "[bus][region]") {
    Ppu ppu;
    MemoryBus bus;
    ppu.set_region(Region::PAL);
    bus.set_region(Region::PAL);
    bus.connect_ppu(&ppu);
    bus.clock_cpu_bus(0, 66495); // 2 * 341 * 312 dots / 3.2
    CHECK(ppu_dot_position(ppu) == 0);
}

TEST_CASE("OAM and DMC DMA share the fractional PAL clock", "[bus][region][dma]") {
    for (uint32_t initial = 0; initial < 5; ++initial) {
        for (const bool dmc : {false, true}) {
            INFO("initial cycles " << initial << ", DMC " << dmc);
            Ppu ppu;
            Apu apu;
            MemoryBus bus;
            ppu.set_region(Region::PAL);
            apu.set_region(Region::PAL);
            bus.set_region(Region::PAL);
            bus.connect_ppu(&ppu);
            bus.connect_apu(&apu);
            if (dmc) {
                bus.write(0x4012, 0);
                bus.write(0x4013, 0);
                bus.write(0x4015, 0x10);
            }
            bus.clock_cpu_bus(0, initial);
            const auto elapsed = bus.run_oam_dma(2, 0x0200, (initial & 1U) != 0);
            CHECK(elapsed >= 513);
            const auto total = initial + elapsed;
            CHECK(ppu_dot_position(ppu) == total * 16U / 5U);
            CHECK(apu.cpu_cycles() == total);
            if (dmc)
                CHECK((bus.read(0x4015) & 0x10U) == 0); // the pending fetch completed
        }
    }
}

TEST_CASE("PAL status reads occur on the selected fractional access boundary",
          "[bus][region][ppu]") {
    for (uint32_t initial = 0; initial < 5; ++initial) {
        INFO("master-clock remainder " << initial);
        Ppu ppu;
        MemoryBus bus;
        ppu.set_region(Region::PAL);
        bus.set_region(Region::PAL);
        bus.connect_ppu(&ppu);
        bus.clock_cpu_bus(0, initial);
        StateWriter writer;
        ppu.save_state(writer);
        auto state = writer.take();
        const uint16_t line = 240, dot = 340;
        std::memcpy(state.data(), &line, sizeof(line));
        std::memcpy(state.data() + sizeof(line), &dot, sizeof(dot));
        StateReader reader(state);
        ppu.load_state(reader);
        REQUIRE(reader.ok());
        ppu.write_register(0, 0x80);
        bus.begin_cpu_cycle(0x2002);
        CHECK((bus.read(0x2002) & 0x80U) == (initial >= 2 ? 0x80U : 0U));
        bus.end_cpu_cycle();
        CHECK_FALSE(ppu.nmi_line());
    }
}

TEST_CASE("Region changes reset the divider while reapplying a region preserves it",
          "[bus][region]") {
    Ppu ppu;
    MemoryBus bus;
    ppu.set_region(Region::PAL);
    bus.set_region(Region::PAL);
    bus.connect_ppu(&ppu);
    for (uint32_t cycle = 1; cycle <= 5; ++cycle) {
        bus.set_region(Region::PAL);
        bus.clock_cpu_bus(0);
        CHECK(ppu_dot_position(ppu) == cycle * 16U / 5U);
    }
    bus.clock_cpu_bus(0, 4); // nonzero PAL remainder
    bus.set_region(Region::NTSC);
    const auto before = ppu_dot_position(ppu);
    bus.clock_cpu_bus(0);
    CHECK(ppu_dot_position(ppu) == before + 3);
    bus.set_region(Region::PAL);
    bus.clock_cpu_bus(0);
    CHECK(ppu_dot_position(ppu) == before + 6); // fresh PAL phase
}
