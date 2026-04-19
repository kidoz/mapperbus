#include "frontends/sdl3/sdl3_input.hpp"

#include <algorithm>

namespace mapperbus::frontend {
namespace {

[[nodiscard]] SDL_GamepadButton to_sdl_button(platform::GamepadButton button) {
    switch (button) {
    case platform::GamepadButton::South:
        return SDL_GAMEPAD_BUTTON_SOUTH;
    case platform::GamepadButton::East:
        return SDL_GAMEPAD_BUTTON_EAST;
    case platform::GamepadButton::West:
        return SDL_GAMEPAD_BUTTON_WEST;
    case platform::GamepadButton::North:
        return SDL_GAMEPAD_BUTTON_NORTH;
    case platform::GamepadButton::Back:
        return SDL_GAMEPAD_BUTTON_BACK;
    case platform::GamepadButton::Start:
        return SDL_GAMEPAD_BUTTON_START;
    case platform::GamepadButton::LeftStick:
        return SDL_GAMEPAD_BUTTON_LEFT_STICK;
    case platform::GamepadButton::RightStick:
        return SDL_GAMEPAD_BUTTON_RIGHT_STICK;
    case platform::GamepadButton::LeftShoulder:
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    case platform::GamepadButton::RightShoulder:
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    case platform::GamepadButton::DpadUp:
        return SDL_GAMEPAD_BUTTON_DPAD_UP;
    case platform::GamepadButton::DpadDown:
        return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
    case platform::GamepadButton::DpadLeft:
        return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
    case platform::GamepadButton::DpadRight:
        return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
    }

    return SDL_GAMEPAD_BUTTON_INVALID;
}

[[nodiscard]] SDL_GamepadAxis to_sdl_axis(platform::GamepadAxis axis) {
    switch (axis) {
    case platform::GamepadAxis::LeftX:
        return SDL_GAMEPAD_AXIS_LEFTX;
    case platform::GamepadAxis::LeftY:
        return SDL_GAMEPAD_AXIS_LEFTY;
    case platform::GamepadAxis::RightX:
        return SDL_GAMEPAD_AXIS_RIGHTX;
    case platform::GamepadAxis::RightY:
        return SDL_GAMEPAD_AXIS_RIGHTY;
    case platform::GamepadAxis::LeftTrigger:
        return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    case platform::GamepadAxis::RightTrigger:
        return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    }

    return SDL_GAMEPAD_AXIS_INVALID;
}

} // namespace

Sdl3Input::Sdl3Input(Sdl3InputConfig config) : config_(config) {
    if (config_.enabled && SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        gamepad_initialized_ = true;
        open_configured_gamepad();
    }
}

Sdl3Input::~Sdl3Input() {
    close_gamepad();
    if (gamepad_initialized_) {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
}

void Sdl3Input::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        handle_event(event);
    }

    button_state_[0] = keyboard_state() | gamepad_state();
    button_state_[1] = 0;
}

void Sdl3Input::open_configured_gamepad() {
    if (!gamepad_initialized_ || gamepad_ != nullptr || !config_.enabled) {
        return;
    }

    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (gamepads == nullptr) {
        return;
    }

    const int target_index = std::max(0, config_.gamepad_index);
    if (target_index < count) {
        open_gamepad(gamepads[target_index]);
    }

    SDL_free(gamepads);
}

void Sdl3Input::open_gamepad(SDL_JoystickID instance_id) {
    close_gamepad();
    gamepad_ = SDL_OpenGamepad(instance_id);
    if (gamepad_ != nullptr) {
        gamepad_id_ = SDL_GetGamepadID(gamepad_);
    }
}

void Sdl3Input::close_gamepad() {
    if (gamepad_ != nullptr) {
        SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
        gamepad_id_ = 0;
    }
}

void Sdl3Input::handle_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_QUIT) {
        quit_requested_ = true;
        return;
    }

    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        open_configured_gamepad();
        return;
    }

    if (event.type == SDL_EVENT_GAMEPAD_REMOVED && event.gdevice.which == gamepad_id_) {
        close_gamepad();
        open_configured_gamepad();
    }
}

std::uint8_t Sdl3Input::keyboard_state() const {
    const bool* keys = SDL_GetKeyboardState(nullptr);

    std::uint8_t state = 0;
    if (keys[SDL_SCANCODE_Z])
        state |= static_cast<std::uint8_t>(core::Button::B);
    if (keys[SDL_SCANCODE_X])
        state |= static_cast<std::uint8_t>(core::Button::A);
    if (keys[SDL_SCANCODE_RSHIFT])
        state |= static_cast<std::uint8_t>(core::Button::Select);
    if (keys[SDL_SCANCODE_RETURN])
        state |= static_cast<std::uint8_t>(core::Button::Start);
    if (keys[SDL_SCANCODE_UP])
        state |= static_cast<std::uint8_t>(core::Button::Up);
    if (keys[SDL_SCANCODE_DOWN])
        state |= static_cast<std::uint8_t>(core::Button::Down);
    if (keys[SDL_SCANCODE_LEFT])
        state |= static_cast<std::uint8_t>(core::Button::Left);
    if (keys[SDL_SCANCODE_RIGHT])
        state |= static_cast<std::uint8_t>(core::Button::Right);

    return state;
}

std::uint8_t Sdl3Input::gamepad_state() const {
    if (gamepad_ == nullptr) {
        return 0;
    }

    std::uint8_t state = 0;
    for (const auto nes_button : platform::controller_buttons()) {
        if (is_control_pressed(platform::gamepad_control_for_button(config_, nes_button))) {
            state |= static_cast<std::uint8_t>(nes_button);
        }
    }

    return state;
}

bool Sdl3Input::is_control_pressed(const GamepadControl& control) const {
    if (gamepad_ == nullptr) {
        return false;
    }

    switch (control.kind) {
    case platform::GamepadControlKind::Button:
        return SDL_GetGamepadButton(gamepad_, to_sdl_button(control.button));
    case platform::GamepadControlKind::AxisNegative:
        return SDL_GetGamepadAxis(gamepad_, to_sdl_axis(control.axis)) <=
               -std::max(1, static_cast<int>(config_.axis_deadzone));
    case platform::GamepadControlKind::AxisPositive:
        return SDL_GetGamepadAxis(gamepad_, to_sdl_axis(control.axis)) >=
               std::max(1, static_cast<int>(config_.axis_deadzone));
    }

    return false;
}

bool Sdl3Input::is_button_pressed(int player, core::Button button) const {
    if (player < 0 || player > 1)
        return false;
    return (button_state_[player] & static_cast<std::uint8_t>(button)) != 0;
}

bool Sdl3Input::should_quit() const {
    return quit_requested_;
}

} // namespace mapperbus::frontend
