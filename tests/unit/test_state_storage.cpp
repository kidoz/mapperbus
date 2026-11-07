#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>

#include "../support/fds_image.hpp"
#include "../support/legacy_state.hpp"
#include "../support/test_rom.hpp"
#include "app/async_state_io.hpp"
#include "app/configuration.hpp"
#include "app/emulation_session.hpp"
#include "app/state_storage.hpp"
#include "core/binary_file.hpp"
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
    auto snapshot = session.emulator().save_state();
    core::StateWriter cpu, ppu;
    session.emulator().cpu().save_state(cpu);
    session.emulator().ppu().save_state(ppu);
    // Legacy files cannot recover the PPU I/O latch introduced in version 13.
    snapshot[43 + cpu.data().size() + ppu.data().size() - 1] = 0;
    auto legacy = mapperbus::tests::legacy_state(session.emulator(), 7);
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

namespace {
void change_fds_payload(core::Fds& drive, core::Byte value) {
    const auto next_byte = [&drive] {
        for (int cycle = 0; cycle < 700000; ++cycle) {
            drive.step(1);
            if ((drive.read(0x4030) & 0x80) != 0)
                return drive.read(0x4031);
        }
        FAIL("Timed out waiting for disk transfer");
        return core::Byte{};
    };
    drive.insert_side(0);
    drive.write(0x4023, 1);
    drive.write(0x4025, 0x65);
    for (const int size : {56, 2, 16}) {
        for (int index = 0; index < size + 2; ++index)
            (void)next_byte();
        drive.write(0x4025, 0x25);
        drive.write(0x4025, 0x65);
    }
    REQUIRE(next_byte() == 4);
    drive.write(0x4025, 0x61);
    drive.write(0x4024, value);
    (void)next_byte();
    drive.write(0x4025, 0x26);
}

core::Result<LoadedState> wait_for_state(AsyncStateIO& io) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = io.poll())
            return std::move(*result);
        std::this_thread::yield();
    }
    return std::unexpected("Timed out waiting for state operation");
}
struct StateIOGate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    bool finished = false;
    void block() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [this] { return released; });
    }
    bool wait(bool StateIOGate::* flag) {
        std::unique_lock lock(mutex);
        return changed.wait_for(
            lock, std::chrono::seconds(5), [this, flag] { return this->*flag; });
    }
    void release() {
        const std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
    void finish() {
        const std::lock_guard lock(mutex);
        finished = true;
        changed.notify_all();
    }
};
struct ReleaseStateIO {
    std::shared_ptr<StateIOGate> gate;
    ~ReleaseStateIO() {
        gate->release();
    }
};
} // namespace

TEST_CASE("Async state save and load preserve pause state and serialized controller hardware",
          "[app][state][async]") {
    StateFixture fixture;
    auto& session = fixture.session;
    REQUIRE(session.open_rom(fixture.rom.string()));
    session.set_paused(true);
    session.emulator().set_controller_mode(core::ControllerMode::FourScore);
    const auto before = session.emulator().save_state();
    const auto path = session.state_path_for_slot(4);
    AsyncStateIO io;
    REQUIRE(io.save(session.emulator_handle(), path));
    CHECK_FALSE(io.cancel());
    REQUIRE(wait_for_state(io));
    CHECK(session.emulator().save_state() == before);
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    REQUIRE(io.load(StateLoadRequest{path, {}}));
    auto loaded = wait_for_state(io);
    REQUIRE(loaded);
    CHECK(loaded->path == path);
    CHECK_FALSE(loaded->legacy);
    CHECK(loaded->data == before);
    REQUIRE(session.load_state_data(loaded->data));
    CHECK(session.emulator().save_state() == before);
    CHECK(session.paused());
    CHECK(session.running());
}

TEST_CASE("Async reads preserve primary and legacy state selection rules", "[app][state][async]") {
    StateFixture fixture;
    REQUIRE(fixture.session.open_rom(fixture.rom.string()));
    const auto blob = fixture.session.emulator().save_state();
    const auto primary = fixture.session.state_path_for_slot(0);
    const auto legacy = legacy_state_path(fixture.rom, 0);
    fixture.write(legacy, blob);
    auto fallback = read_state(StateLoadRequest{primary, legacy});
    REQUIRE(fallback);
    CHECK(fallback->legacy);
    CHECK(fallback->path == legacy.string());
    CHECK_FALSE(read_state(StateLoadRequest{primary, {}}));
    std::filesystem::create_directories(std::filesystem::path(primary).parent_path());
    fixture.write(primary, std::vector<core::Byte>{1, 2, 3});
    auto selected = read_state(StateLoadRequest{primary, legacy});
    REQUIRE(selected);
    CHECK_FALSE(selected->legacy);
    CHECK(selected->path == primary);
    // Present corrupt primary files must never silently import a sibling.
    CHECK_FALSE(fixture.session.load_state_data(selected->data, selected->legacy));
    CHECK(fixture.session.emulator().save_state() == blob);
}

