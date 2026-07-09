#include "frontends/sdl3/sdl3_input.hpp"

#include <algorithm>

namespace mapperbus::frontend {
namespace {

[[nodiscard]] Sdl3KeyboardBindings default_keyboard_bindings() {
    Sdl3KeyboardBindings bindings{};
    bindings[platform::button_index(core::Button::Up)] = SDL_SCANCODE_UP;
    bindings[platform::button_index(core::Button::Down)] = SDL_SCANCODE_DOWN;
    bindings[platform::button_index(core::Button::Left)] = SDL_SCANCODE_LEFT;
    bindings[platform::button_index(core::Button::Right)] = SDL_SCANCODE_RIGHT;
    bindings[platform::button_index(core::Button::A)] = SDL_SCANCODE_X;
    bindings[platform::button_index(core::Button::B)] = SDL_SCANCODE_Z;
    bindings[platform::button_index(core::Button::Start)] = SDL_SCANCODE_RETURN;
    bindings[platform::button_index(core::Button::Select)] = SDL_SCANCODE_RSHIFT;
    return bindings;
}

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

Sdl3Input::Sdl3Input(Sdl3InputConfig config, Sdl3KeyboardBindings keyboard_bindings)
    : config_(config), keyboard_bindings_(keyboard_bindings) {
    if (std::ranges::all_of(keyboard_bindings_, [](int code) { return code == 0; })) {
        keyboard_bindings_ = default_keyboard_bindings();
    }
    init_gamepad_subsystem();
    open_configured_gamepad();
}

Sdl3Input::Sdl3Input(Sdl3TwoPlayerInputConfig config, Sdl3KeyboardBindings keyboard_bindings)
    : config_(config.player1), config2_(config.player2), two_player_(true),
      keyboard_bindings_(keyboard_bindings) {
    if (std::ranges::all_of(keyboard_bindings_, [](int code) { return code == 0; })) {
        keyboard_bindings_ = default_keyboard_bindings();
    }
    init_gamepad_subsystem();
    open_configured_gamepad();
}

Sdl3Input::~Sdl3Input() {
    close_gamepad();
    close_gamepad2();
    if (gamepad_initialized_) {
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
}

void Sdl3Input::init_gamepad_subsystem() {
    bool want_gamepad = config_.enabled || (two_player_ && config2_.enabled);
    if (want_gamepad && SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        gamepad_initialized_ = true;
    }
}

Sdl3ScalerCommand Sdl3Input::scaler_command_for_scancode(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_0:
    case SDL_SCANCODE_1:
        return {.kind = Sdl3ScalerCommandKind::Native};
    case SDL_SCANCODE_2:
        return {.kind = Sdl3ScalerCommandKind::SetFactor, .factor = 2};
    case SDL_SCANCODE_3:
        return {.kind = Sdl3ScalerCommandKind::SetFactor, .factor = 3};
    case SDL_SCANCODE_4:
        return {.kind = Sdl3ScalerCommandKind::SetFactor, .factor = 4};
    case SDL_SCANCODE_5:
        return {.kind = Sdl3ScalerCommandKind::SetFactor, .factor = 5};
    case SDL_SCANCODE_6:
        return {.kind = Sdl3ScalerCommandKind::SetFactor, .factor = 6};
    case SDL_SCANCODE_F9:
        return {.kind = Sdl3ScalerCommandKind::CycleMode};
    default:
        return {};
    }
}

Sdl3ScalerCommand Sdl3Input::consume_scaler_command() {
    const auto command = pending_scaler_command_;
    pending_scaler_command_ = {};
    return command;
}

Sdl3SessionCommand Sdl3Input::session_command_for_scancode(SDL_Scancode scancode,
                                                           bool shift,
                                                           int active_slot) {
    // F5/F7 operate on the active slot. Shift+F5/F7 operate on slot 1.
    // This gives both "select slot then save" and "quick-save slot 1" flows.
    switch (scancode) {
    case SDL_SCANCODE_F5:
        return {.kind = Sdl3SessionCommandKind::SaveState, .slot = shift ? 1 : active_slot};
    case SDL_SCANCODE_F7:
        return {.kind = Sdl3SessionCommandKind::LoadState, .slot = shift ? 1 : active_slot};
    default:
        return {};
    }
}

Sdl3WindowCommand Sdl3Input::window_command_for_scancode(SDL_Scancode scancode) {
    switch (scancode) {
    case SDL_SCANCODE_F11:
        return {.kind = Sdl3WindowCommandKind::ToggleFullscreen};
    case SDL_SCANCODE_V:
        return {.kind = Sdl3WindowCommandKind::ToggleVsync};
    default:
        return {};
    }
}

Sdl3SessionCommand Sdl3Input::consume_session_command() {
    const auto command = pending_session_command_;
    pending_session_command_ = {};
    return command;
}

Sdl3WindowCommand Sdl3Input::consume_window_command() {
    const auto command = pending_window_command_;
    pending_window_command_ = {};
    return command;
}

void Sdl3Input::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        handle_event(event);
    }

