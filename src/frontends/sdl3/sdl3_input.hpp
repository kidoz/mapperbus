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
    SlotSelect,
};

struct Sdl3SessionCommand {
    Sdl3SessionCommandKind kind = Sdl3SessionCommandKind::None;
    int slot = 0;

    [[nodiscard]] explicit operator bool() const {
        return kind != Sdl3SessionCommandKind::None;
    }
};

enum class Sdl3WindowCommandKind {
    None,
    ToggleFullscreen,
    ToggleVsync,
};

struct Sdl3WindowCommand {
    Sdl3WindowCommandKind kind = Sdl3WindowCommandKind::None;

    [[nodiscard]] explicit operator bool() const {
        return kind != Sdl3WindowCommandKind::None;
    }
};

/// Configuration for a second gamepad (player 2). Mirrors Sdl3InputConfig
/// but is opened independently so two physical gamepads can play together.
struct Sdl3TwoPlayerInputConfig {
    Sdl3InputConfig player1{};
    Sdl3InputConfig player2{};
};

class Sdl3Input : public platform::InputBackend {
  public:
    explicit Sdl3Input(Sdl3InputConfig config = {}, Sdl3KeyboardBindings keyboard_bindings = {});
    explicit Sdl3Input(Sdl3TwoPlayerInputConfig config,
                       Sdl3KeyboardBindings keyboard_bindings = {});
    ~Sdl3Input() override;

    void poll() override;
    bool is_button_pressed(int player, core::Button button) const override;
    bool should_quit() const override;
    [[nodiscard]] Sdl3ScalerCommand consume_scaler_command();
    [[nodiscard]] static Sdl3ScalerCommand scaler_command_for_scancode(SDL_Scancode scancode);
    [[nodiscard]] Sdl3SessionCommand consume_session_command();
    [[nodiscard]] static Sdl3SessionCommand session_command_for_scancode(SDL_Scancode scancode,
                                                                         bool shift,
                                                                         int active_slot = 0);
    [[nodiscard]] Sdl3WindowCommand consume_window_command();
    [[nodiscard]] static Sdl3WindowCommand window_command_for_scancode(SDL_Scancode scancode);
    [[nodiscard]] int active_slot() const {
        return active_slot_;
    }

  private:
    void init_gamepad_subsystem();
    void open_configured_gamepad();
    void open_gamepad(SDL_JoystickID instance_id);
    void close_gamepad();
    void close_gamepad2();
    void handle_event(const SDL_Event& event);
    [[nodiscard]] std::uint8_t keyboard_state() const;
    [[nodiscard]] std::uint8_t gamepad_state() const;
    [[nodiscard]] std::uint8_t gamepad2_state() const;
    [[nodiscard]] bool is_control_pressed(const GamepadControl& control) const;
    [[nodiscard]] bool is_control_pressed2(const GamepadControl& control) const;

    Sdl3InputConfig config_;
    Sdl3InputConfig config2_{};
    bool two_player_ = false;
    Sdl3KeyboardBindings keyboard_bindings_{};
    bool quit_requested_ = false;
    bool gamepad_initialized_ = false;
    SDL_Gamepad* gamepad_ = nullptr;
    SDL_Gamepad* gamepad2_ = nullptr;
    SDL_JoystickID gamepad_id_ = 0;
    SDL_JoystickID gamepad2_id_ = 0;
    std::array<std::uint8_t, 2> button_state_{};
    Sdl3ScalerCommand pending_scaler_command_{};
    Sdl3SessionCommand pending_session_command_{};
    Sdl3WindowCommand pending_window_command_{};
    int active_slot_ = 0;
};

} // namespace mapperbus::frontend
