#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <optional>
#include <utility>

#include "../support/fds_image.hpp"
#include "core/binary_file.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cpu/cpu.hpp"
#include "core/emulator.hpp"
#include "core/fds/fds_mapper.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
struct Fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-fds-system-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        std::filesystem::create_directories(root);
    }
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

Byte next_byte(Fds& fds) {
    for (int cycle = 0; cycle < 700000; ++cycle) {
        fds.step(1);
        if ((fds.read(0x4030) & 0x80) != 0)
            return fds.read(0x4031);
    }
    FAIL("Timed out waiting for disk transfer");
    return 0;
}

void write_payload_byte(Fds& drive, Byte value) {
    drive.insert_side(0);
    drive.write(0x4023, 1);
    drive.write(0x4025, 0x65);
    for (const int size : {56, 2, 16}) {
        for (int index = 0; index < size + 2; ++index)
            (void)next_byte(drive);
        drive.write(0x4025, 0x25);
        drive.write(0x4025, 0x65);
    }
    REQUIRE(next_byte(drive) == 4);
    drive.write(0x4025, 0x61);
    drive.write(0x4024, value);
    (void)next_byte(drive);
    drive.write(0x4025, 0x26); // stop motor before flushing
}
} // namespace

TEST_CASE("FDS BIOS boots through the bus and reads disk data into adapter RAM", "[fds][system]") {
    Emulator emulator;
    REQUIRE(
        emulator.load_fds(mapperbus::tests::make_fds_bios(), mapperbus::tests::make_fds_image(2)));
    REQUIRE(emulator.is_fds());
    REQUIRE(emulator.mapper_number() == 20);
    emulator.reset();
    CHECK(emulator.cpu().pc() == 0xE000);
    for (int frame = 0; frame < 30; ++frame)
        emulator.step_frame();
    CHECK(emulator.read_cpu(0x6000) == 1);
    const auto before = emulator.save_state();
    CHECK_FALSE(emulator.load_fds(std::vector<Byte>(8191), mapperbus::tests::make_fds_image()));
    CHECK(emulator.save_state() == before);
    CHECK_FALSE(emulator.load_fds(mapperbus::tests::make_fds_bios(), std::vector<Byte>(65500)));
    CHECK(emulator.save_state() == before);
}

