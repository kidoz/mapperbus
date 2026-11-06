// Synthesized CPU accuracy tests.
//
// Each test builds a minimal NROM image with an inline 6502 program, loads it
// into the emulator, steps per-instruction, and validates the resulting
// register/memory state via Emulator::read_cpu() and Emulator::cpu().
// No external test ROMs are required — everything is synthesized in C++.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <filesystem>

#include "../support/legacy_state.hpp"
#include "../support/test_rom.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"
#include "core/types.hpp"

using namespace mapperbus::core;
using mapperbus::tests::build_ines_rom;

namespace {
struct DmaMachine {
    Apu apu;
    Ppu ppu;
    MemoryBus bus;
    Cpu cpu{bus};
    std::optional<Cartridge> cartridge;

    DmaMachine(std::initializer_list<Byte> program, bool separate_vectors = false) {
        register_builtin_mappers();
        std::vector<Byte> image(16 + 32768 + 8192);
        image[0] = 'N';
        image[1] = 'E';
        image[2] = 'S';
        image[3] = 0x1A;
        image[4] = 2;
        image[5] = 1;
        std::ranges::copy(program, image.begin() + 16);
        image[16 + 0x4000] = 0xD5;
        for (const auto vector : {0x7FFA, 0x7FFC, 0x7FFE}) {
            image[16 + vector] = 0;
            image[16 + vector + 1] = separate_vectors && vector != 0x7FFC
                                         ? static_cast<Byte>(vector == 0x7FFA ? 0x90 : 0xA0)
                                         : Byte{0x80};
        }
        image[16 + 0x1000] = 0xEA; // first NMI/IRQ handler instruction
        image[16 + 0x2000] = 0xEA;
        auto loaded = Cartridge::from_data(image);
        REQUIRE(loaded);
        cartridge = std::move(*loaded);
        bus.connect_cartridge(&*cartridge);
        bus.connect_apu(&apu);
        bus.connect_ppu(&ppu);
        cpu.reset();
    }

    DmcChannel dmc_channel() const {
        StateWriter writer;
        apu.save_state(writer);
        constexpr auto kOffset =
            2 * sizeof(PulseChannel) + sizeof(TriangleChannel) + sizeof(NoiseChannel);
        DmcChannel channel;
        std::memcpy(&channel, writer.data().data() + kOffset, sizeof(channel));
        return channel;
    }

    std::vector<Byte> device_state() const {
        StateWriter writer;
        ppu.save_state(writer);
        apu.save_state(writer);
        bus.save_state(writer);
        return writer.take();
    }

    void restore_devices(std::span<const Byte> state) {
        StateReader reader(state);
        ppu.load_state(reader);
        apu.load_state(reader);
        bus.load_state(reader);
        REQUIRE(reader.ok());
    }

    void start_dmc() {
        bus.write(0x4010, 0x8F); // IRQ after the single byte is fetched
        bus.write(0x4012, 0);
        bus.write(0x4013, 0);
        bus.write(0x4015, 0x10);
    }

    void raise_irq() {
        start_dmc();
        apu.step(3);
        REQUIRE(apu.dmc_dma_ready());
        apu.complete_dmc_dma(0);
        REQUIRE(apu.irq_pending());
    }

    void before_vblank(uint16_t dot) {
        StateWriter writer;
        ppu.save_state(writer);
        auto state = writer.take();
        const uint16_t line = 240;
        std::memcpy(state.data(), &line, sizeof(line));
        std::memcpy(state.data() + sizeof(line), &dot, sizeof(dot));
        StateReader reader(state);
        ppu.load_state(reader);
        REQUIRE(reader.ok());
        ppu.write_register(0, 0x80);
    }

    void reload_on_cycle(uint16_t cycle) {
        // Seed a hardware-facing output-unit state: its next byte boundary
        // empties the sample buffer on this get cycle during OAM DMA.
        DmcChannel channel;
        channel.enabled = true;
        channel.irq_enabled = true;
        channel.timer_period = 428;
        channel.timer = static_cast<uint16_t>((cycle - 1U) % 428U);
        channel.bits_remaining = static_cast<Byte>((cycle - 1U) / 428U + 1U);
        channel.sample_buffer_empty = false;
        channel.sample_buffer = 0xD5;
        channel.bytes_remaining = 1;
        seed_channel(channel);
    }

    void seed_channel(const DmcChannel& channel, bool pending = false) {
        StateWriter writer;
        apu.save_state(writer);
        auto state = writer.take();
        constexpr auto kOffset =
            2 * sizeof(PulseChannel) + sizeof(TriangleChannel) + sizeof(NoiseChannel);
        std::memcpy(state.data() + kOffset, &channel, sizeof(channel));
        state[state.size() - 9] = static_cast<Byte>(pending);
        state[state.size() - 6] = static_cast<Byte>(channel.enabled);
        state[state.size() - 4] = static_cast<Byte>(pending);
        std::memcpy(state.data() + state.size() - 3, &channel.current_address, sizeof(Address));
        state.back() = 0;
        StateReader reader(state);
        apu.load_state(reader);
        REQUIRE(reader.ok());
    }
};
} // namespace

TEST_CASE("CLI and PLP clear I after polling and allow one following instruction",
          "[cpu][interrupt]") {
    for (const Byte opcode : {0x58, 0x28}) {
        DmaMachine machine({opcode, 0xEA, 0xEA});
        machine.bus.write(0x01FE, 0x20); // PLP clears I
        machine.raise_irq();
        REQUIRE(machine.cpu.step() == (opcode == 0x58 ? 2 : 4));
        CHECK(machine.cpu.step() == 2);
        CHECK(machine.cpu.step() == 7);
        CHECK(machine.cpu.pc() == 0x8000);
    }
}

