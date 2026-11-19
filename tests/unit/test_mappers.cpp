#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/cartridge/ines_header.hpp"
#include "core/cpu/cpu.hpp"
#include "core/emulator.hpp"
#include "core/mappers/axrom.hpp"
#include "core/mappers/cnrom.hpp"
#include "core/mappers/color_dreams.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/mmc1.hpp"
#include "core/mappers/mmc2.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/sunsoft5b.hpp"
#include "core/mappers/uxrom.hpp"

using namespace mapperbus::core;

static INesHeader make_header(uint8_t prg_banks, uint8_t chr_banks, MirrorMode mirror) {
    return INesHeader{
        .prg_rom_banks = prg_banks,
        .chr_rom_banks = chr_banks,
        .mapper_number = 0,
        .submapper = 0,
        .mirror_mode = mirror,
        .region = Region::NTSC,
        .has_battery = false,
        .has_trainer = false,
        .is_nes2 = false,
    };
}

// Helper: perform 5 serial writes to MMC1
static void mmc1_serial_write(Mmc1& mapper, Address addr, Byte value) {
    for (int i = 0; i < 5; ++i) {
        mapper.write_prg(addr, (value >> i) & 0x01);
    }
}

// === UxROM ===

TEST_CASE("UxROM fixed last bank at $C000", "[uxrom]") {
    auto header = make_header(8, 0, MirrorMode::Vertical);
    std::vector<Byte> prg(128 * 1024, 0);
    prg[7 * 0x4000] = 0xAA;
    prg[7 * 0x4000 + 0x3FFF] = 0xBB;

    Uxrom mapper(header, std::move(prg), {});

    REQUIRE(mapper.read_prg(0xC000) == 0xAA);
    REQUIRE(mapper.read_prg(0xFFFF) == 0xBB);
}

TEST_CASE("UxROM bank switching", "[uxrom]") {
    auto header = make_header(8, 0, MirrorMode::Vertical);
    std::vector<Byte> prg(128 * 1024, 0);
    prg[0 * 0x4000] = 0x11;
    prg[3 * 0x4000] = 0x33;

    Uxrom mapper(header, std::move(prg), {});

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    mapper.write_prg(0x8000, 0x03);
    REQUIRE(mapper.read_prg(0x8000) == 0x33);
}

TEST_CASE("UxROM and AxROM select non-power-of-two banks", "[uxrom][axrom]") {
    std::vector<Byte> ux_prg(3 * 0x4000, 0);
    ux_prg[0] = 0x10;
    ux_prg[0x4000] = 0x11;
    ux_prg[0x8000] = 0x12;
    Uxrom uxrom(make_header(3, 0, MirrorMode::Vertical), std::move(ux_prg), {});
    uxrom.write_prg(0x8000, 1);
    REQUIRE(uxrom.read_prg(0x8000) == 0x11);
    REQUIRE(uxrom.read_prg(0xC000) == 0x12);

    std::vector<Byte> ax_prg(3 * 0x8000, 0);
    ax_prg[0] = 0x20;
    ax_prg[0x8000] = 0x21;
    ax_prg[0x10000] = 0x22;
    Axrom axrom(make_header(6, 0, MirrorMode::Vertical), std::move(ax_prg), {});
    axrom.write_prg(0x8000, 1);
    REQUIRE(axrom.read_prg(0x8000) == 0x21);
}

TEST_CASE("UxROM CHR RAM", "[uxrom]") {
    Uxrom mapper(make_header(4, 0, MirrorMode::Horizontal), std::vector<Byte>(64 * 1024, 0), {});

    mapper.write_chr(0x0000, 0x42);
    REQUIRE(mapper.read_chr(0x0000) == 0x42);
    mapper.write_chr(0x1FFF, 0x99);
    REQUIRE(mapper.read_chr(0x1FFF) == 0x99);
}

// === CNROM ===

TEST_CASE("CNROM fixed PRG", "[cnrom]") {
    std::vector<Byte> prg(32 * 1024, 0);
    prg[0] = 0xAA;
    prg[0x7FFF] = 0xBB;

    Cnrom mapper(
        make_header(2, 4, MirrorMode::Horizontal), std::move(prg), std::vector<Byte>(32 * 1024, 0));

    REQUIRE(mapper.read_prg(0x8000) == 0xAA);
    REQUIRE(mapper.read_prg(0xFFFF) == 0xBB);
}

