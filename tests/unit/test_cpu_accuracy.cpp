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
        state[state.size() - 3] = static_cast<Byte>(pending);
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
                                         {ppu_end - 1, Byte{2}},
                                         {apu_end - 1, Byte{5}},
                                         {bus_end - 1, Byte{2}},
                                         {bus_end - 2, Byte{2}}}) {
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

TEST_CASE("DMC and OAM overlap setup and preserve every OAM byte", "[cpu][dma]") {
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
            CHECK(machine.bus.read(0x2004) == static_cast<Byte>(i ^ 0x5AU));
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
    DmaMachine fixture({0xA9, 0x10, 0x8D, 0x15, 0x40, 0xEA, 0xEA, 0xEA});
    Emulator emulator;
    if (fixture.cartridge.has_value())
        REQUIRE(emulator.load_cartridge(std::move(*fixture.cartridge), {}));
    else
        FAIL("DMA fixture did not produce a cartridge");
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
    for (const auto [back, value] :
         {std::pair{12U, Byte{0}}, {4U, Byte{4}}, {3U, Byte{2}}, {2U, Byte{2}}, {1U, Byte{5}}}) {
        auto corrupt = before;
        corrupt[dma_end - back] = value;
        CHECK_FALSE(emulator.load_state(corrupt));
        CHECK(emulator.save_state() == after);
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
