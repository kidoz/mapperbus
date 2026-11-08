#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nk/widgets/label.h>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../support/fds_image.hpp"
#include "../support/rom_archive.hpp"
#include "core/binary_file.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "frontends/nodalkit/gui_controller.hpp"

namespace mapperbus::frontend {

// Observe the production controller and drive its widgets/signals. No test
// reproduces its validation, transaction, cancellation or retry implementation.
struct MapperBusGuiTestAccess {
    static bool ready(MapperBusGuiController& gui) {
        return gui.startup_ready_;
    }
    static auto snapshot(MapperBusGuiController& gui) {
        return gui.actions_->snapshot();
    }
    static auto state(MapperBusGuiController& gui) {
        return gui.session_->emulator().save_state();
    }
    static const auto& bios(MapperBusGuiController& gui) {
        return gui.configuration_.fds_bios_path;
    }
    static void action(MapperBusGuiController& gui, std::string_view action) {
        gui.app_.on_native_app_menu_action().emit(action);
    }
    static auto settings(MapperBusGuiController& gui) {
        return gui.settings_dialog_;
    }
    static auto progress(MapperBusGuiController& gui) {
        return gui.settings_bios_dialog_ ? gui.settings_bios_dialog_ : gui.rom_error_dialog_;
    }
    static auto recovery(MapperBusGuiController& gui) {
        return gui.rom_error_dialog_;
    }
    static auto archive(MapperBusGuiController& gui) {
        return gui.archive_dialog_;
    }
    static auto tabs(MapperBusGuiController& gui) {
        return gui.settings_tabs_;
    }
};

namespace {
using namespace std::chrono_literals;
using Access = MapperBusGuiTestAccess;

struct Gate {
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false;
    bool released = false;
    bool finished = false;

    void block() {
        std::unique_lock lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
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
    bool wait(bool Gate::* flag) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [&] { return this->*flag; });
    }
};

struct Work {
    std::mutex mutex;
    std::vector<app::RomLoadRequest> requests;
    std::atomic<int> writes{0};
    std::shared_ptr<Gate> first = std::make_shared<Gate>();
    std::shared_ptr<Gate> second = std::make_shared<Gate>();

    auto loads() {
        const std::lock_guard lock(mutex);
        return requests;
    }
};

template <typename T, typename Predicate> T* find_widget(nk::Widget& root, Predicate matches) {
    if (auto* widget = dynamic_cast<T*>(&root); widget && matches(*widget))
        return widget;
    for (const auto& child : root.children()) {
        if (auto* found = find_widget<T>(*child, matches))
            return found;
    }
    return nullptr;
}

bool contains(nk::Widget& root, std::string_view text) {
    return find_widget<nk::Label>(
        root, [&](const auto& label) { return label.text().find(text) != std::string_view::npos; });
}

void click(nk::Widget& root, std::string_view label) {
    auto* button = find_widget<nk::Button>(root, [&](const auto& candidate) {
        return candidate.label() == label ||
               (candidate.accessible() && candidate.accessible()->name() == label);
    });
    INFO(label);
    REQUIRE(button);
    REQUIRE(button->is_sensitive());
    button->grab_focus();
    REQUIRE(
        button->handle_key_event({.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Return}));
}

struct Fixture {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-gui-bios-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path current = root / "current.nes";
    const std::filesystem::path valid = root / "valid.bin";
    const std::filesystem::path invalid = root / "invalid.bin";
    const std::filesystem::path first = root / "first.bin";
    const std::filesystem::path second = root / "second.bin";
    std::shared_ptr<Work> work = std::make_shared<Work>();
    nk::EventLoop loop;
    nk::Window window{{.title = "BIOS regressions", .width = 1120, .height = 900}};
    std::vector<nk::Application::OpenFileDialogCallback> pickers;
    std::unique_ptr<MapperBusGuiController> gui;

