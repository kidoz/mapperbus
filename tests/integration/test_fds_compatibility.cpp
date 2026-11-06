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
// Opt-in licensed fixtures. Nothing is downloaded, and writes use temporary saves.
struct Assets {
    std::vector<Byte> bios;
    std::vector<Byte> disk;
};

Assets assets(const char* filename, const char* expected_hash) {
    const auto* root = std::getenv("MAPPERBUS_FDS_TEST_ASSETS");
    if (root == nullptr || *root == '\0') {
        SKIP("Set MAPPERBUS_FDS_TEST_ASSETS to a directory containing your BIOS and FDS images");
    }
    const auto read = [&](const char* name, std::size_t cap) {
        const auto path = std::filesystem::path(root) / name;
        auto bytes = read_binary_file_capped(path.string(), cap, "FDS compatibility fixture");
        INFO(path.string());
        INFO((bytes ? "read" : bytes.error()));
        REQUIRE(bytes);
        return std::move(*bytes);
    };
    Assets result{.bios = read("[BIOS] Nintendo Famicom Disk System (Japan).bin", 8192),
                  .disk = read(filename, Fds::kMaxDiskImageBytes + 16)};
    REQUIRE(sha256_hex(rom_sha256(result.bios)) ==
            "99c18490ed9002d9c6d999b9d8d15be5c051bdfa7cc7e73318053c9a994b0178");
    REQUIRE(sha256_hex(rom_sha256(result.disk)) == expected_hash);
    return result;
}

struct TemporarySave {
    std::filesystem::path directory;
    TemporarySave() {
        // Atomic directory creation also keeps concurrent test processes isolated.
        auto base = std::filesystem::temp_directory_path();
        for (int index = 0;; ++index) {
            directory = base / ("mapperbus-fds-compat-" + std::to_string(index));
            std::error_code error;
            if (std::filesystem::create_directory(directory, error))
                break;
            REQUIRE_FALSE(error);
        }
    }
    ~TemporarySave() {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};

struct AudioStats {
    float peak = 0;
    std::size_t samples = 0;
};

void frames(Emulator& emulator, int count, AudioStats& stats) {
    std::array<float, 4096> samples{};
    for (int frame = 0; frame < count; ++frame) {
        emulator.step_frame();
        REQUIRE_FALSE(emulator.cpu().jammed());
        REQUIRE(std::isfinite(emulator.fds().audio_output()));
        for (auto size = emulator.drain_audio(samples.data(), samples.size()); size != 0;
             size = emulator.drain_audio(samples.data(), samples.size())) {
            REQUIRE(std::all_of(samples.begin(),
                                samples.begin() + static_cast<std::ptrdiff_t>(size),
                                [](float value) { return std::isfinite(value); }));
            stats.samples += size;
            for (std::size_t index = 0; index < size; ++index) {
                stats.peak = std::max(stats.peak, std::abs(samples[index]));
            }
        }
    }
}

void press(Emulator& emulator, Button button, AudioStats& stats, int port = 0) {
    emulator.controller().set_button_state(port, button, true);
    frames(emulator, 5, stats);
    emulator.controller().set_button_state(port, button, false);
    frames(emulator, 5, stats);
}

void visible_frame(const Emulator& emulator) {
    const auto& pixels = emulator.frame_buffer().pixels;
    REQUIRE(std::count_if(pixels.begin(), pixels.end(), [](auto pixel) {
                return pixel != 0xFF000000;
            }) > 500);
}

void snapshot_replay(Emulator& emulator) {
    const auto state = emulator.save_state();
    AudioStats stats;
    frames(emulator, 8, stats);
    const auto expected = emulator.save_state();
    const auto pixels = emulator.frame_buffer().pixels;
    REQUIRE(emulator.load_state(state));
    frames(emulator, 8, stats);
    REQUIRE(emulator.save_state() == expected);
    REQUIRE(emulator.frame_buffer().pixels == pixels);
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
        (std::filesystem::path(output) / name).string(), bytes, "FDS compatibility frame"));
}
} // namespace