TEST_CASE("SEI and PLP cannot cancel an IRQ accepted with the old I flag", "[cpu][interrupt]") {
    for (const Byte opcode : {0x78, 0x28}) {
        DmaMachine machine({0x58, 0xEA, opcode, 0xEA});
        machine.bus.write(0x01FE, 0x24); // PLP sets I
        REQUIRE(machine.cpu.step() == 2);
        REQUIRE(machine.cpu.step() == 2);
        machine.raise_irq();
        CHECK(machine.cpu.step() == (opcode == 0x78 ? 2 : 4));
        CHECK((machine.cpu.status() & 4U) != 0);
        CHECK(machine.cpu.step() == 7);
    }
}

TEST_CASE("A final-cycle IRQ acknowledgement cannot cancel the prior poll", "[cpu][interrupt]") {
    DmaMachine machine({0x58, 0xEA, 0x8D, 0x15, 0x40, 0xEA});
    REQUIRE(machine.cpu.step() == 2);
    REQUIRE(machine.cpu.step() == 2);
    machine.raise_irq();
    CHECK(machine.cpu.step() == 4); // STA $4015 with A=0 clears the source
    CHECK_FALSE(machine.apu.irq_pending());
    CHECK(machine.cpu.step() == 7);
}

TEST_CASE("NMI sampling distinguishes penultimate and final instruction cycles",
          "[cpu][interrupt][nmi]") {
    for (const bool late : {false, true}) {
        DmaMachine machine({0xEA, 0xEA, 0xEA}, true);
        machine.before_vblank(late ? 337 : 340);
        REQUIRE(machine.cpu.step() == 2);
        if (late)
            CHECK(machine.cpu.step() == 2);
        // A status acknowledgement cannot erase an edge the CPU has latched.
        (void)machine.bus.read(0x2002);
        CHECK(machine.cpu.step() == 7);
        CHECK(machine.cpu.pc() == 0x9000);
        CHECK(machine.cpu.step() == 2); // at least one handler instruction
    }
}

TEST_CASE("Machine snapshots replay across a shortened odd frame", "[cpu][ppu][state]") {
    DmaMachine fixture({0xA9, 8, 0x8D, 1, 0x20, 0x4C, 5, 0x80});
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("PPU fixture did not produce a cartridge");
    emulator.reset();
    emulator.step_frame();
    emulator.step_frame();
    const auto before = emulator.save_state();
    emulator.step_frame();
    const auto after = emulator.save_state();
    REQUIRE(emulator.load_state(before));
    emulator.step_frame();
    CHECK(emulator.save_state() == after);
}

TEST_CASE("Branches poll NMI before the operand and again on page crossing",
          "[cpu][interrupt][nmi]") {
    SECTION("taken same-page branch defers an edge arriving after the first poll") {
        DmaMachine machine({0xD0, 0, 0xEA}, true);
        machine.before_vblank(337);
        CHECK(machine.cpu.step() == 3);
        CHECK(machine.cpu.step() == 2);
        CHECK(machine.cpu.step() == 7);
        CHECK(machine.cpu.pc() == 0x9000);
    }
    SECTION("page correction provides a second polling opportunity") {
        DmaMachine machine({0xD0, 0xFC}, true); // $8002 - 4 -> $7FFE
        machine.before_vblank(334);
        CHECK(machine.cpu.step() == 4);
        CHECK(machine.cpu.step() == 7);
        CHECK(machine.cpu.pc() == 0x9000);
    }
}

TEST_CASE("NMI hijacks IRQ or BRK without changing the pushed PC and B flag",
          "[cpu][interrupt][nmi]") {
    for (const bool irq : {false, true}) {
        DmaMachine machine(irq ? std::initializer_list<Byte>{0x58, 0xEA, 0xEA}
                               : std::initializer_list<Byte>{0, 0xEA},
                           true);
        if (irq) {
            REQUIRE(machine.cpu.step() == 2);
            machine.raise_irq();
            REQUIRE(machine.cpu.step() == 2);
        }
        machine.before_vblank(334);
        CHECK(machine.cpu.step() == 7);
        CHECK(machine.cpu.pc() == 0x9000);
        CHECK(machine.bus.read(0x01FD) == 0x80);
        CHECK(machine.bus.read(0x01FC) == 2);
        CHECK((machine.bus.read(0x01FB) & 0x30U) == (irq ? 0x20 : 0x30));
    }
}

TEST_CASE("NMI after vector selection waits through the first IRQ handler instruction",
          "[cpu][interrupt][nmi]") {
    DmaMachine machine({0, 0xEA}, true);
    machine.before_vblank(325);
    CHECK(machine.cpu.step() == 7);
    CHECK(machine.cpu.pc() == 0xA000);
    CHECK(machine.cpu.step() == 2);
    CHECK(machine.cpu.step() == 7);
    CHECK(machine.cpu.pc() == 0x9000);
}

TEST_CASE("NMI latched during OAM survives a source read clearing PPUSTATUS",
          "[cpu][interrupt][dma][nmi]") {
    DmaMachine machine({0xA9, 0x20, 0x8D, 0x14, 0x40, 0xEA}, true);
    REQUIRE(machine.cpu.step() == 2);
    machine.before_vblank(328);
    REQUIRE(machine.cpu.step() == 517);
    CHECK_FALSE(machine.ppu.nmi_line()); // page $20 reads the mirrored status port
    CHECK(machine.cpu.step() == 2);
    CHECK(machine.cpu.step() == 7);
    CHECK(machine.cpu.pc() == 0x9000);
}

TEST_CASE("NMI latched during DMC is serviced once after the halted instruction",
          "[cpu][interrupt][dma][nmi]") {
    DmaMachine machine({0xEA, 0xEA}, true);
    machine.start_dmc();
    machine.apu.step(3);
    machine.before_vblank(340);
    REQUIRE(machine.cpu.step() == 5);
    CHECK(machine.cpu.step() == 7);
    CHECK(machine.cpu.pc() == 0x9000);
    CHECK(machine.cpu.step() == 2); // asserted NMI level does not retrigger
}

