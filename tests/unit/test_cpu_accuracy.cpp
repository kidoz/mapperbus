// Synthesized CPU accuracy tests.
//
// Each test builds a minimal NROM image with an inline 6502 program, loads it
// into the emulator, steps per-instruction, and validates the resulting
// register/memory state via Emulator::read_cpu() and Emulator::cpu().
// No external test ROMs are required — everything is synthesized in C++.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>

#include "../support/test_rom.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/types.hpp"

using namespace mapperbus::core;
using mapperbus::tests::build_ines_rom;

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