TEST_CASE("Local SMB2 FDS boots, enters gameplay, and replays snapshots",
          "[integration][fds][local]") {
    const auto media = assets("Super Mario Brothers 2 (Japan).fds",
                              "ff8afb9ae6b705b4e51dbcb193dcebadf4c049800a71003d3a45052648e52eda");
    Emulator emulator;
    REQUIRE(emulator.load_fds(media.bios, media.disk));
    emulator.reset();
    AudioStats audio;
    frames(emulator, 200, audio); // snapshot while BIOS transfers are still in progress
    snapshot_replay(emulator);
    frames(emulator, 1000, audio);
    visible_frame(emulator);
    REQUIRE(emulator.cpu().pc() >= 0x6000);
    REQUIRE(emulator.cpu().pc() < 0xE000);
    artifact(emulator, "smb2-title.ppm");
    press(emulator, Button::Start, audio);
    frames(emulator, 300, audio);
    visible_frame(emulator);
    snapshot_replay(emulator);
    artifact(emulator, "smb2-gameplay.ppm");
    REQUIRE(audio.samples > 10000);
    REQUIRE(audio.peak > 0.001f);
    REQUIRE_FALSE(emulator.fds().dirty());
}

TEST_CASE("Local Metroid FDS saves progress after side changes and reloads it",
          "[integration][fds][local]") {
    const auto media = assets("Metroid (Japan) (Rev 3).fds",
                              "6a4fa984a12e26b394998b17a924aee2033e0313d8c70dcfc6dc48cbbf809299");
    TemporarySave temporary;
    const auto save = (temporary.directory / "progress.fds-save").string();
    Emulator emulator;
    REQUIRE(emulator.load_fds(media.bios, media.disk, save));
    emulator.reset();
    AudioStats audio;
    frames(emulator, 1500, audio);
    visible_frame(emulator);
    press(emulator, Button::Start, audio);
    frames(emulator, 100, audio);
    press(emulator, Button::Start, audio); // register a file
    frames(emulator, 60, audio);
    press(emulator, Button::A, audio);
    for (int index = 0; index < 3; ++index)
        press(emulator, Button::Select, audio);
    press(emulator, Button::Start, audio); // finish registration
    frames(emulator, 60, audio);
    press(emulator, Button::Start, audio);
    frames(emulator, 1600, audio); // BIOS writes the file and requests side B
    emulator.fds().insert_side(1);
    snapshot_replay(emulator);     // includes the temporary-ejection countdown
    frames(emulator, 2100, audio); // include the Samus arrival animation
    visible_frame(emulator);
    artifact(emulator, "metroid-gameplay.ppm");
    press(emulator, Button::Start, audio); // pause
    emulator.controller().set_button_state(1, Button::Up, true);
    emulator.controller().set_button_state(1, Button::A, true);
    frames(emulator, 5, audio);
    emulator.controller().set_button_state(1, Button::Up, false);
    emulator.controller().set_button_state(1, Button::A, false);
    frames(emulator, 120, audio);
    artifact(emulator, "metroid-save-menu.ppm");
    press(emulator, Button::Select, audio);
    press(emulator, Button::Start, audio); // SAVE
    frames(emulator, 1600, audio);         // wait for the request for side A
    emulator.fds().insert_side(0);
    frames(emulator, 1800, audio);
    visible_frame(emulator);
    artifact(emulator, "metroid-after-save.ppm");
    REQUIRE(emulator.flush_disk_changes());
    const auto saved =
        std::vector<Byte>(emulator.fds().disk_image().begin(), emulator.fds().disk_image().end());
    // Both registration and the subsequent game-over count reached SAVEDATA.
    REQUIRE(saved[51873] != media.disk[51873]);
    REQUIRE(saved[51945] == 1);
    Emulator fresh;
    REQUIRE(fresh.load_fds(media.bios, media.disk, save));
    REQUIRE(std::ranges::equal(fresh.fds().disk_image(), saved));
    fresh.reset();
    frames(fresh, 1500, audio);
    press(fresh, Button::Start, audio);
    frames(fresh, 120, audio);
    visible_frame(fresh);
    artifact(fresh, "metroid-reloaded-file.ppm");
    REQUIRE(audio.peak > 0.001f);
}
