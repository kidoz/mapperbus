#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <memory>
#include <vector>

#include "core/apu/apu.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/nrom.hpp"
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

TEST_CASE("RF Famicom controller data pins preserve only the documented floating bits",
          "[bus][controller]") {
    for (const auto wiring : {ControllerWiring::Nes, ControllerWiring::RfFamicom}) {
        for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
            IoMachine machine;
            machine.bus.set_controller_wiring(wiring);
            machine.bus.set_controller_timing(timing);
            machine.bus.write(0x4016, 1);
            for (const bool pressed : {false, true}) {
                for (int player = 0; player < 2; ++player) {
                    machine.pads.set_button_state(player, Button::A, pressed);
                    const Address address = static_cast<Address>(0x4016 + player);
                    const Byte floating =
                        wiring == ControllerWiring::RfFamicom && player == 0 ? 0xF8 : 0xE0;
                    for (unsigned value = 0; value <= 255; ++value) {
                        const auto expected =
                            static_cast<Byte>((value & floating) | (pressed ? 1U : 0U));
                        machine.bus.write(0, static_cast<Byte>(value));
                        CHECK(machine.read(address) == expected);
                        CHECK(machine.bus.open_bus() == expected);
                        machine.bus.write(0, static_cast<Byte>(value));
                        CHECK(machine.bus.read(address) == expected);
                        CHECK(machine.bus.open_bus() == expected);
                    }
                }
            }
        }
    }
    MemoryBus disconnected;
    disconnected.set_controller_wiring(ControllerWiring::RfFamicom);
    disconnected.write(0, 0xFF);
    CHECK(disconnected.read(0x4016) == 0xF8);
    CHECK(disconnected.read(0x4017) == 0xE0);
}

TEST_CASE("Microphone activity is live on RF port one and advances only on CPU clocks",
          "[bus][controller][microphone]") {
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        IoMachine machine;
        machine.bus.set_controller_timing(timing);
        machine.bus.set_microphone_active(true);
        CHECK((machine.bus.read(0x4016) & 4U) == 0); // NES wiring ignores the microphone
        machine.bus.set_controller_wiring(ControllerWiring::RfFamicom);
        for (int count = 0; count < 12; ++count) {
            machine.bus.write(0x4016, static_cast<Byte>(count & 1));
            CHECK((machine.bus.read(0x4016) & 4U) == 4);
            CHECK((machine.bus.read(0x4017) & 4U) == 0);
        }
        constexpr auto half = MemoryBus::kMicrophonePeriodCycles / 2;
        machine.bus.clock_cpu_bus(0, half - 1);
        machine.bus.set_microphone_active(true); // host polling must preserve phase
        CHECK((machine.read(0x4016) & 4U) == 0); // half-period boundary
        const auto before = machine.save();
        machine.bus.clock_cpu_bus(0, half);
        CHECK((machine.bus.read(0x4016) & 4U) == 4);
        const auto after = machine.save();
        machine.restore(before);
        machine.bus.clock_cpu_bus(0, half);
        CHECK((machine.bus.read(0x4016) & 4U) == 4);
        CHECK(machine.save() == after);
        machine.bus.set_microphone_active(false);
        CHECK((machine.bus.read(0x4016) & 4U) == 0);
        machine.bus.set_microphone_active(true);
        CHECK((machine.bus.read(0x4016) & 4U) == 4);
        machine.bus.clock_cpu_bus(0, half);
        CHECK_FALSE(machine.bus.microphone_level());
        machine.bus.reset_clock();
        CHECK(machine.bus.microphone_level());
    }
}

TEST_CASE("RF Famicom built-in controller two has six buttons and still shifts in ones",
          "[bus][controller]") {
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        IoMachine machine;
        machine.bus.set_controller_timing(timing);
        for (const int player : {0, 1})
            for (const auto button : {Button::A,
                                      Button::B,
                                      Button::Select,
                                      Button::Start,
                                      Button::Up,
                                      Button::Down,
                                      Button::Left,
                                      Button::Right})
                machine.pads.set_button_state(player, button, true);
        for (const auto wiring : {ControllerWiring::RfFamicom, ControllerWiring::Nes}) {
            machine.bus.set_controller_wiring(wiring);
            machine.latch();
            const Byte report = wiring == ControllerWiring::RfFamicom ? 0xF3 : 0xFF;
            for (unsigned bit = 0; bit < 12; ++bit) {
                CHECK((machine.read(0x4016) & 1U) == 1);
                CHECK((machine.read(0x4017) & 1U) == (bit < 8 ? ((report >> bit) & 1U) : 1U));
                (void)machine.read(0);
            }
        }
    }
}