TEST_CASE("Accepted IRQ survives snapshots with I set and rejects invalid interrupt state",
          "[cpu][interrupt][state]") {
    DmaMachine fixture(
        {0xA9, 0x80, 0x8D, 0x10, 0x40, 0xA9, 0x10, 0x8D, 0x15, 0x40, 0x58, 0x78, 0xEA});
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("Interrupt fixture did not produce a cartridge");
    emulator.reset();
    for (int instruction = 0; instruction < 6; ++instruction)
        emulator.step_instruction();
    REQUIRE((emulator.cpu().status() & 4U) != 0);
    const auto before = emulator.save_state();
    const auto cycles = emulator.cpu().cycles();
    emulator.step_instruction();
    REQUIRE(emulator.cpu().cycles() == cycles + 7);
    const auto after = emulator.save_state();
    REQUIRE(emulator.load_state(before));
    emulator.step_instruction();
    CHECK(emulator.save_state() == after);

    StateWriter cpu, ppu, apu, bus;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    Apu{}.save_state(apu);
    MemoryBus{}.save_state(bus);
    const auto cpu_end = 43 + cpu.data().size();
    const auto ppu_end = cpu_end + ppu.data().size();
    const auto apu_end = ppu_end + apu.data().size();
    const auto bus_end = apu_end + bus.data().size();
    for (const auto [offset, invalid] : {std::pair{cpu_end - 1, Byte{3}},
                                         {ppu_end - 2, Byte{2}},
                                         {ppu_end - 2, Byte{1}},
                                         {ppu_end - 3, Byte{2}},
                                         {ppu_end - 4, Byte{2}},
                                         {apu_end - 7, Byte{5}},
                                         {bus_end - 5, Byte{1}},
                                         {bus_end - 6, Byte{2}},
                                         {bus_end - 7, Byte{2}}}) {
        auto corrupt = before;
        corrupt[offset] = invalid;
        CHECK_FALSE(emulator.load_state(corrupt));
        CHECK(emulator.save_state() == after);
    }
}

TEST_CASE("DMC load DMA halts an eligible read within the instruction", "[cpu][dma]") {
    DmaMachine machine({0xEA, 0xEA});
    machine.start_dmc();
    CHECK(machine.cpu.step() == 2);
    CHECK(machine.cpu.step() == 5); // load halt + dummy + get, on the NOP dummy read
    CHECK((machine.bus.read(0x4015) & 0x90U) == 0x80);
}

TEST_CASE("DMC get completion wraps the address and applies loop or IRQ state", "[cpu][dma]") {
    for (const bool loop : {false, true}) {
        DmaMachine machine({0xEA});
        DmcChannel channel;
        channel.enabled = true;
        channel.irq_enabled = true;
        channel.loop_flag = loop;
        channel.current_address = 0xFFFF;
        channel.sample_address = 0xC040;
        channel.sample_length = 17;
        channel.bytes_remaining = 1;
        channel.timer_period = 428;
        machine.seed_channel(channel, true);
        CHECK((machine.bus.read(0x4015) & 0x90U) == 0x10);
        REQUIRE(machine.bus.run_dmc_dma(0x0200) == 4);
        CHECK(machine.apu.dmc_dma_address() == (loop ? 0xC040 : 0x8000));
        CHECK((machine.bus.read(0x4015) & 0x90U) == (loop ? 0x10 : 0x80));
        CHECK(machine.bus.run_dmc_dma(0x0200) == 0); // buffer is full
        machine.bus.write(0x4015, 0);
        CHECK((machine.bus.read(0x4015) & 0x90U) == 0);
    }
}

TEST_CASE("DMC halt waits through CPU writes and realigns its get", "[cpu][dma]") {
    SECTION("one write changes a three-cycle load into four cycles") {
        DmaMachine machine({0x8D, 0, 0x03, 0xEA}); // STA $0300
        machine.start_dmc();
        CHECK(machine.cpu.step() == 4);
        CHECK(machine.cpu.step() == 6);
        CHECK(machine.cpu.cycles() == 10);
    }
    SECTION("both NMOS RMW writes complete before the halt") {
        DmaMachine machine({0xE6, 0x10, 0xEA}); // INC $10
        machine.start_dmc();
        CHECK(machine.cpu.step() == 5);
        CHECK(machine.bus.read(0x10) == 1);
        CHECK(machine.cpu.step() == 5);
        CHECK(machine.cpu.cycles() == 10);
    }
}

TEST_CASE("DMC and OAM overlap setup and preserve every implemented OAM bit", "[cpu][dma]") {
    for (const auto [reload, cost] : {std::pair{9U, 515U}, {509U, 514U}, {511U, 516U}}) {
        CAPTURE(reload, cost);
        DmaMachine machine({0xEA});
        machine.reload_on_cycle(static_cast<uint16_t>(reload));
        for (unsigned i = 0; i < 256; ++i)
            machine.bus.write(static_cast<Address>(0x0300U + i), static_cast<Byte>(i ^ 0x5AU));
        // Common overlap costs 2 cycles. Reload halts on the penultimate/last
        // OAM put instead extend DMA by 1/3 cycles (NESdev hardware cases).
        CHECK(machine.bus.run_oam_dma(3, 0x0200, false) == cost);
        CHECK(machine.apu.cpu_cycles() == cost);
        CHECK((machine.bus.read(0x4015) & 0x90U) == 0x80);
        for (unsigned i = 0; i < 256; ++i) {
            machine.bus.write(0x2003, static_cast<Byte>(i));
            const auto mask = (i & 3U) == 2 ? 0xE3U : 0xFFU;
            CHECK(machine.bus.read(0x2004) == static_cast<Byte>((i ^ 0x5AU) & mask));
        }
    }
}