TEST_CASE("CNROM CHR bank switching", "[cnrom]") {
    std::vector<Byte> chr(32 * 1024, 0);
    chr[0 * 0x2000] = 0x11;
    chr[2 * 0x2000] = 0x33;
    chr[3 * 0x2000] = 0x44;

    Cnrom mapper(
        make_header(2, 4, MirrorMode::Horizontal), std::vector<Byte>(32 * 1024, 0), std::move(chr));

    REQUIRE(mapper.read_chr(0x0000) == 0x11);
    mapper.write_prg(0x8000, 0x02);
    REQUIRE(mapper.read_chr(0x0000) == 0x33);
    mapper.write_prg(0xFFFF, 0x03);
    REQUIRE(mapper.read_chr(0x0000) == 0x44);
}

TEST_CASE("CNROM CHR ROM is read-only", "[cnrom]") {
    Cnrom mapper(make_header(1, 1, MirrorMode::Vertical),
                 std::vector<Byte>(16 * 1024, 0),
                 std::vector<Byte>(8 * 1024, 0xAA));

    mapper.write_chr(0x0000, 0xFF);
    REQUIRE(mapper.read_chr(0x0000) == 0xAA);
}

// === AxROM ===

TEST_CASE("AxROM 32KB bank switching", "[axrom]") {
    std::vector<Byte> prg(128 * 1024, 0);
    prg[0 * 0x8000] = 0x11;
    prg[2 * 0x8000] = 0x22;
    prg[3 * 0x8000] = 0x33;

    Axrom mapper(make_header(8, 0, MirrorMode::Horizontal), std::move(prg), {});

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    mapper.write_prg(0x8000, 0x02);
    REQUIRE(mapper.read_prg(0x8000) == 0x22);
    mapper.write_prg(0x8000, 0x03);
    REQUIRE(mapper.read_prg(0x8000) == 0x33);
}

TEST_CASE("AxROM mirroring control", "[axrom]") {
    Axrom mapper(make_header(4, 0, MirrorMode::Horizontal), std::vector<Byte>(64 * 1024, 0), {});

    REQUIRE(mapper.mirror_mode() == MirrorMode::SingleLower);
    mapper.write_prg(0x8000, 0x10);
    REQUIRE(mapper.mirror_mode() == MirrorMode::SingleUpper);
    mapper.write_prg(0x8000, 0x01);
    REQUIRE(mapper.mirror_mode() == MirrorMode::SingleLower);
}

TEST_CASE("AxROM CHR RAM", "[axrom]") {
    Axrom mapper(make_header(4, 0, MirrorMode::Horizontal), std::vector<Byte>(64 * 1024, 0), {});

    mapper.write_chr(0x0000, 0x55);
    REQUIRE(mapper.read_chr(0x0000) == 0x55);
}

TEST_CASE("AxROM NES 2.0 submapper 2 applies PRG-ROM bus conflicts", "[axrom]") {
    auto header = make_header(4, 0, MirrorMode::Horizontal);
    header.is_nes2 = true;
    header.submapper = 2;

    std::vector<Byte> prg(128 * 1024, 0);
    prg[0x0000] = 0x03;
    prg[2 * 0x8000] = 0xAA;
    prg[2 * 0x8000 + 0x0001] = 0x10;

    Axrom mapper(header, std::move(prg), {});

    mapper.write_prg(0x8000, 0x02);
    REQUIRE(mapper.read_prg(0x8000) == 0xAA);

    mapper.write_prg(0x8001, 0x10);
    REQUIRE(mapper.mirror_mode() == MirrorMode::SingleUpper);
}

// === Color Dreams ===

TEST_CASE("Color Dreams PRG bank", "[color-dreams]") {
    std::vector<Byte> prg(64 * 1024, 0);
    prg[0 * 0x8000] = 0x11;
    prg[1 * 0x8000] = 0x22;

    ColorDreams mapper(
        make_header(4, 4, MirrorMode::Vertical), std::move(prg), std::vector<Byte>(32 * 1024, 0));

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    mapper.write_prg(0x8000, 0x01);
    REQUIRE(mapper.read_prg(0x8000) == 0x22);
}

TEST_CASE("Color Dreams CHR bank", "[color-dreams]") {
    std::vector<Byte> chr(32 * 1024, 0);
    chr[0 * 0x2000] = 0xAA;
    chr[2 * 0x2000] = 0xCC;

    ColorDreams mapper(
        make_header(2, 4, MirrorMode::Vertical), std::vector<Byte>(32 * 1024, 0), std::move(chr));

    REQUIRE(mapper.read_chr(0x0000) == 0xAA);
    mapper.write_prg(0x8000, 0x20);
    REQUIRE(mapper.read_chr(0x0000) == 0xCC);
}

