#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

#include "../support/fds_image.hpp"
#include "../support/rom_archive.hpp"
#include "app/async_rom_loader.hpp"
#include "app/configuration.hpp"
#include "app/emulation_session.hpp"
#include "app/rom_source.hpp"
#include "core/binary_file.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "platform/audio/null_audio.hpp"
#include "platform/input/null_input.hpp"
#include "platform/video/null_video.hpp"

namespace mapperbus::app {
namespace {
using core::Byte;

struct ArchiveFixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-archive-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ArchiveFixture() {
        std::filesystem::create_directories(root);
        core::register_builtin_mappers();
    }
    ~ArchiveFixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};

std::vector<Byte> make_rom(Byte value = 42, bool battery = false) {
    std::vector<Byte> bytes(16 + 16384, 0);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1A;
    bytes[4] = 1;
    bytes[6] = battery ? 2 : 0;
    // LDA #value; STA $6000; JMP $8005.
    const Byte program[] = {0xA9, value, 0x8D, 0x00, 0x60, 0x4C, 0x05, 0x80};
    std::copy(std::begin(program), std::end(program), bytes.begin() + 16);
    bytes[bytes.size() - 4] = 0;
    bytes[bytes.size() - 3] = 0x80;
    return bytes;
}

using Member = mapperbus::tests::ArchiveMember;
using mapperbus::tests::write_archive;

// Keep battery files in the fixture instead of the user's application data.
class TestDataHome {
  public:
    explicit TestDataHome(const std::filesystem::path& root) {
        if (const char* original = std::getenv(variable())) {
            original_ = original;
        }
        set(root.string());
    }
    ~TestDataHome() {
        if (original_) {
            set(*original_);
        } else {
#if defined(_WIN32)
            (void)_putenv_s(variable(), "");
#else
            (void)unsetenv(variable());
#endif
        }
    }

  private:
    static const char* variable() {
#if defined(_WIN32)
        return "APPDATA";
#elif defined(__APPLE__)
        return "HOME";
#else
        return "XDG_DATA_HOME";
#endif
    }
    static void set(const std::string& value) {
#if defined(_WIN32)
        (void)_putenv_s(variable(), value.c_str());
#else
        (void)setenv(variable(), value.c_str(), 1);
#endif
    }
    std::optional<std::string> original_;
};
} // namespace

TEST_CASE("ZIP and 7z ROMs match raw media identity and member region", "[app][archive]") {
    ArchiveFixture fixture;
    const auto bytes = make_rom();
    for (bool seven_zip : {false, true}) {
        const auto path = fixture.root / (seven_zip ? "collection.7z" : "collection.ZIP");
        write_archive(path, {{"readme.txt", {}}, {"nested/Game (E).NES", bytes}}, seven_zip);
        core::Emulator emulator;
        auto loaded = load_rom_source(emulator, path.string());
        INFO((loaded ? "loaded" : loaded.error()));
        REQUIRE(loaded);
        CHECK(*loaded == "nested/Game (E).NES");
        CHECK(emulator.region() == core::Region::PAL);
        CHECK(emulator.content_hash() == core::rom_sha256(bytes));
        const auto raw = fixture.root / "Game (E).nes";
        REQUIRE(core::write_binary_file_atomic(raw.string(), bytes, "test ROM"));
        core::Emulator reference;
        REQUIRE(load_rom_source(reference, raw.string()));
        reference.reset();
        reference.step_frame();
        emulator.reset();
        REQUIRE(emulator.load_state(reference.save_state()));
    }
}

TEST_CASE("Archive member selection is explicit and rejects ambiguity", "[app][archive]") {
    ArchiveFixture fixture;
    const auto path = fixture.root / "games.zip";
    write_archive(path, {{"first.nes", make_rom(1)}, {"folder/second.nes", make_rom(2)}});
    core::Emulator emulator;
    auto ambiguous = load_rom_source(emulator, path.string());
    REQUIRE_FALSE(ambiguous);
    CHECK(ambiguous.error().find("--entry") != std::string::npos);
    CHECK(ambiguous.error().find("folder/second.nes") != std::string::npos);
    REQUIRE(load_rom_source(emulator, path.string(), "folder/second.nes"));
    CHECK(emulator.content_hash() == core::rom_sha256(make_rom(2)));
    const auto before = emulator.content_hash();
    CHECK_FALSE(load_rom_source(emulator, path.string(), "missing.nes"));
    CHECK(emulator.content_hash() == before);
    write_archive(path, {{"same.nes", make_rom(1)}, {"same.nes", make_rom(2)}});
    CHECK_FALSE(load_rom_source(emulator, path.string(), "same.nes"));
    CHECK(emulator.content_hash() == before);
}

TEST_CASE("Archived NES 2.0 region overrides the member filename", "[app][archive]") {
    ArchiveFixture fixture;
    auto bytes = make_rom();
    bytes[7] = 8;
    const auto path = fixture.root / "game.zip";
    write_archive(path, {{"Game (E).nes", bytes}});
    core::Emulator emulator;
    REQUIRE(load_rom_source(emulator, path.string()));
    CHECK(emulator.region() == core::Region::NTSC);
}

TEST_CASE("Archived and raw copies reuse identified states without sibling imports",
          "[app][archive]") {
    ArchiveFixture fixture;
    const auto raw = fixture.root / "game.nes";
    const auto zipped = fixture.root / "collection.zip";
    const auto bytes = make_rom();
    REQUIRE(core::write_binary_file_atomic(raw.string(), bytes, "test ROM"));
    write_archive(zipped, {{"renamed.nes", bytes}});
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<platform::NullAudio>(),
                             std::make_unique<platform::NullInput>());
    StateStorageSettings settings;
    settings.directory = fixture.root / "states";
    session.set_state_storage(settings);
    REQUIRE(session.open_rom(raw.string()));
    REQUIRE(session.save_state());
    const auto state_path = session.state_path_for_slot();
    REQUIRE(session.open_rom(zipped.string()));
    CHECK(session.state_path_for_slot() == state_path);
    REQUIRE(session.load_state());
    REQUIRE(core::write_binary_file_atomic(
        legacy_state_path(zipped, 1).string(), session.emulator().save_state(), "legacy state"));
    auto missing = session.state_load_path_for_slot(1);
    REQUIRE(missing);
    CHECK(*missing == session.state_path_for_slot(1));
    CHECK_FALSE(session.load_state(1));
    CHECK_FALSE(load_rom_source(session.emulator(), raw.string(), "unexpected.nes"));
}