TEST_CASE("FDS RAM adapter maps writable PRG/CHR and immutable BIOS", "[fds][system]") {
    Fds drive;
    const auto bios = mapperbus::tests::make_fds_bios();
    FdsMapper mapper(bios, drive);
    for (Address address : {0x6000, 0x8000, 0xDFFF}) {
        mapper.write_prg(address, 42);
        CHECK(mapper.read_prg(address) == 42);
    }
    mapper.write_prg(0xE000, 0);
    CHECK(mapper.read_prg(0xE000) == bios[0]);
    mapper.write_chr(0x1FFF, 23);
    CHECK(mapper.read_chr(0x1FFF) == 23);
    drive.write(0x4023, 1);
    drive.write(0x4025, 8);
    CHECK(mapper.mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("FDS disk changes persist separately and reject a wrong or corrupt save",
          "[fds][system][save]") {
    Fixture fixture;
    const auto save_path = fixture.root / "disk.fds-save";
    const auto bios = mapperbus::tests::make_fds_bios();
    const auto disk = mapperbus::tests::make_fds_image(2);
    Emulator source;
    REQUIRE(source.load_fds(bios, disk, save_path.string()));
    const auto identity = source.content_hash();
    write_payload_byte(source.fds(), 42);
    CHECK(source.fds().dirty());
    REQUIRE(source.flush_disk_changes());
    CHECK_FALSE(source.fds().dirty());
    CHECK(source.content_hash() == identity);
    Emulator restored;
    REQUIRE(restored.load_fds(bios, disk, save_path.string()));
    CHECK(restored.fds().disk_image()[75] == 42);
    const auto exported = fixture.root / "export.fds";
    REQUIRE(restored.export_disk(exported.string()));
    auto bytes = read_binary_file_capped(exported.string(), Fds::kMaxDiskImageBytes, "export");
    REQUIRE(bytes);
    CHECK((*bytes)[75] == 42);
    CHECK(disk[75] == 0);
    const auto state = restored.save_state();
    auto other = disk;
    other[100] ^= 1;
    CHECK_FALSE(restored.load_fds(bios, other, save_path.string()));
    CHECK(restored.save_state() == state);
    {
        std::fstream file(save_path, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-1, std::ios::end);
        file.put('x');
    }
    CHECK_FALSE(restored.load_fds(bios, disk, save_path.string()));
    CHECK(restored.save_state() == state);
}

TEST_CASE("FDS snapshots restore disk side and writes with stable source identity",
          "[fds][system][state]") {
    const auto bios = mapperbus::tests::make_fds_bios();
    const auto disk = mapperbus::tests::make_fds_image(2);
    Emulator emulator;
    REQUIRE(emulator.load_fds(bios, disk));
    emulator.reset();
    for (int frame = 0; frame < 30; ++frame)
        emulator.step_frame();
    const auto state = emulator.save_state();
    write_payload_byte(emulator.fds(), 42);
    emulator.fds().insert_side(1);
    REQUIRE(emulator.load_state(state));
    CHECK(emulator.fds().current_side() == 0);
    CHECK(emulator.fds().disk_image()[75] == 0);
    auto wrong_bios = bios;
    wrong_bios[100] ^= 1;
    Emulator other;
    REQUIRE(other.load_fds(wrong_bios, disk));
    CHECK_FALSE(other.load_state(state));
}

TEST_CASE("FDS switching refuses to discard disk changes when persistence fails",
          "[fds][system][save]") {
    Fixture fixture;
    Emulator emulator;
    REQUIRE(emulator.load_fds(mapperbus::tests::make_fds_bios(),
                              mapperbus::tests::make_fds_image(),
                              (fixture.root / "missing" / "disk.fds-save").string()));
    write_payload_byte(emulator.fds(), 42);
    const auto before = emulator.save_state();
    CHECK_FALSE(emulator.flush_disk_changes());
    CHECK_FALSE(
        emulator.load_fds(mapperbus::tests::make_fds_bios(), mapperbus::tests::make_fds_image(2)));
    CHECK(emulator.save_state() == before);
    std::filesystem::create_directory(fixture.root / "missing");
    REQUIRE(emulator.flush_disk_changes());
}

TEST_CASE("FDS CPU program observes both clean and corrupt block CRCs", "[fds][system][crc]") {
    auto bios = mapperbus::tests::make_fds_bios();
    // Original synthetic firmware: consume block 1 and its first CRC byte,
    // select CRC checking, consume the second byte, and store status at $6000.
    const Byte program[] = {
        0x78, 0xA9, 0x01, 0x8D, 0x23, 0x40, 0xA9, 0x65, 0x8D, 0x25, 0x40, 0xA2,
        0x39, 0xAD, 0x30, 0x40, 0x10, 0xFB, 0xAD, 0x31, 0x40, 0xCA, 0xD0, 0xF5,
        0xA9, 0x75, 0x8D, 0x25, 0x40, 0xAD, 0x30, 0x40, 0x10, 0xFB, 0xAD, 0x31,
        0x40, 0xAD, 0x30, 0x40, 0x29, 0x10, 0x8D, 0x00, 0x60, 0x4C, 0x2D, 0xE0,
    };
    std::ranges::copy(program, bios.begin());
    for (bool corrupt : {false, true}) {
        Emulator emulator;
        REQUIRE(emulator.load_fds(bios, mapperbus::tests::make_fds_image()));
        if (corrupt) {
            StateWriter writer;
            emulator.fds().save_state(writer);
            auto state = writer.take();
            constexpr std::size_t kTrackSize = 84000;
            constexpr std::size_t kFirstCrc = 3537 + 1 + 56;
            state[state.size() - 9 - kTrackSize + kFirstCrc] ^= 1U;
            StateReader reader(state);
            emulator.fds().load_state(reader);
            REQUIRE(reader.ok());
        }
        emulator.reset();
        for (int frame = 0; frame < 30; ++frame)
            emulator.step_frame();
        REQUIRE(emulator.cpu().pc() == 0xE02D);
        REQUIRE(emulator.read_cpu(0x6000) == (corrupt ? 0x10 : 0));
    }
}

namespace {
struct WatchdogCpu {
    Fds drive;
    std::optional<Cartridge> cartridge;
    MemoryBus bus;
    Ppu ppu;
    Cpu cpu{bus};

    WatchdogCpu(std::initializer_list<Byte> program, Address entry = 0x6000) {
        auto bios = mapperbus::tests::make_fds_bios();
        std::ranges::fill(bios, 0xEA);
        for (std::size_t vector = 8192 - 6; vector < bios.size(); vector += 2) {
            bios[vector] = static_cast<Byte>(entry);
            bios[vector + 1] = static_cast<Byte>(entry >> 8U);
        }
        if (entry >= 0xE000)
            std::ranges::copy(program, bios.begin() + (entry - 0xE000));
        REQUIRE(drive.load_disk(mapperbus::tests::make_fds_image()));
        auto loaded = Cartridge::from_fds_bios(bios, drive);
        REQUIRE(loaded);
        cartridge.emplace(std::move(*loaded));
        bus.connect_cartridge(&*cartridge);
        bus.connect_fds(&drive);
        bus.connect_ppu(&ppu);
        if (entry < 0xE000) {
            for (const auto value : program)
                bus.write(entry++, value);
        }
        bus.write(0, 0);
        bus.write(1, 0x60); // indirect address $6000
        cpu.reset();
    }

    void enable() {
        drive.write(0x4023, 0x82);
        drive.write(0x4023, 0x83);
    }

    std::pair<std::uint16_t, Byte> counters() const {
        StateWriter writer;
        drive.save_state(writer);
        const auto& bytes = writer.data();
        std::uint16_t timer = 0;
        std::memcpy(&timer, bytes.data() + bytes.size() - 5, sizeof(timer));
        return {timer, bytes[bytes.size() - 3]};
    }
};

void arm_disk_irqs(WatchdogCpu& machine, uint32_t advance = 160) {
    machine.drive.write(0x4023, 0x83);
    machine.drive.write(0x4025, 0xE5); // read data, byte-transfer IRQ enabled
    REQUIRE(next_byte(machine.drive) == 1);
    machine.drive.step(advance);
    machine.drive.write(0x4020, 1);
    machine.drive.write(0x4021, 0);
    machine.drive.write(0x4022, 2);
    machine.drive.step(3); // one-shot timer expires
    for (int cycle = 0; cycle < 4096; ++cycle)
        machine.drive.clock_cpu_cycle(0x6000); // latch the refresh watchdog too
    REQUIRE(machine.drive.irq_pending());
}

void prepare_dmc(WatchdogCpu& machine, Apu& apu, Region region = Region::NTSC) {
    machine.bus.connect_apu(&apu);
    machine.bus.set_region(region);
    machine.bus.write(0x4012, 0x80); // $E000
    machine.bus.write(0x4013, 0);
    machine.bus.write(0x4015, 0x10);
    // Untimed setup does not advance the FDS drive/watchdog.
    apu.step(3);
    REQUIRE(apu.dmc_dma_ready());
}

Byte oam_byte(WatchdogCpu& machine, Byte address) {
    machine.bus.write(0x2003, address);
    return machine.bus.read(0x2004);
}
} // namespace

TEST_CASE("CPU indexed dummy reads service FDS status and data registers",
          "[fds][cpu][bus-side-effects]") {
    for (const bool data : {false, true}) {
        for (const Byte opcode : {0xBD, 0xB9, 0xB1, 0x9D, 0x99, 0x91}) {
            CAPTURE(data, opcode);
            const bool indirect = opcode == 0xB1 || opcode == 0x91;
            const Byte index = data ? 0x32 : 0x31;
            WatchdogCpu machine({0xA2,
                                 index,
                                 0xA0,
                                 index,
                                 opcode,
                                 static_cast<Byte>(indirect ? 0x10 : 0xFF),
                                 static_cast<Byte>(indirect ? 0xEA : 0x40)});
            machine.bus.write(0x10, 0xFF);
            machine.bus.write(0x11, 0x40);
            REQUIRE(machine.cpu.step() == 2);
            REQUIRE(machine.cpu.step() == 2);
            arm_disk_irqs(machine);
            // $40FF + X/Y crosses to $4130/$4131. The unadjusted read is
            // still $4030/$4031, even for stores and indirect-indexed modes.
            REQUIRE(machine.cpu.step() == (indirect ? 6 : 5));
            CHECK((machine.drive.read(0x4030) & 0x83U) == (data ? 0x03 : 0x80));
        }
    }
}

TEST_CASE("Unofficial NOP and RTS dummy reads acknowledge FDS registers",
          "[fds][cpu][bus-side-effects]") {
    for (const bool data : {false, true}) {
        const Byte low = data ? 0x31 : 0x30;
        DYNAMIC_SECTION("register " << int(low)) {
            SECTION("absolute NOP performs its discarded read") {
                WatchdogCpu machine({0x0C, low, 0x40});
                arm_disk_irqs(machine);
                REQUIRE(machine.cpu.step() == 4);
                CHECK((machine.drive.read(0x4030) & 0x83U) == (data ? 0x03 : 0x80));
            }
            SECTION("RTS reads the popped address before incrementing it") {
                WatchdogCpu machine({0x60});
                machine.bus.write(0x01FE, low);
                machine.bus.write(0x01FF, 0x40);
                arm_disk_irqs(machine);
                REQUIRE(machine.cpu.step() == 6);
                CHECK(machine.cpu.pc() == 0x4001 + low);
                CHECK((machine.drive.read(0x4030) & 0x83U) == (data ? 0x03 : 0x80));
            }
        }
    }
}

TEST_CASE("Accepted IRQ prefetch reads retain the FDS register acknowledgement rules",
          "[fds][cpu][bus-side-effects]") {
    for (const bool data : {false, true}) {
        const Byte low = data ? 0x31 : 0x30;
        WatchdogCpu machine({0x58, 0x4C, low, 0x40}); // CLI; JMP to the interrupted read
        REQUIRE(machine.cpu.step() == 2);
        arm_disk_irqs(machine); // JMP polls the source before its final cycle
        REQUIRE(machine.cpu.step() == 3);
        REQUIRE(machine.cpu.step() == 7);
        CHECK(machine.cpu.pc() == 0x6000); // the IRQ remains accepted after acknowledgement
        CHECK((machine.drive.read(0x4030) & 0x83U) == (data ? 0x03 : 0x80));
    }
}

TEST_CASE("OAM DMA sees FDS bytes arriving during its source reads",
          "[fds][dma][bus-side-effects]") {
    WatchdogCpu machine({0xA9, 0x40, 0x8D, 0x14, 0x40, 0xEA});
    REQUIRE(machine.cpu.step() == 2);
    arm_disk_irqs(machine, 100);
    REQUIRE(machine.cpu.step() == 517);
    CHECK((oam_byte(machine, 0x30) & 0x83U) == 0x83); // $4030 retains the byte flag
    CHECK(oam_byte(machine, 0x31) == '*'); // next disk byte arrived before the $4031 read
    CHECK((machine.drive.read(0x4030) & 0x83U) == 0x80); // later bytes assert it again
}

TEST_CASE("OAM DMA halt reads establish open bus and RMW uses only the final page",
          "[fds][dma][bus-side-effects]") {
    SECTION("STA halts on the following opcode read") {
        WatchdogCpu machine({0xA9, 0x41, 0x8D, 0x14, 0x40, 0xEA});
        REQUIRE(machine.cpu.step() == 2);
        REQUIRE(machine.cpu.step() == 517);
        for (unsigned index = 0; index < 256; ++index)
            CHECK(oam_byte(machine, static_cast<Byte>(index)) == ((index & 3U) == 2 ? 0xE2 : 0xEA));
    }
    SECTION("INC writes $40 then $41, followed by one DMA") {
        WatchdogCpu machine({0xEE, 0x14, 0x40, 0xEA});
        REQUIRE(machine.cpu.step() == 519);
        for (unsigned index = 0; index < 256; ++index)
            CHECK(oam_byte(machine, static_cast<Byte>(index)) == ((index & 3U) == 2 ? 0xE2 : 0xEA));
    }
}

TEST_CASE("DMC halt reads acknowledge FDS registers without duplicating their clocks",
          "[fds][dma][bus-side-effects]") {
    for (const bool data : {false, true}) {
        for (const auto region : {Region::NTSC, Region::PAL}) {
            CAPTURE(data, region);
            WatchdogCpu machine({0xEA});
            Apu apu;
            prepare_dmc(machine, apu, region);
            arm_disk_irqs(machine);
            REQUIRE(machine.bus.run_dmc_dma(data ? 0x4031 : 0x4030) == 3);
            // Both chips perform the initial halt read; PAL suppresses repeats.
            const unsigned expected = data ? 0x03 : 0x80;
            CHECK((machine.drive.read(0x4030) & 0x83U) == expected);
            const auto after = machine.counters();
            machine.cpu.add_stall_cycles(3);
            CHECK(machine.counters() == after);
        }
    }
}

TEST_CASE("DMC repeated data reads consume a byte arriving between halt cycles",
          "[fds][dma][bus-side-effects]") {
    WatchdogCpu machine({0xEA});
    Apu apu;
    prepare_dmc(machine, apu);
    arm_disk_irqs(machine, 0);
    machine.drive.step(146); // next byte arrives on halt cycle 1 or 2
    REQUIRE(machine.bus.run_dmc_dma(0x4031) == 3);
    CHECK((machine.drive.read(0x4030) & 0x83U) == 0x03);
    CHECK(machine.drive.read(0x4031) == '*');
}

TEST_CASE("FDS timer arming excludes cycles before its control write",
          "[fds][cpu][bus-side-effects]") {
    WatchdogCpu machine({0xA9, 2, 0x8D, 0x22, 0x40});
    machine.drive.write(0x4023, 3);
    machine.drive.write(0x4020, 1);
    machine.drive.write(0x4021, 0);
    REQUIRE(machine.cpu.step() == 2);
    REQUIRE(machine.cpu.step() == 4);
    CHECK_FALSE(machine.drive.irq_pending());
    machine.drive.step(3);
    CHECK(machine.drive.irq_pending());
}

TEST_CASE("OAM DMA restores full FDS transfer state without double-clocking the drive",
          "[fds][dma][state][bus-side-effects]") {
    auto bios = mapperbus::tests::make_fds_bios();
    constexpr std::array<Byte, 6> kProgram{0xA9, 0x40, 0x8D, 0x14, 0x40, 0xEA};
    std::ranges::copy(kProgram, bios.begin());
    Emulator emulator;
    REQUIRE(emulator.load_fds(bios, mapperbus::tests::make_fds_image()));
    emulator.reset();
    emulator.step_instruction(); // LDA #$40
    emulator.fds().write(0x4023, 3);
    emulator.fds().write(0x4025, 0xE5);
    REQUIRE(next_byte(emulator.fds()) == 1);
    emulator.fds().step(100);
    const auto before = emulator.save_state();
    emulator.step_instruction();
    REQUIRE(emulator.cpu().cycles() == 519);
    REQUIRE(emulator.read_cpu(0x4031) == 'N'); // four more bytes, not eight
    const auto expected = emulator.save_state();
    REQUIRE(emulator.load_state(before));
    emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0x4031) == 'N');
    CHECK(emulator.save_state() == expected);
}

