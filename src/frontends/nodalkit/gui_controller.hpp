#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/runtime/event_loop.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/context_menu.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>
#include <nk/widgets/toast_overlay.h>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "app/async_configuration_load.hpp"
#include "app/async_configuration_save.hpp"
#include "app/async_disk_export.hpp"
#include "app/async_rom_loader.hpp"
#include "app/async_state_io.hpp"
#include "app/configuration.hpp"
#include "app/emulation_session.hpp"
#include "app/session_actions.hpp"
#include "app/state_storage_tasks.hpp"
#include "frontends/nodalkit/gui_audio_backend.hpp"
#include "frontends/nodalkit/gui_widgets.hpp"
#include "frontends/nodalkit/input_monitor_dialog.hpp"
#include "frontends/nodalkit/nodalkit_input.hpp"
#include "platform/video/upscaler.hpp"

#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
#include "frontends/sdl3/gamepad_assignment.hpp"
#endif

namespace nk {
class Headerbar;
class Label;
class ScrollArea;
class Spinner;
} // namespace nk

namespace mapperbus::frontend {

class MapperBusGuiController {
  public:
    MapperBusGuiController(
        nk::Application& app,
        nk::Window& window,
        app::AsyncDiskExport::Write disk_export_write = app::write_disk_export,
        app::AsyncConfigurationSave::Write configuration_write =
            app::save_mapperbus_configuration_to_file,
        app::AsyncConfigurationLoad::Read configuration_read = app::read_startup_configuration,
        app::AsyncRomLoader::CancellablePrepare rom_prepare = app::prepare_rom);

    void open_initial_rom(std::string_view rom_path);

    enum class PreviewScaleOption {
        PixelPerfect,
        Smooth,
        Xbrz2x,
        Fsr2x,
    };

  private:
    bool configure_session();
    void start_configuration_load();
    void poll_configuration_load();
    void show_startup_view();
    void finish_startup(app::LoadedConfiguration loaded, bool allow_saving);
    enum class SettingsPage {
        Input,
        Video,
        Audio,
        General,
    };

    enum class RebindDevice {
        Keyboard,
        Gamepad,
    };

    struct PendingInputRebind {
        RebindDevice device = RebindDevice::Keyboard;
        platform::InputAction button = platform::InputAction::A;
        std::chrono::steady_clock::time_point started_at{};
        std::vector<platform::GamepadControl> initially_pressed_gamepad_controls{};
    };

    void build_ui();
    void wire_ui();
    [[nodiscard]] std::shared_ptr<nk::Widget> build_welcome_view();
    void refresh_welcome_view();
    void clear_preview();
    void refresh_preview();
    void refresh_ui();
    void set_message(std::string message);
    void rebuild_header_menu();
    void focus_game_surface();
    void toggle_fullscreen();
    void update_fullscreen_chrome();
    void update_native_menu();
    void update_native_toolbar();
    void browse_for_rom(bool recovering = false);
    void attempt_open(std::string rom_path, std::string member = {});
    void start_rom_read();
    void poll_rom_load();
    void present_rom_loading(bool saving);
    void dismiss_rom_loading();
    void abandon_rom_load(std::string message);
    void show_rom_load_error(std::string error);
    void present_rom_load_error();
    void dismiss_rom_load_error(bool keep_request = false);
    void finish_rom_open(std::string rom_path, std::string member);
    void choose_archive_rom(std::string rom_path, std::vector<std::string> members);
    void open_disk_controls();
    void choose_disk_export();
    void start_disk_export(std::string path);
    void poll_disk_export();
    void dismiss_disk_export_dialog();
    void perform_disk_action(std::string_view action);
    void toggle_pause();
    void step_frame();
    void reset_session();
    void power_cycle_session();
    void select_state_slot(int slot);
    void choose_state_slot();
    void open_states_folder();
    void request_state_slot_scan();
    void poll_state_storage();
    void update_state_storage_controls();
    [[nodiscard]] bool machine_worker_active() const;
    void begin_state_operation(int slot, bool save);
    void poll_state_operation();
    void finish_state_operation(std::string message, bool success);
    void dismiss_state_operation();
    void save_session_state(int slot);
    void load_session_state(int slot);
    void close_current_rom();
    void request_session_close(bool quit);
    void poll_session_close();
    void start_close_save();
    void show_close_error(std::string error);
    void dismiss_close_dialog();
    void finish_close_rom();
    void apply_region_preference();
    void confirm_clear_recents();
    void open_dropped_files(const std::vector<std::filesystem::path>& files);
    [[nodiscard]] std::shared_ptr<nk::Widget> build_settings_dialog_shell();
    [[nodiscard]] std::shared_ptr<nk::Widget> build_settings_page_content();
    [[nodiscard]] std::shared_ptr<nk::Widget> build_settings_footer_content();
    void open_settings_dialog();
    void close_settings();
    [[nodiscard]] nk::Window& settings_host_window();
    void refresh_settings_dialog_sections();
    void handle_menu_action(std::string_view action);
    void set_preview_scale_option(PreviewScaleOption option);
    void update_preview_surface(const std::uint32_t* data, int width, int height);
    void update_configuration_from_state();
    void save_configuration_state();
    void poll_configuration_saves();
    void refresh_configuration_save_status();
    void retry_configuration_save();
    void begin_configuration_change(app::MapperBusConfiguration proposed,
                                    std::function<void()> apply,
                                    std::string success_message = {});
    void show_configuration_change(bool failed);
    void dismiss_configuration_change();
    void discard_configuration_change();
    void change_controller_hardware(const app::ControllerHardwareOverrides& change);
    void remember_game_controller_hardware(bool remember);
    void save_controller_hardware_change(app::MapperBusConfiguration proposed,
                                         const app::ControllerHardwareSettings& settings,
                                         std::string success_message = {});
    void open_rebind_dialog(RebindDevice device, platform::InputAction button);
    void close_rebind_dialog();
    void cancel_rebind_dialog();
    void poll_pending_rebind();
    void remember_game_assignments(bool remember);
    void open_gamepad_assignment();
    void poll_gamepad_assignment();
    void cancel_gamepad_assignment();
    void apply_keyboard_rebind(platform::InputAction button, nk::KeyCode key);
    void apply_gamepad_rebind(platform::InputAction button, platform::GamepadControl control);
    void apply_audio_settings_change(std::string message);
    void update_input_test_status();
    void open_input_monitor();
    void close_input_monitor();
    void update_input_monitor();
    void update_settings_save_status(std::string text);