TEST_CASE("Controller game preferences follow content across raw, ZIP and 7z sources",
          "[app][archive][game-controllers]") {
    ArchiveFixture fixture;
    const auto raw = fixture.root / "game.nes";
    const auto bytes = make_rom();
    REQUIRE(core::write_binary_file_atomic(raw.string(), bytes, "test ROM"));
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<platform::NullAudio>(),
                             std::make_unique<platform::NullInput>());
    MapperBusConfiguration configuration;
    REQUIRE(session.open_rom(raw.string()));
    const auto identity = session.game_identity();
    const ControllerHardwareSettings hori{core::ControllerMode::HoriFourPlayer,
                                          core::ControllerWiring::RfFamicom,
                                          core::ControllerTiming::RfFamicom,
                                          3};
    remember_controller_hardware(configuration, identity, hori);
    auto assignments = gamepad_assignments_for_game(configuration, {});
    assignments[0].enabled = false;
    assignments[3].gamepad_index = 17;
    configuration.game_gamepads[identity] = assignments;
    const auto config_path = fixture.root / "mapperbus.conf";
    REQUIRE(save_mapperbus_configuration_to_file(configuration, config_path));
    configuration = load_mapperbus_configuration_from_file(config_path);
    apply_controller_hardware(session.emulator(),
                              controller_hardware_for_game(configuration, identity));
    CHECK(controller_hardware(session.emulator()) == hori);
    const auto snapshot = session.emulator().save_state();
    const auto preferences = configuration.game_controllers;

    for (bool seven_zip : {false, true}) {
        const auto archive = fixture.root / (seven_zip ? "renamed.7z" : "renamed.zip");
        write_archive(
            archive, {{"other.nes", make_rom(17)}, {"folder/renamed.nes", bytes}}, seven_zip);
        REQUIRE(session.open_rom(archive.string(), "folder/renamed.nes"));
        CHECK(session.game_identity() == identity);
        CHECK(gamepad_assignments_for_game(configuration, session.game_identity()) == assignments);
        apply_controller_hardware(
            session.emulator(),
            controller_hardware_for_game(configuration, session.game_identity()));
        CHECK(controller_hardware(session.emulator()) == hori);
        session.reset();
        CHECK(controller_hardware(session.emulator()) == hori);

        REQUIRE(session.open_rom(archive.string(), "other.nes"));
        REQUIRE(session.game_identity() != identity);
        CHECK(gamepad_assignments_for_game(configuration, session.game_identity()) ==
              gamepad_assignments_for_game(configuration, {}));
        apply_controller_hardware(
            session.emulator(),
            controller_hardware_for_game(configuration, session.game_identity()));
        CHECK(controller_hardware(session.emulator()) == controller_hardware(configuration.input));
        const auto before_failure = session.emulator().save_state();
        REQUIRE_FALSE(session.open_rom(archive.string(), "missing.nes"));
        CHECK(session.emulator().save_state() == before_failure);

        REQUIRE(session.open_rom(raw.string()));
        apply_controller_hardware(session.emulator(), controller_hardware(configuration.input));
        REQUIRE(session.emulator().load_state(snapshot));
        CHECK(controller_hardware(session.emulator()) == hori);
        CHECK(configuration.game_controllers == preferences);
        CHECK(controller_hardware(configuration.input) == ControllerHardwareSettings{});
    }
}

TEST_CASE("Archives reject invalid media and preserve the active session", "[app][archive]") {
    ArchiveFixture fixture;
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<platform::NullAudio>(),
                             std::make_unique<platform::NullInput>());
    const auto good = fixture.root / "good.zip";
    write_archive(good, {{"good.nes", make_rom()}});
    REQUIRE(session.open_rom(good.string()));
    session.set_paused(true);
    const auto before = session.emulator().save_state();
    const auto bad = fixture.root / "bad.zip";
    for (const auto& members : std::vector<std::vector<Member>>{{{"readme.txt", {}}},
                                                                {{"bad.nes", {1, 2, 3}}},
                                                                {{"link.nes", {}, true}},
                                                                {{"nested.zip", {}}}}) {
        write_archive(bad, members);
        CHECK_FALSE(session.open_rom(bad.string()));
        CHECK(session.current_rom_path() == good.string());
        CHECK(session.current_rom_member() == "good.nes");
        CHECK(session.paused());
        CHECK(session.emulator().save_state() == before);
    }
    session.close_rom();
    CHECK(session.current_rom_member().empty());
}

TEST_CASE("Archives enforce ROM size, archive size, and entry limits", "[app][archive]") {
    ArchiveFixture fixture;
    const auto path = fixture.root / "large.zip";
    core::Emulator emulator;
    write_archive(path, {{"large.nes", std::vector<Byte>(core::kMaxRomBytes + 1)}});
    auto large = load_rom_source(emulator, path.string());
    REQUIRE_FALSE(large);
    CHECK(large.error().find("16 MiB") != std::string::npos);
    std::vector<Member> members;
    for (std::size_t index = 0; index <= kMaxArchiveEntries; ++index) {
        members.push_back({std::to_string(index) + ".txt", {}});
    }
    write_archive(path, members);
    auto crowded = list_archive_roms(path.string());
    REQUIRE_FALSE(crowded);
    CHECK(crowded.error().find("entry count") != std::string::npos);
    std::filesystem::resize_file(path, kMaxArchiveBytes + 1);
    CHECK_FALSE(load_rom_source(emulator, path.string()));
}

TEST_CASE("Archives reject encrypted data and corrupted checksums", "[app][archive]") {
    ArchiveFixture fixture;
    const auto path = fixture.root / "broken.zip";
    core::Emulator emulator;
    write_archive(path, {{"game.nes", make_rom()}}, false, true);
    CHECK_FALSE(load_rom_source(emulator, path.string()));
    write_archive(path, {{"game.nes", make_rom()}});
    auto bytes = core::read_binary_file_capped(path.string(), kMaxArchiveBytes, "test archive");
    REQUIRE(bytes);
    const std::vector<Byte> magic{'N', 'E', 'S', 0x1A};
    auto start = std::search(bytes->begin(), bytes->end(), magic.begin(), magic.end());
    REQUIRE(start != bytes->end());
    *(start + 17) ^= 0xFF;
    REQUIRE(core::write_binary_file_atomic(path.string(), *bytes, "test archive"));
    CHECK_FALSE(load_rom_source(emulator, path.string()));
    std::filesystem::resize_file(path, 10);
    CHECK_FALSE(load_rom_source(emulator, path.string()));
}

TEST_CASE("Archived battery saves are isolated by ROM content", "[app][archive]") {
    ArchiveFixture fixture;
    TestDataHome data_home(fixture.root);
    const auto path = fixture.root / "games.zip";
    const auto first = make_rom(11, true);
    const auto second = make_rom(22, true);
    write_archive(path, {{"../first.nes", first}, {"second.nes", second}});
    core::Emulator emulator;
    REQUIRE(load_rom_source(emulator, path.string(), "../first.nes"));
    emulator.reset();
    emulator.step_frame();
    REQUIRE(load_rom_source(emulator, path.string(), "second.nes"));
    emulator.reset();
    emulator.step_frame();
    emulator.unload_cartridge();
    const auto directory = mapperbus_data_directory() / "battery";
    for (const auto& rom : {first, second}) {
        auto saved = core::read_binary_file_capped(
            (directory / (core::sha256_hex(core::rom_sha256(rom)) + ".sav")).string(),
            8192,
            "battery save");
        REQUIRE(saved);
        REQUIRE_FALSE(saved->empty());
        CHECK(saved->front() == rom[17]);
    }
    REQUIRE(load_rom_source(emulator, path.string(), "../first.nes"));
    CHECK(emulator.read_cpu(0x6000) == 11);
    CHECK_FALSE(std::filesystem::exists(fixture.root / "games.sav"));
    CHECK_FALSE(std::filesystem::exists(fixture.root / "first.nes"));
}

} // namespace mapperbus::app

