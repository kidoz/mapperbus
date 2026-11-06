#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/binary_file.hpp"
#include "core/emulator.hpp"

using namespace mapperbus::core;

namespace {
void load_diagnostic(Emulator& emulator, const char* filename, const char* digest) {
    const auto* directory = std::getenv("MAPPERBUS_FDS_DIAGNOSTICS");
    if (directory == nullptr || *directory == '\0') {
        SKIP("Fetch public diagnostics and set MAPPERBUS_FDS_DIAGNOSTICS to run this case");
    }
    const auto* bios_path = std::getenv("MAPPERBUS_FDS_TEST_BIOS");
    REQUIRE(bios_path != nullptr);
    auto bios = read_binary_file_capped(bios_path, 8192, "FDS BIOS");
    INFO((bios ? "BIOS read" : bios.error()));
    REQUIRE(bios);
    auto disk = read_binary_file_capped(
        (std::filesystem::path(directory) / filename).string(), 65516, "FDS diagnostic");
    INFO((disk ? filename : disk.error()));
    REQUIRE(disk);
    REQUIRE(sha256_hex(rom_sha256(*disk)) == digest);
    auto loaded = emulator.load_fds(*bios, *disk);
    INFO((loaded ? "loaded" : loaded.error()));
    REQUIRE(loaded);
    emulator.reset();
}

float frames(Emulator& emulator, int count) {
    float expansion_peak = 0;
    std::array<float, 4096> samples{};
    for (int frame = 0; frame < count; ++frame) {
        emulator.step_frame();
        REQUIRE_FALSE(emulator.cpu().jammed());
        const float expansion = emulator.fds().audio_output();
        REQUIRE(std::isfinite(expansion));
        expansion_peak = std::max(expansion_peak, std::abs(expansion));
        while (emulator.drain_audio(samples.data(), samples.size()) != 0) {
        }
    }
    return expansion_peak;
}

void press(Emulator& emulator, Button button) {
    emulator.controller().set_button_state(0, button, true);
    frames(emulator, 5);
    emulator.controller().set_button_state(0, button, false);
    frames(emulator, 5);
}

void artifact(const Emulator& emulator, const char* name) {
    const auto* output = std::getenv("MAPPERBUS_FDS_TEST_OUTPUT");
    if (output == nullptr || *output == '\0')
        return;
    std::filesystem::create_directories(output);
    const std::string header = "P6\n256 240\n255\n";
    std::vector<Byte> bytes(header.begin(), header.end());
    for (auto pixel : emulator.frame_buffer().pixels) {
        bytes.push_back(static_cast<Byte>(pixel >> 16U));
        bytes.push_back(static_cast<Byte>(pixel >> 8U));
        bytes.push_back(static_cast<Byte>(pixel));
    }
    REQUIRE(write_binary_file_atomic(
        (std::filesystem::path(output) / name).string(), bytes, "FDS diagnostic frame"));
}
} // namespace

TEST_CASE("Public Sour FDS IRQ v7 reports success for all 21 cases",
          "[integration][fds][diagnostic]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "fdsirqtests-v7.fds",
                    "38c3ed71d73c71bd09c54e687173bcc2ceea9aa78c78a2813f6c56106814e86b");
    frames(emulator, 600);
    // The ROM increments $0303 per case. Its own comparison writes 'X' on
    // failure and 'O' on success into $0404 + case, independently of rendering.
    REQUIRE(emulator.read_cpu(0x0303) == 21);
    for (int test = 1; test <= 21; ++test) {
        INFO("Sour IRQ case " << test);
        REQUIRE(emulator.read_cpu(static_cast<Address>(0x0404 + test)) == 'O');
    }
    REQUIRE(emulator.cpu().pc() == 0x6093); // completed, displaying results
    artifact(emulator, "sour-irq-v7.ppm");
}

TEST_CASE("Public FDS mirroring diagnostic passes all three checks",
          "[integration][fds][diagnostic]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "mirroring-test.fds",
                    "501bda77be6190bfb57d07a707880e74029e3e0f910441f6f7ba88431e70c2e3");
    frames(emulator, 600);
    // Symbols from the upstream zero-page allocation: Mode and failure counts.
    REQUIRE(emulator.read_cpu(0x15) == 1);
    REQUIRE(emulator.read_cpu(0x16) == 0); // NametableFails
    REQUIRE(emulator.read_cpu(0x17) == 0); // ReadFails
    REQUIRE(emulator.read_cpu(0x18) == 0); // IOToggleFails
    artifact(emulator, "mirroring.ppm");
}

