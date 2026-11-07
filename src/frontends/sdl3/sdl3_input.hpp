#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "platform/input/gamepad_config.hpp"
#include "platform/input/input_backend.hpp"

namespace mapperbus::frontend {

using GamepadControl = platform::GamepadControl;
using GamepadControlKind = platform::GamepadControlKind;
using Sdl3InputConfig = platform::GamepadInputConfig;
using Sdl3KeyboardBindings = std::array<int, platform::kInputActionCount>;
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
    NextDiskSide,
    EjectDisk,
    ToggleDiskProtection,
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

/// Independent gamepad profiles for the two NES ports.
struct Sdl3TwoPlayerInputConfig {
    Sdl3InputConfig player1{};
    Sdl3InputConfig player2{};
};

struct Sdl3FourPlayerInputConfig {
    std::array<Sdl3InputConfig, 4> players{};
    std::array<Sdl3KeyboardBindings, 4> keyboards{};
    Sdl3FourPlayerInputConfig();
};

struct Sdl3SixPlayerInputConfig {
    std::array<Sdl3InputConfig, platform::kInputProfileCount> players{};
    std::array<Sdl3KeyboardBindings, platform::kInputProfileCount> keyboards{};
    Sdl3SixPlayerInputConfig();
};

class Sdl3Input : public platform::InputBackend {
  public:
    explicit Sdl3Input(Sdl3InputConfig config = {},
                       std::optional<Sdl3KeyboardBindings> keyboard_bindings = std::nullopt);
    explicit Sdl3Input(Sdl3TwoPlayerInputConfig config,
                       std::optional<Sdl3KeyboardBindings> keyboard_bindings = std::nullopt,
                       Sdl3KeyboardBindings player2_keyboard_bindings = {});
    explicit Sdl3Input(Sdl3FourPlayerInputConfig config);
    explicit Sdl3Input(Sdl3SixPlayerInputConfig config);
    ~Sdl3Input() override;

    void poll() override;
    bool is_button_pressed(int player, core::Button button) const override;
    bool is_action_pressed(int player, platform::InputAction action) const override;
    bool should_quit() const override;
    [[nodiscard]] Sdl3ScalerCommand consume_scaler_command();
    [[nodiscard]] static Sdl3ScalerCommand scaler_command_for_scancode(SDL_Scancode scancode);
    [[nodiscard]] Sdl3SessionCommand consume_session_command();
    [[nodiscard]] static Sdl3SessionCommand session_command_for_scancode(SDL_Scancode scancode,
                                                                         bool shift,
                                                                         int active_slot = 0,
                                                                         bool ctrl = false);
    [[nodiscard]] Sdl3WindowCommand consume_window_command();
    [[nodiscard]] static Sdl3WindowCommand window_command_for_scancode(SDL_Scancode scancode);
    void set_active_slot(int slot);
    [[nodiscard]] int active_slot() const {
        return active_slot_;
    }

  private:
    void init_gamepad_subsystem();
    void open_configured_gamepad();
    void close_gamepad(int player);
    void handle_event(const SDL_Event& event);
    [[nodiscard]] std::uint16_t keyboard_state(const Sdl3KeyboardBindings& bindings) const;
    [[nodiscard]] std::uint16_t gamepad_state(int player) const;
    [[nodiscard]] bool is_control_pressed(int player, const GamepadControl& control) const;

    std::array<Sdl3InputConfig, platform::kInputProfileCount> configs_{};
    std::array<Sdl3KeyboardBindings, platform::kInputProfileCount> keyboard_bindings_{};
    int player_count_ = 1;
    bool quit_requested_ = false;
    bool gamepad_initialized_ = false;
    std::array<SDL_Gamepad*, platform::kInputProfileCount> gamepads_{};
    std::array<SDL_JoystickID, platform::kInputProfileCount> gamepad_ids_{};
    std::array<std::vector<SDL_JoystickID>, platform::kInputProfileCount> disconnected_peers_;
    std::vector<SDL_JoystickID> known_gamepads_;
    std::array<std::uint16_t, platform::kInputProfileCount> button_state_{};
    Sdl3ScalerCommand pending_scaler_command_{};
    Sdl3SessionCommand pending_session_command_{};
    Sdl3WindowCommand pending_window_command_{};
    int active_slot_ = 0;
};

} // namespace mapperbus::frontend
