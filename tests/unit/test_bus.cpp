#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <vector>

#include "core/apu/apu.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/input/controller.hpp"
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
struct IoMachine {
    Apu apu;
    Ppu ppu;
    Controller pads;
    MemoryBus bus;

    IoMachine() {
        bus.connect_apu(&apu);
        bus.connect_ppu(&ppu);
        bus.connect_controller(&pads);
    }
    Byte read(Address address) {
        bus.begin_cpu_cycle(address);
        const auto value = bus.read_cpu(address);
        bus.end_cpu_cycle();
        return value;
    }
    void write(Address address, Byte value) {
        bus.begin_cpu_cycle(address);
        bus.write_cpu(address, value);
        bus.end_cpu_cycle();
    }
    void latch() {
        bus.write(0x4016, 1);
        bus.write(0x4016, 0);
    }
    std::vector<Byte> save() const {
        StateWriter writer;
        ppu.save_state(writer);
        apu.save_state(writer);
        bus.save_state(writer);
        pads.save_state(writer);
        return writer.take();
    }
    void restore(std::span<const Byte> state) {
        StateReader reader(state);
        ppu.load_state(reader);
        apu.load_state(reader);
        bus.load_state(reader);
        pads.load_state(reader);
        REQUIRE(reader.ok());
    }
};

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

TEST_CASE("Standard controller shifts ones and reloads live buttons while strobed",
          "[bus][controller]") {
    Controller pads;
    for (const int player : {0, 1}) {
        pads.set_button_state(player, Button::A, true);
        pads.set_button_state(player, Button::Select, true);
    }
    pads.write(1);
    pads.write(0);
    for (const int player : {0, 1}) {
        for (unsigned index = 0; index < 8; ++index)
            CHECK(pads.read(player) == (index == 0 || index == 2 ? 1 : 0));
        for (int index = 0; index < 16; ++index)
            CHECK(pads.read(player) == 1);
    }
    pads.write(1);
    pads.set_button_state(0, Button::A, false);
    pads.set_button_state(0, Button::B, true);
    for (int index = 0; index < 12; ++index)
        CHECK(pads.read(0) == 0);
    pads.write(0);
    CHECK(pads.read(0) == 0);
    CHECK(pads.read(0) == 1);
    CHECK(pads.read(0) == 1);
}

TEST_CASE("NES controller reads clock once per continuous output enable", "[bus][controller]") {
    IoMachine machine;
    machine.pads.set_button_state(0, Button::A, true);
    machine.pads.set_button_state(0, Button::Select, true);
    machine.pads.set_button_state(1, Button::B, true);
    machine.latch();
    CHECK((machine.read(0x4016) & 1U) == 1);
    CHECK((machine.read(0x4016) & 1U) == 1); // same /OE assertion, same bit
    CHECK((machine.read(0x4017) & 1U) == 0); // releases player 1
    CHECK((machine.read(0x4016) & 1U) == 0); // B
    CHECK((machine.read(0x4017) & 1U) == 1); // player 2's B
    (void)machine.read(0);
    CHECK((machine.read(0x4016) & 1U) == 1); // Select
}

TEST_CASE("Controller strobe samples one-cycle pulses at the get-to-put boundary",
          "[bus][controller]") {
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        for (const bool starts_on_get : {false, true}) {
            IoMachine machine;
            machine.bus.set_controller_timing(timing);
            machine.latch();
            for (int index = 0; index < 8; ++index)
                (void)machine.pads.read(0);
            REQUIRE(machine.pads.peek(0) == 1);
            if (starts_on_get)
                (void)machine.read(0);
            machine.write(0x4016, 1);
            machine.write(0x4016, 0);
            (void)machine.read(0);
            CHECK(machine.pads.peek(0) == (starts_on_get ? 0 : 1));
        }
    }
}

