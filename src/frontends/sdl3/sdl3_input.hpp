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
using platform::apply_gamepad_mapping;
using platform::gamepad_control_for_button;
using platform::parse_gamepad_control;

class Sdl3Input : public platform::InputBackend {
  public:
    explicit Sdl3Input(Sdl3InputConfig config = {});
    ~Sdl3Input() override;

    void poll() override;
    bool is_button_pressed(int player, core::Button button) const override;
    bool should_quit() const override;

  private:
    void open_configured_gamepad();
    void open_gamepad(SDL_JoystickID instance_id);
    void close_gamepad();
    void handle_event(const SDL_Event& event);
    [[nodiscard]] std::uint8_t keyboard_state() const;
    [[nodiscard]] std::uint8_t gamepad_state() const;
    [[nodiscard]] bool is_control_pressed(const GamepadControl& control) const;

    Sdl3InputConfig config_;
    bool quit_requested_ = false;
    bool gamepad_initialized_ = false;
    SDL_Gamepad* gamepad_ = nullptr;
    SDL_JoystickID gamepad_id_ = 0;
    std::array<std::uint8_t, 2> button_state_{};
};

} // namespace mapperbus::frontend
