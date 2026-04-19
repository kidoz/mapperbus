#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/runtime/event_loop.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/segmented_control.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>
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

namespace nk {
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
    };

    enum class RebindDevice {
        Keyboard,
        Gamepad,
    };

    struct PendingInputRebind {
        RebindDevice device = RebindDevice::Keyboard;
        core::Button button = core::Button::A;
        std::chrono::steady_clock::time_point started_at{};
    };

    void build_ui();
    void wire_ui();
    void clear_preview();
    void refresh_preview();
    void refresh_ui();
    void set_message(std::string message);
    void focus_game_surface();
    void browse_for_rom();
    void attempt_open(std::string rom_path);
    void toggle_pause();
    void step_frame();
    void reset_session();
    void power_cycle_session();
    void close_current_rom();
    [[nodiscard]] std::shared_ptr<nk::Widget> build_settings_dialog_shell();
    [[nodiscard]] std::shared_ptr<nk::Widget> build_settings_page_content();
    [[nodiscard]] std::shared_ptr<nk::Widget> build_settings_footer_content();
    void open_settings_dialog();
    void refresh_settings_dialog_sections();
    void handle_menu_action(std::string_view action);
    void set_preview_scale_option(PreviewScaleOption option);
    void update_preview_surface(const std::uint32_t* data, int width, int height);
    void update_configuration_from_state();
    void save_configuration_state();
    void open_rebind_dialog(RebindDevice device, core::Button button);
    void close_rebind_dialog();
    void cancel_rebind_dialog();
    void poll_pending_rebind();
    void apply_keyboard_rebind(core::Button button, nk::KeyCode key);
    void apply_gamepad_rebind(core::Button button, platform::GamepadControl control);
    void update_input_test_status();
    void update_settings_save_status(std::string text);

    [[nodiscard]] std::string input_status_text() const;
    [[nodiscard]] std::string gameplay_hint_text() const;
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
    std::shared_ptr<nk::MenuBar> menu_bar_;
    std::shared_ptr<PreviewCanvas> preview_;
    std::shared_ptr<nk::StatusBar> status_bar_;
    std::shared_ptr<nk::Dialog> settings_dialog_;
    std::shared_ptr<nk::Dialog> rebind_dialog_;
    std::shared_ptr<nk::SegmentedControl> settings_tabs_;
    std::shared_ptr<ContentSlot> settings_page_slot_;
    std::shared_ptr<ContentSlot> settings_footer_slot_;
    std::shared_ptr<nk::ScrollArea> settings_scroll_area_;
    std::shared_ptr<SecondaryText> input_test_label_;
    std::shared_ptr<SecondaryText> settings_save_label_;
    std::string settings_save_status_ = "Saved automatically";
    SettingsPage settings_page_ = SettingsPage::Input;
    PreviewScaleOption preview_scale_option_ = PreviewScaleOption::PixelPerfect;
    std::optional<PendingInputRebind> pending_rebind_;
    std::unique_ptr<platform::Upscaler> preview_upscaler_;
    std::vector<std::uint32_t> preview_staging_pixels_;
    std::chrono::steady_clock::time_point last_tick_time_ = std::chrono::steady_clock::now();
    std::chrono::nanoseconds frame_accumulator_{0};
    nk::CallbackHandle tick_handle_{};
};

} // namespace mapperbus::frontend