TEST_CASE("DMC loads use the get in the second APU cycle after a CPU write", "[cpu][dma]") {
    for (const bool odd : {false, true}) {
        DmaMachine machine({0xA9, 0x10, 0x8D, 0x15, 0x40, 0xEA, 0xEA});
        if (odd)
            machine.bus.clock_cpu_bus(0x0200); // choose the other fixed power-on phase
        machine.bus.write(0x4010, 0x8F);
        REQUIRE(machine.cpu.step() == 2);
        REQUIRE(machine.cpu.step() == 4);
        REQUIRE(machine.cpu.step() == 2);
        CHECK(machine.cpu.step() == 5);
    }
}

TEST_CASE("DMC reload requests wait through writes without stopping the output unit",
          "[cpu][dma]") {
    SECTION("reload on a read takes four cycles") {
        DmaMachine machine({0xEA, 0xEA});
        machine.reload_on_cycle(1);
        REQUIRE(machine.cpu.step() == 2);
        CHECK(machine.cpu.step() == 6);
    }
    SECTION("one write changes reload alignment to three cycles") {
        DmaMachine machine({0x85, 0x10, 0xEA});
        machine.reload_on_cycle(1);
        REQUIRE(machine.cpu.step() == 3);
        CHECK(machine.cpu.step() == 5);
    }
    SECTION("two RMW writes retain four-cycle reload alignment") {
        DmaMachine machine({0xEE, 0, 0x03, 0xEA});
        machine.reload_on_cycle(3);
        REQUIRE(machine.cpu.step() == 6);
        CHECK(machine.bus.read(0x0300) == 1);
        CHECK(machine.cpu.step() == 6);
    }
    SECTION("a disabling write before the halt cancels the pending load") {
        DmaMachine machine({0x8D, 0x15, 0x40, 0xEA}); // A=0
        machine.start_dmc();
        REQUIRE(machine.cpu.step() == 4);
        CHECK(machine.cpu.step() == 2);
        CHECK((machine.bus.read(0x4015) & 0x90U) == 0);
    }
}

TEST_CASE("Pending DMC loads survive snapshots and reject corrupt scheduling state",
          "[cpu][dma][state]") {
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        DmaMachine fixture({0xA9, 0x10, 0x8D, 0x15, 0x40, 0xEA, 0xEA, 0xEA});
        Emulator emulator;
        if (fixture.cartridge.has_value())
            REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
        else
            FAIL("DMA fixture did not produce a cartridge");
        emulator.set_region(region);
        emulator.reset();
        emulator.step_instruction();
        emulator.step_instruction(); // $4015 load request is still delayed
        const auto before = emulator.save_state();
        emulator.step_instruction();
        emulator.step_instruction();
        CHECK(emulator.cpu().cycles() == 13);
        const auto after = emulator.save_state();
        REQUIRE(emulator.load_state(before));
        emulator.step_instruction();
        emulator.step_instruction();
        CHECK(emulator.save_state() == after);

        StateWriter prefix;
        emulator.cpu().save_state(prefix);
        emulator.ppu().save_state(prefix);
        StateWriter apu_state;
        Apu{}.save_state(apu_state);
        const auto dma_end = 43 + prefix.data().size() + apu_state.data().size();
        for (const auto [back, value] : {std::pair{18U, Byte{0}},
                                         {10U, Byte{4}},
                                         {9U, Byte{2}},
                                         {8U, Byte{2}},
                                         {7U, Byte{5}},
                                         {6U, Byte{0}},
                                         {5U, Byte{3}},
                                         {4U, Byte{2}},
                                         {2U, Byte{0}},
                                         {1U, Byte{2}}}) {
            auto corrupt = before;
            corrupt[dma_end - back] = value;
            CHECK_FALSE(emulator.load_state(corrupt));
            CHECK(emulator.save_state() == after);
        }
    }
}

namespace {
// Ensures mappers are registered before any load_cartridge call.
// Idempotent — the registry is a singleton.
struct MapperRegistrar {
    MapperRegistrar() {
        register_builtin_mappers();
    }
};
MapperRegistrar g_registrar;
} // namespace