    button_state_[0] = keyboard_state() | gamepad_state();
    // Player 2 is driven by a second gamepad when two-player mode is active;
    // otherwise no player-2 input.
    button_state_[1] = two_player_ ? gamepad2_state() : 0;
}

void Sdl3Input::open_configured_gamepad() {
    if (!gamepad_initialized_) {
        return;
    }

    int count = 0;
    SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
    if (gamepads == nullptr) {
        return;
    }

    // Player 1
    if (gamepad_ == nullptr && config_.enabled) {
        const int target_index = std::max(0, config_.gamepad_index);
        if (target_index < count) {
            open_gamepad(gamepads[target_index]);
        }
    }

    // Player 2 (only in two-player mode)
    if (two_player_ && gamepad2_ == nullptr && config2_.enabled) {
        const int target_index = std::max(0, config2_.gamepad_index);
        if (target_index < count) {
            gamepad2_ = SDL_OpenGamepad(gamepads[target_index]);
            if (gamepad2_ != nullptr) {
                gamepad2_id_ = SDL_GetGamepadID(gamepad2_);
            }
        }
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

void Sdl3Input::close_gamepad2() {
    if (gamepad2_ != nullptr) {
        SDL_CloseGamepad(gamepad2_);
        gamepad2_ = nullptr;
        gamepad2_id_ = 0;
    }
}

void Sdl3Input::handle_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_QUIT) {
        quit_requested_ = true;
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;

        if (auto cmd = scaler_command_for_scancode(event.key.scancode)) {
            pending_scaler_command_ = cmd;
            return;
        }
        if (auto cmd = window_command_for_scancode(event.key.scancode)) {
            pending_window_command_ = cmd;
            return;
        }
        if (auto cmd = session_command_for_scancode(event.key.scancode, shift, active_slot_)) {
            pending_session_command_ = cmd;
            return;
        }
    }

    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        open_configured_gamepad();
        return;
    }

    if (event.type == SDL_EVENT_GAMEPAD_REMOVED) {
        if (event.gdevice.which == gamepad_id_) {
            close_gamepad();
            open_configured_gamepad();
        } else if (event.gdevice.which == gamepad2_id_) {
            close_gamepad2();
            open_configured_gamepad();
        }
    }
}

std::uint8_t Sdl3Input::keyboard_state() const {
    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);

    std::uint8_t state = 0;
    for (const auto button : platform::controller_buttons()) {
        const int scancode = keyboard_bindings_[platform::button_index(button)];
        if (scancode >= 0 && scancode < key_count && keys[scancode]) {
            state |= static_cast<std::uint8_t>(button);
        }
    }

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

std::uint8_t Sdl3Input::gamepad2_state() const {
    if (gamepad2_ == nullptr) {
        return 0;
    }

    std::uint8_t state = 0;
    for (const auto nes_button : platform::controller_buttons()) {
        if (is_control_pressed2(platform::gamepad_control_for_button(config2_, nes_button))) {
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

bool Sdl3Input::is_control_pressed2(const GamepadControl& control) const {
    if (gamepad2_ == nullptr) {
        return false;
    }

    switch (control.kind) {
    case platform::GamepadControlKind::Button:
        return SDL_GetGamepadButton(gamepad2_, to_sdl_button(control.button));
    case platform::GamepadControlKind::AxisNegative:
        return SDL_GetGamepadAxis(gamepad2_, to_sdl_axis(control.axis)) <=
               -std::max(1, static_cast<int>(config2_.axis_deadzone));
    case platform::GamepadControlKind::AxisPositive:
        return SDL_GetGamepadAxis(gamepad2_, to_sdl_axis(control.axis)) >=
               std::max(1, static_cast<int>(config2_.axis_deadzone));
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
