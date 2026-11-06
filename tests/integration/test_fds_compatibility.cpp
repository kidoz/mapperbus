#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "../support/rom_archive.hpp"
#include "app/rom_source.hpp"
#include "app/state_storage.hpp"
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

enum class SourceKind { Raw, Zip, SevenZip };
constexpr std::array kSourceKinds = {SourceKind::Raw, SourceKind::Zip, SourceKind::SevenZip};

const char* source_name(SourceKind kind) {
    switch (kind) {
    case SourceKind::Raw:
        return "raw";
    case SourceKind::Zip:
        return "zip";
    case SourceKind::SevenZip:
        return "7z";
    }
    return "unknown";
}

struct MediaSources {
    TemporarySave temporary;
    std::filesystem::path storage = temporary.directory / "storage";
    std::filesystem::path bios = temporary.directory / "bios.bin";
    std::array<std::filesystem::path, 3> paths = {temporary.directory / "game.fds",
                                                  temporary.directory / "game.zip",
                                                  temporary.directory / "renamed.7z"};
    std::array<Sha256Digest, 3> source_hashes{};
    Sha256Digest bios_hash{};
    Sha256Digest disk_hash{};

    explicit MediaSources(const Assets& media) {
        REQUIRE(media.disk.size() % Fds::kSideSize == 0);
        REQUIRE(write_binary_file_atomic(bios.string(), media.bios, "test BIOS"));
        REQUIRE(write_binary_file_atomic(paths[0].string(), media.disk, "test disk"));
        std::vector<Byte> headered(16, 0);
        headered[0] = 'F';
        headered[1] = 'D';
        headered[2] = 'S';
        headered[3] = 0x1A;
        headered[4] = static_cast<Byte>(media.disk.size() / Fds::kSideSize);
        headered.insert(headered.end(), media.disk.begin(), media.disk.end());
        mapperbus::tests::write_archive(paths[1],
                                        {{.name = "readme.txt", .bytes = {}},
                                         {.name = "disks/Headered.FDS", .bytes = headered}});
        mapperbus::tests::write_archive(
            paths[2], {{.name = "nested/Renamed.fds", .bytes = media.disk}}, true);
        for (std::size_t index = 0; index < paths.size(); ++index) {
            auto bytes = read_binary_file_capped(
                paths[index].string(), mapperbus::app::kMaxArchiveBytes, "source fixture");
            REQUIRE(bytes);
            source_hashes[index] = rom_sha256(*bytes);
        }
        bios_hash = rom_sha256(media.bios);
        disk_hash = rom_sha256(media.disk);
    }

    const std::filesystem::path& path(SourceKind kind) const {
        return paths[static_cast<std::size_t>(kind)];
    }

    void open(Emulator& emulator, SourceKind kind) const {
        auto loaded = mapperbus::app::load_rom_source(
            emulator, path(kind).string(), {}, bios.string(), storage);
        INFO(source_name(kind));
        INFO((loaded ? "loaded through shared source service" : loaded.error()));
        REQUIRE(loaded);
        CHECK(*loaded == (kind == SourceKind::Raw   ? ""
                          : kind == SourceKind::Zip ? "disks/Headered.FDS"
                                                    : "nested/Renamed.fds"));
        REQUIRE(emulator.is_fds());
    }

    std::filesystem::path progress_path() const {
        return storage / "fds" / (sha256_hex(disk_hash) + ".fds-save");
    }

    void verify_sources_unchanged() const {
        for (std::size_t index = 0; index < paths.size(); ++index) {
            auto bytes = read_binary_file_capped(
                paths[index].string(), mapperbus::app::kMaxArchiveBytes, "source fixture");
            REQUIRE(bytes);
            CHECK(rom_sha256(*bytes) == source_hashes[index]);
        }
        auto bytes = read_binary_file_capped(bios.string(), 8192, "test BIOS");
        REQUIRE(bytes);
        CHECK(rom_sha256(*bytes) == bios_hash);
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

void snapshot_replay(Emulator& emulator, const MediaSources& sources, SourceKind kind) {
    const auto identity = emulator.content_hash();
    const mapperbus::app::StateStorageSettings settings{.directory =
                                                            sources.temporary.directory / "states"};
    const auto directory = mapperbus::app::state_directory_for_game(
        settings, sources.path(kind), sha256_hex(identity));
    REQUIRE(mapperbus::app::ensure_state_directory(directory));
    const auto file = (directory / "slot1.state").string();
    REQUIRE(emulator.save_state_to_file(file));
    AudioStats stats;
    frames(emulator, 8, stats);
    const auto expected = emulator.save_state();
    const auto pixels = emulator.frame_buffer().pixels;
    REQUIRE(emulator.load_state_from_file(file));
    frames(emulator, 8, stats);
    REQUIRE(emulator.save_state() == expected);
    REQUIRE(emulator.frame_buffer().pixels == pixels);
    for (const auto other : kSourceKinds) {
        Emulator fresh;
        sources.open(fresh, other);
        REQUIRE(fresh.content_hash() == identity);
        REQUIRE(mapperbus::app::state_directory_for_game(
                    settings, sources.path(other), sha256_hex(identity)) == directory);
        REQUIRE(fresh.load_state_from_file(file));
        frames(fresh, 8, stats);
        REQUIRE(fresh.save_state() == expected);
        REQUIRE(fresh.frame_buffer().pixels == pixels);
    }
}

void artifact(const Emulator& emulator, const char* name, SourceKind kind) {
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
        (std::filesystem::path(output) / (std::string(source_name(kind)) + "-" + name)).string(),
        bytes,
        "FDS compatibility frame"));
}
void check_smb2(SourceKind kind) {
    INFO(source_name(kind));
    const auto media = assets("Super Mario Brothers 2 (Japan).fds",
                              "ff8afb9ae6b705b4e51dbcb193dcebadf4c049800a71003d3a45052648e52eda");
    MediaSources sources(media);
    Emulator emulator;
    sources.open(emulator, kind);
    const auto identity = emulator.content_hash();
    emulator.reset();
    AudioStats audio;
    frames(emulator, 200, audio); // snapshot while BIOS transfers are still in progress
    snapshot_replay(emulator, sources, kind);
    frames(emulator, 1000, audio);
    visible_frame(emulator);
    REQUIRE(emulator.cpu().pc() >= 0x6000);
    REQUIRE(emulator.cpu().pc() < 0xE000);
    artifact(emulator, "smb2-title.ppm", kind);
    press(emulator, Button::Start, audio);
    frames(emulator, 300, audio);
    visible_frame(emulator);
    snapshot_replay(emulator, sources, kind);
    artifact(emulator, "smb2-gameplay.ppm", kind);
    REQUIRE(audio.samples > 10000);
    REQUIRE(audio.peak > 0.001f);
    REQUIRE_FALSE(emulator.fds().dirty());
    REQUIRE(emulator.content_hash() == identity);
    REQUIRE_FALSE(std::filesystem::exists(sources.progress_path()));
    sources.verify_sources_unchanged();
}