TEST_CASE("Missing oversized corrupt and mismatched async state loads preserve the game",
          "[app][state][async]") {
    StateFixture fixture;
    auto& session = fixture.session;
    REQUIRE(session.open_rom(fixture.rom.string()));
    session.set_paused(true);
    const auto before = session.emulator().save_state();
    const auto path = fixture.root / "bad.state";
    bool read_fails = false;
    SECTION("missing file") {
        read_fails = true;
    }
    SECTION("directory") {
        std::filesystem::create_directory(path);
        read_fails = true;
    }
    SECTION("oversized file") {
        fixture.write(path, std::vector<core::Byte>(core::Emulator::kMaxStateBytes + 1));
        read_fails = true;
    }
    SECTION("bad header") {
        fixture.write(path, std::vector<core::Byte>{1, 2, 3});
    }
    SECTION("truncated body rolls back") {
        auto truncated = before;
        truncated.pop_back();
        fixture.write(path, truncated);
    }
    SECTION("wrong ROM identity") {
        auto bytes = core::read_binary_file_capped(fixture.rom.string(), core::kMaxRomBytes, "ROM");
        REQUIRE(bytes);
        (*bytes)[16] ^= 1;
        auto cartridge = core::Cartridge::from_data(*bytes);
        REQUIRE(cartridge);
        core::Emulator other;
        REQUIRE(other.load_cartridge(std::move(*cartridge), {}));
        other.reset();
        fixture.write(path, other.save_state());
    }
    AsyncStateIO io;
    REQUIRE(io.load(StateLoadRequest{path, {}}));
    auto loaded = wait_for_state(io);
    if (read_fails) {
        REQUIRE_FALSE(loaded);
    } else {
        REQUIRE(loaded);
        CHECK_FALSE(session.load_state_data(loaded->data));
    }
    CHECK(session.emulator().save_state() == before);
    CHECK(session.paused());
    CHECK(session.current_rom_path() == fixture.rom.string());
}

TEST_CASE("Cancelling a blocked state read discards late bytes and permits another load",
          "[app][state][async]") {
    StateFixture fixture;
    REQUIRE(fixture.session.open_rom(fixture.rom.string()));
    const auto before = fixture.session.emulator().save_state();
    auto gate = std::make_shared<StateIOGate>();
    const ReleaseStateIO cleanup{gate};
    AsyncStateIO io([gate, before](const StateLoadRequest& request) -> core::Result<LoadedState> {
        if (request.primary != "ready") {
            gate->block();
            gate->finish();
        }
        return LoadedState{request.primary.string(), before, false};
    });
    REQUIRE(io.load(StateLoadRequest{"blocked", {}}));
    REQUIRE(gate->wait(&StateIOGate::entered));
    REQUIRE(io.cancel());
    CHECK(io.phase() == AsyncStateIO::Phase::Idle);
    CHECK_FALSE(io.poll());
    CHECK(fixture.session.emulator().save_state() == before);
    REQUIRE(io.load(StateLoadRequest{"ready", {}}));
    auto ready = wait_for_state(io);
    REQUIRE(ready);
    CHECK(ready->path == "ready");
    gate->release();
    REQUIRE(gate->wait(&StateIOGate::finished));
    CHECK_FALSE(io.poll());
    CHECK(fixture.session.emulator().save_state() == before);
}