namespace mapperbus::app {
TEST_CASE("FDS archive sources share disk identity, BIOS validation, and side controls",
          "[app][archive][fds]") {
    ArchiveFixture fixture;
    TestDataHome data_home(fixture.root);
    const auto disk = mapperbus::tests::make_fds_image(2);
    const auto bios = fixture.root / "bios.bin";
    const auto raw = fixture.root / "game.fds";
    REQUIRE(
        core::write_binary_file_atomic(bios.string(), mapperbus::tests::make_fds_bios(), "BIOS"));
    REQUIRE(core::write_binary_file_atomic(raw.string(), disk, "disk"));
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<platform::NullAudio>(),
                             std::make_unique<platform::NullInput>());
    CHECK_FALSE(session.insert_disk_side(0));
    CHECK_FALSE(session.open_rom(raw.string()));
    session.set_fds_bios_path(bios.string());
    REQUIRE(session.open_rom(raw.string()));
    const auto identity = session.emulator().content_hash();
    MapperBusConfiguration configuration;
    const ControllerHardwareSettings rf{core::ControllerMode::Standard,
                                        core::ControllerWiring::RfFamicom,
                                        core::ControllerTiming::RfFamicom,
                                        1};
    remember_controller_hardware(configuration, session.game_identity(), rf);
    REQUIRE(session.insert_disk_side(1));
    CHECK(session.emulator().fds().current_side() == 1);
    REQUIRE(session.eject_disk());
    CHECK(session.emulator().fds().current_side() == -1);
    REQUIRE(session.insert_disk_side(0));
    CHECK_FALSE(session.insert_disk_side(2));
    REQUIRE(session.set_disk_write_protected(true));
    CHECK(session.emulator().fds().write_protected());
    StateStorageSettings settings;
    settings.directory = fixture.root / "states";
    session.set_state_storage(settings);
    REQUIRE(session.save_state());
    for (bool seven_zip : {false, true}) {
        const auto path = fixture.root / (seven_zip ? "game.7z" : "game.zip");
        write_archive(path, {{"Game.FDS", mapperbus::tests::make_fds_image(2, true)}}, seven_zip);
        REQUIRE(session.open_rom(path.string()));
        CHECK(session.current_rom_member() == "Game.FDS");
        CHECK(session.emulator().content_hash() == identity);
        apply_controller_hardware(
            session.emulator(),
            controller_hardware_for_game(configuration, session.game_identity()));
        CHECK(controller_hardware(session.emulator()) == rf);
        REQUIRE(session.load_state());
    }
    const auto before = session.emulator().save_state();
    session.set_fds_bios_path((fixture.root / "missing.bin").string());
    CHECK_FALSE(session.open_rom(raw.string()));
    CHECK(session.emulator().save_state() == before);
    session.close_rom();
    CHECK_FALSE(session.emulator().fds().is_loaded());
}
} // namespace mapperbus::app

namespace mapperbus::app {
TEST_CASE("Mixed NES and FDS archives select exact members and retain active FDS on failure",
          "[app][archive][fds]") {
    ArchiveFixture fixture;
    const auto storage = fixture.root / "isolated-data";
    const auto bios = storage / "bios" / "disksys.rom";
    std::filesystem::create_directories(bios.parent_path());
    REQUIRE(
        core::write_binary_file_atomic(bios.string(), mapperbus::tests::make_fds_bios(), "BIOS"));
    const auto disk = mapperbus::tests::make_fds_image(2);
    const auto cartridge = make_rom(22, true);
    const std::string disk_member = "../" + fixture.root.filename().string() + "-not-extracted.FDS";
    for (const bool seven_zip : {false, true}) {
        const auto path = fixture.root / (seven_zip ? "mixed.7z" : "mixed.zip");
        write_archive(path,
                      {{.name = "readme.txt", .bytes = {}},
                       {.name = disk_member, .bytes = disk},
                       {.name = "second.fds", .bytes = mapperbus::tests::make_fds_image()},
                       {.name = "Cart.NES", .bytes = cartridge},
                       {.name = "broken.fds", .bytes = {1, 2, 3}}},
                      seven_zip);
        auto members = list_archive_roms(path.string());
        REQUIRE(members);
        CHECK(*members ==
              std::vector<std::string>{disk_member, "second.fds", "Cart.NES", "broken.fds"});
        core::Emulator emulator;
        // The explicit root also supplies the default BIOS location, without
        // changing HOME or any process-wide application-data environment.
        auto selected = load_rom_source(emulator, path.string(), disk_member, {}, storage);
        REQUIRE(selected);
        CHECK(*selected == disk_member);
        emulator.reset();
        for (int frame = 0; frame < 30; ++frame)
            emulator.step_frame();
        REQUIRE(emulator.read_cpu(0x6000) == 1); // synthetic BIOS really ran
        emulator.fds().insert_side(1);
        const auto state = emulator.save_state();
        const auto identity = emulator.content_hash();
        const auto unchanged = [&] {
            CHECK(emulator.is_fds());
            CHECK(emulator.content_hash() == identity);
            CHECK(emulator.fds().current_side() == 1);
            CHECK(emulator.save_state() == state);
        };
        auto ambiguous = load_rom_source(emulator, path.string(), {}, {}, storage);
        REQUIRE_FALSE(ambiguous);
        CHECK(ambiguous.error().find("--entry") != std::string::npos);
        CHECK(ambiguous.error().find(disk_member) != std::string::npos);
        CHECK(ambiguous.error().find("Cart.NES") != std::string::npos);
        unchanged();
        for (const auto* bad_member : {"missing.fds", "cart.nes", "readme.txt", "broken.fds"}) {
            INFO(bad_member);
            REQUIRE_FALSE(load_rom_source(emulator, path.string(), bad_member, {}, storage));
            unchanged();
        }
        REQUIRE_FALSE(load_rom_source(emulator,
                                      path.string(),
                                      disk_member,
                                      (fixture.root / "missing-bios.bin").string(),
                                      storage));
        unchanged();
        const auto bad_bios = fixture.root / "short-bios.bin";
        REQUIRE(core::write_binary_file_atomic(
            bad_bios.string(), std::vector<core::Byte>(8191), "short BIOS"));
        REQUIRE_FALSE(
            load_rom_source(emulator, path.string(), disk_member, bad_bios.string(), storage));
        unchanged();
        const auto blocked = fixture.root / "not-a-directory";
        REQUIRE(core::write_binary_file_atomic(
            blocked.string(), std::vector<core::Byte>{1, 2, 3}, "blocked storage"));
        REQUIRE_FALSE(
            load_rom_source(emulator, path.string(), disk_member, bios.string(), blocked));
        unchanged();
        const auto damaged = fixture.root / (seven_zip ? "damaged.7z" : "damaged.zip");
        auto encoded =
            core::read_binary_file_capped(path.string(), kMaxArchiveBytes, "archive fixture");
        REQUIRE(encoded);
        if (seven_zip) {
            encoded->resize(encoded->size() / 2);
        } else {
            // ZIP entries use store mode. Alter a disk-info byte which is
            // otherwise legal FDS data, leaving the container CRC incorrect.
            const auto marker =
                std::search(encoded->begin(), encoded->end(), disk.begin(), disk.begin() + 16);
            REQUIRE(marker != encoded->end());
            *(marker + 16) ^= 1U;
        }
        REQUIRE(core::write_binary_file_atomic(damaged.string(), *encoded, "damaged archive"));
        REQUIRE_FALSE(load_rom_source(emulator, damaged.string(), disk_member, {}, storage));
        unchanged();
        CHECK_FALSE(std::filesystem::exists(fixture.root.parent_path() /
                                            std::filesystem::path(disk_member).filename()));
        // Switching from the FDS to a NES member ignores BIOS policy and puts
        // battery data under the same explicit, isolated persistence root.
        REQUIRE(load_rom_source(emulator, path.string(), "Cart.NES", "missing-bios.bin", storage));
        CHECK_FALSE(emulator.is_fds());
        emulator.reset();
        emulator.step_frame();
        emulator.unload_cartridge();
        auto battery = core::read_binary_file_capped(
            (storage / "battery" / (core::sha256_hex(core::rom_sha256(cartridge)) + ".sav"))
                .string(),
            8192,
            "isolated battery");
        REQUIRE(battery);
        REQUIRE_FALSE(battery->empty());
        CHECK(battery->front() == 22);
        CHECK_FALSE(std::filesystem::exists(fixture.root / "mixed.sav"));
    }
}
namespace {
// A controlled stand-in for a file operation that cannot be interrupted by the OS.
// Cleanup always releases it, including when an assertion fails.
struct BlockedRomReads {
    std::mutex mutex;
    std::condition_variable changed;
    bool released = false;
    int entered = 0;
    int finished = 0;

