#include <algorithm>
#include <archive.h>
#include <archive_entry.h>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

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

struct Member {
    std::string name;
    std::vector<Byte> bytes;
    bool symlink = false;
};

void write_archive(const std::filesystem::path& path,
                   const std::vector<Member>& members,
                   bool seven_zip = false,
                   bool encrypt = false) {
    std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(),
                                                                   archive_write_free);
    REQUIRE(writer);
    REQUIRE((seven_zip ? archive_write_set_format_7zip(writer.get())
                       : archive_write_set_format_zip(writer.get())) == ARCHIVE_OK);
    if (!seven_zip) {
        REQUIRE(archive_write_set_options(writer.get(), "zip:compression=store") == ARCHIVE_OK);
    }
    if (encrypt) {
        REQUIRE(archive_write_set_passphrase(writer.get(), "test-password") == ARCHIVE_OK);
        REQUIRE(archive_write_set_options(writer.get(), "zip:encryption=traditional") ==
                ARCHIVE_OK);
    }
    REQUIRE(archive_write_open_filename(writer.get(), path.string().c_str()) == ARCHIVE_OK);
    for (const auto& member : members) {
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(),
                                                                            archive_entry_free);
        REQUIRE(entry);
        archive_entry_set_pathname(entry.get(), member.name.c_str());
        archive_entry_set_filetype(entry.get(), member.symlink ? AE_IFLNK : AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_size(entry.get(), static_cast<la_int64_t>(member.bytes.size()));
        if (member.symlink) {
            archive_entry_set_symlink(entry.get(), "game.nes");
        }
        REQUIRE(archive_write_header(writer.get(), entry.get()) == ARCHIVE_OK);
        if (!member.bytes.empty()) {
            REQUIRE(archive_write_data(writer.get(), member.bytes.data(), member.bytes.size()) ==
                    static_cast<la_ssize_t>(member.bytes.size()));
        }
    }
    REQUIRE(archive_write_close(writer.get()) == ARCHIVE_OK);
}

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