TEST_CASE("Blocked state writes reject cancellation and own their machine through teardown",
          "[app][state][async]") {
    StateFixture fixture;
    auto machine = std::make_shared<core::Emulator>();
    REQUIRE(machine->load_cartridge(fixture.rom.string()));
    machine->reset();
    const auto blob = machine->save_state();
    std::weak_ptr<core::Emulator> weak = machine;
    auto gate = std::make_shared<StateIOGate>();
    const ReleaseStateIO cleanup{gate};
    auto io = std::make_unique<AsyncStateIO>(
        read_state, [gate](core::Emulator& emulator, const std::string& path) {
            gate->block();
            auto saved = write_state(emulator, path);
            gate->finish();
            return saved;
        });
    const auto path = fixture.root / "snapshot.state";
    REQUIRE(io->save(machine, path.string()));
    REQUIRE(gate->wait(&StateIOGate::entered));
    CHECK_FALSE(io->cancel());
    io.reset(); // Must not join the blocked writer.
    machine.reset();
    CHECK_FALSE(weak.expired());
    gate->release();
    REQUIRE(gate->wait(&StateIOGate::finished));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!weak.expired() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    CHECK(weak.expired());
    auto saved =
        core::read_binary_file_capped(path.string(), core::Emulator::kMaxStateBytes, "state");
    REQUIRE(saved);
    CHECK(*saved == blob);
}

TEST_CASE("Failed async state writes retain both the prior slot and paused game",
          "[app][state][async]") {
    StateFixture fixture;
    REQUIRE(fixture.session.open_rom(fixture.rom.string()));
    fixture.session.set_paused(true);
    const auto before = fixture.session.emulator().save_state();
    const auto path = fixture.session.state_path_for_slot(2);
    REQUIRE(fixture.session.save_state(2));
    // Prevent opening the temporary output without modifying the existing slot.
    std::filesystem::create_directory(path + ".tmp");
    AsyncStateIO io;
    REQUIRE(io.save(fixture.session.emulator_handle(), path));
    CHECK_FALSE(wait_for_state(io));
    CHECK(fixture.session.emulator().save_state() == before);
    CHECK(fixture.session.paused());
    auto saved = core::read_binary_file_capped(path, core::Emulator::kMaxStateBytes, "state");
    REQUIRE(saved);
    CHECK(*saved == before);
}

TEST_CASE("Async FDS snapshots persist dirty disks before serializing adapter state",
          "[app][state][async][fds]") {
    StateFixture fixture;
    auto& session = fixture.session;
    REQUIRE(session.open_rom(fixture.rom.string()));
    const auto changes = fixture.root / "disk.fds-save";
    REQUIRE(session.emulator().load_fds(
        tests::make_fds_bios(), tests::make_fds_image(2), changes.string()));
    session.emulator().reset();
    session.set_paused(true);
    change_fds_payload(session.emulator().fds(), 42);
    REQUIRE(session.emulator().fds().dirty());
    const auto path = fixture.root / "fds.state";
    AsyncStateIO io;
    REQUIRE(io.save(session.emulator_handle(), path.string()));
    REQUIRE(wait_for_state(io));
    CHECK_FALSE(session.emulator().fds().dirty());
    CHECK(std::filesystem::exists(changes));
    CHECK(session.emulator().fds().disk_image()[75] == 42);
    const auto saved_state = session.emulator().save_state();
    REQUIRE(session.step_frame() == TickResult::FrameAdvanced);
    REQUIRE(io.load(StateLoadRequest{path, {}}));
    auto loaded = wait_for_state(io);
    REQUIRE(loaded);
    REQUIRE(session.load_state_data(loaded->data));
    CHECK(session.emulator().save_state() == saved_state);
    CHECK(session.emulator().fds().side_count() == 2);
    CHECK(session.paused());
}

TEST_CASE("Async FDS persistence failures retain dirty data and the previous state slot",
          "[app][state][async][fds]") {
    StateFixture fixture;
    auto& session = fixture.session;
    REQUIRE(session.open_rom(fixture.rom.string()));
    const auto changes = fixture.root / "missing-directory" / "disk.fds-save";
    REQUIRE(session.emulator().load_fds(
        tests::make_fds_bios(), tests::make_fds_image(2), changes.string()));
    session.emulator().reset();
    session.set_paused(true);
    change_fds_payload(session.emulator().fds(), 42);
    const auto cycles = session.emulator().cpu().cycles();
    const auto path = fixture.root / "previous.state";
    const std::vector<core::Byte> previous{1, 2, 3};
    fixture.write(path, previous);
    AsyncStateIO io;
    REQUIRE(io.save(session.emulator_handle(), path.string()));
    auto saved = wait_for_state(io);
    REQUIRE_FALSE(saved);
    CHECK(session.emulator().fds().dirty());
    CHECK(session.emulator().fds().disk_image()[75] == 42);
    CHECK(session.emulator().cpu().cycles() == cycles);
    auto retained = core::read_binary_file_capped(path.string(), 64, "previous slot");
    REQUIRE(retained);
    CHECK(*retained == previous);
    session.emulator().set_persistence_enabled(false);
}

} // namespace mapperbus::app
