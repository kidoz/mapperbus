#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
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
#include <vector>

#include "app/configuration.hpp"
#include "app/emulation_session.hpp"
#include "app/session_actions.hpp"
#include "frontends/nodalkit/gui_audio_backend.hpp"
#include "frontends/nodalkit/gui_widgets.hpp"
#include "frontends/nodalkit/nodalkit_input.hpp"
#include "platform/video/upscaler.hpp"

#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
#include "frontends/sdl3/gamepad_assignment.hpp"
#endif

namespace nk {
class Headerbar;
class Label;
class ScrollArea;
} // namespace nk

namespace mapperbus::frontend {

class MapperBusGuiController {
  public:
    MapperBusGuiController(nk::Application& app, nk::Window& window);

    void open_initial_rom(std::string_view rom_path);

    enum class PreviewScaleOption {
        PixelPerfect,
        Smooth,
        Xbrz2x,
        Fsr2x,
    };

  private:
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
    void browse_for_rom();
    void attempt_open(std::string rom_path, std::string member = {});
    void choose_archive_rom(std::string rom_path, std::vector<std::string> members);
    void open_disk_controls();
    void perform_disk_action(std::string_view action);
    void toggle_pause();
    void step_frame();
    void reset_session();
    void power_cycle_session();
    void select_state_slot(int slot);
    void choose_state_slot();
    void open_states_folder();
    void save_session_state(int slot);
    void load_session_state(int slot);
    void close_current_rom();
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
    void change_controller_hardware(const app::ControllerHardwareOverrides& change);
    void remember_game_controller_hardware(bool remember);
    bool save_controller_hardware_change(app::MapperBusConfiguration proposed,
                                         const app::ControllerHardwareSettings& settings);
    void open_rebind_dialog(RebindDevice device, platform::InputAction button);
    void close_rebind_dialog();
    void cancel_rebind_dialog();
    void poll_pending_rebind();
    void open_gamepad_assignment();
    void poll_gamepad_assignment();
    void cancel_gamepad_assignment();
    void apply_keyboard_rebind(platform::InputAction button, nk::KeyCode key);
    void apply_gamepad_rebind(platform::InputAction button, platform::GamepadControl control);
    void apply_audio_settings_change(std::string message);
    void update_input_test_status();
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
    std::unique_ptr<app::EmulationSession> session_;
    std::unique_ptr<app::SessionActions> actions_;
    core::FrameBuffer blank_frame_{};

    std::shared_ptr<Box> root_;
    std::shared_ptr<ContentSlot> main_slot_;
    std::shared_ptr<nk::Widget> welcome_view_;
    std::shared_ptr<nk::Button> welcome_open_button_;
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
    std::shared_ptr<nk::ContextMenu> header_menu_;
    std::vector<std::string> header_menu_actions_;
    std::shared_ptr<PreviewCanvas> preview_;
    std::shared_ptr<nk::ToastOverlay> toast_overlay_;
    std::shared_ptr<nk::StatusBar> status_bar_;
    std::shared_ptr<nk::Dialog> settings_dialog_;
    std::unique_ptr<nk::Window> settings_window_;
    std::shared_ptr<nk::Dialog> rebind_dialog_;
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