    Fixture() {
        core::register_builtin_mappers();
        std::filesystem::create_directory(root);
        std::vector<core::Byte> rom(16 + 16384 + 8192);
        const std::array<core::Byte, 6> header{'N', 'E', 'S', 0x1A, 1, 1};
        std::ranges::copy(header, rom.begin());
        rom[16] = 0x4C; // JMP $8000
        rom[18] = 0x80;
        for (int offset : {16384 - 6, 16384 - 4, 16384 - 2})
            rom[16 + offset + 1] = 0x80;
        REQUIRE(core::write_binary_file_atomic(current.string(), rom, "ROM"));
        for (const auto& path : {valid, first, second})
            REQUIRE(core::write_binary_file_atomic(path.string(), tests::make_fds_bios(), "BIOS"));
        REQUIRE(core::write_binary_file_atomic(
            invalid.string(), std::vector<core::Byte>(8191), "BIOS"));
        // The write tripwire is the only possible settings writer. Neither it
        // nor the startup reader touches a real user configuration path.
        gui = std::make_unique<MapperBusGuiController>(
            GuiHost(loop,
                    [this](std::string_view title, const auto& filters, auto callback) {
                        CHECK_FALSE(title.empty());
                        CHECK_FALSE(filters.empty());
                        pickers.push_back(std::move(callback));
                    }),
            window,
            app::write_disk_export,
            [work = work](const auto&, const auto&) -> core::Result<void> {
                ++work->writes;
                return std::unexpected("Unexpected settings write");
            },
            [](const auto&) -> core::Result<app::LoadedConfiguration> {
                return std::unexpected("Settings unavailable for session-only regression");
            },
            [work = work, root = root](const app::RomLoadRequest& original, std::stop_token stop) {
                auto request = original;
                request.persistence_directory = root / "data";
                request.state_storage.directory = root / "states";
                // Isolate the default BIOS lookup, including on machines which
                // have a working BIOS in their normal application data folder.
                if (request.fds_bios_path.empty())
                    request.fds_bios_path = (root / "missing.bin").string();
                {
                    const std::lock_guard lock(work->mutex);
                    work->requests.push_back(request);
                }
                return app::prepare_rom(request, stop);
            },
            [work = work, first = first, second = second](const std::string& path) {
                auto gate = path == first.string()    ? work->first
                            : path == second.string() ? work->second
                                                      : nullptr;
                if (gate)
                    gate->block();
                auto result = app::read_fds_bios(path);
                if (gate)
                    gate->finish();
                return result;
            });
        REQUIRE(nk::Application::instance() == nullptr);
        window.present();
        CHECK(window.native_surface() == nullptr);
        wait([&] { return contains(*window.child(), "Settings unavailable for"); });
        click(*window.child(), "Continue Without Saving");
        wait([&] { return Access::ready(*gui); });
        gui->open_initial_rom(current.string());
        wait([&] { return Access::snapshot(*gui).has_cartridge; });
        action("emu.pause");
        REQUIRE(Access::snapshot(*gui).paused);
    }
    ~Fixture() {
        work->first->release();
        work->second->release();
        // Wait for file reads before removing their owned fixtures.
        for (const auto& gate : {work->first, work->second}) {
            std::unique_lock lock(gate->mutex);
            if (gate->entered)
                (void)gate->changed.wait_for(lock, 5s, [&] { return gate->finished; });
        }
        gui.reset();
        std::error_code error;
        std::filesystem::remove_all(root, error);
        nk::Theme::set_active(nullptr);
    }
    template <typename Predicate> void wait(Predicate ready) {
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        while (!ready() && std::chrono::steady_clock::now() < deadline) {
            loop.poll();
            std::this_thread::yield();
        }
        REQUIRE(ready());
    }
    void drain() {
        loop.post([] {});
        loop.poll();
    }
    void action(std::string_view name) {
        Access::action(*gui, name);
        drain();
    }
    void settings() {
        action("settings.open");
        REQUIRE(Access::settings(*gui));
        Access::tabs(*gui)->set_selected_index(3);
        REQUIRE(contains(*Access::settings(*gui), "Changes are not saved"));
    }
    auto take_picker() {
        REQUIRE(pickers.size() == 1);
        auto callback = std::move(pickers.back());
        pickers.clear();
        return callback;
    }
    void choose(nk::Widget& content, const std::filesystem::path& path) {
        click(content, "Choose FDS BIOS");
        take_picker()(path.string());
    }
    void choose_recovery(const std::filesystem::path& path) {
        auto dialog = Access::recovery(*gui);
        REQUIRE(dialog);
        click(*dialog, "Choose BIOS…");
        drain();
        take_picker()(path.string());
    }
    auto open_disk(std::string_view format) {
        const auto path = root / ("disk." + std::string(format));
        if (format == "fds") {
            REQUIRE(core::write_binary_file_atomic(path.string(), tests::make_fds_image(2), "FDS"));
        } else {
            std::vector<tests::ArchiveMember> members{
                {"folder/disk.fds", tests::make_fds_image(2)}};
            if (format == "zip")
                members.push_back({"other.fds", tests::make_fds_image()});
            tests::write_archive(path, members, format == "7z");
        }
        gui->open_initial_rom(path.string());
        if (format == "zip") {
            wait([&] { return Access::archive(*gui) != nullptr; });
            auto dialog = Access::archive(*gui);
            auto* combo = find_widget<nk::ComboBox>(*dialog, [](const auto&) { return true; });
            REQUIRE(combo);
            CHECK(combo->selected_text() == "folder/disk.fds");
            click(*dialog, "Open");
            drain();
        }
        return path;
    }
};

TEST_CASE("Settings BIOS selection applies in memory and preserves a paused game on rejection",
          "[gui][fds][bios][session-only]") {
    Fixture fixture;
    const auto before = Access::state(*fixture.gui);
    fixture.settings();
    auto settings = Access::settings(*fixture.gui);
    click(*settings, "Choose FDS BIOS");
    fixture.take_picker()(nk::Unexpected(nk::FileDialogError::Cancelled));
    CHECK(Access::bios(*fixture.gui).empty());
    fixture.choose(*settings, fixture.invalid);
    fixture.wait([&] { return contains(*settings, "exactly 8192 bytes"); });
    CHECK(Access::bios(*fixture.gui).empty());
    CHECK(Access::state(*fixture.gui) == before);
    fixture.choose(*settings, fixture.valid);
    fixture.wait([&] { return Access::bios(*fixture.gui) == fixture.valid.string(); });
    CHECK(Access::state(*fixture.gui) == before);
    auto* selected = find_widget<nk::Button>(*settings, [](const auto& button) {
        return button.accessible() && button.accessible()->name() == "Choose FDS BIOS";
    });
    REQUIRE(selected);
    CHECK(selected->accessible()->description() == fixture.valid.string());
    CHECK(fixture.work->loads().size() == 1);
    CHECK(fixture.work->writes == 0);
    settings->close(nk::DialogResponse::Cancel);
    fixture.drain();
    const auto disk = fixture.open_disk("7z");
    fixture.wait([&] { return Access::snapshot(*fixture.gui).is_fds; });
    const auto snapshot = Access::snapshot(*fixture.gui);
    CHECK(snapshot.rom_path == disk.string());
    CHECK(snapshot.rom_member == "folder/disk.fds");
    CHECK(fixture.work->loads().back().fds_bios_path == fixture.valid.string());
    CHECK(fixture.work->writes == 0);
}

TEST_CASE("Session-only recovery validates BIOS and retries the exact raw or archived disk",
          "[gui][fds][bios][session-only][recovery]") {
    for (const auto format : {"fds", "zip", "7z"}) {
        INFO(format);
        Fixture fixture;
        const auto before = Access::state(*fixture.gui);
        const auto disk = fixture.open_disk(format);
        fixture.wait([&] { return Access::recovery(*fixture.gui) != nullptr; });
        auto error = Access::recovery(*fixture.gui);
        const auto member = std::string(format) == "fds" ? "" : "folder/disk.fds";
        CHECK(contains(*error, disk.string()));
        if (std::string(format) != "fds")
            CHECK(contains(*error, member));
        click(*error, "Choose BIOS…");
        fixture.drain();
        fixture.take_picker()(nk::Unexpected(nk::FileDialogError::Cancelled));
        fixture.choose_recovery(fixture.invalid);
        fixture.wait([&] {
            auto dialog = Access::recovery(*fixture.gui);
            return dialog && contains(*dialog, "exactly 8192 bytes");
        });
        CHECK(Access::state(*fixture.gui) == before);
        CHECK(Access::bios(*fixture.gui).empty());
        const auto count = fixture.work->loads().size();
        fixture.choose_recovery(fixture.valid);
        fixture.wait([&] { return Access::snapshot(*fixture.gui).is_fds; });
        const auto snapshot = Access::snapshot(*fixture.gui);
        CHECK(snapshot.rom_path == disk.string());
        CHECK(snapshot.rom_member == member);
        CHECK(fixture.work->loads().size() == count + 1);
        CHECK(fixture.work->loads().back().member == member);
        CHECK(fixture.work->loads().back().fds_bios_path == fixture.valid.string());
        CHECK(fixture.work->writes == 0);
    }
}

TEST_CASE("Closing Settings rejects stale picker callbacks and completed BIOS reads",
          "[gui][fds][bios][cancel][lifetime]") {
    Fixture fixture;
    const auto before = Access::state(*fixture.gui);
    fixture.settings();
    auto settings = Access::settings(*fixture.gui);
    click(*settings, "Choose FDS BIOS");
    auto stale_picker = fixture.take_picker();
    settings->close(nk::DialogResponse::Cancel);
    fixture.drain();
    fixture.settings();
    settings = Access::settings(*fixture.gui);
    click(*settings, "Choose FDS BIOS");
    stale_picker(fixture.valid.string());
    CHECK(Access::bios(*fixture.gui).empty());
    fixture.take_picker()(fixture.first.string());
    REQUIRE(fixture.work->first->wait(&Gate::entered));
    auto stale_dialog = Access::progress(*fixture.gui);
    REQUIRE(stale_dialog);
    settings->close(nk::DialogResponse::Cancel);
    fixture.drain();
    fixture.settings();
    fixture.choose(*Access::settings(*fixture.gui), fixture.second);
    REQUIRE(fixture.work->second->wait(&Gate::entered));
    auto current_dialog = Access::progress(*fixture.gui);
    stale_dialog->on_response().emit(nk::DialogResponse::Cancel);
    fixture.drain();
    REQUIRE(current_dialog->is_presented());
    fixture.work->first->release();
    REQUIRE(fixture.work->first->wait(&Gate::finished));
    fixture.drain();
    CHECK(Access::bios(*fixture.gui).empty());
    fixture.work->second->release();
    fixture.wait([&] { return Access::bios(*fixture.gui) == fixture.second.string(); });
    CHECK(Access::state(*fixture.gui) == before);
    CHECK(fixture.work->loads().size() == 1);
    CHECK(fixture.work->writes == 0);
}

TEST_CASE("Recovery Cancel rejects late reads and obsolete dialog responses",
          "[gui][fds][bios][cancel][recovery]") {
    Fixture fixture;
    const auto before = Access::state(*fixture.gui);
    fixture.open_disk("7z");
    fixture.wait([&] { return Access::recovery(*fixture.gui) != nullptr; });
    fixture.choose_recovery(fixture.first);
    REQUIRE(fixture.work->first->wait(&Gate::entered));
    auto stale_dialog = Access::progress(*fixture.gui);
    click(*stale_dialog, "Cancel");
    fixture.drain();
    fixture.choose_recovery(fixture.second);
    REQUIRE(fixture.work->second->wait(&Gate::entered));
    auto current_dialog = Access::progress(*fixture.gui);
    stale_dialog->on_response().emit(nk::DialogResponse::Cancel);
    fixture.drain();
    REQUIRE(current_dialog->is_presented());
    fixture.work->first->release();
    REQUIRE(fixture.work->first->wait(&Gate::finished));
    fixture.drain();
    CHECK(Access::state(*fixture.gui) == before);
    CHECK(Access::bios(*fixture.gui).empty());
    CHECK(fixture.work->loads().size() == 2);
    fixture.work->second->release();
    fixture.wait([&] { return Access::snapshot(*fixture.gui).is_fds; });
    CHECK(Access::bios(*fixture.gui) == fixture.second.string());
    CHECK(fixture.work->loads().back().member == "folder/disk.fds");
    CHECK(fixture.work->writes == 0);
}
} // namespace
} // namespace mapperbus::frontend
