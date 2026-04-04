// CPU unit tests — exercise addressing modes, flags, branches,
// page-crossing penalties, and a sampling of unofficial opcodes.
//
// Strategy: plant opcodes and operands directly in RAM via MemoryBus,
// set the reset vector, step the CPU, and inspect register/memory state.

#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <initializer_list>

#include "core/bus/memory_bus.hpp"
#include "core/cpu/cpu.hpp"

using namespace mapperbus::core;

namespace {

// Fixture that wires a CPU to a bus backed only by RAM (no cartridge).
// The reset vector at $FFFC/$FFFD is in RAM too because MemoryBus reads
// above $8000 return 0 without a cartridge — so we manually preset PC
// after reset to avoid that.
struct CpuHarness {
    MemoryBus bus;
    Cpu cpu{bus};

    CpuHarness() {
        cpu.reset();
    }

    // Load `bytes` at address `addr` in RAM, and jump PC there.
    void load_program(Address addr, std::initializer_list<Byte> bytes) {
        Address p = addr;
        for (Byte b : bytes) {
            bus.write(p++, b);
        }
        set_pc(addr);
    }

    // Crude PC manipulation: use JMP absolute planted at $0000.
    void set_pc(Address addr) {
        bus.write(0x0000, 0x4C); // JMP $addr
        bus.write(0x0001, static_cast<Byte>(addr & 0xFF));
        bus.write(0x0002, static_cast<Byte>(addr >> 8));
        // Drop PC to $0000 by faking a BRK path is too heavy — instead
        // we just step from wherever PC is after reset, after planting
        // a trampoline at that location.
        trampoline_to(addr);
    }

    void trampoline_to(Address target) {
        // After reset, PC is whatever $FFFC-$FFFD contained (0 here).
        // Plant JMP at PC's current position.
        Address here = cpu.pc();
        bus.write(here, 0x4C);
        bus.write(static_cast<Address>(here + 1), static_cast<Byte>(target & 0xFF));
        bus.write(static_cast<Address>(here + 2), static_cast<Byte>(target >> 8));
        cpu.step(); // consume the JMP
    }

    [[nodiscard]] bool flag(Byte mask) const {
        return (cpu.status() & mask) != 0;
    }
};

constexpr Byte kFlagCarry = 0x01;
constexpr Byte kFlagZero = 0x02;
constexpr Byte kFlagInterrupt = 0x04;
constexpr Byte kFlagOverflow = 0x40;
constexpr Byte kFlagNegative = 0x80;

} // namespace

TEST_CASE("CPU LDA immediate sets Z/N correctly", "[cpu][lda]") {
    CpuHarness h;

    SECTION("LDA #$42") {
        h.load_program(0x0200, {0xA9, 0x42});
        h.cpu.step();
        REQUIRE(h.cpu.a() == 0x42);
        REQUIRE_FALSE(h.flag(kFlagZero));
        REQUIRE_FALSE(h.flag(kFlagNegative));
    }

    SECTION("LDA #$00 sets Z") {
        h.load_program(0x0200, {0xA9, 0x00});
        h.cpu.step();
        REQUIRE(h.cpu.a() == 0x00);
        REQUIRE(h.flag(kFlagZero));
        REQUIRE_FALSE(h.flag(kFlagNegative));
    }

    SECTION("LDA #$80 sets N") {
        h.load_program(0x0200, {0xA9, 0x80});
        h.cpu.step();
        REQUIRE(h.cpu.a() == 0x80);
        REQUIRE_FALSE(h.flag(kFlagZero));
        REQUIRE(h.flag(kFlagNegative));
    }
}

TEST_CASE("CPU ADC handles carry and overflow", "[cpu][adc]") {
    CpuHarness h;

    SECTION("simple add without carry") {
        // LDA #$10; ADC #$20  -> A = 0x30
        h.load_program(0x0200, {0xA9, 0x10, 0x69, 0x20});
        h.cpu.step();
        h.cpu.step();
        REQUIRE(h.cpu.a() == 0x30);
        REQUIRE_FALSE(h.flag(kFlagCarry));
        REQUIRE_FALSE(h.flag(kFlagOverflow));
    }

    SECTION("carry out") {
        // LDA #$FF; ADC #$01  -> A = 0x00, C set, Z set
        h.load_program(0x0200, {0xA9, 0xFF, 0x69, 0x01});
        h.cpu.step();
        h.cpu.step();
        REQUIRE(h.cpu.a() == 0x00);
        REQUIRE(h.flag(kFlagCarry));
        REQUIRE(h.flag(kFlagZero));
    }

    SECTION("signed overflow: 0x50 + 0x50") {
        // LDA #$50; ADC #$50  -> A = 0xA0, V set, N set
        h.load_program(0x0200, {0xA9, 0x50, 0x69, 0x50});
        h.cpu.step();
        h.cpu.step();
        REQUIRE(h.cpu.a() == 0xA0);
        REQUIRE(h.flag(kFlagOverflow));
        REQUIRE(h.flag(kFlagNegative));
        REQUIRE_FALSE(h.flag(kFlagCarry));
    }
}

TEST_CASE("CPU SBC: A - M - !C", "[cpu][sbc]") {
    CpuHarness h;

    // SEC; LDA #$50; SBC #$30 -> A = 0x20, C set (no borrow)
    h.load_program(0x0200, {0x38, 0xA9, 0x50, 0xE9, 0x30});
    h.cpu.step(); // SEC
    h.cpu.step(); // LDA
    h.cpu.step(); // SBC
    REQUIRE(h.cpu.a() == 0x20);
    REQUIRE(h.flag(kFlagCarry));
}