TEST_CASE("NMOS read-modify-write sends both old and new values to memory-mapped I/O",
          "[fds][cpu][bus-side-effects]") {
    WatchdogCpu machine({0xEE, 0x04, 0x20}); // INC OAMDATA
    machine.bus.write(0x2003, 0);
    machine.bus.write(0x2004, 0x22);
    machine.bus.write(0x2003, 0);
    REQUIRE(machine.cpu.step() == 6);
    CHECK(oam_byte(machine, 0) == 0x22);
    CHECK(oam_byte(machine, 1) == 0x23);
}

TEST_CASE("Every CPU opcode accounts for all elapsed watchdog cycles", "[fds][cpu][watchdog]") {
    for (unsigned opcode = 0; opcode < 256; ++opcode) {
        WatchdogCpu machine({static_cast<Byte>(opcode), 0, 0x60});
        machine.enable();
        const auto cycles = machine.cpu.step();
        INFO("opcode " << opcode << " cycles " << cycles);
        CHECK(machine.counters().first == 4095 - cycles);
    }
}

TEST_CASE("CPU dummy stack and indexed cycles refresh FDS RAM at their bus addresses",
          "[fds][cpu][watchdog]") {
    struct Case {
        std::initializer_list<Byte> program;
        Address entry;
        unsigned cycles;
        Byte refreshes;
    };
    for (const auto& test : {
             Case{.program = {0xEA},
                  .entry = 0x6000,
                  .cycles = 2,
                  .refreshes = 0}, // implied NOP, dummy PC in DRAM
             Case{.program = {0xEA},
                  .entry = 0xE000,
                  .cycles = 2,
                  .refreshes = 2}, // same NOP in BIOS, both clocks refresh
             Case{.program = {0x20, 0, 0x60},
                  .entry = 0x6000,
                  .cycles = 6,
                  .refreshes = 3}, // JSR: dummy stack read + two pushes
             Case{.program = {0x48}, .entry = 0x6000, .cycles = 3, .refreshes = 1}, // PHA
             Case{.program = {0x68},
                  .entry = 0x6000,
                  .cycles = 4,
                  .refreshes = 2}, // PLA: dummy stack read + pop
             Case{.program = {0x40},
                  .entry = 0x6000,
                  .cycles = 6,
                  .refreshes = 4}, // RTI: dummy + three pops
             Case{.program = {0xB5, 0x10},
                  .entry = 0x6000,
                  .cycles = 4,
                  .refreshes = 2}, // LDA zp,X: dummy + data
             Case{.program = {0xA1, 0},
                  .entry = 0x6000,
                  .cycles = 6,
                  .refreshes = 3}, // LDA (zp,X): dummy + pointer
             Case{.program = {0xE6, 0x10},
                  .entry = 0x6000,
                  .cycles = 5,
                  .refreshes = 3}, // INC zp: read + old/new writes
             Case{.program = {0xEE, 0, 0x60},
                  .entry = 0xE000,
                  .cycles = 6,
                  .refreshes = 3}, // INC DRAM from BIOS: operand writes do not refresh
             Case{.program = {0xD0, 2},
                  .entry = 0xDFFD,
                  .cycles = 4,
                  .refreshes = 0}, // branch correction address stays in DRAM
         }) {
        WatchdogCpu machine(test.program, test.entry);
        machine.enable();
        INFO("entry " << test.entry << " opcode " << static_cast<int>(*test.program.begin()));
        REQUIRE(machine.cpu.step() == test.cycles);
        CHECK(machine.counters().second == test.refreshes);
        CHECK(machine.counters().first == 4095 - test.cycles);
    }
    SECTION("page-cross correction observes the unadjusted address") {
        WatchdogCpu machine({0xA0, 1, 0xB1, 0}); // LDY #1; LDA ($00),Y
        machine.bus.write(0, 0xFF);
        machine.bus.write(1, 0xDF); // final $E000, correction $DF00
        REQUIRE(machine.cpu.step() == 2);
        machine.enable();
        REQUIRE(machine.cpu.step() == 6);
        CHECK(machine.counters().second == 3); // two ZP pointer reads + BIOS data
    }
}

