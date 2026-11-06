#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>

#include "../support/test_rom.hpp"
#include "app/configuration.hpp"
#include "app/emulation_session.hpp"
#include "app/state_storage.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"
#include "platform/audio/null_audio.hpp"
#include "platform/input/null_input.hpp"
#include "platform/video/null_video.hpp"

namespace mapperbus::app {
namespace {
struct StateFixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-storage-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path rom = root / "library" / "Game.nes";
    StateStorageSettings settings{StateStorageMode::UserData, root / "user-data" / "states"};
    EmulationSession session{std::make_unique<platform::NullVideo>(),
                             std::make_unique<platform::NullAudio>(),
                             std::make_unique<platform::NullInput>()};

    StateFixture() {
        core::register_builtin_mappers();
        std::filesystem::create_directories(rom.parent_path());
        const auto source = tests::write_visible_nrom_test_rom("storage-source");
        std::filesystem::copy_file(source, rom);
        std::filesystem::remove(source);
        session.set_state_storage(settings);
    }
    ~StateFixture() {
        std::error_code error;
        std::filesystem::permissions(rom.parent_path(),
                                     std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::add,
                                     error);
        std::filesystem::remove_all(root, error);
    }
    void write(const std::filesystem::path& path, const std::vector<core::Byte>& data) {
        std::ofstream file(path, std::ios::binary);
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
        REQUIRE(file.good());
    }
};

TEST_CASE("Central states survive ROM moves and selected slots follow content",
          "[app][state][storage]") {
    StateFixture fixture;
    auto& session = fixture.session;
    REQUIRE(session.open_rom(fixture.rom.string()));
    const auto identity = session.game_identity();
    MapperBusConfiguration config;
    remember_state_slot(config, identity, 9);
    REQUIRE(session.save_state(9));
    const auto path = session.state_path_for_slot(9);
    REQUIRE(std::filesystem::path(path).filename() == "slot-9.state");
    REQUIRE(
        std::filesystem::path(path).parent_path().filename().string().ends_with("--" + identity));
    const auto snapshot = session.emulator().save_state();
    session.close_rom();
    const auto renamed = fixture.root / "moved" / "Renamed.nes";
    std::filesystem::create_directories(renamed.parent_path());
    std::filesystem::rename(fixture.rom, renamed);
    REQUIRE(session.open_rom(renamed.string()));
    REQUIRE(session.game_identity() == identity);
    REQUIRE(selected_state_slot(config, session.game_identity()) == 9);
    REQUIRE(session.state_path_for_slot(9) == path);
    REQUIRE(session.load_state(9));
    REQUIRE(session.emulator().save_state() == snapshot);
}

TEST_CASE("Same filenames and mapper numbers do not share states for different content",
          "[app][state][storage]") {
    StateFixture fixture;
    REQUIRE(fixture.session.open_rom(fixture.rom.string()));
    const auto first_identity = fixture.session.game_identity();
    REQUIRE(fixture.session.save_state());
    const auto first_path = fixture.session.state_path_for_slot();
    const auto other = fixture.root / "other" / "Game.nes";
    std::filesystem::create_directories(other.parent_path());
    std::filesystem::copy_file(fixture.rom, other);
    {
        std::fstream file(other, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(-1, std::ios::end);
        file.put('x');
    }
    REQUIRE(fixture.session.open_rom(other.string()));
    REQUIRE(fixture.session.game_identity() != first_identity);
    REQUIRE(fixture.session.state_path_for_slot() != first_path);
    REQUIRE_FALSE(fixture.session.load_state());
}

TEST_CASE("States save outside a read-only ROM library", "[app][state][storage]") {
    StateFixture fixture;
    std::filesystem::permissions(fixture.rom.parent_path(),
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_exec);
    REQUIRE(fixture.session.open_rom(fixture.rom.string()));
    REQUIRE(fixture.session.save_state());
    REQUIRE(std::filesystem::exists(fixture.session.state_path_for_slot()));
    REQUIRE_FALSE(std::filesystem::exists(legacy_state_path(fixture.rom, 0)));
}

TEST_CASE("Legacy sibling import is read-only and never masks a corrupt centralized state",
          "[app][state][storage]") {
    StateFixture fixture;
    auto& session = fixture.session;
    REQUIRE(session.open_rom(fixture.rom.string()));
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    const auto snapshot = session.emulator().save_state();
    auto legacy = snapshot;
    core::StateWriter prefix;
    session.emulator().cpu().save_state(prefix);
    session.emulator().ppu().save_state(prefix);
    core::StateWriter apu_state;
    core::Apu{}.save_state(apu_state);
    const auto dma_end = 43 + prefix.data().size() + apu_state.data().size();
    legacy.erase(legacy.begin() + static_cast<std::ptrdiff_t>(dma_end - 11),
                 legacy.begin() + static_cast<std::ptrdiff_t>(dma_end));
    legacy.erase(legacy.end() - 16384 - 27, legacy.end() - 16384);
    legacy.erase(legacy.begin() + 11, legacy.begin() + 43);
    const std::uint32_t version = 3;
    std::memcpy(legacy.data() + 4, &version, sizeof(version));
    const auto sibling = legacy_state_path(fixture.rom, 2);
    fixture.write(sibling, legacy);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    REQUIRE(session.load_state(2));
    REQUIRE(session.emulator().save_state() == snapshot);
    REQUIRE_FALSE(std::filesystem::exists(session.state_path_for_slot(2)));
    REQUIRE(std::filesystem::file_size(sibling) == legacy.size());
    REQUIRE(session.save_state(2));
    const auto primary = session.state_path_for_slot(2);
    REQUIRE(*session.state_load_path_for_slot(2) == primary);
    fixture.write(primary, std::vector<core::Byte>{0, 1, 2});
    const auto before = session.emulator().save_state();
    REQUIRE_FALSE(session.load_state(2));
    REQUIRE(session.emulator().save_state() == before);
    REQUIRE(std::filesystem::exists(sibling));
}

TEST_CASE("Portable storage and custom roots preserve content isolation", "[app][state][storage]") {
    StateFixture fixture;
    fixture.session.set_state_storage({StateStorageMode::Portable, {}});
    REQUIRE(fixture.session.open_rom(fixture.rom.string()));
    REQUIRE(fixture.session.state_directory().parent_path() ==
            fixture.rom.parent_path() / "mapperbus-states");
    REQUIRE(fixture.session.save_state());
    const auto portable = fixture.session.state_path_for_slot();
    fixture.session.set_state_storage(fixture.settings);
    REQUIRE(fixture.session.save_state());
    REQUIRE(fixture.session.state_path_for_slot() != portable);
    REQUIRE(std::filesystem::exists(portable));
    const auto blocker = fixture.root / "file-instead-of-directory";
    {
        std::ofstream file(blocker);
        file << "keep";
    }
    fixture.session.set_state_storage({StateStorageMode::UserData, blocker});
    REQUIRE_FALSE(fixture.session.save_state());
    REQUIRE(std::filesystem::file_size(blocker) == 4);
}

#if defined(__linux__)
TEST_CASE("Linux save data uses XDG_DATA_HOME and ignores relative overrides",
          "[app][state][storage]") {
    struct Restore {
        std::optional<std::string> old;
        Restore() {
            if (const auto* value = std::getenv("XDG_DATA_HOME"))
                old = value;
        }
        ~Restore() {
            if (old)
                (void)setenv("XDG_DATA_HOME", old->c_str(), 1);
            else
                (void)unsetenv("XDG_DATA_HOME");
        }
    } restore;
    REQUIRE(unsetenv("XDG_DATA_HOME") == 0);
    const auto fallback = mapperbus_data_directory();
    REQUIRE(fallback.filename() == "mapperbus");
    REQUIRE(setenv("XDG_DATA_HOME", "relative", 1) == 0);
    REQUIRE(mapperbus_data_directory() == fallback);
    REQUIRE(setenv("XDG_DATA_HOME", "/tmp/mapperbus-data", 1) == 0);
    REQUIRE(mapperbus_data_directory() == "/tmp/mapperbus-data/mapperbus");
}
#endif
} // namespace
} // namespace mapperbus::app
