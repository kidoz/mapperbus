#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <nk/platform/events.h>
#include <nk/platform/window.h>
#include <string>
#include <thread>

#include "../support/fds_image.hpp"
#include "app/background_task.hpp"
#include "core/binary_file.hpp"
#include "core/emulator.hpp"
#include "frontends/nodalkit/gui_close_policy.hpp"

namespace {
struct Fixture {
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-gui-close-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Fixture() {
        std::filesystem::create_directories(root);
    }
    ~Fixture() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }
};
} // namespace

TEST_CASE("Native window close preserves unsaved FDS progress until storage recovers",
          "[gui][fds][save]") {
    Fixture fixture;
    const auto save = fixture.root / "unavailable" / "progress.fds-save";
    const auto original = mapperbus::tests::make_fds_image(2);
    const auto bios = mapperbus::tests::make_fds_bios();
    mapperbus::core::Emulator emulator;
    REQUIRE(emulator.load_fds(bios, original, save.string()));
    const auto identity = emulator.content_hash();

    // Restoring a snapshot with different disk contents creates unsaved disk
    // progress, just like restoring an older registration in the GUI.
    auto changed = original;
    changed[75] = 42;
    REQUIRE(emulator.fds().apply_disk_changes(changed));
    const auto changed_state = emulator.save_state();
    REQUIRE(emulator.fds().apply_disk_changes(original));
    REQUIRE(emulator.load_state(changed_state));
    emulator.fds().insert_side(1);
    emulator.fds().set_write_protected(true);
    REQUIRE(emulator.fds().dirty());
    const auto before = emulator.save_state();

    // Without an Application, NodalKit uses its software window model. Drive
    // the same Close event used by the native surface; no display is required.
    nk::Window window({.title = "FDS close recovery", .width = 320, .height = 240});
    int closed = 0;
    int errors = 0;
    std::string error;
    mapperbus::frontend::guard_window_close(
        window,
        [&] { return emulator.flush_disk_changes(); },
        [&](const std::string& message) {
            ++errors;
            error = message;
        });
    (void)window.on_close_requested().connect([&] { ++closed; });
    window.present();
    REQUIRE(window.is_visible());
    nk::WindowEvent close{};
    close.type = nk::WindowEvent::Type::Close;
    window.dispatch_window_event(close);
    CHECK(window.is_visible());
    CHECK(closed == 0);
    CHECK(errors == 1);
    CHECK(error.find("FDS disk save") != std::string::npos);
    CHECK(emulator.fds().dirty());
    CHECK(emulator.save_state() == before);

    // A veto leaves the session available for export as well as retry.
    const auto exported = fixture.root / "recovered.fds";
    REQUIRE(emulator.export_disk(exported.string()));
    auto disk = mapperbus::core::read_binary_file_capped(
        exported.string(), mapperbus::core::Fds::kMaxDiskImageBytes, "exported disk");
    REQUIRE(disk);
    CHECK(*disk == changed);

    std::filesystem::create_directory(save.parent_path());
    window.dispatch_window_event(close);
    CHECK_FALSE(window.is_visible());
    CHECK(closed == 1);
    CHECK(errors == 1);
    CHECK_FALSE(emulator.fds().dirty());
    mapperbus::core::Emulator restored;
    REQUIRE(restored.load_fds(bios, original, save.string()));
    CHECK(restored.content_hash() == identity);
    CHECK(std::ranges::equal(restored.fds().disk_image(), changed));
}

TEST_CASE(
    "Deferred native Close remains visible on pending or failed persistence and permits Retry",
    "[gui][fds][async]") {
    Fixture fixture;
    const auto disk = mapperbus::tests::make_fds_image(2);
    const auto path = fixture.root / "unavailable" / "progress.fds-save";
    auto emulator = std::make_shared<mapperbus::core::Emulator>();
    REQUIRE(emulator->load_fds(mapperbus::tests::make_fds_bios(), disk, path.string()));
    auto changed = disk;
    changed[75] = 42;
    REQUIRE(emulator->fds().apply_disk_changes(changed));
    const auto snapshot = emulator->save_state();
    REQUIRE(emulator->fds().apply_disk_changes(disk));
    REQUIRE(emulator->load_state(snapshot));
    REQUIRE(emulator->fds().dirty());
    nk::Window window({.title = "Deferred FDS Close", .width = 320, .height = 240});
    mapperbus::app::BackgroundTask<void> saving;
    bool authorized = false;
    bool pending = false;
    int closed = 0;
    mapperbus::frontend::guard_async_window_close(
        window,
        [&] { return authorized; },
        [&] {
            if (!pending) {
                REQUIRE(
                    saving.start([emulator] { return emulator->flush_persistent_data(); }, false));
                pending = true;
            }
        });
    (void)window.on_close_requested().connect([&] { ++closed; });
    window.present();
    nk::WindowEvent close{};
    close.type = nk::WindowEvent::Type::Close;
    const auto complete = [&]() -> mapperbus::core::Result<void> {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (auto result = saving.poll()) {
                pending = false;
                return std::move(*result);
            }
            std::this_thread::yield();
        }
        return std::unexpected("Timed out waiting for close save");
    };
    window.dispatch_window_event(close);
    CHECK(window.is_visible());
    CHECK(closed == 0);
    CHECK_FALSE(complete());
    CHECK(emulator->fds().dirty());
    CHECK(window.is_visible());
    std::filesystem::create_directory(path.parent_path());
    window.dispatch_window_event(close); // Retry requests another checked save.
    CHECK(window.is_visible());
    REQUIRE(complete());
    authorized = true;
    window.request_close();
    CHECK_FALSE(window.is_visible());
    CHECK(closed == 1);
    CHECK_FALSE(emulator->fds().dirty());
}