TEST_CASE("RF button masking survives host updates and snapshots while strobed",
          "[bus][controller][state]") {
    IoMachine machine;
    machine.bus.set_controller_wiring(ControllerWiring::RfFamicom);
    machine.bus.set_controller_timing(ControllerTiming::RfFamicom);
    machine.bus.write(0x4016, 1);
    machine.pads.set_button_state(1, Button::A, true);
    machine.pads.set_button_state(1, Button::Select, true);
    machine.pads.set_button_state(1, Button::Start, true);
    machine.pads.set_button_state(1, Button::Up, true);
    const auto before = machine.save();
    const auto release_and_read = [&] {
        machine.write(0x4016, 0);
        (void)machine.read(0);
        REQUIRE_FALSE(machine.pads.strobed());
        for (const Byte expected : {Byte{1}, Byte{0}, Byte{0}, Byte{0}, Byte{1}})
            CHECK((machine.read(0x4017) & 1U) == expected);
    };
    release_and_read();
    const auto after = machine.save();
    machine.bus.set_controller_wiring(ControllerWiring::Nes);
    machine.restore(before);
    CHECK(machine.bus.controller_wiring() == ControllerWiring::RfFamicom);
    release_and_read();
    CHECK(machine.save() == after);
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

TEST_CASE("External Famicom pads have independent eight-button reports on D1",
          "[bus][controller][expansion]") {
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        for (const auto wiring : {ControllerWiring::Nes, ControllerWiring::RfFamicom}) {
            for (const Byte attached : {0, 1, 2, 3}) {
                INFO("timing " << int(timing) << ", wiring " << int(wiring) << ", attached "
                               << int(attached));
                IoMachine machine;
                Controller external;
                machine.bus.connect_expansion_controller(&external);
                machine.bus.set_controller_timing(timing);
                machine.bus.set_controller_wiring(wiring);
                machine.bus.set_expansion_controllers(attached);
                const Byte built_in[] = {0xA5, 0x5A};
                const Byte expansion[] = {0x3C, 0xCC}; // external pad two keeps Select/Start
                for (int player = 0; player < 2; ++player) {
                    for (unsigned bit = 0; bit < 8; ++bit) {
                        const auto button = static_cast<Button>(1U << bit);
                        machine.pads.set_button_state(player, button, built_in[player] >> bit & 1U);
                        external.set_button_state(player, button, expansion[player] >> bit & 1U);
                    }
                }
                machine.latch();
                for (unsigned bit = 0; bit < 12; ++bit) {
                    for (int player = 0; player < 2; ++player) {
                        const Byte mask =
                            player == 1 && wiring == ControllerWiring::RfFamicom ? 0xF3 : 0xFF;
                        const auto d0 = bit < 8 ? ((built_in[player] & mask) >> bit & 1U) : 1U;
                        const auto d1 = !(attached & (1U << player)) ? 0U
                                        : bit < 8 ? (expansion[player] >> bit & 1U)
                                                  : 1U;
                        machine.bus.write(0, 0xF8); // also check the floating/open-bus lines
                        const auto value = machine.read(static_cast<Address>(0x4016 + player));
                        CHECK((value & 3U) == (d0 | d1 << 1U));
                        CHECK((value & 0xFCU) ==
                              (wiring == ControllerWiring::RfFamicom && player == 0 ? 0xF8 : 0xE0));
                        (void)machine.read(0);
                    }
                }
            }
        }
    }
}