TEST_CASE("Color Dreams combined PRG+CHR", "[color-dreams]") {
    std::vector<Byte> prg(64 * 1024, 0);
    prg[1 * 0x8000] = 0x77;
    std::vector<Byte> chr(64 * 1024, 0);
    chr[3 * 0x2000] = 0x88;

    ColorDreams mapper(make_header(4, 8, MirrorMode::Horizontal), std::move(prg), std::move(chr));

    mapper.write_prg(0x8000, 0x31);
    REQUIRE(mapper.read_prg(0x8000) == 0x77);
    REQUIRE(mapper.read_chr(0x0000) == 0x88);
}

// === MMC1 ===

TEST_CASE("MMC1 CPU RMW writes filter consecutive data but always accept reset",
          "[mmc1][cpu][bus-side-effects]") {
    register_builtin_mappers();
    for (const bool reset : {false, true}) {
        CAPTURE(reset);
        constexpr std::size_t kBankSize = 16384;
        std::vector<Byte> rom(16 + 4 * kBankSize);
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1A;
        rom[4] = 4;
        rom[6] = 0x10;
        for (unsigned bank = 0; bank < 4; ++bank)
            std::fill_n(rom.begin() + static_cast<std::ptrdiff_t>(16 + bank * kBankSize),
                        kBankSize,
                        static_cast<Byte>(bank));
        rom[16 + 3 * kBankSize + 0x2000] = reset ? 0x7F : 0;
        for (const std::size_t offset : {rom.size() - 6, rom.size() - 4, rom.size() - 2}) {
            rom[offset] = 0;
            rom[offset + 1] = 2; // run from RAM $0200
        }
        auto result = Cartridge::from_data(rom);
        REQUIRE(result);
        auto cartridge = std::move(*result);
        MemoryBus bus;
        bus.connect_cartridge(&cartridge);
        Cpu cpu(bus);
        Address pc = 0x0200;
        if (reset) {
            for (const Byte value : {0xEE, 0, 0xE0, 0xA9, 0}) // INC $E000; LDA #0
                bus.write(pc++, value);
            for (int index = 0; index < 4; ++index) {
                bus.write(pc++, 0x8D);
                bus.write(pc++, 0);
                bus.write(pc++, 0xE0);
            }
        } else {
            for (int index = 0; index < 5; ++index) {
                bus.write(pc++, 0xEE);
                bus.write(pc++, 0);
                bus.write(pc++, 0xE0);
            }
        }
        cpu.reset();
        for (int instruction = 0; instruction < (reset ? 6 : 5); ++instruction)
            cpu.step();
        // Five INCs should clock five zero bits, not ten alternating bits.
        // INC $7F -> $80 must reset even though its reset write is consecutive;
        // four following STA writes must then leave the five-bit latch incomplete.
        CHECK(bus.read(0x8000) == 0);
    }
}

TEST_CASE("MMC1 default fixed last bank", "[mmc1]") {
    std::vector<Byte> prg(256 * 1024, 0);
    prg[15 * 0x4000] = 0xFF;

    Mmc1 mapper(make_header(16, 0, MirrorMode::Horizontal), std::move(prg), {});

    REQUIRE(mapper.read_prg(0xC000) == 0xFF);
}

TEST_CASE("MMC1 PRG bank switching", "[mmc1]") {
    std::vector<Byte> prg(256 * 1024, 0);
    prg[0 * 0x4000] = 0x11;
    prg[5 * 0x4000] = 0x55;

    Mmc1 mapper(make_header(16, 0, MirrorMode::Horizontal), std::move(prg), {});

    REQUIRE(mapper.read_prg(0x8000) == 0x11);
    mmc1_serial_write(mapper, 0xE000, 0x05);
    REQUIRE(mapper.read_prg(0x8000) == 0x55);
}

TEST_CASE("MMC1 reset on bit 7", "[mmc1]") {
    Mmc1 mapper(make_header(16, 0, MirrorMode::Horizontal), std::vector<Byte>(256 * 1024, 0), {});

    mapper.write_prg(0x8000, 0x01);
    mapper.write_prg(0x8000, 0x00);
    mapper.write_prg(0x8000, 0x01);
    mapper.write_prg(0x8000, 0x80); // reset
    // Should not crash
    REQUIRE(true);
}