TEST_CASE("OAM DMA clocks the FDS watchdog for both source reads and PPU writes",
          "[fds][dma][watchdog]") {
    for (const Byte page : {0, 0x60}) {
        for (const bool odd : {false, true}) {
            WatchdogCpu machine({0xA9, page, 0x48, 0x8D, 0x14, 0x40});
            REQUIRE(machine.cpu.step() == 2);
            if (odd) {
                REQUIRE(machine.cpu.step() == 3); // PHA makes the DMA start odd
            } else {
                machine.bus.write(0x6002, 0xEA); // NOP keeps the start even
                REQUIRE(machine.cpu.step() == 2);
            }
            machine.enable();
            for (int row = 0; row < 63; ++row)
                machine.drive.clock_cpu_cycle(0);
            REQUIRE(machine.cpu.step() == (odd ? 518 : 517));
            // Starting row 63 + the $4014 write + 256 PPU writes (+256 RAM
            // reads on page 0). Last reload is 128 or 64 clocks before the end.
            CHECK(machine.counters().second == 64);
            CHECK(machine.counters().first == (page == 0 ? 3181 : 3117));
        }
    }
}

TEST_CASE("DMC stalls count once and debugger reads do not clock the FDS watchdog",
          "[fds][dma][watchdog]") {
    WatchdogCpu machine({0xEA});
    Apu apu;
    prepare_dmc(machine, apu);
    machine.enable();
    for (int read = 0; read < 10000; ++read) {
        (void)machine.bus.read(0x6000);
        (void)machine.bus.read(0xE000);
    }
    CHECK(machine.counters() == std::pair<std::uint16_t, Byte>{4095, 0});
    REQUIRE(machine.bus.run_dmc_dma(0x6000) == 3);
    CHECK(machine.counters() == std::pair<std::uint16_t, Byte>{4092, 1});
    machine.cpu.add_stall_cycles(3); // clocks were already accounted by the DMA path
    CHECK(machine.counters() == std::pair<std::uint16_t, Byte>{4092, 1});
    // A filled sample buffer does not request a duplicate DMA.
    CHECK(machine.bus.run_dmc_dma(0xE000) == 0);
    CHECK(machine.counters() == std::pair<std::uint16_t, Byte>{4092, 1});
}