TEST_CASE("External pads share live strobe and read enables without attachment side effects",
          "[bus][controller][expansion]") {
    IoMachine machine;
    Controller external;
    machine.bus.connect_expansion_controller(&external);
    machine.bus.set_expansion_controllers(3);
    machine.bus.write(0x4016, 1);
    external.set_button_state(0, Button::A, true);
    CHECK((machine.read(0x4016) & 3U) == 2);
    external.set_button_state(0, Button::A, false);
    CHECK((machine.read(0x4016) & 3U) == 0);
    external.set_button_state(0, Button::B, true);
    external.set_button_state(1, Button::A, true);
    machine.latch();
    CHECK((machine.read(0x4016) & 2U) == 0);
    CHECK((machine.read(0x4016) & 2U) == 0); // continuous NES /OE, no second clock
    machine.bus.set_expansion_controllers(2);
    CHECK((machine.read(0x4016) & 2U) == 0);
    machine.bus.set_expansion_controllers(3);
    CHECK((machine.read(0x4016) & 2U) == 0); // changing attachment does not reload/clock
    CHECK((machine.read(0x4017) & 2U) == 2); // independent second report
    CHECK((machine.read(0x4016) & 2U) == 2); // first report advanced to B
    machine.bus.set_controller_timing(ControllerTiming::RfFamicom);
    CHECK((machine.read(0x4016) & 2U) == 0); // timing change released the held read once
    CHECK((machine.read(0x4017) & 2U) == 0);
}

TEST_CASE("OAM DMA clocks external and built-in serial lines together",
          "[bus][controller][dma][expansion]") {
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        IoMachine machine;
        Controller external;
        machine.bus.connect_expansion_controller(&external);
        machine.bus.set_expansion_controllers(3);
        machine.bus.set_controller_timing(timing);
        machine.pads.set_button_state(0, Button::B, true);
        external.set_button_state(0, Button::Select, true);
        machine.latch();
        REQUIRE(machine.bus.run_oam_dma(0x00, 0x4016, false) >= 513);
        // The NTSC split decoder aliases source offsets $16/$17 to their controller
        // registers. Both serial lines must experience exactly the same clocks.
        StateWriter built_in, expansion;
        machine.pads.save_state(built_in);
        external.save_state(expansion);
        CHECK(built_in.data()[2] == expansion.data()[2]); // all eight bits shifted to ones
        CHECK(built_in.data()[2] == 0xFF);
        CHECK((machine.read(0x4016) & 3U) == 3);
    }
}

TEST_CASE("DMC halted reads clock external pads with the selected console timing",
          "[bus][controller][dma][expansion]") {
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        IoMachine machine;
        Controller external, expected;
        machine.bus.connect_expansion_controller(&external);
        machine.bus.set_expansion_controllers(1);
        machine.bus.set_controller_timing(timing);
        for (const auto button : {Button::A, Button::Select, Button::Up, Button::Left}) {
            machine.pads.set_button_state(0, button, true);
            external.set_button_state(0, button, true);
            expected.set_button_state(0, button, true);
        }
        machine.latch();
        expected.write(1);
        expected.write(0);
        machine.bus.write(0x4012, 0); // sample $C000 does not alias either controller
        machine.bus.write(0x4013, 0);
        machine.bus.write(0x4015, 0x10);
        machine.bus.clock_cpu_bus(0, 3);
        const auto elapsed = machine.bus.run_dmc_dma(0x4016);
        REQUIRE(elapsed >= 3);
        const auto clocks = timing == ControllerTiming::RfFamicom ? elapsed - 1 : 1;
        for (unsigned clock = 0; clock < clocks; ++clock)
            expected.clock(0);
        StateWriter actual_state, expected_state;
        external.save_state(actual_state);
        expected.save_state(expected_state);
        CHECK(actual_state.data() == expected_state.data());
        CHECK((machine.read(0x4016) & 3U) == (expected.peek(0) ? 3 : 0));
    }
}