    core::Result<PreparedRom> read(const RomLoadRequest& request) {
        if (request.path == "ready") {
            PreparedRom result;
            result.path = request.path;
            return result;
        }
        std::unique_lock lock(mutex);
        ++entered;
        changed.notify_all();
        changed.wait(lock, [this] { return released; });
        ++finished;
        changed.notify_all();
        PreparedRom result;
        result.path = request.path;
        return result;
    }
    bool wait_entered(int count) {
        std::unique_lock lock(mutex);
        return changed.wait_for(
            lock, std::chrono::seconds(5), [this, count] { return entered >= count; });
    }
    void release() {
        const std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
struct ReleaseReads {
    std::shared_ptr<BlockedRomReads> reads;
    ~ReleaseReads() {
        reads->release();
    }
};
core::Result<PreparedRom> wait_for_rom(AsyncRomLoader& loader) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = loader.poll())
            return std::move(*result);
        std::this_thread::yield();
    }
    return std::unexpected("Timed out waiting for ROM job");
}
} // namespace

TEST_CASE("Blocked ROM reads can be cancelled without waiting or accepting late results",
          "[rom-source][async]") {
    auto reads = std::make_shared<BlockedRomReads>();
    const ReleaseReads cleanup{reads};
    AsyncRomLoader loader([reads](const auto& request) { return reads->read(request); });
    RomLoadRequest blocked;
    blocked.path = "blocked";
    REQUIRE(loader.start(blocked));
    REQUIRE(reads->wait_entered(1));
    CHECK(loader.phase() == AsyncRomLoader::Phase::Loading);
    REQUIRE(loader.cancel());
    CHECK(loader.phase() == AsyncRomLoader::Phase::Idle);
    CHECK_FALSE(loader.poll());
    RomLoadRequest ready;
    ready.path = "ready";
    REQUIRE(loader.start(ready));
    auto result = wait_for_rom(loader);
    REQUIRE(result);
    CHECK(result->path == "ready");
    reads->release();
    CHECK_FALSE(loader.poll());
}

TEST_CASE("Abandoned ROM reads are bounded and loader destruction never joins them",
          "[rom-source][async]") {
    auto reads = std::make_shared<BlockedRomReads>();
    const ReleaseReads cleanup{reads};
    auto loader = std::make_unique<AsyncRomLoader>(
        [reads](const auto& request) { return reads->read(request); });
    RomLoadRequest request;
    request.path = "blocked";
    for (int i = 1; i <= 4; ++i) {
        REQUIRE(loader->start(request));
        REQUIRE(reads->wait_entered(i));
        REQUIRE(loader->cancel());
    }
    CHECK_FALSE(loader->start(request));
    loader.reset();
    // This line is reached while every reader is still blocked.
    const std::lock_guard lock(reads->mutex);
    CHECK(reads->finished == 0);
}

TEST_CASE("Background preparation reads persistence but never writes discarded media",
          "[rom-source][async]") {
    ArchiveFixture fixture;
    const auto path = fixture.root / "battery.nes";
    const auto save = fixture.root / "battery.sav";
    REQUIRE(core::write_binary_file_atomic(path.string(), make_rom(42, true), "ROM"));
    const std::vector<Byte> original(8192, 73);
    REQUIRE(core::write_binary_file_atomic(save.string(), original, "battery"));
    RomLoadRequest request;
    request.path = path.string();
    request.state_storage.directory = fixture.root / "states";
    auto prepared = prepare_rom(request);
    REQUIRE(prepared);
    REQUIRE(prepared->emulator);
    CHECK(prepared->emulator->read_cpu(0x6000) == 73);
    prepared->emulator->step_frame();
    CHECK(prepared->emulator->read_cpu(0x6000) == 42);
    prepared->emulator.reset();
    auto persisted = core::read_binary_file_capped(save.string(), 8192, "battery");
    REQUIRE(persisted);
    CHECK(*persisted == original);
    std::filesystem::remove(save);
    prepared = prepare_rom(request);
    REQUIRE(prepared);
    prepared->emulator.reset();
    CHECK_FALSE(std::filesystem::exists(save));
}