TEST_CASE("LDA immediate sets zero flag on zero", "[cpu][accuracy]") {
    // SEI; CLD; LDA #$00 → Z flag set, A=0.
    const uint8_t program[]{0x78, 0xD8, 0xA9, 0x00};
    const auto rom = build_ines_rom("lda-zero", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    emu.step_instruction(); // SEI
    emu.step_instruction(); // CLD
    emu.step_instruction(); // LDA #$00
    REQUIRE(emu.cpu().a() == 0x00);
    REQUIRE((emu.cpu().status() & 0x02) != 0); // Zero flag
}

TEST_CASE("LDA immediate sets negative flag on high bit", "[cpu][accuracy]") {
    const uint8_t program[]{0x78, 0xA9, 0x80};
    const auto rom = build_ines_rom("lda-neg", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    emu.step_instruction(); // SEI
    emu.step_instruction(); // LDA #$80
    REQUIRE(emu.cpu().a() == 0x80);
    REQUIRE((emu.cpu().status() & 0x80) != 0); // Negative flag
}

TEST_CASE("ADC carry and overflow", "[cpu][accuracy]") {
    // CLC; LDA #$7F; ADC #$01 → A=$80, V (overflow) set, N set, carry clear.
    const uint8_t program[]{0x78, 0x18, 0xA9, 0x7F, 0x69, 0x01};
    const auto rom = build_ines_rom("adc", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    for (int i = 0; i < 4; ++i) {
        emu.step_instruction();
    }
    REQUIRE(emu.cpu().a() == 0x80);
    REQUIRE((emu.cpu().status() & 0x40) != 0); // Overflow
    REQUIRE((emu.cpu().status() & 0x80) != 0); // Negative
    REQUIRE((emu.cpu().status() & 0x01) == 0); // Carry clear
}

TEST_CASE("ADC sets carry on unsigned overflow", "[cpu][accuracy]") {
    // CLC; LDA #$FF; ADC #$01 → A=$00, C set, Z set.
    const uint8_t program[]{0x78, 0x18, 0xA9, 0xFF, 0x69, 0x01};
    const auto rom = build_ines_rom("adc-carry", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    for (int i = 0; i < 4; ++i) {
        emu.step_instruction();
    }
    REQUIRE(emu.cpu().a() == 0x00);
    REQUIRE((emu.cpu().status() & 0x01) != 0); // Carry
    REQUIRE((emu.cpu().status() & 0x02) != 0); // Zero
}

TEST_CASE("SBC borrow produces correct result", "[cpu][accuracy]") {
    // SEC; LDA #$00; SBC #$01 → A=$FF, C clear (borrow), N set.
    const uint8_t program[]{0x78, 0x38, 0xA9, 0x00, 0xE9, 0x01};
    const auto rom = build_ines_rom("sbc", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    for (int i = 0; i < 4; ++i) {
        emu.step_instruction();
    }
    REQUIRE(emu.cpu().a() == 0xFF);
    REQUIRE((emu.cpu().status() & 0x01) == 0); // Carry clear (borrow)
}

TEST_CASE("CMP sets carry when equal", "[cpu][accuracy]") {
    // LDX #$05; CPX #$05 → Z set, C set.
    const uint8_t program[]{0x78, 0xA2, 0x05, 0xE0, 0x05};
    const auto rom = build_ines_rom("cmp", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    for (int i = 0; i < 3; ++i) {
        emu.step_instruction();
    }
    REQUIRE(emu.cpu().x() == 0x05);
    REQUIRE((emu.cpu().status() & 0x01) != 0); // Carry (X >= operand)
    REQUIRE((emu.cpu().status() & 0x02) != 0); // Zero (X == operand)
}

TEST_CASE("Unofficial LAX loads accumulator and X", "[cpu][accuracy]") {
    // LAX zero-page ($A7): loads both A and X from [$zp]. Store $42 to $00
    // first via LDA/STA, then LAX $00 → A=$42, X=$42.
    const uint8_t program[]{0x78, 0xA9, 0x42, 0x85, 0x00, 0xA7, 0x00};
    const auto rom = build_ines_rom("lax", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    for (int i = 0; i < 4; ++i) {
        emu.step_instruction();
    }
    REQUIRE(emu.cpu().a() == 0x42);
    REQUIRE(emu.cpu().x() == 0x42);
}

TEST_CASE("Unofficial SAX stores A∧X to memory", "[cpu][accuracy]") {
    // LDA #$0F; LDX #$F0; SAX $00 → [$0000] = $0F ∧ $F0 = $00.
    const uint8_t program[]{0x78, 0xA9, 0x0F, 0xA2, 0xF0, 0x87, 0x00};
    const auto rom = build_ines_rom("sax", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    for (int i = 0; i < 4; ++i) {
        emu.step_instruction();
    }
    REQUIRE(emu.read_cpu(0x0000) == 0x00); // 0x0F & 0xF0
}

TEST_CASE("Unofficial DCP decrements and compares", "[cpu][accuracy]") {
    // Build [$00]=$01, then DCP $00 → [$00]=$00, CMP with A=$00 → C set, Z set.
    const uint8_t program[]{0x78, 0xA9, 0x00, 0x85, 0x00, 0xE6, 0x00, 0xC7, 0x00};
    const auto rom = build_ines_rom("dcp", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    for (int i = 0; i < 5; ++i) {
        emu.step_instruction();
    }
    REQUIRE(emu.read_cpu(0x0000) == 0x00);     // decremented $01 → $00
    REQUIRE((emu.cpu().status() & 0x01) != 0); // Carry (A >= operand)
}

TEST_CASE("Program writes result to PRG-RAM via $6000", "[cpu][accuracy]") {
    // LDA #$5A; STA $6000 — validates the memory path used by blargg-style
    // result polling.
    const uint8_t program[]{0x78, 0xA9, 0x5A, 0x8D, 0x00, 0x60};
    const auto rom = build_ines_rom("prgram", 0, 2, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    for (int i = 0; i < 3; ++i) {
        emu.step_instruction();
    }
    REQUIRE(emu.read_cpu(0x6000) == 0x5A);
}

TEST_CASE("Step instruction advances PC monotonically", "[cpu][accuracy]") {
    // SEI, CLC, LDA #$01 — PC should advance by the right byte counts.
    const uint8_t program[]{0x78, 0x18, 0xA9, 0x01};
    const auto rom = build_ines_rom("pc", 0, 1, 0, program);
    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()));
    emu.reset();
    const uint16_t start_pc = emu.cpu().pc();
    emu.step_instruction(); // SEI (1 byte)
    emu.step_instruction(); // CLC (1 byte)
    REQUIRE(emu.cpu().pc() == start_pc + 2);
    emu.step_instruction(); // LDA #$01 (2 bytes)
    REQUIRE(emu.cpu().pc() == start_pc + 4);
}

TEST_CASE("PAL machine snapshots retain every divider phase and replay frame boundaries",
          "[cpu][ppu][region][state]") {
    register_builtin_mappers();
    DmaMachine fixture({0x4C, 0, 0x80}); // three-cycle loop visits all five phases
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("PAL fixture did not produce a cartridge");
    emulator.set_region(Region::PAL);
    emulator.reset();
    const auto position = [&] {
        StateWriter writer;
        emulator.ppu().save_state(writer);
        StateReader reader(writer.data());
        const auto line = reader.read<uint16_t>();
        const auto dot = reader.read<uint16_t>();
        return static_cast<uint64_t>(line) * 341U + dot;
    };
    for (unsigned phase = 0; phase < 5; ++phase) {
        INFO("phase sample " << phase);
        CHECK(position() == emulator.cpu().cycles() * 16U / 5U % (uint64_t{312} * 341U));
        const auto before = emulator.save_state();
        emulator.step_frame();
        emulator.step_frame();
        const auto after = emulator.save_state();
        emulator.set_region(Region::NTSC); // restore also installs the PAL timing policy
        REQUIRE(emulator.load_state(before));
        CHECK(emulator.region() == Region::PAL);
        emulator.step_frame();
        emulator.step_frame();
        CHECK(emulator.save_state() == after);
        REQUIRE(emulator.load_state(before));
        emulator.step_instruction();
    }
    emulator.step_instruction(); // move to a nonzero remainder before resetting
    emulator.reset();
    emulator.step_instruction();
    CHECK(position() == 9); // reset restarts at divider phase zero
}

TEST_CASE("PAL version 10 snapshots import with a defined fresh divider phase",
          "[cpu][ppu][region][state]") {
    DmaMachine fixture({0x4C, 0, 0x80});
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("PAL fixture did not produce a cartridge");
    emulator.set_region(Region::PAL);
    emulator.reset();
    emulator.step_instruction(); // remainder three in the new scheduler
    const auto legacy = mapperbus::tests::legacy_state(emulator, 10);
    StateWriter ppu_before;
    emulator.ppu().save_state(ppu_before);
    REQUIRE(emulator.load_state(legacy));
    StateWriter ppu_after;
    emulator.ppu().save_state(ppu_after);
    CHECK(ppu_after.data() == ppu_before.data()); // no old PPU position is rewritten
    const auto before = emulator.save_state();
    emulator.step_instruction();
    StateWriter ppu;
    emulator.ppu().save_state(ppu);
    StateReader reader(ppu.data());
    CHECK(reader.read<uint16_t>() == 0);
    CHECK(reader.read<uint16_t>() == 18); // 9 retained dots + 9 dots from phase zero
    const auto after = emulator.save_state();
    REQUIRE(emulator.load_state(before));
    emulator.step_instruction();
    CHECK(emulator.save_state() == after);
}

TEST_CASE("Invalid CPU PPU divider phases reject snapshots with rollback", "[cpu][region][state]") {
    DmaMachine fixture({0x4C, 0, 0x80});
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("PAL fixture did not produce a cartridge");
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        emulator.set_region(region);
        emulator.reset();
        emulator.step_instruction();
        StateWriter cpu, ppu, apu, bus;
        emulator.cpu().save_state(cpu);
        emulator.ppu().save_state(ppu);
        Apu{}.save_state(apu);
        MemoryBus{}.save_state(bus);
        const auto phase_offset =
            43 + cpu.data().size() + ppu.data().size() + apu.data().size() + bus.data().size() - 5;
        const auto before = emulator.save_state();
        for (const Byte invalid :
             {Byte{5}, Byte{255}, static_cast<Byte>(region == Region::PAL ? 6U : 1U)}) {
            auto corrupt = before;
            corrupt[phase_offset] = invalid;
            CHECK_FALSE(emulator.load_state(corrupt));
            CHECK(emulator.save_state() == before);
        }
        if (region == Region::PAL) {
            auto wrong_region = before;
            wrong_region[10] = static_cast<Byte>(Region::NTSC);
            CHECK_FALSE(emulator.load_state(wrong_region)); // phase three is invalid for NTSC
            CHECK(emulator.region() == Region::PAL);
            CHECK(emulator.save_state() == before);
        }
    }
}

TEST_CASE("Explicit DMC stops latch on the next get and abort exactly one read cycle",
          "[cpu][dma][dmc-stop]") {
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        for (const bool stop_after_get : {false, true}) {
            DmaMachine machine({0xEA});
            machine.apu.set_region(region);
            machine.ppu.set_region(region);
            machine.bus.set_region(region);
            const bool abort = region == Region::NTSC;
            DmcChannel channel;
            channel.enabled = true;
            channel.bytes_remaining = 2;
            channel.timer_period = 54;
            channel.timer = 2;
            channel.bits_remaining = 1;
            channel.sample_buffer_empty = false;
            channel.sample_buffer = 0xD5;
            machine.seed_channel(channel);
            machine.bus.clock_cpu_bus(0, stop_after_get ? 2 : 3);
            machine.bus.write(0x4015, 0);
            machine.bus.clock_cpu_bus(0, stop_after_get ? 2 : 1);
            REQUIRE(machine.apu.dmc_dma_ready() == abort);
            Controller controller;
            controller.set_button_state(0, Button::A, true);
            controller.set_button_state(0, Button::Select, true);
            controller.write(1);
            controller.write(0);
            machine.bus.connect_controller(&controller);
            const auto cycles = machine.apu.cpu_cycles();
            CHECK(machine.bus.run_dmc_dma(0x4016) == (abort ? 1 : 0));
            CHECK(machine.apu.cpu_cycles() == cycles + (abort ? 1U : 0U));
            const auto read_cycle = [&](Address address) {
                machine.bus.begin_cpu_cycle(address);
                const auto value = machine.bus.read_cpu(address);
                machine.bus.end_cpu_cycle();
                return value;
            };
            // NES /OE stays asserted across an aborted halt and its resumed read.
            CHECK((read_cycle(0x4016) & 1U) == 1);
            (void)read_cycle(0);
            CHECK((read_cycle(0x4016) & 1U) == 0);
            (void)read_cycle(0);
            CHECK(controller.peek(0) == 1); // Select follows the two completed reads
            CHECK(machine.dmc_channel().bytes_remaining == 0);
            CHECK(machine.dmc_channel().sample_buffer_empty); // no sample fetch
            CHECK(machine.bus.run_dmc_dma(0) == 0);
        }
    }
}

TEST_CASE("Implicit DMC aborts expire on writes and overlap OAM without stealing a byte",
          "[cpu][dma][dmc-stop][state]") {
    DmaMachine machine({0xEA});
    DmcChannel channel;
    channel.enabled = true;
    channel.bytes_remaining = 1;
    channel.timer_period = 54;
    channel.timer = 4; // fetch on get 3, byte clock on get 5
    channel.bits_remaining = 1;
    machine.seed_channel(channel, true);
    REQUIRE(machine.bus.run_dmc_dma(0) == 4);
    machine.bus.clock_cpu_bus(0, 2);
    REQUIRE(machine.apu.dmc_dma_ready());
    const auto before = machine.device_state();
    CHECK(machine.dmc_channel().shift_register == 0xD5);
    SECTION("aborted halt survives a device snapshot") {
        CHECK(machine.bus.run_dmc_dma(0) == 1);
        const auto after = machine.device_state();
        machine.restore_devices(before);
        CHECK(machine.bus.run_dmc_dma(0) == 1);
        CHECK(machine.device_state() == after);
    }
    SECTION("write suppresses the pulse instead of postponing it") {
        machine.bus.begin_cpu_cycle(0x0001);
        machine.bus.write_cpu(0x0001, 0x5A);
        machine.bus.end_cpu_cycle();
        CHECK(machine.bus.run_dmc_dma(0) == 0);
        CHECK(machine.bus.read(1) == 0x5A);
    }
    SECTION("aborted DMA overlaps the OAM halt") {
        CHECK(machine.bus.run_oam_dma(2, 0, false) == 513);
        CHECK_FALSE(machine.apu.dmc_dma_ready());
    }
    CHECK(machine.dmc_channel().bytes_remaining == 0);
    CHECK(machine.dmc_channel().current_address == 0xC001);
}

TEST_CASE("Late NTSC DMC completion queues a duplicate address and plays both bytes",
          "[cpu][dma][dmc-stop][region]") {
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        DmaMachine machine({0xEA});
        machine.apu.set_region(region);
        machine.ppu.set_region(region);
        machine.bus.set_region(region);
        DmcChannel channel;
        channel.enabled = true;
        channel.bytes_remaining = 1;
        channel.timer_period = 54;
        channel.timer = 2; // output byte clock coincides with the DMA get
        channel.bits_remaining = 1;
        machine.seed_channel(channel, true);
        CHECK(machine.bus.run_dmc_dma(0) == (region == Region::NTSC ? 8 : 4));
        const auto after = machine.dmc_channel();
        CHECK(after.bytes_remaining == 0);
        CHECK(after.current_address == 0xC001);
        CHECK(after.sample_buffer == 0xD5); // $C001 is zero; the duplicate read used $C000
        CHECK_FALSE(after.sample_buffer_empty);
        if (region == Region::NTSC) {
            CHECK(after.shift_register == 0xD5); // first byte is already playing
            machine.bus.clock_cpu_bus(0, 8 * 54);
            CHECK(machine.dmc_channel().shift_register == 0xD5); // second byte follows
            CHECK(machine.dmc_channel().sample_buffer_empty);
        }
        CHECK_FALSE(machine.apu.dmc_dma_ready());
    }
}

TEST_CASE("Queued duplicate DMC requests retain their address across device snapshots",
          "[cpu][dma][dmc-stop][state]") {
    DmaMachine machine({0xEA});
    DmcChannel channel;
    channel.enabled = true;
    channel.bytes_remaining = 1;
    channel.timer_period = 54;
    channel.timer = 2;
    channel.bits_remaining = 1;
    machine.seed_channel(channel, true);
    REQUIRE_FALSE(machine.apu.start_dmc_dma());
    machine.bus.clock_cpu_bus(0, 3); // halt, dummy, alignment
    machine.bus.begin_cpu_cycle(0xC000);
    machine.apu.complete_dmc_dma(machine.bus.read(0xC000));
    machine.bus.end_cpu_cycle();
    REQUIRE(machine.apu.dmc_dma_ready());
    REQUIRE(machine.apu.dmc_dma_address() == 0xC000);
    const auto before = machine.device_state();
    CHECK(machine.bus.run_dmc_dma(0) == 4);
    const auto after = machine.device_state();
    machine.restore_devices(before);
    CHECK(machine.apu.dmc_dma_address() == 0xC000);
    CHECK(machine.bus.run_dmc_dma(0) == 4);
    CHECK(machine.device_state() == after);
}

TEST_CASE("Malformed DMC stop latches reject machine snapshots with rollback",
          "[cpu][dma][dmc-stop][state]") {
    DmaMachine fixture({0xEA});
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("DMC fixture did not produce a cartridge");
    emulator.reset();
    const auto before = emulator.save_state();
    StateWriter cpu, ppu, apu;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    Apu{}.save_state(apu);
    const auto end = 43 + cpu.data().size() + ppu.data().size() + apu.data().size();
    for (const auto [back, value] : {std::pair{6U, Byte{2}},
                                     {5U, Byte{3}},
                                     {5U, Byte{1}},
                                     {4U, Byte{2}},
                                     {4U, Byte{1}},
                                     {1U, Byte{2}},
                                     {1U, Byte{1}}}) {
        auto corrupt = before;
        corrupt[end - back] = value;
        CHECK_FALSE(emulator.load_state(corrupt));
        CHECK(emulator.save_state() == before);
    }
}

TEST_CASE("Machine snapshots replay a delayed explicit DMC stop", "[cpu][dma][dmc-stop][state]") {
    DmaMachine fixture({0xA9, 0x10, 0x8D, 0x15, 0x40, 0xA9, 0, 0x8D, 0x15, 0x40, 0xEA, 0xEA});
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("DMC fixture did not produce a cartridge");
    emulator.reset();
    for (int instruction = 0; instruction < 4; ++instruction)
        emulator.step_instruction();
    const auto before = emulator.save_state();
    for (int instruction = 0; instruction < 2; ++instruction)
        emulator.step_instruction();
    const auto after = emulator.save_state();
    REQUIRE(emulator.load_state(before));
    CHECK(emulator.save_state() == before);
    for (int instruction = 0; instruction < 2; ++instruction)
        emulator.step_instruction();
    CHECK(emulator.save_state() == after);
}

TEST_CASE("DMC rate writes after an empty byte clock retain valid snapshots",
          "[cpu][dma][dmc-stop][state]") {
    DmaMachine machine({0xEA});
    DmcChannel channel;
    channel.timer_period = 54;
    channel.bits_remaining = 1;
    machine.seed_channel(channel);
    machine.bus.clock_cpu_bus(0, 2);
    machine.bus.write(0x4010, 0); // new period, previous timer remains running
    const auto before = machine.device_state();
    machine.restore_devices(before);
    CHECK(machine.device_state() == before);
}

TEST_CASE("Cancelling a regional DMC reload leaves restorable request state",
          "[cpu][dma][dmc-stop][state][region]") {
    for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
        DmaMachine machine({0xEA});
        machine.apu.set_region(region);
        machine.ppu.set_region(region);
        machine.bus.set_region(region);
        DmcChannel channel;
        channel.enabled = true;
        channel.bytes_remaining = 1;
        channel.timer_period = 54;
        channel.bits_remaining = 1;
        channel.sample_buffer_empty = false;
        machine.seed_channel(channel);
        machine.bus.clock_cpu_bus(0, 2);
        REQUIRE(machine.apu.dmc_dma_ready());
        machine.bus.write(0x4015, 0);
        CHECK(machine.apu.dmc_dma_ready() == (region == Region::NTSC));
        const auto before = machine.device_state();
        machine.restore_devices(before);
        CHECK(machine.device_state() == before);
    }
}

TEST_CASE("DMC partial decoding can clock the other controller", "[cpu][dma][controller]") {
    DmaMachine machine({0xEA});
    Controller pads;
    pads.set_button_state(0, Button::A, true);
    pads.set_button_state(1, Button::A, true);
    machine.bus.connect_controller(&pads);
    machine.bus.write(0x4016, 1);
    machine.bus.write(0x4016, 0);
    DmcChannel channel;
    channel.enabled = true;
    channel.bytes_remaining = 1;
    channel.current_address = 0xC017;
    channel.timer = 100;
    machine.seed_channel(channel, true);
    REQUIRE(machine.bus.run_dmc_dma(0x4016) == 4);
    machine.bus.begin_cpu_cycle(0x4016);
    CHECK((machine.bus.read_cpu(0x4016) & 1U) == 0); // player 1 B, after its halted A read
    machine.bus.end_cpu_cycle();
    CHECK(pads.peek(1) == 0); // the aliased $4017 read clocked player 2's A
}

TEST_CASE("Machine snapshots preserve a controller read across an instruction boundary",
          "[cpu][controller][state]") {
    DmaMachine fixture({0xA9, 1,    0x8D, 0x16, 0x40, 0xA9, 0,    0x8D, 0x16, 0x40,
                        0xAD, 0x16, 0x40, 0x8D, 0,    0,    0xAD, 0x16, 0x40, 0x8D,
                        1,    0,    0xAD, 0x16, 0x40, 0x8D, 2,    0});
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("Controller fixture did not produce a cartridge");
    emulator.reset();
    emulator.controller().set_button_state(0, Button::A, true);
    emulator.controller().set_button_state(0, Button::Select, true);
    for (int instruction = 0; instruction < 5; ++instruction)
        emulator.step_instruction();
    const auto before = emulator.save_state();
    for (int instruction = 0; instruction < 5; ++instruction)
        emulator.step_instruction();
    const auto after = emulator.save_state();
    CHECK((emulator.read_cpu(0) & 1U) == 1);
    CHECK((emulator.read_cpu(1) & 1U) == 0);
    CHECK((emulator.read_cpu(2) & 1U) == 1);
    REQUIRE(emulator.load_state(before));
    for (int instruction = 0; instruction < 5; ++instruction)
        emulator.step_instruction();
    CHECK(emulator.save_state() == after);
}

TEST_CASE("Invalid controller line states reject snapshots with rollback",
          "[cpu][controller][state]") {
    Emulator emulator;
    emulator.reset();
    const auto before = emulator.save_state();
    StateWriter cpu, ppu, apu, bus, controller;
    emulator.cpu().save_state(cpu);
    emulator.ppu().save_state(ppu);
    Apu{}.save_state(apu);
    MemoryBus{}.save_state(bus);
    Controller{}.save_state(controller);
    const auto end =
        43 + cpu.data().size() + ppu.data().size() + apu.data().size() + bus.data().size();
    for (const auto [offset, value] : {std::pair{end - 1, Byte{2}},
                                       {end - 2, Byte{2}},
                                       {end - 3, Byte{2}},
                                       {end - 4, Byte{3}},
                                       {end + controller.data().size() - 1, Byte{2}}}) {
        auto corrupt = before;
        corrupt[offset] = value;
        CHECK_FALSE(emulator.load_state(corrupt));
        CHECK(emulator.save_state() == before);
    }
}

TEST_CASE("Version 12 imports reconstruct the controller strobe request",
          "[cpu][controller][state]") {
    DmaMachine fixture({0xA9, 1, 0x8D, 0x16, 0x40, 0xEA});
    Emulator emulator;
    if (fixture.cartridge)
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("Controller fixture did not produce a cartridge");
    emulator.reset();
    emulator.step_instruction();
    emulator.step_instruction();
    REQUIRE(emulator.controller().strobed());
    const auto original = emulator.save_state();
    const auto legacy = mapperbus::tests::legacy_state(emulator, 12);
    REQUIRE(emulator.load_state(legacy));
    CHECK(emulator.save_state() == original);
    emulator.step_instruction();
    CHECK(emulator.controller().strobed());
}