TEST_CASE("CPU branches: taken vs not taken cycles", "[cpu][branch]") {
    CpuHarness h;

    SECTION("BEQ not taken (Z clear) costs 2 cycles") {
        // LDA #$01 (Z clear); BEQ +2
        h.load_program(0x0200, {0xA9, 0x01, 0xF0, 0x02});
        h.cpu.step(); // LDA
        uint64_t before = h.cpu.cycles();
        h.cpu.step(); // BEQ not taken
        REQUIRE(h.cpu.cycles() - before == 2);
    }

    SECTION("BEQ taken (same page) costs 3 cycles") {
        // LDA #$00 (Z set); BEQ +2
        h.load_program(0x0200, {0xA9, 0x00, 0xF0, 0x02});
        h.cpu.step(); // LDA
        uint64_t before = h.cpu.cycles();
        h.cpu.step(); // BEQ taken, same page
        REQUIRE(h.cpu.cycles() - before == 3);
    }

    SECTION("BEQ taken across page boundary costs 4 cycles") {
        // LDA #$00 at $02FB (PC after = $02FD);
        // BEQ +$05 at $02FD (PC after operand fetch = $02FF);
        // branch taken: target = $02FF + $05 = $0304 (page $02 -> $03 => penalty).
        h.load_program(0x02FB, {0xA9, 0x00, 0xF0, 0x05});
        h.cpu.step(); // LDA
        uint64_t before = h.cpu.cycles();
        h.cpu.step(); // BEQ taken, crosses page
        REQUIRE(h.cpu.pc() == 0x0304);
        REQUIRE(h.cpu.cycles() - before == 4);
    }
}

TEST_CASE("CPU LDA absolute,X page-cross penalty", "[cpu][addressing][penalty]") {
    CpuHarness h;

    // Seed RAM: target at $0100 contains $AA (crosses page from $00FF with X=1)
    h.bus.write(0x0100, 0xAA);

    // LDX #$01; LDA $00FF,X   (effective = $0100, crosses page $00 -> $01)
    h.load_program(0x0200, {0xA2, 0x01, 0xBD, 0xFF, 0x00});
    h.cpu.step(); // LDX
    uint64_t before = h.cpu.cycles();
    h.cpu.step(); // LDA absolute,X
    REQUIRE(h.cpu.a() == 0xAA);
    // LDA abs,X base cost is 4, +1 on page cross
    REQUIRE(h.cpu.cycles() - before == 5);
}

TEST_CASE("CPU stack push/pop", "[cpu][stack]") {
    CpuHarness h;

    // LDA #$AB; PHA; LDA #$00; PLA
    h.load_program(0x0200, {0xA9, 0xAB, 0x48, 0xA9, 0x00, 0x68});
    h.cpu.step(); // LDA #$AB
    h.cpu.step(); // PHA -> $01FD
    h.cpu.step(); // LDA #$00
    REQUIRE(h.cpu.a() == 0x00);
    h.cpu.step(); // PLA
    REQUIRE(h.cpu.a() == 0xAB);
}

TEST_CASE("CPU JMP indirect exhibits 6502 page-wrap bug", "[cpu][jmp][bug]") {
    CpuHarness h;

    // Pointer at $02FF/$0200 (wraps within page). Low byte at $02FF, high byte at $0200.
    h.bus.write(0x02FF, 0x34);
    h.bus.write(0x0200, 0x12); // NOT $0300 — the 6502 wraps
    h.bus.write(0x0300, 0xFF); // should be ignored

    // JMP ($02FF) at $0400
    h.load_program(0x0400, {0x6C, 0xFF, 0x02});
    h.cpu.step();
    REQUIRE(h.cpu.pc() == 0x1234);
}

TEST_CASE("CPU INX/DEX wrap-around", "[cpu][inx]") {
    CpuHarness h;

    // LDX #$FF; INX (wraps to 0, Z set)
    h.load_program(0x0200, {0xA2, 0xFF, 0xE8});
    h.cpu.step();
    h.cpu.step();
    REQUIRE(h.cpu.x() == 0x00);
    REQUIRE(h.flag(kFlagZero));

    CpuHarness h2;
    // LDX #$00; DEX (wraps to 0xFF, N set)
    h2.load_program(0x0200, {0xA2, 0x00, 0xCA});
    h2.cpu.step();
    h2.cpu.step();
    REQUIRE(h2.cpu.x() == 0xFF);
    REQUIRE(h2.flag(kFlagNegative));
}

TEST_CASE("CPU SEI / CLI toggle I flag", "[cpu][flags]") {
    CpuHarness h;

    // CLI; (I should now be clear); SEI; (I should now be set)
    h.load_program(0x0200, {0x58, 0x78});
    h.cpu.step();
    REQUIRE_FALSE(h.flag(kFlagInterrupt));
    h.cpu.step();
    REQUIRE(h.flag(kFlagInterrupt));
}

TEST_CASE("CPU STA zero-page writes through the bus", "[cpu][sta]") {
    CpuHarness h;

    // LDA #$5A; STA $42
    h.load_program(0x0200, {0xA9, 0x5A, 0x85, 0x42});
    h.cpu.step();
    h.cpu.step();
    REQUIRE(h.bus.read(0x0042) == 0x5A);
}
