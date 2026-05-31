#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/nrom.hpp"

using namespace mapperbus::core;

namespace {

INesHeader nrom_header(bool battery) {
    return INesHeader{
        .prg_rom_banks = 1,
        .chr_rom_banks = 1,
        .mapper_number = 0,
        .submapper = 0,
        .mirror_mode = MirrorMode::Horizontal,
        .region = Region::NTSC,
        .has_battery = battery,
        .has_trainer = false,
        .is_nes2 = false,
    };
}

[[nodiscard]] std::uint64_t hash_frame(const FrameBuffer& fb) {
    std::uint64_t h = 1469598103934665603ULL;
    for (std::uint32_t pixel : fb.pixels) {
        h = (h ^ pixel) * 1099511628211ULL;
    }
    return h;
}

// Writes an NROM ROM whose program keeps an incrementing counter in PRG-RAM
// ($6000) and mirrors it to the backdrop palette every frame. The visible
// frame colour therefore depends only on the persisted PRG-RAM byte, which
// lets a battery save be observed end-to-end through the framebuffer.
[[nodiscard]] std::filesystem::path write_counter_rom(std::string_view name, bool battery = true) {
    constexpr std::size_t kPrgSize = 16 * 1024;
    constexpr std::size_t kChrSize = 8 * 1024;

    const std::array<std::uint8_t, 0x30> program = {
        0x78, // SEI
        0xD8, // CLD
        0xA2,
        0xFF, // LDX #$FF
        0x9A, // TXS
        // loop ($8005):
        0x2C,
        0x02,
        0x20, // BIT $2002
        0x10,
        0xFB, // BPL loop  (wait for vblank)
        0xEE,
        0x00,
        0x60, // INC $6000
        0xA9,
        0x3F, // LDA #$3F
        0x8D,
        0x06,
        0x20, // STA $2006
        0xA9,
        0x00, // LDA #$00
        0x8D,
        0x06,
        0x20, // STA $2006
        0xAD,
        0x00,
        0x60, // LDA $6000
        0x8D,
        0x07,
        0x20, // STA $2007  (backdrop palette = counter)
        0xA9,
        0x00, // LDA #$00
        0x8D,
        0x05,
        0x20, // STA $2005
        0x8D,
        0x05,
        0x20, // STA $2005
        0x8D,
        0x00,
        0x20, // STA $2000
        0xA9,
        0x08, // LDA #$08
        0x8D,
        0x01,
        0x20, // STA $2001  (enable background)
        0x4C,
        0x05,
        0x80, // JMP $8005
    };

    std::vector<std::uint8_t> prg(kPrgSize, 0xEA);
    std::copy(program.begin(), program.end(), prg.begin());
    const std::size_t vec = kPrgSize - 6;
    prg[vec + 0] = 0x00;
    prg[vec + 1] = 0x80; // RESET = $8000
    prg[vec + 2] = 0x00;
    prg[vec + 3] = 0x80; // NMI
    prg[vec + 4] = 0x00;
    prg[vec + 5] = 0x80; // IRQ

    std::array<std::uint8_t, 16> header = {'N', 'E', 'S', 0x1A, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    if (battery) {
        header[6] |= 0x02; // battery flag
    }

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / ("mapperbus-" + std::string(name) + ".nes");
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header.data()), header.size());
    file.write(reinterpret_cast<const char*>(prg.data()), static_cast<std::streamsize>(prg.size()));
    const std::vector<std::uint8_t> chr(kChrSize, 0);
    file.write(reinterpret_cast<const char*>(chr.data()), static_cast<std::streamsize>(chr.size()));
    return path;
}

} // namespace

TEST_CASE("Mapper battery RAM round-trips PRG-RAM", "[battery]") {
    Nrom mapper(nrom_header(true), std::vector<Byte>(16384, 0), std::vector<Byte>(8192, 0));
    mapper.write_prg(0x6000, 0x42);
    mapper.write_prg(0x6001, 0x99);

    const std::span<const Byte> ram = mapper.battery_ram();
    REQUIRE(ram.size() == 0x2000);
    REQUIRE(ram[0] == 0x42);
    REQUIRE(ram[1] == 0x99);

    std::vector<Byte> restored(0x2000, 0);
    restored[0] = 0x7F;
    mapper.set_battery_ram(restored);
    REQUIRE(mapper.read_prg(0x6000) == 0x7F);
    REQUIRE(mapper.read_prg(0x6001) == 0x00);
}

TEST_CASE("Non-battery cartridge writes no .sav", "[battery]") {
    register_builtin_mappers();
    const std::filesystem::path rom = write_counter_rom("battery-none", /*battery=*/false);
    const std::filesystem::path sav = std::filesystem::path(rom).replace_extension(".sav");
    std::filesystem::remove(sav);

    Emulator emu;
    REQUIRE(emu.load_cartridge(rom.string()).has_value());
    REQUIRE_FALSE(emu.has_battery());
    emu.reset();
    for (int i = 0; i < 10; ++i) {
        emu.step_frame();
    }
    emu.unload_cartridge();

    // A non-battery board must not leave a save file behind.
    REQUIRE_FALSE(std::filesystem::exists(sav));
}

TEST_CASE("Battery PRG-RAM persists across reload via .sav", "[battery][integration]") {
    register_builtin_mappers();
    const std::filesystem::path rom = write_counter_rom("battery-persist");
    const std::filesystem::path sav = std::filesystem::path(rom).replace_extension(".sav");
    std::filesystem::remove(sav); // start clean

    // First session: build up a counter in PRG-RAM, then unload (auto-saves).
    {
        Emulator emu;
        REQUIRE(emu.load_cartridge(rom.string()).has_value());
        REQUIRE(emu.has_battery());
        emu.reset();
        for (int i = 0; i < 40; ++i) {
            emu.step_frame();
        }
        emu.unload_cartridge();
    }
    REQUIRE(std::filesystem::exists(sav));
    REQUIRE(std::filesystem::file_size(sav) == 0x2000);

    // Reference: a continuous run of 52 frames.
    Emulator reference;
    REQUIRE(reference.load_cartridge(rom.string()).has_value());
    // Loading the cart auto-restored the .sav from the first session, so reset
    // and continue: the counter resumes from frame 40.
    reference.reset();
    for (int i = 0; i < 12; ++i) {
        reference.step_frame();
    }
    const std::uint64_t with_battery = hash_frame(reference.frame_buffer());

    // Negative control: same ROM, but with no battery save present.
    std::filesystem::remove(sav);
    Emulator fresh;
    REQUIRE(fresh.load_cartridge(rom.string()).has_value());
    fresh.reset();
    for (int i = 0; i < 12; ++i) {
        fresh.step_frame();
    }
    const std::uint64_t no_battery = hash_frame(fresh.frame_buffer());

    // The persisted counter (resumed at 40) must differ from a fresh counter.
    REQUIRE(with_battery != no_battery);

    std::filesystem::remove(sav);
}