TEST_CASE("RF Famicom clocks each consecutive read on both ports", "[bus][controller]") {
    for (const auto region : {Region::NTSC, Region::Multi, Region::PAL, Region::Dendy}) {
        IoMachine machine;
        machine.bus.set_region(region);
        machine.bus.set_controller_timing(ControllerTiming::RfFamicom);
        machine.pads.set_button_state(0, Button::A, true);
        machine.pads.set_button_state(0, Button::Select, true);
        machine.pads.set_button_state(1, Button::B, true);
        machine.latch();
        CHECK((machine.read(0x4016) & 1U) == 1);
        const auto before = machine.save();
        CHECK((machine.read(0x4016) & 1U) == 0);
        CHECK((machine.read(0x4016) & 1U) == 1);
        CHECK((machine.read(0x4017) & 1U) == 0);
        CHECK((machine.read(0x4017) & 1U) == 1);
        const auto after = machine.save();
        machine.bus.set_controller_timing(ControllerTiming::NesAvFamicom);
        machine.restore(before);
        REQUIRE(machine.bus.controller_timing() == ControllerTiming::RfFamicom);
        for (const Address address :
             {Address{0x4016}, Address{0x4016}, Address{0x4017}, Address{0x4017}})
            (void)machine.read(address);
        CHECK(machine.save() == after);
    }
}

TEST_CASE("Controller timing changes release a held NES read exactly once", "[bus][controller]") {
    IoMachine machine;
    machine.pads.set_button_state(0, Button::A, true);
    machine.pads.set_button_state(0, Button::Select, true);
    machine.latch();
    CHECK((machine.read(0x4016) & 1U) == 1);
    machine.bus.set_controller_timing(ControllerTiming::NesAvFamicom);
    CHECK((machine.read(0x4016) & 1U) == 1);
    machine.bus.set_controller_timing(ControllerTiming::RfFamicom);
    CHECK((machine.read(0x4016) & 1U) == 0);
    machine.bus.set_controller_timing(ControllerTiming::RfFamicom);
    CHECK((machine.read(0x4016) & 1U) == 1);
}

TEST_CASE("Controller read enables and pending strobes survive device snapshots",
          "[bus][controller][state]") {
    IoMachine machine;
    machine.pads.set_button_state(0, Button::A, true);
    machine.latch();
    SECTION("a continuous read resumes without shifting twice") {
        REQUIRE((machine.read(0x4016) & 1U) == 1);
        const auto state = machine.save();
        CHECK((machine.read(0x4016) & 1U) == 1);
        (void)machine.read(0);
        CHECK((machine.read(0x4016) & 1U) == 0);
        const auto after = machine.save();
        machine.restore(state);
        CHECK((machine.read(0x4016) & 1U) == 1);
        (void)machine.read(0);
        CHECK((machine.read(0x4016) & 1U) == 0);
        CHECK(machine.save() == after);
    }
    SECTION("a pending strobe is applied on the next get") {
        machine.write(0x4016, 1); // initial put: request is not latched yet
        REQUIRE_FALSE(machine.pads.strobed());
        const auto state = machine.save();
        (void)machine.read(0);
        CHECK(machine.pads.strobed());
        const auto after = machine.save();
        machine.restore(state);
        REQUIRE_FALSE(machine.pads.strobed());
        (void)machine.read(0);
        CHECK(machine.pads.strobed());
        CHECK(machine.save() == after);
    }
}

TEST_CASE("APU status returns internal data without driving external open bus", "[bus][apu]") {
    IoMachine machine;
    machine.bus.write(0, 0xA5);
    CHECK(machine.read(0x4015) == 0x20);
    CHECK(machine.bus.open_bus() == 0xA5);
    CHECK(machine.bus.read(0x4015) == 0x20); // untimed status access has the same bus isolation
    CHECK(machine.bus.open_bus() == 0xA5);
    CHECK(machine.read(0x4000) == 0xA5);
}

TEST_CASE("OAM DMA activates APU registers only through the halted CPU address",
          "[bus][dma][apu]") {
    for (const Address halted : {Address{0x0200}, Address{0x4000}}) {
        IoMachine machine;
        machine.apu.step(29900);
        REQUIRE(machine.apu.irq_pending());
        machine.bus.write(0x0200, 0x50);
        machine.pads.set_button_state(0, Button::A, true);
        machine.latch();
        REQUIRE(machine.bus.run_oam_dma(0x40, halted, false) >= 513);
        machine.bus.write(0x2003, 0x15);
        CHECK(machine.bus.read(0x2004) == (halted == 0x4000 ? 0x40 : 0x50));
        CHECK(machine.apu.irq_pending() == (halted != 0x4000));
        machine.bus.write(0x2003, 0x35);
        CHECK(machine.bus.read(0x2004) == (halted == 0x4000 ? 0 : 0x50));
    }
}