TEST_CASE("Four Score multiplexes four full pads and distinct signatures on D0",
          "[bus][controller][four-score]") {
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
            for (const auto wiring : {ControllerWiring::Nes, ControllerWiring::RfFamicom}) {
                IoMachine machine;
                Controller extra;
                machine.bus.connect_expansion_controller(&extra);
                machine.bus.set_region(region);
                machine.bus.set_controller_timing(timing);
                machine.bus.set_controller_wiring(wiring);
                machine.bus.set_expansion_controllers(3); // suppressed, retained preference
                const Byte reports[] = {0xA5, 0x0C, 0x5A, 0xC3};
                for (int player = 0; player < 4; ++player)
                    for (unsigned bit = 0; bit < 8; ++bit)
                        (player < 2 ? machine.pads : extra)
                            .set_button_state(player % 2,
                                              static_cast<Button>(1U << bit),
                                              reports[player] >> bit & 1U);
                machine.bus.set_controller_mode(ControllerMode::FourScore);
                machine.latch();
                const uint32_t expected[] = {0x085AA5, 0x04C30C};
                for (unsigned bit = 0; bit < 32; ++bit) {
                    for (int port = 0; port < 2; ++port) {
                        const auto value = machine.read(static_cast<Address>(0x4016 + port));
                        CHECK((value & 3U) == (bit < 24 ? (expected[port] >> bit & 1U) : 1U));
                        if (bit >= 16 && bit < 24)
                            CHECK((value & 1U) == (bit == (port == 0 ? 19U : 18U) ? 1U : 0U));
                        (void)machine.read(0);
                    }
                }
                CHECK(machine.bus.expansion_controllers() == 3);
            }
        }
    }
}

TEST_CASE("Four Score shares live strobe and continuous read clocks and switches modes",
          "[bus][controller][four-score]") {
    IoMachine machine;
    Controller extra;
    machine.bus.connect_expansion_controller(&extra);
    machine.bus.set_expansion_controllers(3);
    machine.bus.set_controller_mode(ControllerMode::FourScore);
    machine.bus.write(0x4016, 1);
    machine.pads.set_button_state(0, Button::A, true);
    CHECK((machine.read(0x4016) & 3U) == 1);
    machine.pads.set_button_state(0, Button::A, false);
    CHECK((machine.read(0x4016) & 3U) == 0);
    machine.pads.set_button_state(0, Button::B, true);
    extra.set_button_state(0, Button::A, true); // changed after high strobe
    machine.bus.write(0x4016, 0);
    CHECK((machine.read(0x4016) & 3U) == 0);
    CHECK((machine.read(0x4016) & 3U) == 0);                    // same NES /OE
    machine.bus.set_controller_mode(ControllerMode::FourScore); // reapply preserves report
    (void)machine.read(0);
    CHECK((machine.read(0x4016) & 3U) == 1); // B
    for (int bit = 2; bit <= 8; ++bit) {
        (void)machine.read(0);
        CHECK((machine.read(0x4016) & 3U) == (bit == 8 ? 1 : 0)); // player three A
    }
    machine.bus.set_controller_mode(ControllerMode::Standard);
    machine.latch();
    CHECK((machine.read(0x4016) & 3U) == 2); // D1 attachment restored
}

TEST_CASE("Four Score DMC and OAM reads advance the full 24-bit report",
          "[bus][controller][four-score][dma]") {
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        for (const bool oam : {false, true}) {
            IoMachine machine;
            Controller extra;
            machine.bus.connect_expansion_controller(&extra);
            machine.bus.set_controller_timing(timing);
            extra.set_button_state(0, Button::B, true);
            machine.bus.set_controller_mode(ControllerMode::FourScore);
            machine.latch();
            unsigned clocks = 9;
            if (oam) {
                REQUIRE(machine.bus.run_oam_dma(0, 0x4016, false) == 513);
            } else {
                machine.bus.write(0x4012, 0);
                machine.bus.write(0x4013, 0);
                machine.bus.write(0x4015, 0x10);
                machine.bus.clock_cpu_bus(0, 3);
                const auto elapsed = machine.bus.run_dmc_dma(0x4016);
                REQUIRE(elapsed >= 3);
                clocks = timing == ControllerTiming::RfFamicom ? elapsed - 1 : 1;
            }
            for (unsigned bit = clocks; bit < 28; ++bit) {
                CHECK((machine.read(0x4016) & 1U) == (bit < 24 ? (0x080200U >> bit & 1U) : 1U));
                (void)machine.read(0);
            }
        }
    }
}