TEST_CASE("Async replacement saves outgoing RAM before reopening and resumes the session",
          "[rom-source][async]") {
    ArchiveFixture fixture;
    const auto path = fixture.root / "reopen.nes";
    REQUIRE(core::write_binary_file_atomic(path.string(), make_rom(91, true), "ROM"));
    EmulationSession session(std::make_unique<platform::NullVideo>(),
                             std::make_unique<platform::NullAudio>(),
                             std::make_unique<platform::NullInput>());
    StateStorageSettings settings;
    settings.directory = fixture.root / "states";
    session.set_state_storage(settings);
    REQUIRE(session.open_rom(path.string()));
    session.emulator().step_frame();
    session.set_paused(true);
    const auto before = session.emulator().save_state();
    const auto identity = session.game_identity();
    AsyncRomLoader loader;
    REQUIRE(loader.save_current(session.emulator_handle()));
    CHECK_FALSE(loader.cancel());
    REQUIRE(wait_for_rom(loader));
    CHECK(session.emulator().save_state() == before);
    RomLoadRequest request;
    request.path = (fixture.root / "missing.nes").string();
    REQUIRE(loader.start(request));
    CHECK_FALSE(wait_for_rom(loader));
    CHECK(session.game_identity() == identity);
    CHECK(session.emulator().save_state() == before);
    CHECK(session.paused());
    request.path = path.string();
    request.state_storage = settings;
    REQUIRE(loader.start(request));
    auto prepared = wait_for_rom(loader);
    REQUIRE(prepared);
    CHECK(prepared->emulator->read_cpu(0x6000) == 91);
    const auto directory = prepared->state_directory;
    session.adopt_rom(std::move(*prepared));
    CHECK(session.emulator().read_cpu(0x6000) == 91);
    CHECK(session.running());
    CHECK_FALSE(session.paused());
    CHECK(session.state_directory() == directory);
}

TEST_CASE("Outgoing persistence failure leaves the paused machine available",
          "[rom-source][async]") {
    ArchiveFixture fixture;
    auto current = std::make_shared<core::Emulator>();
    auto cartridge = core::Cartridge::from_data(make_rom(66, true));
    REQUIRE(cartridge);
    // A directory cannot be replaced by a battery file on any supported OS.
    const auto blocked = fixture.root / "blocked.sav";
    std::filesystem::create_directory(blocked);
    REQUIRE(current->load_cartridge(std::move(*cartridge), blocked.string()));
    current->reset();
    current->step_frame();
    const auto before = current->save_state();
    AsyncRomLoader loader;
    REQUIRE(loader.save_current(current));
    auto result = wait_for_rom(loader);
    REQUIRE_FALSE(result);
    CHECK(result.error().find("Cannot save the current game") != std::string::npos);
    CHECK(current->save_state() == before);
    CHECK(current->read_cpu(0x6000) == 66);
    current->set_persistence_enabled(false);
}

TEST_CASE("Background ROM preparation reports missing inaccessible and invalid media",
          "[rom-source][async]") {
    ArchiveFixture fixture;
    AsyncRomLoader loader;
    RomLoadRequest request;
    request.path = (fixture.root / "missing.nes").string();
    REQUIRE(loader.start(request));
    CHECK_FALSE(wait_for_rom(loader));
    request.path = fixture.root.string();
    REQUIRE(loader.start(request));
    CHECK_FALSE(wait_for_rom(loader));
    const auto invalid = fixture.root / "invalid.nes";
    REQUIRE(core::write_binary_file_atomic(invalid.string(), std::vector<Byte>{1, 2, 3}, "ROM"));
    request.path = invalid.string();
    REQUIRE(loader.start(request));
    CHECK_FALSE(wait_for_rom(loader));
    // Reading a directory also exercises regular-file validation without relying
    // on user-specific privileges or an OS permission prompt.
    CHECK_FALSE(core::read_binary_file_capped(fixture.root.string(), 64, "ROM"));
#ifndef _WIN32
    const auto unreadable = fixture.root / "unreadable.nes";
    REQUIRE(core::write_binary_file_atomic(unreadable.string(), make_rom(), "ROM"));
    std::filesystem::permissions(unreadable, std::filesystem::perms::none);
    const std::ifstream probe(unreadable, std::ios::binary);
    if (!probe) {
        request.path = unreadable.string();
        REQUIRE(loader.start(request));
        CHECK_FALSE(wait_for_rom(loader));
    } else {
        WARN("Unreadable regular-file check requires an unprivileged test process");
    }
    std::filesystem::permissions(unreadable, std::filesystem::perms::owner_all);
#endif
}

TEST_CASE("Async archive discovery returns a choice before loading an exact member",
          "[rom-source][async]") {
    ArchiveFixture fixture;
    const auto archive = fixture.root / "choice.zip";
    write_archive(archive, {Member{"one.nes", make_rom(1)}, Member{"two.nes", make_rom(2)}});
    RomLoadRequest request;
    request.path = archive.string();
    request.state_storage.directory = fixture.root / "states";
    AsyncRomLoader loader;
    REQUIRE(loader.start(request));
    auto listed = wait_for_rom(loader);
    REQUIRE(listed);
    CHECK_FALSE(listed->emulator);
    REQUIRE(listed->members.size() == 2);
    request.member = "two.nes";
    REQUIRE(loader.start(request));
    auto loaded = wait_for_rom(loader);
    REQUIRE(loaded);
    REQUIRE(loaded->emulator);
    CHECK(loaded->member == "two.nes");
    loaded->emulator->step_frame();
    CHECK(loaded->emulator->read_cpu(0x6000) == 2);
}

TEST_CASE("Async FDS preparation validates BIOS and restores disk overlays",
          "[rom-source][async][fds]") {
    ArchiveFixture fixture;
    const auto disk = mapperbus::tests::make_fds_image(2);
    const auto path = fixture.root / "disk.zip";
    const auto bios = fixture.root / "bios.bin";
    write_archive(path, {{"game.fds", disk}});
    REQUIRE(
        core::write_binary_file_atomic(bios.string(), mapperbus::tests::make_fds_bios(), "BIOS"));
    RomLoadRequest request;
    request.path = path.string();
    request.fds_bios_path = bios.string();
    request.persistence_directory = fixture.root / "data";
    request.state_storage.directory = fixture.root / "states";
    const auto identity = core::rom_sha256(disk);
    const auto save =
        request.persistence_directory / "fds" / (core::sha256_hex(identity) + ".fds-save");
    std::filesystem::create_directories(save.parent_path());
    auto changed = disk;
    changed[75] = 42;
    const auto digest = core::rom_sha256(changed);
    std::vector<Byte> overlay{'M', 'B', 'F', 'D', 1, 0, 0, 0};
    overlay.insert(overlay.end(), identity.begin(), identity.end());
    overlay.insert(overlay.end(), digest.begin(), digest.end());
    overlay.insert(overlay.end(), changed.begin(), changed.end());
    REQUIRE(core::write_binary_file_atomic(save.string(), overlay, "disk save"));
    AsyncRomLoader loader;
    REQUIRE(loader.start(request));
    auto prepared = wait_for_rom(loader);
    REQUIRE(prepared);
    REQUIRE(prepared->emulator);
    CHECK(prepared->emulator->is_fds());
    CHECK(prepared->emulator->fds().side_count() == 2);
    CHECK(prepared->emulator->fds().disk_image()[75] == 42);
    prepared->emulator.reset();
    auto persisted = core::read_binary_file_capped(save.string(), overlay.size(), "disk save");
    REQUIRE(persisted);
    CHECK(*persisted == overlay);
    request.fds_bios_path = (fixture.root / "missing.bin").string();
    REQUIRE(loader.start(request));
    CHECK_FALSE(wait_for_rom(loader));
}