TEST_CASE("CPU loops which refresh RAM avoid the FDS watchdog while tight DRAM loops trip it",
          "[fds][cpu][watchdog]") {
    for (const bool refreshing : {false, true}) {
        WatchdogCpu machine(refreshing ? std::initializer_list<Byte>{0xA5, 0, 0x4C, 0, 0x60}
                                       : std::initializer_list<Byte>{0x4C, 0, 0x60});
        machine.enable();
        for (int instruction = 0; instruction < 5000; ++instruction)
            machine.cpu.step();
        CHECK(machine.drive.irq_pending() == !refreshing);
    }
}

TEST_CASE("Emulator rejects malformed watchdog state and restores the running machine",
          "[fds][state][watchdog]") {
    Emulator emulator;
    REQUIRE(
        emulator.load_fds(mapperbus::tests::make_fds_bios(), mapperbus::tests::make_fds_image()));
    emulator.reset();
    emulator.fds().write(0x4023, 0x83);
    for (int cycle = 0; cycle < 4096; ++cycle)
        emulator.fds().clock_cpu_cycle(0x6000);
    REQUIRE(emulator.fds().irq_pending());
    const auto original = emulator.save_state();
    constexpr std::size_t kMapperRamBytes = 32 * 1024 + 8 * 1024;
    for (const auto [offset, value] :
         std::array<std::pair<unsigned, Byte>, 4>{{{1, 0x10}, {2, 128}, {3, 2}, {4, 2}}}) {
        auto invalid = original;
        invalid[invalid.size() - kMapperRamBytes - 5 + offset] = value;
        REQUIRE_FALSE(emulator.load_state(invalid));
        REQUIRE(emulator.save_state() == original);
        REQUIRE(emulator.fds().irq_pending());
    }
}