TEST_CASE("Hori exposes four adapter pads on D1 beside two independent built-in pads",
          "[bus][controller][hori]") {
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
            for (const auto wiring : {ControllerWiring::Nes, ControllerWiring::RfFamicom}) {
                for (const Byte attached : {0, 3}) {
                    IoMachine machine;
                    Controller first, second;
                    machine.bus.connect_expansion_controller(&first);
                    machine.bus.connect_additional_expansion_controller(&second);
                    machine.bus.set_region(region);
                    machine.bus.set_controller_timing(timing);
                    machine.bus.set_controller_wiring(wiring);
                    machine.bus.set_expansion_controllers(attached);
                    machine.bus.set_microphone_active(true);
                    const Byte buttons[] = {0xA5, 0x5A, 0x0C, 0xFF, 0xC3, 0xA5};
                    for (int player = 0; player < 6; ++player)
                        for (unsigned bit = 0; bit < 8; ++bit)
                            (player < 2   ? machine.pads
                             : player < 4 ? first
                                          : second)
                                .set_button_state(player % 2,
                                                  static_cast<Button>(1U << bit),
                                                  buttons[player] >> bit & 1U);
                    machine.bus.set_controller_mode(ControllerMode::HoriFourPlayer);
                    machine.latch();
                    const uint32_t reports[] = {0x04C30C, 0x08A5FF};
                    for (unsigned bit = 0; bit < 32; ++bit) {
                        for (int port = 0; port < 2; ++port) {
                            machine.bus.write(0, 0xF8);
                            const auto value = machine.read(static_cast<Address>(0x4016 + port));
                            const auto mask =
                                port == 1 && wiring == ControllerWiring::RfFamicom ? 0xF3U : 0xFFU;
                            const auto d0 = bit < 8 ? (buttons[port] & mask) >> bit & 1U : 1U;
                            const auto d1 = bit < 24 ? reports[port] >> bit & 1U : 1U;
                            CHECK((value & 3U) == (d0 | d1 << 1U));
                            if (bit >= 16 && bit < 24)
                                CHECK((value >> 1U & 1U) ==
                                      (bit == (port == 0 ? 18U : 19U) ? 1U : 0U));
                            const bool rf_microphone =
                                wiring == ControllerWiring::RfFamicom && port == 0;
                            CHECK((value & 4U) == (rf_microphone ? 4U : 0U));
                            CHECK((value & 0xF8U) == (rf_microphone ? 0xF8U : 0xE0U));
                            (void)machine.read(0);
                        }
                    }
                    CHECK(machine.bus.expansion_controllers() == attached);
                }
            }
        }
    }
}

TEST_CASE("Hori shares live strobe and read enables without merging built-in inputs",
          "[bus][controller][hori]") {
    IoMachine machine;
    Controller first, second;
    machine.bus.connect_expansion_controller(&first);
    machine.bus.connect_additional_expansion_controller(&second);
    machine.bus.set_controller_mode(ControllerMode::HoriFourPlayer);
    machine.bus.write(0x4016, 1);
    machine.pads.set_button_state(0, Button::A, true);
    CHECK((machine.read(0x4016) & 3U) == 1);
    machine.pads.set_button_state(0, Button::A, false);
    first.set_button_state(0, Button::A, true);
    first.set_button_state(0, Button::B, true);
    second.set_button_state(0, Button::Select, true);
    CHECK((machine.read(0x4016) & 3U) == 2);
    machine.bus.write(0x4016, 0);
    CHECK((machine.read(0x4016) & 3U) == 2);
    machine.bus.set_controller_mode(ControllerMode::HoriFourPlayer);
    CHECK((machine.read(0x4016) & 3U) == 2); // same /OE, reapplying mode preserves position
    for (unsigned bit = 1; bit < 24; ++bit) {
        (void)machine.read(0);
        CHECK((machine.read(0x4016) & 2U) == ((0x040403U >> bit & 1U) << 1U));
    }
    machine.bus.set_controller_mode(ControllerMode::FourScore);
    machine.latch();
    for (unsigned bit = 0; bit < 24; ++bit) {
        CHECK((machine.read(0x4016) & 3U) == (0x080300U >> bit & 1U));
        (void)machine.read(0);
    }
}

