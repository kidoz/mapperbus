#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>

#include "platform/input/gamepad_config.hpp"
#include "platform/input/input_backend.hpp"

namespace mapperbus::frontend {

using GamepadControl = platform::GamepadControl;
using GamepadControlKind = platform::GamepadControlKind;
using Sdl3InputConfig = platform::GamepadInputConfig;
using Sdl3KeyboardBindings = std::array<int, 8>;
using platform::apply_gamepad_mapping;
using platform::gamepad_control_for_button;
using platform::parse_gamepad_control;

enum class Sdl3ScalerCommandKind {
    None,
    Native,
    SetFactor,
    CycleMode,
};

struct Sdl3ScalerCommand {
    Sdl3ScalerCommandKind kind = Sdl3ScalerCommandKind::None;
    int factor = 0;

    [[nodiscard]] explicit operator bool() const {
        return kind != Sdl3ScalerCommandKind::None;
    }
};

enum class Sdl3SessionCommandKind {
    None,
    SaveState,
    LoadState,
};

struct Sdl3SessionCommand {
    Sdl3SessionCommandKind kind = Sdl3SessionCommandKind::None;

    [[nodiscard]] explicit operator bool() const {
        return kind != Sdl3SessionCommandKind::None;
    }
};

class Sdl3Input : public platform::InputBackend {
  public:
    explicit Sdl3Input(Sdl3InputConfig config = {}, Sdl3KeyboardBindings keyboard_bindings = {});
    ~Sdl3Input() override;

    void poll() override;
    bool is_button_pressed(int player, core::Button button) const override;
    bool should_quit() const override;
    [[nodiscard]] Sdl3ScalerCommand consume_scaler_command();
    [[nodiscard]] static Sdl3ScalerCommand scaler_command_for_scancode(SDL_Scancode scancode);
    [[nodiscard]] Sdl3SessionCommand consume_session_command();
    [[nodiscard]] static Sdl3SessionCommand session_command_for_scancode(SDL_Scancode scancode);

  private:
    void open_configured_gamepad();
    void open_gamepad(SDL_JoystickID instance_id);
    void close_gamepad();
    void handle_event(const SDL_Event& event);
    [[nodiscard]] std::uint8_t keyboard_state() const;
    [[nodiscard]] std::uint8_t gamepad_state() const;
    [[nodiscard]] bool is_control_pressed(const GamepadControl& control) const;

    Sdl3InputConfig config_;
    Sdl3KeyboardBindings keyboard_bindings_{};
    bool quit_requested_ = false;
    bool gamepad_initialized_ = false;
    SDL_Gamepad* gamepad_ = nullptr;
    SDL_JoystickID gamepad_id_ = 0;
    std::array<std::uint8_t, 2> button_state_{};
    Sdl3ScalerCommand pending_scaler_command_{};
    Sdl3SessionCommand pending_session_command_{};
};

} // namespace mapperbus::frontend