    /// True while a sheet/dialog (settings, rebind, archive, state slot) is presented. Session
    /// hotkeys must not dispatch then — e.g. the rebind sheet captures P/R as
    /// bindings and they must not also toggle pause/reset.
    [[nodiscard]] bool modal_dialog_presented() const;

    [[nodiscard]] std::string input_status_text() const;
    [[nodiscard]] std::string video_features_text() const;
    [[nodiscard]] std::string input_test_status_text() const;

    nk::Application& app_;
    nk::Window& window_;

    GuiAudioBackend* audio_backend_ = nullptr;
    NodalKitInput* input_backend_ = nullptr;
    app::MapperBusConfiguration configuration_;
    app::AsyncConfigurationLoad configuration_loader_;
    bool startup_ready_ = false;
    bool startup_loading_ = false;
    bool configuration_saves_enabled_ = true;
    std::string startup_error_;
    std::optional<std::pair<std::string, std::string>> initial_rom_request_;
    app::AsyncConfigurationSave configuration_saver_;
    struct PendingConfigurationChange {
        std::uint64_t revision;
        app::MapperBusConfiguration proposed;
        std::function<void()> apply;
        std::string success_message;
    };
    std::optional<PendingConfigurationChange> configuration_change_;
    bool configuration_save_deferred_ = false;
    std::shared_ptr<nk::Dialog> configuration_change_dialog_;
    std::unique_ptr<app::EmulationSession> session_;
    std::unique_ptr<app::SessionActions> actions_;
    core::FrameBuffer blank_frame_{};
    app::AsyncRomLoader rom_loader_;
    std::optional<app::RomLoadRequest> loading_request_;
    std::shared_ptr<nk::Dialog> loading_dialog_;
    std::weak_ptr<nk::Label> loading_status_;
    bool paused_before_rom_load_ = false;
    struct FailedRomLoad {
        std::string path;
        std::string member;
        std::string error;
        std::string chooser_error;
        bool choosing_file = false;
    };
    std::shared_ptr<FailedRomLoad> failed_rom_load_;
    std::shared_ptr<nk::Dialog> rom_error_dialog_;
    struct PendingStateOperation {
        int slot;
        bool save;
        bool previously_paused;
        std::string path;
    };
    app::AsyncStateIO state_io_;
    std::optional<PendingStateOperation> pending_state_operation_;
    std::shared_ptr<nk::Dialog> state_operation_dialog_;
    std::optional<bool> close_intent_; // false: close ROM, true: quit application
    bool close_ready_ = false;
    bool close_captured_ = false;
    bool close_previously_paused_ = false;
    bool close_saving_ = false;
    bool close_authorized_ = false;
    bool close_data_saved_ = false;
    bool close_configuration_submitted_ = false;
    bool close_waiting_configuration_ = false;
    app::BackgroundTask<void> close_save_task_;
    std::shared_ptr<nk::Dialog> close_save_dialog_;
    std::string announced_disk_save_error_;
    std::weak_ptr<nk::Label> disk_save_status_;
    std::weak_ptr<nk::Button> disk_save_retry_;
    app::AsyncDiskExport disk_export_;
    bool choosing_disk_export_ = false;
    std::string disk_export_path_;
    std::shared_ptr<nk::Dialog> disk_export_dialog_;
    std::weak_ptr<nk::Label> disk_export_status_;
    // Native chooser callbacks may outlive the controller during shutdown.
    std::shared_ptr<int> disk_export_lifetime_ = std::make_shared<int>(0);