TEST_CASE("NTSC Hori DMC and OAM reads clock D0 and D1 together", "[bus][controller][hori][dma]") {
    for (const auto timing : {ControllerTiming::NesAvFamicom, ControllerTiming::RfFamicom}) {
        for (const bool oam : {false, true}) {
            IoMachine machine;
            Controller first, second;
            machine.bus.connect_expansion_controller(&first);
            machine.bus.connect_additional_expansion_controller(&second);
            machine.bus.set_controller_timing(timing);
            first.set_button_state(0, Button::B, true);
            second.set_button_state(1, Button::Start, true);
            machine.bus.set_controller_mode(ControllerMode::HoriFourPlayer);
            machine.latch();
            unsigned clocks[] = {9, 8};
            if (oam) {
                REQUIRE(machine.bus.run_oam_dma(0, 0x4016, false) == 513);
            } else {
                machine.bus.write(0x4012, 0);
                machine.bus.write(0x4013, 0);
                machine.bus.write(0x4015, 0x10);
                machine.bus.clock_cpu_bus(0, 3);
                const auto elapsed = machine.bus.run_dmc_dma(0x4016);
                REQUIRE(elapsed >= 3);
                clocks[0] = timing == ControllerTiming::RfFamicom ? elapsed - 1 : 1;
                clocks[1] = 0;
            }
            const uint32_t reports[] = {0x040002, 0x080800};
            for (int port = 0; port < 2; ++port)
                for (unsigned bit = clocks[port]; bit < 28; ++bit) {
                    const auto d0 = bit < 8 ? 0U : 1U;
                    const auto d1 = bit < 24 ? reports[port] >> bit & 1U : 1U;
                    CHECK((machine.read(static_cast<Address>(0x4016 + port)) & 3U) ==
                          (d0 | d1 << 1U));
                    (void)machine.read(0);
                }
        }
    }
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

namespace {
struct NativePpuProbe {
    std::array<Byte, 16> values{};
    Byte open_bus_mask = 0xFFU;
    uint16_t read_claims = 0xFFFFU;
    uint16_t write_claims = 0xFFFFU;
    Address read_address = 0U;
    Address write_address = 0U;
    Address observed_read = 0U;
    Address observed_write = 0U;
    Byte observed_value = 0U;
    unsigned native_reads = 0U;
};
// A synthetic test device, not a OneBus implementation. Decode only the exact
// CPU aperture and drive its low nibble to exercise CPU open-bus forwarding.
class NativePpuMapper final : public Nrom {
  public:
    NativePpuMapper(const INesHeader& h,
                    std::vector<Byte> prg,
                    std::vector<Byte> chr,
                    std::shared_ptr<NativePpuProbe> probe)
        : Nrom(h, std::move(prg), std::move(chr)), probe_(std::move(probe)) {}
    std::optional<Byte> read_ppu_register(Address addr, Byte open_bus) override {
        probe_->read_address = addr;
        if (addr < 0x2010U || addr > 0x201FU || (probe_->read_claims & (1U << (addr & 15U))) == 0U)
            return std::nullopt;
        ++probe_->native_reads;
        return static_cast<Byte>((open_bus & 0xF0U) | (probe_->values[addr & 15U] & 15U));
    }
    bool write_ppu_register(Address addr, Byte value) override {
        probe_->write_address = addr;
        if (addr < 0x2010U || addr > 0x201FU || (probe_->write_claims & (1U << (addr & 15U))) == 0U)
            return false;
        probe_->values[addr & 15U] = value;
        return true;
    }
    Byte cpu_open_bus_mask() const override {
        return probe_->open_bus_mask;
    }
    void observe_cpu_read(Address addr, Byte value) override {
        probe_->observed_read = addr;
        probe_->observed_value = value;
    }
    void observe_cpu_write(Address addr, Byte) override {
        probe_->observed_write = addr;
    }

  private:
    std::shared_ptr<NativePpuProbe> probe_;
};
std::vector<Byte> native_ppu_image() {
    std::vector<Byte> image(16U + 16384U + 8192U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 1U;
    image[5] = 1U;
    image[7] = 8U;
    for (unsigned v = 16U + 16384U - 6U; v < 16U + 16384U; v += 2U)
        image[v + 1U] = 0x80U;
    return image;
}
Cartridge native_ppu_cartridge(const std::shared_ptr<NativePpuProbe>& probe,
                               const std::vector<Byte>& image = native_ppu_image()) {
    register_builtin_mappers();
    // Override a real factory only during construction; restore it before any
    // assertion so this fixture cannot add a fake registered mapper ID.
    MapperRegistry::instance().register_mapper(
        0U, [probe](const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr) {
            return std::make_unique<NativePpuMapper>(h, std::move(prg), std::move(chr), probe);
        });
    auto result = Cartridge::from_data(image);
    register_builtin_mappers();
    REQUIRE(result);
    return std::move(*result);
}
std::vector<Byte> ppu_state(const Ppu& ppu) {
    StateWriter w;
    ppu.save_state(w);
    return w.take();
}
} // namespace

TEST_CASE("Native PPU register hooks receive raw addresses and CPU open bus before mirroring",
          "[bus][native-ppu]") {
    for (bool timed : {false, true}) {
        auto probe = std::make_shared<NativePpuProbe>();
        auto c = native_ppu_cartridge(probe);
        MemoryBus bus;
        bus.connect_cartridge(&c); // Claimed registers also work without a PPU attached.
        for (unsigned address = 0x2000U; address < 0x4000U; ++address) {
            const auto addr = static_cast<Address>(address);
            const auto value = static_cast<Byte>(address ^ (address >> 8U));
            if (timed)
                bus.write_cpu(addr, value);
            else
                bus.write(addr, value);
            REQUIRE(probe->write_address == addr);
            REQUIRE(probe->observed_write == addr);
            bus.write(0U, 0xB0U);
            const auto result = timed ? bus.read_cpu(addr) : bus.read(addr);
            const bool claimed = address >= 0x2010U && address <= 0x201FU;
            REQUIRE(result == (claimed ? 0xB0U | (value & 15U) : 0U));
            REQUIRE(probe->read_address == addr);
            REQUIRE(probe->observed_read == addr);
            REQUIRE(probe->observed_value == result);
            REQUIRE(bus.open_bus() == result);
        }
        REQUIRE(probe->native_reads == 16U);
    }
}

TEST_CASE("Claimed native reads and writes leave every ordinary PPU register side effect untouched",
          "[bus][native-ppu]") {
    auto probe = std::make_shared<NativePpuProbe>();
    auto c = native_ppu_cartridge(probe);
    MemoryBus bus;
    Ppu ppu;
    ppu.reset();
    ppu.connect_cartridge(&c);
    bus.connect_ppu(&ppu);
    bus.connect_cartridge(&c);
    bus.write(0x2000U, 0x84U);
    bus.write(0x2003U, 0x53U);
    bus.write(0x2005U, 0x19U); // Keep the shared scroll/address latch half-written.
    ppu.step_dots(241U * 341U + 2U);
    REQUIRE(ppu.nmi_line());
    const auto before = ppu_state(ppu);
    for (unsigned addr = 0x2010U; addr <= 0x201FU; ++addr) {
        bus.write(static_cast<Address>(addr), static_cast<Byte>(addr));
        bus.write(0U, 0xA0U);
        REQUIRE(bus.read(static_cast<Address>(addr)) == (0xA0U | (addr & 15U)));
        REQUIRE(ppu_state(ppu) == before);
    }
    probe->values[0] = 0U;
    bus.write(0U, 0U);
    REQUIRE(bus.read(0x2010U) == 0U); // Zero is a consumed read, not a fallthrough marker.
    REQUIRE(ppu_state(ppu) == before);
    REQUIRE(ppu.nmi_line());
    REQUIRE(ppu.write_latch());
    REQUIRE((bus.read(0x2002U) & 0x80U) != 0U); // Only the real status read clears vblank/w.
    REQUIRE_FALSE(ppu.nmi_line());
    REQUIRE_FALSE(ppu.write_latch());
}

TEST_CASE("Native register reads and writes can independently decline normal PPU aliases",
          "[bus][native-ppu]") {
    auto probe = std::make_shared<NativePpuProbe>();
    probe->read_claims = 2U;
    probe->write_claims = 1U;
    probe->values[1] = 7U;
    auto c = native_ppu_cartridge(probe);
    MemoryBus bus;
    Ppu ppu;
    ppu.reset();
    bus.connect_ppu(&ppu);
    bus.connect_cartridge(&c);
    bus.write(0x2000U, 0x94U);
    bus.write(0x2010U, 0x2BU); // Write consumed; read declines to the PPU I/O latch.
    REQUIRE(ppu.ppuctrl() == 0x94U);
    REQUIRE(bus.read(0x2010U) == 0x94U);
    bus.write(0x2011U, 0xA5U); // Write declines to PPUMASK; read consumed by device.
    REQUIRE(ppu.ppumask() == 0xA5U);
    bus.write(0U, 0xD0U);
    REQUIRE(bus.read(0x2011U) == 0xD7U);
    REQUIRE(bus.read(0x2000U) == 0xA5U); // Native read did not overwrite PPU I/O latch.
    probe->open_bus_mask = 0x7FU;
    bus.write(0U, 0xF0U);
    REQUIRE(bus.read(0x2011U) == 0x77U); // The hook receives the cartridge-masked CPU bus.
    bus.write(0x3010U, 0x1CU);           // Raw decode must not invent mirrors of native registers.
    REQUIRE(ppu.ppuctrl() == 0x1CU);
    REQUIRE(probe->values[0] == 0x2BU);
}

TEST_CASE("Ordinary cartridge PPU mirrors retain existing read and write behavior",
          "[bus][native-ppu]") {
    register_builtin_mappers();
    auto result = Cartridge::from_data(native_ppu_image());
    REQUIRE(result);
    MemoryBus bus;
    Ppu ppu;
    ppu.reset();
    bus.connect_cartridge(&*result);
    bus.connect_ppu(&ppu);
    for (unsigned addr = 0x2000U; addr < 0x4000U; addr += 8U) {
        const auto value = static_cast<Byte>(addr >> 3U);
        bus.write(static_cast<Address>(addr), value);
        REQUIRE(ppu.ppuctrl() == value);
        REQUIRE(bus.read(static_cast<Address>(addr)) == value);
    }
    bus.write(0x3FF6U, 0x20U);
    bus.write(0x3FF6U, 0U);
    bus.write(0x3FF7U, 0x5AU);
    REQUIRE(ppu.vram()[0] == 0x5AU);
    bus.connect_cartridge(nullptr);
    bus.write(0x2010U, 0xA7U);
    REQUIRE(ppu.ppuctrl() == 0xA7U);
    REQUIRE(bus.read(0x2010U) == 0xA7U);
}

TEST_CASE("CPU instructions and OAM DMA use the native PPU register aperture",
          "[bus][native-ppu][dma]") {
    auto probe = std::make_shared<NativePpuProbe>();
    auto image = native_ppu_image();
    const std::array<Byte, 25> program{
        0x78U, 0xA9U, 0xA7U, 0x8DU, 0x16U, 0x20U, 0xADU, 0x16U, 0x20U, 0x29U, 0x0FU, 0xC9U, 7U,
        0xD0U, 0xFEU, 0xA9U, 0xD6U, 0x85U, 0x12U, 0xE6U, 0x13U, 0x4CU, 0x13U, 0x80U, 0xEAU};
    std::ranges::copy(program, image.begin() + 16);
    auto c = native_ppu_cartridge(probe, image);
    Emulator machine;
    REQUIRE(machine.load_cartridge(std::move(c), ""));
    machine.power_on();
    machine.step_frame();
    REQUIRE(machine.read_cpu(0x12U) == 0xD6U);
    REQUIRE(probe->values[6] == 0xA7U);
    auto dma_probe = std::make_shared<NativePpuProbe>();
    for (unsigned i = 0U; i < 16U; ++i)
        dma_probe->values[i] = static_cast<Byte>(i);
    auto dma_cart = native_ppu_cartridge(dma_probe);
    Ppu ppu;
    ppu.reset();
    MemoryBus bus;
    bus.connect_ppu(&ppu);
    bus.connect_cartridge(&dma_cart);
    REQUIRE(bus.run_oam_dma(0x20U, 0x0200U, false) >= 513U);
    REQUIRE(dma_probe->native_reads == 16U);
    for (unsigned i = 0U; i < 16U; ++i) {
        bus.write(0x2003U, static_cast<Byte>(0x10U + i));
        const unsigned mask = (i & 3U) == 2U ? 3U : 15U;
        REQUIRE((bus.read(0x2004U) & mask) == (i & mask));
    }
}
