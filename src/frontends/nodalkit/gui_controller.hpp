#pragma once

#include <memory>
#include <nk/platform/application.h>
#include <nk/platform/window.h>
#include <nk/runtime/event_loop.h>
#include <nk/widgets/button.h>
#include <nk/widgets/combo_box.h>
#include <nk/widgets/dialog.h>
#include <nk/widgets/menu_bar.h>
#include <nk/widgets/status_bar.h>
#include <nk/widgets/text_field.h>
#include <string>
#include <string_view>

#include "app/emulation_session.hpp"
#include "app/session_actions.hpp"
#include "frontends/nodalkit/gui_audio_backend.hpp"
#include "frontends/nodalkit/gui_widgets.hpp"
#include "frontends/nodalkit/nodalkit_input.hpp"

namespace mapperbus::frontend {

class MapperBusGuiController {
  public:
    MapperBusGuiController(nk::Application& app, nk::Window& window);

    void open_initial_rom(std::string_view rom_path);

  private:
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
    void open_input_dialog();
    void handle_menu_action(std::string_view action);

    [[nodiscard]] std::string input_status_text() const;
    [[nodiscard]] std::string gameplay_hint_text() const;

    nk::Application& app_;
    nk::Window& window_;

    GuiAudioBackend* audio_backend_ = nullptr;
    NodalKitInput* input_backend_ = nullptr;
    std::unique_ptr<app::EmulationSession> session_;
    std::unique_ptr<app::SessionActions> actions_;
    core::FrameBuffer blank_frame_{};

    std::shared_ptr<Box> root_;
    std::shared_ptr<nk::MenuBar> menu_bar_;
    std::shared_ptr<PreviewCanvas> preview_;
    std::shared_ptr<nk::StatusBar> status_bar_;
    std::shared_ptr<nk::Dialog> input_dialog_;
    nk::CallbackHandle tick_handle_{};
};

} // namespace mapperbus::frontend