TEST_CASE("MMC1 mirroring control", "[mmc1]") {
    Mmc1 mapper(make_header(8, 0, MirrorMode::Horizontal), std::vector<Byte>(128 * 1024, 0), {});

    mmc1_serial_write(mapper, 0x8000, 0x0E);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);

    mmc1_serial_write(mapper, 0x8000, 0x0F);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("MMC1 CHR 4KB mode", "[mmc1]") {
    std::vector<Byte> chr(128 * 1024, 0);
    chr[5 * 0x1000] = 0x55;

    Mmc1 mapper(make_header(8, 16, MirrorMode::Horizontal),
                std::vector<Byte>(128 * 1024, 0),
                std::move(chr));

    mmc1_serial_write(mapper, 0x8000, 0x1C);
    mmc1_serial_write(mapper, 0xA000, 0x05);
    REQUIRE(mapper.read_chr(0x0000) == 0x55);
}

TEST_CASE("MMC1 PRG RAM", "[mmc1]") {
    Mmc1 mapper(make_header(8, 0, MirrorMode::Horizontal), std::vector<Byte>(128 * 1024, 0), {});

    mapper.write_prg(0x6000, 0x42);
    REQUIRE(mapper.read_prg(0x6000) == 0x42);
}

TEST_CASE("MMC1 disabled PRG RAM is not mapped", "[mmc1]") {
    Mmc1 mapper(make_header(8, 0, MirrorMode::Horizontal), std::vector<Byte>(128 * 1024, 0), {});

    REQUIRE(mapper.maps_prg(0x6000));
    mmc1_serial_write(mapper, 0xE000, 0x10);

    REQUIRE_FALSE(mapper.maps_prg(0x6000));
    REQUIRE(mapper.maps_prg(0x8000));
}

// === MMC3 ===

TEST_CASE("MMC3 PRG bank switching", "[mmc3]") {
    std::vector<Byte> prg(256 * 1024, 0);
    prg[5 * 0x2000] = 0x55;
    prg[(32 - 1) * 0x2000] = 0xFF;

    Mmc3 mapper(make_header(16, 0, MirrorMode::Vertical), std::move(prg), {});

    REQUIRE(mapper.read_prg(0xE000) == 0xFF);
    mapper.write_prg(0x8000, 0x06);
    mapper.write_prg(0x8001, 0x05);
    REQUIRE(mapper.read_prg(0x8000) == 0x55);
}

TEST_CASE("MMC3 CHR bank switching", "[mmc3]") {
    std::vector<Byte> chr(256 * 1024, 0);
    chr[10 * 0x0400] = 0xAA;

    Mmc3 mapper(
        make_header(8, 32, MirrorMode::Vertical), std::vector<Byte>(128 * 1024, 0), std::move(chr));

    mapper.write_prg(0x8000, 0x02);
    mapper.write_prg(0x8001, 10);
    REQUIRE(mapper.read_chr(0x1000) == 0xAA);
}

TEST_CASE("MMC3 mirroring", "[mmc3]") {
    Mmc3 mapper(make_header(8, 8, MirrorMode::Vertical),
                std::vector<Byte>(128 * 1024, 0),
                std::vector<Byte>(64 * 1024, 0));

    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    mapper.write_prg(0xA000, 0x01);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Horizontal);
    mapper.write_prg(0xA000, 0x00);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("MMC3 IRQ counter", "[mmc3]") {
    Mmc3 mapper(make_header(8, 8, MirrorMode::Vertical),
                std::vector<Byte>(128 * 1024, 0),
                std::vector<Byte>(64 * 1024, 0));

    mapper.write_prg(0xC000, 0x03);
    mapper.write_prg(0xC001, 0x00);
    mapper.write_prg(0xE001, 0x00);

    REQUIRE_FALSE(mapper.irq_pending());

    mapper.clock_irq_counter(); // reload to 3
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.clock_irq_counter(); // 2
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.clock_irq_counter(); // 1
    REQUIRE_FALSE(mapper.irq_pending());
    mapper.clock_irq_counter(); // 0 → IRQ
    REQUIRE(mapper.irq_pending());

    mapper.write_prg(0xE000, 0x00);
    REQUIRE_FALSE(mapper.irq_pending());
}

