#include <algorithm>
#include <array>
#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <nk/widgets/label.h>
#include <source_location>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "../support/dcs_rom.hpp"
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
    static auto identity(MapperBusGuiController& gui) {
        return gui.session_->emulator().content_hash();
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
    static auto change(MapperBusGuiController& gui) {
        return gui.configuration_change_dialog_;
    }
    static auto closing(MapperBusGuiController& gui) {
        return gui.close_save_dialog_;
    }
    static bool unsaved(MapperBusGuiController& gui) {
        return gui.configuration_saver_.busy() || gui.configuration_saver_.unsaved() ||
               gui.configuration_change_.has_value();
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
    bool reached(bool Gate::* flag) {
        const std::lock_guard lock(mutex);
        return this->*flag;
    }
    bool wait(bool Gate::* flag) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 5s, [&] { return this->*flag; });
    }
};

struct SaveStep {
    std::string bios;
    std::shared_ptr<Gate> gate;
    bool fail = false;
};

struct Work {
    std::mutex mutex;
    std::vector<app::RomLoadRequest> requests;
    std::atomic<int> writes{0};
    std::deque<SaveStep> save_steps;
    std::vector<std::shared_ptr<Gate>> save_gates;

    void plan_save(const std::filesystem::path& bios,
                   std::shared_ptr<Gate> gate = {},
                   bool fail = false) {
        const std::lock_guard lock(mutex);
        save_steps.push_back({bios.string(), gate, fail});
        if (gate)
            save_gates.push_back(std::move(gate));
    }
    core::Result<void> save(const app::MapperBusConfiguration& configuration,
                            const std::filesystem::path& path) {
        ++writes;
        SaveStep step;
        {
            const std::lock_guard lock(mutex);
            if (!save_steps.empty() && save_steps.front().bios == configuration.fds_bios_path) {
                step = std::move(save_steps.front());
                save_steps.pop_front();
            }
        }
        if (step.gate)
            step.gate->block();
        auto result = step.fail
                          ? core::Result<void>(std::unexpected("Injected settings save failure"))
                          : app::save_mapperbus_configuration_to_file(configuration, path);
        if (step.gate)
            step.gate->finish();
        return result;
    }
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

enum class SettingsMode { SessionOnly, Saved };

struct Fixture {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("mapperbus-gui-bios-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const std::filesystem::path configuration = root / "mapperbus.conf";
    const std::filesystem::path original_bios = root / "original.bin";
    const std::filesystem::path current = root / "current.nes";
    const SettingsMode mode;
    const std::filesystem::path valid = root / "valid.bin";
    const std::filesystem::path invalid = root / "invalid.bin";
    const std::filesystem::path first = root / "first.bin";
    const std::filesystem::path second = root / "second.bin";
    std::shared_ptr<Work> work = std::make_shared<Work>();
    nk::EventLoop loop;
    nk::Window window{{.title = "BIOS regressions", .width = 1120, .height = 900}};
    std::vector<nk::Application::OpenFileDialogCallback> pickers;
    std::unique_ptr<MapperBusGuiController> gui;

    explicit Fixture(SettingsMode settings_mode = SettingsMode::SessionOnly) : mode(settings_mode) {
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
        for (const auto& path : {original_bios, valid, first, second})
            REQUIRE(core::write_binary_file_atomic(path.string(), tests::make_fds_bios(), "BIOS"));
        REQUIRE(core::write_binary_file_atomic(
            invalid.string(), std::vector<core::Byte>(8191), "BIOS"));
        app::MapperBusConfiguration initial;
        initial.fds_bios_path = original_bios.string();
        initial.frontend.settings_page_index = 3;
        initial.save_states.directory = root / "states";
        REQUIRE(app::save_mapperbus_configuration_to_file(initial, configuration));
        // Both modes redirect all configuration I/O to the fixture. Session-only
        // mode keeps the original write tripwire; saved mode uses the real atomic
        // writer with optional failure/gate steps for BIOS proposals.
        gui = std::make_unique<MapperBusGuiController>(
            GuiHost(loop,
                    [this](std::string_view title, const auto& filters, auto callback) {
                        CHECK_FALSE(title.empty());
                        CHECK_FALSE(filters.empty());
                        pickers.push_back(std::move(callback));
                    }),
            window,
            app::write_disk_export,
            [work = work, mode = mode, path = configuration](const auto& proposal,
                                                             const auto&) -> core::Result<void> {
                if (mode == SettingsMode::Saved)
                    return work->save(proposal, path);
                ++work->writes;
                return std::unexpected("Unexpected settings write");
            },
            [mode = mode,
             path = configuration](const auto&) -> core::Result<app::LoadedConfiguration> {
                if (mode == SettingsMode::Saved)
                    return app::read_startup_configuration({path, {}});
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
        if (mode == SettingsMode::SessionOnly) {
            wait([&] { return contains(*window.child(), "Settings unavailable for"); });
            click(*window.child(), "Continue Without Saving");
        }
        wait([&] { return Access::ready(*gui); });
        gui->open_initial_rom(current.string());
        wait([&] { return Access::snapshot(*gui).has_cartridge; });
        action("emu.pause");
        REQUIRE(Access::snapshot(*gui).paused);
        if (mode == SettingsMode::Saved)
            settle_saves();
    }
    ~Fixture() {
        auto gates = work->save_gates;
        gates.push_back(work->first);
        gates.push_back(work->second);
        for (const auto& gate : gates)
            gate->release();
        // Release blocked I/O before retiring the controller and its fixtures.
        for (const auto& gate : gates) {
            std::unique_lock lock(gate->mutex);
            if (gate->entered)
                (void)gate->changed.wait_for(lock, 5s, [&] { return gate->finished; });
        }
        gui.reset();
        std::error_code error;
        std::filesystem::remove_all(root, error);
        nk::Theme::set_active(nullptr);
    }
    template <typename Predicate>
    void wait(Predicate ready, std::source_location location = std::source_location::current()) {
        INFO(location.file_name() << ":" << location.line());
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
        if (mode == SettingsMode::Saved)
            settle_saves();
        REQUIRE(contains(*Access::settings(*gui),
                         mode == SettingsMode::Saved ? "Saved automatically"
                                                     : "Changes are not saved"));
    }
    void settle_saves() {
        wait([&] { return !Access::unsaved(*gui); });
    }
    auto saved_bytes() const {
        auto bytes = core::read_binary_file_capped(
            configuration.string(), app::kMaxConfigurationBytes, "test settings");
        REQUIRE(bytes);
        return *bytes;
    }
    auto saved_bios() const {
        const auto bytes = saved_bytes();
        return app::parse_mapperbus_configuration(std::string(bytes.begin(), bytes.end()))
            .fds_bios_path;
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

TEST_CASE("Video settings select and persist Hybrid while keeping the game paused",
          "[gui][hybrid][saved-settings]") {
    Fixture fixture(SettingsMode::Saved);
    fixture.settings();
    Access::tabs(*fixture.gui)->set_selected_index(1);
    fixture.settle_saves();
    auto* combo = find_widget<nk::ComboBox>(*Access::settings(*fixture.gui), [](const auto& item) {
        return item.item_count() == 4 && item.item(0) == "Automatic";
    });
    REQUIRE(combo);
    CHECK(combo->item(3) == "Hybrid");
    combo->set_selected_index(3);
    fixture.settle_saves();
    CHECK(combo->selected_text() == "Hybrid");
    CHECK(Access::snapshot(*fixture.gui).region == core::Region::Dendy);
    CHECK(Access::snapshot(*fixture.gui).paused);
    const auto bytes = fixture.saved_bytes();
    CHECK(std::string(bytes.begin(), bytes.end()).find("region=hybrid\n") != std::string::npos);
    combo->set_selected_index(0);
    fixture.settle_saves();
    CHECK(Access::snapshot(*fixture.gui).region == core::Region::NTSC);
}

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
TEST_CASE("Saved Settings applies a BIOS only after the atomic write and matching UI receipt",
          "[gui][fds][bios][saved-settings]") {
    Fixture fixture(SettingsMode::Saved);
    const auto before = Access::state(*fixture.gui);
    fixture.settings();
    const auto saved_before = fixture.saved_bytes();
    auto gate = std::make_shared<Gate>();
    fixture.work->plan_save(fixture.valid, gate);
    fixture.choose(*Access::settings(*fixture.gui), fixture.valid);
    fixture.wait([&] {
        auto dialog = Access::change(*fixture.gui);
        return dialog && contains(*dialog, "Saving settings before applying");
    });
    fixture.wait([&] { return gate->reached(&Gate::entered); });
    CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
    CHECK(fixture.saved_bytes() == saved_before);
    CHECK(Access::state(*fixture.gui) == before);
    CHECK(fixture.work->loads().size() == 1);
    fixture.window.dispatch_key_event(
        {.type = nk::KeyEvent::Type::Press, .key = nk::KeyCode::Escape});
    CHECK(Access::change(*fixture.gui)->is_presented());
    gate->release();
    REQUIRE(gate->wait(&Gate::finished));
    // The worker has written the new file; no UI poll has accepted its receipt.
    CHECK(fixture.saved_bios() == fixture.valid.string());
    CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
    fixture.settle_saves();
    CHECK(Access::bios(*fixture.gui) == fixture.valid.string());
    CHECK(Access::state(*fixture.gui) == before);
    CHECK(fixture.work->loads().size() == 1);
    auto* selected =
        find_widget<nk::Button>(*Access::settings(*fixture.gui), [](const auto& button) {
            return button.accessible() && button.accessible()->name() == "Choose FDS BIOS";
        });
    REQUIRE(selected);
    CHECK(selected->accessible()->description() == fixture.valid.string());
}

TEST_CASE("Failed saved Settings BIOS proposals retain the original selection for Keep or Retry",
          "[gui][fds][bios][saved-settings][failure]") {
    for (bool retry : {false, true}) {
        INFO(retry);
        Fixture fixture(SettingsMode::Saved);
        const auto before = Access::state(*fixture.gui);
        fixture.settings();
        const auto saved_before = fixture.saved_bytes();
        fixture.work->plan_save(fixture.valid, {}, true);
        fixture.choose(*Access::settings(*fixture.gui), fixture.valid);
        fixture.wait([&] {
            auto dialog = Access::change(*fixture.gui);
            return dialog && contains(*dialog, "Injected settings save failure");
        });
        CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
        CHECK(fixture.saved_bytes() == saved_before);
        CHECK(Access::state(*fixture.gui) == before);
        if (retry) {
            auto gate = std::make_shared<Gate>();
            fixture.work->plan_save(fixture.valid, gate);
            click(*Access::change(*fixture.gui), "Retry");
            fixture.drain();
            fixture.wait([&] { return gate->reached(&Gate::entered); });
            CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
            CHECK(fixture.saved_bytes() == saved_before);
            gate->release();
            REQUIRE(gate->wait(&Gate::finished));
            fixture.settle_saves();
            CHECK(Access::bios(*fixture.gui) == fixture.valid.string());
            CHECK(fixture.saved_bios() == fixture.valid.string());
        } else {
            click(*Access::change(*fixture.gui), "Keep Current Settings");
            fixture.drain();
            fixture.settle_saves();
            CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
            CHECK(fixture.saved_bios() == fixture.original_bios.string());
            CHECK(contains(*Access::settings(*fixture.gui), "The previous BIOS remains selected"));
        }
        CHECK(Access::state(*fixture.gui) == before);
        CHECK(Access::snapshot(*fixture.gui).paused);
        CHECK(fixture.work->loads().size() == 1);
    }
}

TEST_CASE("Saved BIOS recovery retries its exact member only after a successful settings save",
          "[gui][fds][bios][saved-settings][recovery]") {
    for (const auto format : {"fds", "zip", "7z"}) {
        for (bool retry : {false, true}) {
            INFO(format);
            INFO(retry);
            Fixture fixture(SettingsMode::Saved);
            const auto before = Access::state(*fixture.gui);
            REQUIRE(std::filesystem::remove(fixture.original_bios));
            const auto disk = fixture.open_disk(format);
            fixture.wait([&] { return Access::recovery(*fixture.gui) != nullptr; });
            fixture.settle_saves();
            const auto saved_before = fixture.saved_bytes();
            const auto count = fixture.work->loads().size();
            const auto member = std::string(format) == "fds" ? "" : "folder/disk.fds";
            fixture.work->plan_save(fixture.valid, {}, true);
            fixture.choose_recovery(fixture.valid);
            fixture.wait([&] {
                auto dialog = Access::change(*fixture.gui);
                return dialog && contains(*dialog, "Injected settings save failure");
            });
            CHECK(fixture.saved_bytes() == saved_before);
            CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
            CHECK(Access::state(*fixture.gui) == before);
            CHECK(fixture.work->loads().size() == count);
            auto gate = std::make_shared<Gate>();
            fixture.work->plan_save(fixture.valid, gate);
            if (retry) {
                click(*Access::change(*fixture.gui), "Retry");
                fixture.drain();
            } else {
                click(*Access::change(*fixture.gui), "Keep Current Settings");
                fixture.drain();
                fixture.settle_saves();
                auto recovery = Access::recovery(*fixture.gui);
                REQUIRE(recovery);
                CHECK(contains(*recovery, disk.string()));
                if (std::string(format) != "fds")
                    CHECK(contains(*recovery, member));
                CHECK(fixture.saved_bios() == fixture.original_bios.string());
                CHECK(fixture.work->loads().size() == count);
                fixture.choose_recovery(fixture.valid);
                fixture.wait([&] { return Access::change(*fixture.gui) != nullptr; });
            }
            fixture.wait([&] { return gate->reached(&Gate::entered); });
            CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
            CHECK(fixture.saved_bios() == fixture.original_bios.string());
            CHECK(fixture.work->loads().size() == count);
            CHECK(Access::state(*fixture.gui) == before);
            gate->release();
            REQUIRE(gate->wait(&Gate::finished));
            CHECK(fixture.saved_bios() == fixture.valid.string());
            CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
            CHECK(fixture.work->loads().size() == count);
            fixture.wait([&] { return Access::snapshot(*fixture.gui).is_fds; });
            fixture.settle_saves();
            CHECK(Access::snapshot(*fixture.gui).rom_path == disk.string());
            CHECK(Access::snapshot(*fixture.gui).rom_member == member);
            CHECK(fixture.work->loads().size() == count + 1);
            CHECK(fixture.work->loads().back().member == member);
            CHECK(fixture.work->loads().back().fds_bios_path == fixture.valid.string());
            CHECK(Access::bios(*fixture.gui) == fixture.valid.string());
            CHECK(fixture.saved_bios() == fixture.valid.string());
        }
    }
}

TEST_CASE("Quit drains a pending BIOS settings save and does not launch a retired recovery",
          "[gui][fds][bios][saved-settings][quit]") {
    for (bool recovery : {false, true}) {
        INFO(recovery);
        Fixture fixture(SettingsMode::Saved);
        const auto before = Access::state(*fixture.gui);
        if (recovery) {
            REQUIRE(std::filesystem::remove(fixture.original_bios));
            fixture.open_disk("7z");
            fixture.wait([&] { return Access::recovery(*fixture.gui) != nullptr; });
            fixture.settle_saves();
        } else {
            fixture.settings();
        }
        const auto saved_before = fixture.saved_bytes();
        const auto count = fixture.work->loads().size();
        auto gate = std::make_shared<Gate>();
        fixture.work->plan_save(fixture.valid, gate);
        if (recovery)
            fixture.choose_recovery(fixture.valid);
        else
            fixture.choose(*Access::settings(*fixture.gui), fixture.valid);
        fixture.wait([&] { return Access::change(*fixture.gui) != nullptr; });
        fixture.wait([&] { return gate->reached(&Gate::entered); });
        int closed = 0;
        (void)fixture.window.on_close_requested().connect([&] { ++closed; });
        fixture.action("file.quit");
        fixture.wait([&] {
            auto dialog = Access::closing(*fixture.gui);
            return dialog && contains(*dialog, "Saving settings before quitting");
        });
        CHECK(fixture.window.is_visible());
        CHECK(closed == 0);
        CHECK(fixture.saved_bytes() == saved_before);
        CHECK(Access::bios(*fixture.gui) == fixture.original_bios.string());
        CHECK(Access::state(*fixture.gui) == before);
        CHECK(fixture.work->loads().size() == count);
        gate->release();
        REQUIRE(gate->wait(&Gate::finished));
        fixture.wait([&] { return !fixture.window.is_visible(); });
        CHECK(closed == 1);
        CHECK(fixture.saved_bios() == fixture.valid.string());
        CHECK(Access::bios(*fixture.gui) == fixture.valid.string());
        CHECK(fixture.work->loads().size() == count);
    }
}

TEST_CASE("GUI DCS startup requests preserve option identity through failure and retry",
          "[gui][dcs]") {
    Fixture fixture;
    const auto base = fixture.root / "dcs.nes";
    const auto option = fixture.root / "option.bin";
    const auto image = tests::make_dcs_base_rom();
    const std::vector<core::Byte> chip(0x4000U, 0x5AU);
    REQUIRE(core::write_binary_file_atomic(base.string(), image, "DCS base"));
    const auto before = Access::state(*fixture.gui);
    fixture.gui->open_initial_rom(base.string(), option.string());
    fixture.wait([&] { return Access::recovery(*fixture.gui) != nullptr; });
    REQUIRE(contains(*Access::recovery(*fixture.gui), "DCS option ROM"));
    REQUIRE(Access::state(*fixture.gui) == before);
    REQUIRE(core::write_binary_file_atomic(option.string(), chip, "DCS option"));
    const auto expected = core::Cartridge::from_data(image, {}, chip);
    REQUIRE(expected);
    click(*Access::recovery(*fixture.gui), "Retry");
    fixture.wait([&] { return Access::identity(*fixture.gui) == expected->content_hash(); });
    {
        const std::lock_guard lock(fixture.work->mutex);
        REQUIRE(fixture.work->requests.back().dcs_option_path == option.string());
    }
    fixture.action("emu.pause");
    REQUIRE(Access::snapshot(*fixture.gui).paused);
}

} // namespace
} // namespace mapperbus::frontend
