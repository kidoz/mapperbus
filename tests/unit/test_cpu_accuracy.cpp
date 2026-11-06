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

    DmaMachine(std::initializer_list<Byte> program) {
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
            image[16 + vector + 1] = 0x80;
        }
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
        state[state.size() - 2] = static_cast<Byte>(pending);
        StateReader reader(state);
        apu.load_state(reader);
        REQUIRE(reader.ok());
    }
};
} // namespace

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
         {std::pair{11U, Byte{0}}, {3U, Byte{4}}, {2U, Byte{2}}, {1U, Byte{2}}}) {
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