TEST_CASE("MMC3 submapper 4 suppresses repeated zero-latch IRQs", "[mmc3]") {
    auto header = make_header(8, 8, MirrorMode::Vertical);
    header.is_nes2 = true;
    header.submapper = 4;

    Mmc3 mapper(header, std::vector<Byte>(128 * 1024, 0), std::vector<Byte>(64 * 1024, 0));

    mapper.write_prg(0xC000, 0x00);
    mapper.write_prg(0xC001, 0x00);
    mapper.write_prg(0xE001, 0x00);

    mapper.clock_irq_counter();
    REQUIRE(mapper.irq_pending());

    mapper.write_prg(0xE000, 0x00);
    mapper.write_prg(0xE001, 0x00);
    mapper.clock_irq_counter();
    REQUIRE_FALSE(mapper.irq_pending());
}

TEST_CASE("MMC3 PRG RAM", "[mmc3]") {
    Mmc3 mapper(make_header(8, 8, MirrorMode::Vertical),
                std::vector<Byte>(128 * 1024, 0),
                std::vector<Byte>(64 * 1024, 0));

    mapper.write_prg(0x7000, 0xAB);
    REQUIRE(mapper.read_prg(0x7000) == 0xAB);

    mapper.write_prg(0xA001, 0xC0);
    mapper.write_prg(0x7000, 0xCD);
    REQUIRE(mapper.read_prg(0x7000) == 0xAB);

    mapper.write_prg(0xA001, 0x00);
    REQUIRE_FALSE(mapper.maps_prg(0x7000));
    REQUIRE(mapper.maps_prg(0x8000));
}

TEST_CASE("iNES trainers execute after cold power on mapper WRAM", "[cartridge][trainer]") {
    register_builtin_mappers();
    constexpr std::size_t trainer_offset = 16U;
    constexpr std::size_t prg_offset = trainer_offset + 512U;
    constexpr std::array<Byte, 5> trainer_program = {0xA9U, 0x5AU, 0x85U, 0x02U, 0x60U};
    constexpr std::array<Byte, 6> reset_program = {0x20U, 0x00U, 0x70U, 0x4CU, 0x05U, 0xC0U};

    for (const unsigned mapper : {4U, 55U, 241U}) {
        CAPTURE(mapper);
        const bool malee2 = mapper == 55U;
        const Byte prg_banks = malee2 ? 3U : 2U;
        const Byte chr_banks = malee2 ? 1U : 0U;
        std::vector<Byte> image(prg_offset + prg_banks * 16384U + chr_banks * 8192U, 0xEAU);
        std::fill_n(image.begin(), trainer_offset, 0U);
        image[0] = 'N';
        image[1] = 'E';
        image[2] = 'S';
        image[3] = 0x1AU;
        image[4] = prg_banks;
        image[5] = chr_banks;
        image[6] = static_cast<Byte>(((mapper & 0x0FU) << 4U) | 0x04U);
        image[7] = static_cast<Byte>(mapper & 0xF0U);

        std::ranges::copy(trainer_program, image.begin() + trainer_offset);
        image[prg_offset - 1U] = 0xA5U;
        std::ranges::copy(reset_program, image.begin() + prg_offset + 0x4000U);
        constexpr auto reset_vector = prg_offset + 0x7FFCU;
        image[reset_vector] = 0x00U;
        image[reset_vector + 1U] = 0xC0U;

        auto result = Cartridge::from_data(image);
        REQUIRE(result);
        REQUIRE(result->read_prg(0x7000U) == trainer_program[0]);
        REQUIRE(result->read_prg(0x71FFU) == 0xA5U);
        result->write_prg(0x71FFU, 0x55U);
        result->reset();
        REQUIRE(result->read_prg(0x71FFU) == 0x55U);

        Emulator emulator;
        REQUIRE(emulator.load_cartridge(std::move(*result), {}));
        emulator.power_on();
        CHECK(emulator.read_cpu(0x71FFU) == 0xA5U);
        for (unsigned instruction = 0U; instruction < 16U; ++instruction)
            emulator.step_instruction();
        REQUIRE_FALSE(emulator.cpu().jammed());
        REQUIRE(emulator.read_cpu(0x0002U) == 0x5AU);
    }
}

