#include "frontends/sdl3/sdl3_input.hpp"

#include <algorithm>

#include "app/configuration.hpp"

namespace mapperbus::frontend {
namespace {

[[nodiscard]] Sdl3KeyboardBindings default_keyboard_bindings() {
    Sdl3KeyboardBindings bindings{};
    bindings[platform::button_index(platform::InputAction::Up)] = SDL_SCANCODE_UP;
    bindings[platform::button_index(platform::InputAction::Down)] = SDL_SCANCODE_DOWN;
    bindings[platform::button_index(platform::InputAction::Left)] = SDL_SCANCODE_LEFT;
    bindings[platform::button_index(platform::InputAction::Right)] = SDL_SCANCODE_RIGHT;
    bindings[platform::button_index(platform::InputAction::A)] = SDL_SCANCODE_X;
    bindings[platform::button_index(platform::InputAction::B)] = SDL_SCANCODE_Z;
    bindings[platform::button_index(platform::InputAction::Start)] = SDL_SCANCODE_RETURN;
    bindings[platform::button_index(platform::InputAction::Select)] = SDL_SCANCODE_RSHIFT;
    bindings[platform::button_index(platform::InputAction::TurboA)] = SDL_SCANCODE_S;
    bindings[platform::button_index(platform::InputAction::TurboB)] = SDL_SCANCODE_A;
    bindings[platform::button_index(platform::InputAction::Microphone)] = SDL_SCANCODE_M;
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

Sdl3FourPlayerInputConfig::Sdl3FourPlayerInputConfig() {
    keyboards[0] = default_keyboard_bindings();
    for (int player = 0; player < 4; ++player)
        players[player].gamepad_index = player;
}

Sdl3SixPlayerInputConfig::Sdl3SixPlayerInputConfig() {
    keyboards[0] = default_keyboard_bindings();
    for (int player = 0; player < platform::kInputProfileCount; ++player)
        players[player].gamepad_index = player;
}

Sdl3Input::Sdl3Input(Sdl3InputConfig config, std::optional<Sdl3KeyboardBindings> keyboard_bindings)
    : configs_{config},
      keyboard_bindings_{keyboard_bindings.value_or(default_keyboard_bindings())} {
    init_gamepad_subsystem();
    open_configured_gamepad();
}

Sdl3Input::Sdl3Input(Sdl3TwoPlayerInputConfig config,
                     std::optional<Sdl3KeyboardBindings> keyboard_bindings,
                     Sdl3KeyboardBindings player2_keyboard_bindings)
    : configs_{config.player1, config.player2},
      keyboard_bindings_{keyboard_bindings.value_or(default_keyboard_bindings()),
                         player2_keyboard_bindings},
      player_count_(2) {
    init_gamepad_subsystem();
    open_configured_gamepad();
}

Sdl3Input::Sdl3Input(Sdl3FourPlayerInputConfig config) : player_count_(4) {
    std::ranges::copy(config.players, configs_.begin());
    std::ranges::copy(config.keyboards, keyboard_bindings_.begin());
    init_gamepad_subsystem();
    open_configured_gamepad();
}

Sdl3Input::Sdl3Input(Sdl3SixPlayerInputConfig config)
    : configs_(config.players), keyboard_bindings_(config.keyboards),
      player_count_(platform::kInputProfileCount) {
    init_gamepad_subsystem();
    open_configured_gamepad();
}

Sdl3Input::~Sdl3Input() {
    for (int player = 0; player < player_count_; ++player)
        close_gamepad(player);
    if (gamepad_initialized_)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

void Sdl3Input::set_gamepad_assignments(const platform::GamepadAssignments& assignments) {
    for (int player = 0; player < platform::kInputProfileCount; ++player) {
        if (platform::gamepad_assignment(configs_[player]) == assignments[player])
            continue;
        Sdl3GamepadSelection previous;
        selections_[player].swap(previous);
        platform::apply_gamepad_assignment(configs_[player], assignments[player]);
    }
    init_gamepad_subsystem();
    open_configured_gamepad();
}

void Sdl3Input::init_gamepad_subsystem() {
    bool wanted = false;
    for (int player = 0; player < player_count_; ++player)
        wanted |= configs_[player].enabled;
    if (wanted && SDL_InitSubSystem(SDL_INIT_GAMEPAD))
        gamepad_initialized_ = true;
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
                                                           int active_slot,
                                                           bool ctrl) {
    // Ctrl+1..9/0 selects slots 1..10 without conflicting with scaler keys.
    if (ctrl && !shift && scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_0) {
        return {.kind = Sdl3SessionCommandKind::SlotSelect,
                .slot = static_cast<int>(scancode - SDL_SCANCODE_1)};
    }
    // F5/F7 operate on the active slot. Shift+F5/F7 operate on UI slot 2.
    // This gives both "select slot then save" and "quick-save slot 2" flows.
    switch (scancode) {
    case SDL_SCANCODE_F6:
        return {.kind = shift ? Sdl3SessionCommandKind::EjectDisk
                              : Sdl3SessionCommandKind::NextDiskSide};
    case SDL_SCANCODE_F8:
        return {.kind = Sdl3SessionCommandKind::ToggleDiskProtection};
    case SDL_SCANCODE_F5:
        return {.kind = Sdl3SessionCommandKind::SaveState, .slot = shift ? 1 : active_slot};
    case SDL_SCANCODE_F7:
        return {.kind = Sdl3SessionCommandKind::LoadState, .slot = shift ? 1 : active_slot};
    default:
        return {};
    }
}

void Sdl3Input::set_active_slot(int slot) {
    if (slot >= 0 && slot < app::kSaveStateSlotCount) {
        active_slot_ = slot;
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

    open_configured_gamepad();
    for (int player = 0; player < player_count_; ++player)
        button_state_[player] = keyboard_state(keyboard_bindings_[player]) | gamepad_state(player);
}

void Sdl3Input::open_configured_gamepad() {
    if (gamepad_initialized_)
        for (int player = 0; player < player_count_; ++player)
            selections_[player].poll(configs_[player]);
}

void Sdl3Input::close_gamepad(int player) {
    selections_[player].close();
}

void Sdl3Input::handle_event(const SDL_Event& event) {
    if (event.type == SDL_EVENT_QUIT) {
        quit_requested_ = true;
        return;
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
        const bool shift = (event.key.mod & SDL_KMOD_SHIFT) != 0;

        const bool ctrl = (event.key.mod & SDL_KMOD_CTRL) != 0;
        if (auto cmd =
                session_command_for_scancode(event.key.scancode, shift, active_slot_, ctrl)) {
            if (cmd.kind == Sdl3SessionCommandKind::SlotSelect) {
                set_active_slot(cmd.slot);
            }
            pending_session_command_ = cmd;
            return;
        }
        if (auto cmd = scaler_command_for_scancode(event.key.scancode)) {
            pending_scaler_command_ = cmd;
            return;
        }
        if (auto cmd = window_command_for_scancode(event.key.scancode)) {
            pending_window_command_ = cmd;
            return;
        }
    }

    if (event.type == SDL_EVENT_GAMEPAD_ADDED) {
        open_configured_gamepad();
        return;
    }
}

std::uint16_t Sdl3Input::keyboard_state(const Sdl3KeyboardBindings& bindings) const {
    int key_count = 0;
    const bool* keys = SDL_GetKeyboardState(&key_count);

    std::uint16_t state = 0;
    for (const auto button : platform::input_actions()) {
        const int scancode = bindings[platform::button_index(button)];
        if (scancode > 0 && scancode < key_count && keys[scancode]) {
            state |= (1U << platform::button_index(button));
        }
    }

    return state;
}

std::uint16_t Sdl3Input::gamepad_state(int player) const {
    if (!selections_[player].gamepad())
        return 0;
    std::uint16_t state = 0;
    for (const auto button : platform::input_actions())
        if (is_control_pressed(player,
                               platform::gamepad_control_for_button(configs_[player], button)))
            state |= (1U << platform::button_index(button));
    return state;
}

bool Sdl3Input::is_control_pressed(int player, const GamepadControl& control) const {
    auto* pad = selections_[player].gamepad();
    if (!pad)
        return false;
    const auto deadzone = std::max(1, static_cast<int>(configs_[player].axis_deadzone));
    switch (control.kind) {
    case GamepadControlKind::None:
        return false;
    case GamepadControlKind::Button:
        return SDL_GetGamepadButton(pad, to_sdl_button(control.button));
    case GamepadControlKind::AxisNegative:
        return SDL_GetGamepadAxis(pad, to_sdl_axis(control.axis)) <= -deadzone;
    case GamepadControlKind::AxisPositive:
        return SDL_GetGamepadAxis(pad, to_sdl_axis(control.axis)) >= deadzone;
    }
    return false;
}

bool Sdl3Input::is_button_pressed(int player, core::Button button) const {
    return is_action_pressed(player, platform::input_action(button));
}

bool Sdl3Input::is_action_pressed(int player, platform::InputAction button) const {
    if (player < 0 || player >= player_count_)
        return false;
    return (button_state_[player] & (1U << platform::button_index(button))) != 0;
}

bool Sdl3Input::should_quit() const {
    return quit_requested_;
}

} // namespace mapperbus::frontend