    std::shared_ptr<Box> root_;
    std::shared_ptr<ContentSlot> main_slot_;
    std::shared_ptr<nk::Widget> welcome_view_;
    std::shared_ptr<nk::Button> welcome_open_button_;
    std::shared_ptr<nk::Button> header_open_button_;
    bool welcome_active_ = true;
    std::shared_ptr<nk::Headerbar> headerbar_;
    std::shared_ptr<SessionIconButton> pause_button_;
    std::shared_ptr<SessionIconButton> fullscreen_button_;
    std::shared_ptr<nk::Button> header_menu_button_;
    /// Region the loaded cartridge selected, before a manual override.
    std::optional<core::Region> detected_region_;
    int active_state_slot_ = 0;
    int settings_input_player_ = 0;
    std::shared_ptr<nk::Dialog> state_slot_dialog_;
    std::shared_ptr<nk::Dialog> archive_dialog_;
    std::shared_ptr<nk::Dialog> fds_dialog_;
    std::weak_ptr<nk::ComboBox> state_slot_control_;
    app::StateStorageTasks storage_tasks_;
    std::weak_ptr<nk::Label> state_slots_status_;
    std::weak_ptr<nk::Label> state_location_status_;
    std::weak_ptr<nk::Spinner> state_storage_spinner_;
    std::weak_ptr<nk::ComboBox> state_storage_mode_;
    std::weak_ptr<nk::TextField> state_storage_directory_;
    std::weak_ptr<nk::Button> state_storage_apply_;
    std::weak_ptr<nk::Button> state_storage_open_;
    std::shared_ptr<nk::ContextMenu> header_menu_;
    std::vector<std::string> header_menu_actions_;
    std::shared_ptr<PreviewCanvas> preview_;
    std::shared_ptr<nk::ToastOverlay> toast_overlay_;
    std::shared_ptr<nk::StatusBar> status_bar_;
    std::shared_ptr<nk::Dialog> settings_dialog_;
    std::unique_ptr<nk::Window> settings_window_;
    std::shared_ptr<nk::Dialog> rebind_dialog_;
    std::shared_ptr<InputMonitorDialog> input_monitor_dialog_;
    std::array<std::shared_ptr<nk::Label>, platform::kInputProfileCount> input_monitor_labels_{};
    bool resume_after_input_monitor_ = false;
    std::chrono::steady_clock::time_point input_monitor_last_poll_{};
    std::shared_ptr<nk::Dialog> gamepad_assignment_dialog_;
    std::shared_ptr<nk::Label> gamepad_assignment_status_;
    std::array<std::shared_ptr<nk::Label>, 4> gamepad_assignment_labels_{};
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
    std::unique_ptr<Sdl3GamepadAssignment> gamepad_assignment_;
    std::vector<int> gamepad_assignment_profiles_;
    bool resume_after_gamepad_assignment_ = false;
    std::chrono::steady_clock::time_point gamepad_assignment_last_poll_{};
#endif
    std::shared_ptr<nk::SegmentedControl> settings_tabs_;
    std::shared_ptr<ContentSlot> settings_page_slot_;
    std::shared_ptr<ContentSlot> settings_footer_slot_;
    std::shared_ptr<nk::ScrollArea> settings_scroll_area_;
    std::shared_ptr<SecondaryText> input_test_label_;
    std::shared_ptr<nk::Label> live_microphone_status_;
    std::shared_ptr<nk::Label> settings_save_label_;
    std::shared_ptr<nk::Button> settings_save_retry_;
    std::string settings_save_status_ = "Saved automatically";
    SettingsPage settings_page_ = SettingsPage::Input;
    SettingsPage settings_built_page_ = SettingsPage::Input;
    PreviewScaleOption preview_scale_option_ = PreviewScaleOption::PixelPerfect;
    std::optional<PendingInputRebind> pending_rebind_;
    std::unique_ptr<platform::Upscaler> preview_upscaler_;
    std::vector<std::uint32_t> preview_staging_pixels_;
    std::chrono::steady_clock::time_point last_tick_time_ = std::chrono::steady_clock::now();
    std::chrono::nanoseconds frame_accumulator_{0};
    nk::CallbackHandle tick_handle_{};
    bool announced_fullscreen_ = false;
    std::optional<int> native_menu_slot_;
    std::optional<int> native_toolbar_state_;
};

} // namespace mapperbus::frontend