TEST_CASE("Cold power preserves imported battery RAM on a trained cartridge",
          "[cartridge][trainer]") {
    register_builtin_mappers();
    // Mapper 241 with battery WRAM, a trainer, and 32 KiB PRG ROM.
    std::vector<Byte> image(16U + 512U + 32768U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 2U;
    image[6] = 0x16U;
    image[7] = 0xF0U;
    image[16] = 0xA5U;
    auto cartridge = Cartridge::from_data(image);
    REQUIRE(cartridge);
    REQUIRE(cartridge->read_prg(0x7000U) == 0xA5U);
    std::array<Byte, 8192> saved{};
    saved[0x1000U] = 0x5AU;
    cartridge->set_battery_ram(saved);
    cartridge->power_on();
    CHECK(cartridge->read_prg(0x7000U) == 0x5AU);
}

TEST_CASE("Mappers 514 through 530 load through the NES 2.0 cartridge registry", "[cartridge]") {
    register_builtin_mappers();
    struct Geometry {
        Byte prg_banks;
        Byte chr_banks;
        Byte prg_ram;
        Byte chr_ram;
    };
    constexpr std::array<Geometry, 17> geometry{{
        {8U, 0U, 7U, 7U},    // 514
        {96U, 0U, 0U, 7U},   // 515
        {32U, 64U, 0U, 0U},  // 516
        {8U, 0U, 0U, 7U},    // 517
        {128U, 0U, 7U, 7U},  // 518
        {8U, 4U, 0U, 0U},    // 519
        {32U, 0U, 0U, 7U},   // 520
        {12U, 0U, 0U, 7U},   // 521
        {8U, 0U, 7U, 7U},    // 522
        {128U, 32U, 0U, 0U}, // 523
        {16U, 32U, 0U, 0U},  // 524
        {8U, 32U, 0U, 0U},   // 525
        {128U, 32U, 0U, 0U}, // 526
        {16U, 32U, 0U, 0U},  // 527
        {24U, 32U, 7U, 0U},  // 528
        {32U, 64U, 0U, 0U},  // 529
        {16U, 64U, 0U, 0U},  // 530
    }};
    for (std::size_t i = 0; i < geometry.size(); ++i) {
        const auto mapper = 514U + i;
        CAPTURE(mapper);
        const auto layout = geometry[i];
        std::vector<Byte> image(16U + layout.prg_banks * 16384U + layout.chr_banks * 8192U);
        image[0] = 'N';
        image[1] = 'E';
        image[2] = 'S';
        image[3] = 0x1AU;
        image[4] = layout.prg_banks;
        image[5] = layout.chr_banks;
        image[6] = static_cast<Byte>(((mapper & 0x0FU) << 4U) | 0x01U);
        image[7] = static_cast<Byte>((mapper & 0xF0U) | 0x08U);
        image[8] = static_cast<Byte>(mapper >> 8U);
        image[10] = layout.prg_ram;
        image[11] = layout.chr_ram;

        auto cartridge = Cartridge::from_data(image);
        INFO((cartridge ? "" : cartridge.error()));
        REQUIRE(cartridge);
        CHECK(cartridge->header().mapper_number == mapper);

        image[8] |= 0xF0U;
        CHECK_FALSE(Cartridge::from_data(image));
    }
}

// === Sunsoft 5B audio ===

TEST_CASE("Sunsoft 5B noise generator produces output", "[sunsoft5b][audio]") {
    auto header = make_header(8, 0, MirrorMode::Vertical);
    Sunsoft5b m(header, std::vector<Byte>(128 * 1024, 0), {});

    auto write_reg = [&](Byte reg, Byte value) {
        m.write_prg(0xC000, reg);
        m.write_prg(0xE000, value);
    };
    write_reg(0x06, 0x04); // noise period
    write_reg(0x07, 0x37); // tones off, noise on channel A only
    write_reg(0x08, 0x0F); // channel A fixed volume 15

    float lo = 1e9f;
    float hi = -1e9f;
    int transitions = 0;
    float prev = 0.0f;
    for (int i = 0; i < 500000; ++i) {
        m.clock_audio();
        const float s = m.audio_output();
        lo = std::min(lo, s);
        hi = std::max(hi, s);
        if (i > 0 && s != prev)
            ++transitions;
        prev = s;
    }
    REQUIRE(hi > lo);           // noise toggles the channel
    REQUIRE(transitions > 100); // and does so irregularly/often
}

TEST_CASE("Sunsoft 5B envelope ramps channel volume", "[sunsoft5b][audio]") {
    auto header = make_header(8, 0, MirrorMode::Vertical);
    Sunsoft5b m(header, std::vector<Byte>(128 * 1024, 0), {});

    auto write_reg = [&](Byte reg, Byte value) {
        m.write_prg(0xC000, reg);
        m.write_prg(0xE000, value);
    };
    write_reg(0x07, 0x3F); // tone and noise both disabled: channel held high
    write_reg(0x08, 0x10); // channel A driven by envelope
    write_reg(0x0B, 0x40); // envelope period low
    write_reg(0x0C, 0x00);
    write_reg(0x0D, 0x00); // shape: single decay 31 -> 0, then hold at 0

    auto peak = [&](int cycles) {
        float p = 0.0f;
        for (int i = 0; i < cycles; ++i) {
            m.clock_audio();
            p = std::max(p, m.audio_output());
        }
        return p;
    };
    const float early = peak(10000);
    peak(4000000); // let the decay complete
    const float late = peak(10000);
    REQUIRE(early > 0.0f);
    REQUIRE(late < 0.1f * early);
}

// === MMC2 (mapper 9, PxROM) ===

TEST_CASE("MMC2 PRG bank switching at $8000", "[mmc2]") {
    // PxROM: 8 KB switchable at $8000, fixed 24 KB at $A000-$FFFF.
    std::vector<Byte> prg(128 * 1024, 0);
    prg[3 * 0x2000] = 0x55; // bank 3 sentinel
    Mmc2 mapper(make_header(8, 0, MirrorMode::Vertical), std::move(prg), {});

    mapper.write_prg(0xA000, 0x03);
    REQUIRE(mapper.read_prg(0x8000) == 0x55);
}

TEST_CASE("MMC2 fixed last banks at $A000-$FFFF", "[mmc2]") {
    // 8 PRG banks = 128 KB → 16×8KB. Last three 8KB banks fill $A000-$FFFF.
    std::vector<Byte> prg(128 * 1024, 0);
    prg[(16 - 3) * 0x2000] = 0xAA; // $A000 region
    prg[(16 - 2) * 0x2000] = 0xBB; // $C000 region
    prg[(16 - 1) * 0x2000] = 0xCC; // $E000 region

    Mmc2 mapper(make_header(8, 0, MirrorMode::Vertical), std::move(prg), {});

    REQUIRE(mapper.read_prg(0xA000) == 0xAA);
    REQUIRE(mapper.read_prg(0xC000) == 0xBB);
    REQUIRE(mapper.read_prg(0xE000) == 0xCC);
}

TEST_CASE("MMC2 PRG-RAM at $6000", "[mmc2]") {
    Mmc2 mapper(make_header(8, 0, MirrorMode::Vertical), std::vector<Byte>(128 * 1024, 0), {});

    REQUIRE(mapper.maps_prg(0x6000));
    mapper.write_prg(0x6000, 0xDE);
    REQUIRE(mapper.read_prg(0x6000) == 0xDE);
}

TEST_CASE("MMC2 CHR latch toggles left half", "[mmc2]") {
    // CHR layout: 4 KB pages. The latch selects between FD ($B000) and FE
    // ($D000) registered pages for the left ($0000-$0FFF) half.
    std::vector<Byte> chr(128 * 1024, 0);
    chr[2 * 0x1000] = 0xFD; // page 2 = FD content
    chr[5 * 0x1000] = 0xFE; // page 5 = FE content

    Mmc2 mapper(
        make_header(8, 16, MirrorMode::Vertical), std::vector<Byte>(128 * 1024, 0), std::move(chr));

    mapper.write_prg(0xB000, 2); // left FD page = 2
    mapper.write_prg(0xD000, 5); // left FE page = 5

    // Power-on latch = FD: reading $0000 sees page 2.
    REQUIRE(mapper.read_chr(0x0000) == 0xFD);

    // Reading $0FE8 flips the left latch to FE.
    mapper.read_chr(0x0FE8);
    REQUIRE(mapper.read_chr(0x0000) == 0xFE);

    // Reading $0FD8 flips back to FD.
    mapper.read_chr(0x0FD8);
    REQUIRE(mapper.read_chr(0x0000) == 0xFD);
}

TEST_CASE("MMC2 CHR latch toggles right half independently", "[mmc2]") {
    std::vector<Byte> chr(128 * 1024, 0);
    chr[3 * 0x1000] = 0x31; // right FD page = 3
    chr[7 * 0x1000] = 0x72; // right FE page = 7

    Mmc2 mapper(
        make_header(8, 16, MirrorMode::Vertical), std::vector<Byte>(128 * 1024, 0), std::move(chr));

    mapper.write_prg(0xC000, 3); // right FD page = 3
    mapper.write_prg(0xE000, 7); // right FE page = 7

    REQUIRE(mapper.read_chr(0x1000) == 0x31); // FD active

    mapper.read_chr(0x1FE8); // flip right to FE
    REQUIRE(mapper.read_chr(0x1000) == 0x72);

    mapper.read_chr(0x1FD8); // flip back to FD
    REQUIRE(mapper.read_chr(0x1000) == 0x31);
}

TEST_CASE("MMC2 battery RAM round-trip", "[mmc2]") {
    Mmc2 mapper(make_header(8, 0, MirrorMode::Vertical), std::vector<Byte>(128 * 1024, 0), {});

    mapper.write_prg(0x7FFF, 0x99);
    auto ram = mapper.battery_ram();
    REQUIRE(ram.size() == 0x2000);
    REQUIRE(ram[0x1FFF] == 0x99);

    std::vector<Byte> restored(0x2000, 0);
    restored[0] = 0x42;
    mapper.set_battery_ram(restored);
    REQUIRE(mapper.read_prg(0x6000) == 0x42);
}

TEST_CASE("MMC2 CHR wraps modulo for a non-power-of-two CHR size", "[mmc2]") {
    // Three 4 KB CHR pages (12 KB): the 5-bit page registers can name pages
    // past the end, which must wrap modulo the ROM size (a power-of-two mask
    // reads the wrong page for non-power-of-two sizes).
    std::vector<Byte> chr(3 * 0x1000, 0);
    chr[0 * 0x1000] = 0xA0;
    chr[1 * 0x1000] = 0xA1;
    chr[2 * 0x1000] = 0xA2;

    Mmc2 mapper(
        make_header(8, 0, MirrorMode::Vertical), std::vector<Byte>(128 * 1024, 0), std::move(chr));

    mapper.write_prg(0xB000, 4); // left FD page 4 -> 4 % 3 = page 1
    REQUIRE(mapper.read_chr(0x0000) == 0xA1);
    mapper.write_prg(0xB000, 5); // page 5 -> 5 % 3 = page 2
    REQUIRE(mapper.read_chr(0x0000) == 0xA2);
    mapper.write_prg(0xB000, 6); // page 6 -> 6 % 3 = page 0
    REQUIRE(mapper.read_chr(0x0000) == 0xA0);
}

TEST_CASE("MMC3 maps a non-power-of-two PRG/CHR without corruption", "[mmc3]") {
    // Three 8 KB PRG banks and three 1 KB CHR banks.
    std::vector<Byte> prg(3 * 0x2000, 0);
    prg[0 * 0x2000] = 0xB0;
    prg[1 * 0x2000] = 0xB1;
    prg[2 * 0x2000] = 0xB2;
    std::vector<Byte> chr(3 * 0x0400, 0);
    chr[0 * 0x0400] = 0xC0;
    chr[1 * 0x0400] = 0xC1;
    chr[2 * 0x0400] = 0xC2;

    Mmc3 mapper(make_header(2, 0, MirrorMode::Vertical), std::move(prg), std::move(chr));

    // R6 = 5 wraps to bank 5 % 3 = 2; fixed windows are (-2) = 1, (-1) = 2.
    mapper.write_prg(0x8000, 0x06);
    mapper.write_prg(0x8001, 0x05);
    REQUIRE(mapper.read_prg(0x8000) == 0xB2);
    REQUIRE(mapper.read_prg(0xC000) == 0xB1);
    REQUIRE(mapper.read_prg(0xE000) == 0xB2);

    // R2 = 4 wraps to 1 KB bank 4 % 3 = 1 at $1000.
    mapper.write_prg(0x8000, 0x02);
    mapper.write_prg(0x8001, 0x04);
    REQUIRE(mapper.read_chr(0x1000) == 0xC1);
}

TEST_CASE("Cartridge rejects MMC3 PRG beyond the board limit", "[mmc3][cartridge]") {
    register_builtin_mappers();
    std::vector<Byte> rom(16 + 128 * 16384 + 8192, 0);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1A;
    rom[4] = 128;
    rom[5] = 1;
    rom[6] = 0x40;

    const auto result = Cartridge::from_data(rom);
    REQUIRE_FALSE(result.has_value());
    REQUIRE(result.error().find("512") != std::string::npos);
}