namespace {
std::vector<Byte> large_cancel_rom() {
    auto data = make_rom(17);
    data.resize(16 + 512 * 1024, 0xEA);
    data[4] = 32;
    return data;
}
struct ArchiveCheckpointGate {
    ArchiveReadPhase phase = ArchiveReadPhase::Data;
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    bool finished = false;
    std::string error;
    std::size_t bytes = 0;
    void observe(ArchiveReadProgress progress) {
        if (progress.phase != phase || (phase == ArchiveReadPhase::Data && progress.bytes == 0) ||
            (phase == ArchiveReadPhase::Header && progress.entries == 0))
            return;
        std::unique_lock lock(mutex);
        if (entered)
            return;
        entered = true;
        bytes = progress.bytes;
        changed.notify_all();
        changed.wait(lock, [this] { return released; });
    }
    bool wait(bool ArchiveCheckpointGate::* flag) {
        std::unique_lock lock(mutex);
        return changed.wait_for(
            lock, std::chrono::seconds(5), [this, flag] { return this->*flag; });
    }
    void finish(std::string result_error) {
        const std::lock_guard lock(mutex);
        error = std::move(result_error);
        finished = true;
        changed.notify_all();
    }
    void release() {
        const std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
struct ReleaseArchiveCheckpoint {
    std::shared_ptr<ArchiveCheckpointGate> gate;
    ~ReleaseArchiveCheckpoint() {
        gate->release();
    }
};
core::Result<void> start_after_cleanup(AsyncRomLoader& loader, const RomLoadRequest& request) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        auto result = loader.start(request);
        if (result || result.error().find("still waiting") == std::string::npos)
            return result;
        std::this_thread::yield(); // A finished worker may still be releasing its permit.
    }
    return std::unexpected("Cancelled archive workers did not release capacity");
}
} // namespace

TEST_CASE("Archive cancellation checks scan and decoded-data boundaries without changing media",
          "[rom-source][archive][cancel]") {
    ArchiveFixture fixture;
    const auto large = large_cancel_rom();
    for (const bool seven_zip : {false, true}) {
        INFO((seven_zip ? "7z" : "deflated ZIP"));
        const auto path = fixture.root / (seven_zip ? "cancel.7z" : "cancel.zip");
        write_archive(path,
                      {{"readme.txt", std::vector<Byte>(100000, 7)},
                       {"game.nes", large},
                       {"tail.nes", make_rom()}},
                      seven_zip,
                      false,
                      true);
        auto original = core::read_binary_file_capped(path.string(), kMaxArchiveBytes, "archive");
        REQUIRE(original);
        core::Emulator current;
        auto old = core::Cartridge::from_data(make_rom(99));
        REQUIRE(old);
        REQUIRE(current.load_cartridge(std::move(*old), ""));
        current.reset();
        current.step_frame();
        const auto snapshot = current.save_state();
        const auto identity = current.content_hash();
        for (auto phase : {ArchiveReadPhase::Header,
                           ArchiveReadPhase::Skip,
                           ArchiveReadPhase::Data,
                           ArchiveReadPhase::Close}) {
            INFO(static_cast<int>(phase));
            std::stop_source stop;
            bool reached = false;
            std::size_t bytes = 0;
            RomReadControl control{
                stop.get_token(), [&](ArchiveReadProgress progress) {
                    if (progress.phase == phase &&
                        (phase != ArchiveReadPhase::Header || progress.entries > 0) &&
                        (phase != ArchiveReadPhase::Data || progress.bytes > 0)) {
                        reached = true;
                        bytes = progress.bytes;
                        stop.request_stop();
                    }
                }};
            if (phase == ArchiveReadPhase::Header || phase == ArchiveReadPhase::Skip) {
                auto result = list_archive_roms(path.string(), control);
                REQUIRE_FALSE(result);
                CHECK(result.error() == kRomLoadCancelled);
            } else {
                auto result = load_rom_source(
                    current, path.string(), "game.nes", {}, fixture.root / "data", control);
                REQUIRE_FALSE(result);
                CHECK(result.error() == kRomLoadCancelled);
            }
            REQUIRE(reached);
            if (phase == ArchiveReadPhase::Data) {
                CHECK(bytes > 0);
                CHECK(bytes < large.size());
            }
            CHECK(current.content_hash() == identity);
            CHECK(current.save_state() == snapshot);
        }
        auto retained = core::read_binary_file_capped(path.string(), kMaxArchiveBytes, "archive");
        REQUIRE(retained);
        CHECK(*retained == *original);
        CHECK_FALSE(std::filesystem::exists(fixture.root / "data"));
    }
}

TEST_CASE("A pre-cancelled ROM job does no source access or archive observer work",
          "[rom-source][cancel]") {
    std::stop_source stop;
    stop.request_stop();
    bool observed = false;
    const RomReadControl control{stop.get_token(), [&](auto) { observed = true; }};
    auto listed = list_archive_roms("missing.zip", control);
    REQUIRE_FALSE(listed);
    CHECK(listed.error() == kRomLoadCancelled);
    core::Emulator current;
    auto loaded = load_rom_source(current, "missing.nes", {}, {}, {}, control);
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error() == kRomLoadCancelled);
    RomLoadRequest request;
    request.path = "missing.7z";
    auto prepared = prepare_rom(request, stop.get_token());
    REQUIRE_FALSE(prepared);
    CHECK(prepared.error() == kRomLoadCancelled);
    CHECK_FALSE(observed);
}