TEST_CASE("Public FDS I/O viewer observes disk removal with I/O writes disabled",
          "[integration][fds][diagnostic]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "4023-test.fds",
                    "958dfad1656aaf1c3c82b162b68acada253ff59ed01726ff79430222fa49df3c");
    frames(emulator, 600);
    REQUIRE(emulator.read_cpu(0x14) == 0x83); // upstream FDS_IO_ENABLE_MIRROR
    press(emulator, Button::A);
    REQUIRE(emulator.read_cpu(0x14) == 0x82);
    emulator.fds().eject();
    frames(emulator, 4);
    // Upstream DumpRegisters writes $4032 to nametable row 13, column 10.
    constexpr auto kDriveStatusTile = 13 * 32 + 10;
    REQUIRE((emulator.ppu().vram()[kDriveStatusTile] & 5U) == 5);
    emulator.fds().insert_side(0);
    frames(emulator, 4);
    REQUIRE((emulator.ppu().vram()[kDriveStatusTile] & 1U) == 0);
    press(emulator, Button::A);
    REQUIRE(emulator.read_cpu(0x14) == 0x83);
    artifact(emulator, "io-reset.ppm");
}

TEST_CASE("Public FDS audio viewer plays, resets, and reads the modulation counter",
          "[integration][fds][diagnostic]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "audio-registers.fds",
                    "80ccda5443653426f27e6b4b3125c9834e97931c50b9fcdccc00b8367ef9fe28");
    frames(emulator, 600);
    REQUIRE(emulator.read_cpu(0x14) == 0x83);
    REQUIRE((emulator.fds().read(0x4097) & 0x7FU) == 0);
    // Upstream DoNothing copies $4090-$4097 into eight raw hex tiles at
    // nametable row 12, column 12 using the BIOS PrepareVRAMString routine.
    // Inspect the program's CPU-driven reads, not just direct host reads.
    constexpr auto kRegisterTiles = 12 * 32 + 12;
    const auto& tiles = emulator.ppu().vram();
    REQUIRE(tiles[kRegisterTiles + 2] == 0x7F);         // mod gain 63 + open-bus bit 6
    REQUIRE(tiles[kRegisterTiles + 3] == 0);            // halted mod accumulator
    REQUIRE(tiles[kRegisterTiles + 4] == 0);            // zero counter * gain
    REQUIRE((tiles[kRegisterTiles + 5] & 0x0FU) == 12); // reset entry's increment code
    REQUIRE(tiles[kRegisterTiles + 6] == 0x40);         // muted PWM output
    press(emulator, Button::Up);
    REQUIRE((emulator.fds().read(0x4097) & 0x7FU) == 0x7F);
    REQUIRE(tiles[kRegisterTiles + 4] == 0xFC); // settled (-1 * 63) >> 4
    press(emulator, Button::Up);
    REQUIRE((emulator.fds().read(0x4097) & 0x7FU) == 0x7E);
    REQUIRE(tiles[kRegisterTiles + 4] == 0xF8); // settled (-2 * 63) >> 4
    press(emulator, Button::A);
    REQUIRE(frames(emulator, 20) > 0.001f);
    bool saw_wave_phase = false, saw_mod_phase = false;
    for (int frame = 0; frame < 32; ++frame) {
        frames(emulator, 1);
        saw_wave_phase |= tiles[kRegisterTiles + 1] != 0;
        saw_mod_phase |= tiles[kRegisterTiles + 3] != 0;
    }
    REQUIRE(saw_wave_phase);
    REQUIRE(saw_mod_phase);
    const auto playing = emulator.save_state();
    frames(emulator, 8);
    const auto replay = emulator.save_state();
    REQUIRE(emulator.load_state(playing));
    frames(emulator, 8);
    REQUIRE(emulator.save_state() == replay);
    artifact(emulator, "audio-playing.ppm");
    press(emulator, Button::B);
    REQUIRE(emulator.read_cpu(0x14) == 0x81);
    REQUIRE((emulator.fds().read(0x4090) & 0x3FU) == 0);
    REQUIRE(frames(emulator, 10) == 0.0f);
    artifact(emulator, "audio-reset.ppm");
    press(emulator, Button::B);
    press(emulator, Button::A);
    REQUIRE(frames(emulator, 20) > 0.001f);
}

TEST_CASE("Public FDS watchdog diagnostic captures two NOP-slide IRQ addresses",
          "[integration][fds][diagnostic][watchdog]") {
    Emulator emulator;
    load_diagnostic(emulator,
                    "4030d1-addr.fds",
                    "ed9f62f8c85c3d94909359a55ff94a53427473378a2eff556f4b465f268fe0c5");
    frames(emulator, 600);
    REQUIRE(emulator.read_cpu(0x10) == 2); // upstream IRQ_Count
    // Despite the upstream array names, the stack extraction stores the
    // high bytes at $11/$12 and low bytes at $13/$14.
    const auto first = static_cast<Address>((static_cast<uint32_t>(emulator.read_cpu(0x11)) << 8U) |
                                            emulator.read_cpu(0x13));
    const auto second = static_cast<Address>(
        (static_cast<uint32_t>(emulator.read_cpu(0x12)) << 8U) | emulator.read_cpu(0x14));
    INFO("IRQ addresses " << first << " / " << second);
    artifact(emulator, "watchdog-addresses.ppm");
    // Upstream's published Twin Famicom capture shows $67FF / $6E25.
    CHECK(first == 0x67FF);
    CHECK(second == 0x6E25);
}