void check_metroid(SourceKind kind) {
    INFO(source_name(kind));
    const auto media = assets("Metroid (Japan) (Rev 3).fds",
                              "6a4fa984a12e26b394998b17a924aee2033e0313d8c70dcfc6dc48cbbf809299");
    MediaSources sources(media);
    Emulator emulator;
    sources.open(emulator, kind);
    const auto identity = emulator.content_hash();
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
    snapshot_replay(emulator, sources, kind); // includes the temporary-ejection countdown
    frames(emulator, 2100, audio);            // include the Samus arrival animation
    visible_frame(emulator);
    artifact(emulator, "metroid-gameplay.ppm", kind);
    press(emulator, Button::Start, audio); // pause
    emulator.controller().set_button_state(1, Button::Up, true);
    emulator.controller().set_button_state(1, Button::A, true);
    frames(emulator, 5, audio);
    emulator.controller().set_button_state(1, Button::Up, false);
    emulator.controller().set_button_state(1, Button::A, false);
    frames(emulator, 120, audio);
    artifact(emulator, "metroid-save-menu.ppm", kind);
    press(emulator, Button::Select, audio);
    press(emulator, Button::Start, audio); // SAVE
    frames(emulator, 1600, audio);         // wait for the request for side A
    emulator.fds().insert_side(0);
    frames(emulator, 1800, audio);
    visible_frame(emulator);
    artifact(emulator, "metroid-after-save.ppm", kind);
    REQUIRE(emulator.flush_disk_changes());
    const auto saved =
        std::vector<Byte>(emulator.fds().disk_image().begin(), emulator.fds().disk_image().end());
    // Both registration and the subsequent game-over count reached SAVEDATA.
    REQUIRE(saved[51873] != media.disk[51873]);
    REQUIRE(saved[51945] == 1);
    auto persisted = read_binary_file_capped(
        sources.progress_path().string(), Fds::kMaxDiskImageBytes + 72, "disk progress");
    REQUIRE(persisted);
    REQUIRE(persisted->size() == saved.size() + 72);
    REQUIRE(std::equal(saved.begin(), saved.end(), persisted->begin() + 72));
    REQUIRE(emulator.content_hash() == identity);
    for (const auto other : kSourceKinds) {
        Emulator copy;
        sources.open(copy, other);
        REQUIRE(copy.content_hash() == identity);
        REQUIRE(std::ranges::equal(copy.fds().disk_image(), saved));
    }
    Emulator fresh;
    const auto other = kind == SourceKind::Raw   ? SourceKind::Zip
                       : kind == SourceKind::Zip ? SourceKind::SevenZip
                                                 : SourceKind::Raw;
    sources.open(fresh, other);
    REQUIRE(std::ranges::equal(fresh.fds().disk_image(), saved));
    fresh.reset();
    frames(fresh, 1500, audio);
    press(fresh, Button::Start, audio);
    frames(fresh, 120, audio);
    visible_frame(fresh);
    artifact(fresh, "metroid-reloaded-file.ppm", kind);
    REQUIRE(audio.peak > 0.001f);
    sources.verify_sources_unchanged();
}

} // namespace

TEST_CASE("Local SMB2 FDS boots, enters gameplay, and replays snapshots",
          "[integration][fds][local][raw]") {
    check_smb2(SourceKind::Raw);
}

TEST_CASE("Local Metroid FDS saves progress after side changes and reloads it",
          "[integration][fds][local][raw]") {
    check_metroid(SourceKind::Raw);
}

TEST_CASE("Archived ZIP SMB2 FDS enters gameplay and shares raw snapshots",
          "[integration][fds][local][archive][zip]") {
    check_smb2(SourceKind::Zip);
}

TEST_CASE("Archived 7z SMB2 FDS enters gameplay and shares raw snapshots",
          "[integration][fds][local][archive][7z]") {
    check_smb2(SourceKind::SevenZip);
}

TEST_CASE("Archived ZIP Metroid FDS saves and reloads progress across sources",
          "[integration][fds][local][archive][zip]") {
    check_metroid(SourceKind::Zip);
}

TEST_CASE("Archived 7z Metroid FDS saves and reloads progress across sources",
          "[integration][fds][local][archive][7z]") {
    check_metroid(SourceKind::SevenZip);
}