TEST_CASE("Repeated archive Cancel signals real decoding workers and recovers their capacity",
          "[rom-source][archive][async][cancel]") {
    ArchiveFixture fixture;
    const auto large = large_cancel_rom();
    const auto small = fixture.root / "ready.nes";
    REQUIRE(core::write_binary_file_atomic(small.string(), make_rom(42), "ROM"));
    core::Emulator current;
    REQUIRE(current.load_cartridge(small.string()));
    current.reset();
    current.step_frame();
    const auto before = current.save_state();
    using Gates = std::map<std::string, std::shared_ptr<ArchiveCheckpointGate>>;
    auto gates = std::make_shared<Gates>();
    for (int cycle = 0; cycle < 8; ++cycle) {
        const bool seven_zip = cycle % 2 != 0;
        const auto path =
            fixture.root / ("repeat-" + std::to_string(cycle) + (seven_zip ? ".7z" : ".zip"));
        write_archive(path, {{"game.nes", large}}, seven_zip, false, true);
        gates->emplace(path.string(), std::make_shared<ArchiveCheckpointGate>());
    }
    struct ReleasePool {
        std::shared_ptr<Gates> gates;
        ~ReleasePool() {
            for (const auto& [path, gate] : *gates) {
                (void)path;
                gate->release();
            }
        }
    } cleanup{gates};
    // The map is immutable while workers run. Record the real prepare result
    // before it is discarded, without invoking any frontend callback.
    AsyncRomLoader loader(
        AsyncRomLoader::CancellablePrepare{[gates](const auto& request, std::stop_token stop) {
            auto result = prepare_rom(request, stop);
            if (auto found = gates->find(request.path); found != gates->end())
                found->second->finish(result ? "unexpected success" : result.error());
            return result;
        }});
    for (const auto& [path, gate] : *gates) {
        RomLoadRequest request;
        request.path = path;
        request.member = "game.nes";
        request.state_storage.directory = fixture.root / "states";
        request.archive_observer = [gate](auto progress) { gate->observe(progress); };
        REQUIRE(start_after_cleanup(loader, request));
        REQUIRE(gate->wait(&ArchiveCheckpointGate::entered));
        REQUIRE(loader.cancel());
        CHECK(loader.phase() == AsyncRomLoader::Phase::Idle);
        CHECK_FALSE(loader.poll());
        gate->release();
        REQUIRE(gate->wait(&ArchiveCheckpointGate::finished));
        CHECK(gate->error == kRomLoadCancelled);
        CHECK(gate->bytes > 0);
        CHECK(gate->bytes < large.size());
        CHECK(current.save_state() == before);
    }
    // Repetition beyond the four-job cap and a fresh successful load prove
    // cancelled decoding returns worker permits without publishing old media.
    RomLoadRequest ready;
    ready.path = small.string();
    ready.state_storage.directory = fixture.root / "states";
    REQUIRE(start_after_cleanup(loader, ready));
    auto completed = wait_for_rom(loader);
    REQUIRE(completed);
    REQUIRE(completed->emulator);
    completed->emulator->step_frame();
    CHECK(completed->emulator->read_cpu(0x6000) == 42);
    CHECK(current.save_state() == before);
}

TEST_CASE("Destroying an archive loader requests cooperative stop without joining its reader",
          "[rom-source][archive][async][cancel]") {
    ArchiveFixture fixture;
    const auto path = fixture.root / "retired.7z";
    write_archive(path, {{"game.nes", large_cancel_rom()}}, true);
    auto gate = std::make_shared<ArchiveCheckpointGate>();
    const ReleaseArchiveCheckpoint cleanup{gate};
    {
        AsyncRomLoader loader(
            AsyncRomLoader::CancellablePrepare{[gate](const auto& request, std::stop_token stop) {
                auto result = prepare_rom(request, stop);
                gate->finish(result ? "unexpected success" : result.error());
                return result;
            }});
        RomLoadRequest request;
        request.path = path.string();
        request.member = "game.nes";
        request.archive_observer = [gate](auto progress) { gate->observe(progress); };
        REQUIRE(loader.start(request));
        REQUIRE(gate->wait(&ArchiveCheckpointGate::entered));
    }
    gate->release();
    REQUIRE(gate->wait(&ArchiveCheckpointGate::finished));
    CHECK(gate->error == kRomLoadCancelled);
}

TEST_CASE("Retiring a noncancellable writer does not request its stop token",
          "[rom-source][async][cancel]") {
    auto gate = std::make_shared<ArchiveCheckpointGate>();
    const ReleaseArchiveCheckpoint cleanup{gate};
    auto stopped = std::make_shared<bool>(true);
    {
        BackgroundTask<void> writer;
        REQUIRE(writer.start(
            [gate, stopped](std::stop_token stop) -> core::Result<void> {
                gate->observe({ArchiveReadPhase::Data, 1, 1});
                *stopped = stop.stop_requested();
                gate->finish({});
                return {};
            },
            false));
        REQUIRE(gate->wait(&ArchiveCheckpointGate::entered));
        CHECK_FALSE(writer.cancel());
    }
    gate->release();
    REQUIRE(gate->wait(&ArchiveCheckpointGate::finished));
    CHECK_FALSE(*stopped);
}

TEST_CASE("Archive progress reports real scan and decode checkpoints for NES and FDS",
          "[rom-source][archive][async][progress]") {
    ArchiveFixture fixture;
    const auto bios = fixture.root / "bios.bin";
    REQUIRE(
        core::write_binary_file_atomic(bios.string(), mapperbus::tests::make_fds_bios(), "BIOS"));
    auto padded_rom = make_rom();
    padded_rom.resize(128 * 1024, 0); // Valid NROM with padding beyond one decoder chunk.
    for (bool seven_zip : {false, true}) {
        for (bool fds : {false, true}) {
            INFO((seven_zip ? "7z" : "deflated ZIP"));
            INFO(fds);
            const auto bytes = fds ? mapperbus::tests::make_fds_image(2) : padded_rom;
            const std::string member = fds ? "game.fds" : "game.nes";
            const auto path = fixture.root / (seven_zip ? "progress.7z" : "progress.zip");
            write_archive(
                path, {{"readme.txt", {1, 2, 3}}, {member, bytes}}, seven_zip, false, true);
            for (auto phase :
                 {ArchiveReadPhase::Header, ArchiveReadPhase::Data, ArchiveReadPhase::Close}) {
                INFO(static_cast<int>(phase));
                auto gate = std::make_shared<ArchiveCheckpointGate>();
                gate->phase = phase;
                const ReleaseArchiveCheckpoint cleanup{gate};
                AsyncRomLoader loader;
                CHECK_FALSE(loader.archive_progress());
                RomLoadRequest request;
                request.path = path.string();
                // Exercise automatic member scanning as well as exact-member decoding.
                request.member = phase == ArchiveReadPhase::Header ? "" : member;
                request.fds_bios_path = bios.string();
                request.persistence_directory = fixture.root / "data";
                request.state_storage.directory = fixture.root / "states";
                request.archive_observer = [gate](auto update) { gate->observe(update); };
                REQUIRE(loader.start(request));
                REQUIRE(gate->wait(&ArchiveCheckpointGate::entered));
                auto progress = loader.archive_progress();
                REQUIRE(progress);
                CHECK(progress->phase == phase);
                CHECK(progress->entries > 0);
                if (phase == ArchiveReadPhase::Data) {
                    CHECK(progress->bytes == 65536);
                    CHECK(progress->bytes < bytes.size());
                } else if (phase == ArchiveReadPhase::Close) {
                    CHECK(progress->bytes == bytes.size());
                } else {
                    CHECK(progress->bytes == 0);
                }
                // A rejected duplicate start must not clear the active snapshot.
                CHECK_FALSE(loader.start(request));
                REQUIRE(loader.archive_progress());
                CHECK(loader.archive_progress()->phase == phase);
                gate->release();
                auto result = wait_for_rom(loader);
                REQUIRE(result);
                REQUIRE(result->emulator);
                CHECK(result->emulator->is_fds() == fds);
                CHECK(result->member == member);
                CHECK_FALSE(loader.archive_progress());
                CHECK(loader.phase() == AsyncRomLoader::Phase::Idle);
            }
        }
    }
}

TEST_CASE("Cancelled archive progress cannot overwrite a newer job or survive failure",
          "[rom-source][archive][async][progress]") {
    ArchiveFixture fixture;
    const auto old_path = fixture.root / "old.zip";
    const auto new_path = fixture.root / "new.zip";
    auto padded_rom = make_rom();
    padded_rom.resize(128 * 1024, 0);
    write_archive(old_path, {{"game.nes", padded_rom}}, false, false, true);
    write_archive(new_path, {{"readme.txt", {}}, {"game.nes", make_rom()}});
    auto old_gate = std::make_shared<ArchiveCheckpointGate>();
    auto new_gate = std::make_shared<ArchiveCheckpointGate>();
    new_gate->phase = ArchiveReadPhase::Header;
    const ReleaseArchiveCheckpoint release_old{old_gate};
    const ReleaseArchiveCheckpoint release_new{new_gate};
    AsyncRomLoader loader(AsyncRomLoader::CancellablePrepare{
        [old_gate, old_path](const auto& request, std::stop_token stop) {
            auto result = prepare_rom(request, stop);
            if (request.path == old_path.string()) {
                // Even a misbehaving injected reader can only write its own mailbox.
                request.archive_observer({ArchiveReadPhase::Close, 999, 777});
                old_gate->finish(result ? "unexpected success" : result.error());
            }
            return result;
        }});
    RomLoadRequest request;
    request.path = old_path.string();
    request.member = "game.nes";
    request.state_storage.directory = fixture.root / "states";
    request.archive_observer = [old_gate](auto update) { old_gate->observe(update); };
    REQUIRE(loader.start(request));
    REQUIRE(old_gate->wait(&ArchiveCheckpointGate::entered));
    REQUIRE(loader.archive_progress());
    CHECK(loader.archive_progress()->phase == ArchiveReadPhase::Data);
    REQUIRE(loader.cancel());
    CHECK_FALSE(loader.archive_progress());
    request.path = new_path.string();
    request.archive_observer = [new_gate](auto update) { new_gate->observe(update); };
    REQUIRE(loader.start(request));
    REQUIRE(new_gate->wait(&ArchiveCheckpointGate::entered));
    old_gate->release();
    REQUIRE(old_gate->wait(&ArchiveCheckpointGate::finished));
    CHECK(old_gate->error == kRomLoadCancelled);
    auto progress = loader.archive_progress();
    REQUIRE(progress);
    CHECK(progress->phase == ArchiveReadPhase::Header);
    CHECK(progress->entries == 1);
    CHECK(progress->bytes == 0);
    new_gate->release();
    REQUIRE(wait_for_rom(loader));
    CHECK_FALSE(loader.archive_progress());

    const auto broken = fixture.root / "broken.zip";
    write_archive(broken, {{"broken.nes", {1, 2, 3}}});
    request.path = broken.string();
    request.member.clear();
    request.archive_observer = {};
    REQUIRE(start_after_cleanup(loader, request));
    CHECK_FALSE(wait_for_rom(loader));
    CHECK_FALSE(loader.archive_progress());

    const auto raw = fixture.root / "raw.nes";
    REQUIRE(core::write_binary_file_atomic(raw.string(), make_rom(), "ROM"));
    request.path = raw.string();
    REQUIRE(start_after_cleanup(loader, request));
    REQUIRE(wait_for_rom(loader));
    CHECK_FALSE(loader.archive_progress());
}

TEST_CASE("Retrying an owned archive selection after repair preserves the active session",
          "[rom-source][archive][async][recovery]") {
    ArchiveFixture fixture;
    const auto current = fixture.root / "current.nes";
    REQUIRE(core::write_binary_file_atomic(current.string(), make_rom(63), "ROM"));
    for (bool seven_zip : {false, true}) {
        for (int failure : {0, 1, 2}) {
            for (bool paused : {false, true}) {
                INFO(seven_zip);
                INFO(failure);
                INFO(paused);
                const bool fds = failure == 1;
                const auto path = fixture.root / (seven_zip ? "retry.7z" : "retry.zip");
                const auto bios = fixture.root / "retry-bios.bin";
                std::filesystem::remove(path);
                std::filesystem::remove(bios);
                const std::string member = fds ? "selected.fds" : "selected.nes";
                const auto bytes = fds ? mapperbus::tests::make_fds_image(2) : make_rom(77);
                if (failure == 0) {
                    REQUIRE(core::write_binary_file_atomic(
                        path.string(), std::vector<Byte>{1, 2, 3}, "archive"));
                } else if (fds) {
                    write_archive(path, {{member, bytes}}, seven_zip);
                } else {
                    // Metadata/read failure, recoverable without choosing a new path.
                    std::filesystem::create_directory(path);
                }
                EmulationSession session(std::make_unique<platform::NullVideo>(),
                                         std::make_unique<platform::NullAudio>(),
                                         std::make_unique<platform::NullInput>());
                StateStorageSettings storage;
                storage.directory = fixture.root / "states";
                session.set_state_storage(storage);
                REQUIRE(session.open_rom(current.string()));
                session.emulator().step_frame();
                session.set_paused(paused);
                const auto snapshot = session.emulator().save_state();
                const auto identity = session.game_identity();
                RomLoadRequest request;
                request.path = path.string();
                request.member = member;
                request.fds_bios_path = bios.string();
                request.persistence_directory = fixture.root / "data";
                request.state_storage = storage;
                AsyncRomLoader loader;
                REQUIRE(loader.start(request));
                auto failed = wait_for_rom(loader);
                REQUIRE_FALSE(failed);
                CHECK(session.paused() == paused);
                CHECK(session.game_identity() == identity);
                CHECK(session.emulator().save_state() == snapshot);
                std::filesystem::remove(path);
                // A second candidate ensures Retry must retain the exact selection.
                write_archive(path, {{"other.nes", make_rom(22)}, {member, bytes}}, seven_zip);
                REQUIRE(core::write_binary_file_atomic(
                    bios.string(), mapperbus::tests::make_fds_bios(), "BIOS"));
                REQUIRE(start_after_cleanup(loader, request));
                auto prepared = wait_for_rom(loader);
                REQUIRE(prepared);
                REQUIRE(prepared->emulator);
                CHECK(prepared->member == member);
                CHECK(prepared->path == path.string());
                CHECK(session.paused() == paused);
                CHECK(session.emulator().save_state() == snapshot);
                session.adopt_rom(std::move(*prepared));
                CHECK(session.current_rom_member() == member);
                CHECK(session.emulator().is_fds() == fds);
                if (fds) {
                    CHECK(session.emulator().fds().side_count() == 2);
                } else {
                    session.emulator().step_frame();
                    CHECK(session.emulator().read_cpu(0x6000) == 77);
                }
            }
        }
    }
}

} // namespace mapperbus::app
