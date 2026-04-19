#pragma once

#include <algorithm>
#include <array>
#include <nk/platform/key_codes.h>
#include <nk/platform/window.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
#include <SDL3/SDL.h>
#endif

#include "platform/input/gamepad_config.hpp"
#include "platform/input/input_backend.hpp"

namespace mapperbus::frontend {

class NodalKitInput : public platform::InputBackend {
  public:
    explicit NodalKitInput(nk::Window& window, platform::GamepadInputConfig gamepad_config = {})
        : window_(window), gamepad_config_(gamepad_config) {
        reset_default_bindings();
        initialize_gamepad_subsystem();
    }

    ~NodalKitInput() override {
        shutdown_gamepad_subsystem();
    }

    void poll() override {
        poll_gamepad();
    }

    bool is_button_pressed(int player, core::Button button) const override {
        if (player != 0) {
            return false;
        }

        return window_.is_key_pressed(binding(button)) || is_gamepad_button_pressed(button);
    }

    bool should_quit() const override {
        return false;
    }

    void set_binding(core::Button button, nk::KeyCode key) {
        bindings_[button_index(button)] = key;
    }

    [[nodiscard]] nk::KeyCode binding(core::Button button) const {
        return bindings_[button_index(button)];
    }

    void set_gamepad_enabled(bool enabled) {
        if (gamepad_config_.enabled == enabled) {
            return;
        }

        gamepad_config_.enabled = enabled;
        if (gamepad_config_.enabled) {
            initialize_gamepad_subsystem();
            open_configured_gamepad();
        } else {
            close_gamepad();
        }
    }

    void set_gamepad_index(int index) {
        const int clamped = std::max(0, index);
        if (gamepad_config_.gamepad_index == clamped) {
            return;
        }

        gamepad_config_.gamepad_index = clamped;
        close_gamepad();
        open_configured_gamepad();
    }

    void set_gamepad_deadzone(std::int16_t deadzone) {
        gamepad_config_.axis_deadzone =
            static_cast<std::int16_t>(std::clamp(static_cast<int>(deadzone), 1, 32767));
    }

    void set_gamepad_binding(core::Button button, platform::GamepadControl control) {
        gamepad_config_.bindings[platform::button_index(button)] = control;
    }

    [[nodiscard]] const platform::GamepadControl& gamepad_binding(core::Button button) const {
        return platform::gamepad_control_for_button(gamepad_config_, button);
    }

    [[nodiscard]] const platform::GamepadInputConfig& gamepad_config() const {
        return gamepad_config_;
    }

    [[nodiscard]] bool gamepad_support_available() const {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        return true;
#else
        return false;
#endif
    }

    [[nodiscard]] bool gamepad_connected() const {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        return gamepad_ != nullptr;
#else
        return false;
#endif
    }

    [[nodiscard]] int gamepad_device_count() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        initialize_gamepad_subsystem();
        if (!gamepad_initialized_) {
            return 0;
        }

        int count = 0;
        SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
        if (gamepads != nullptr) {
            SDL_free(gamepads);
        }
        return std::max(0, count);
#else
        return 0;
#endif
    }

    [[nodiscard]] std::vector<std::string> gamepad_device_labels(int minimum_slots = 4) {
        std::vector<std::string> labels;
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        initialize_gamepad_subsystem();

        int count = 0;
        SDL_JoystickID* gamepads = gamepad_initialized_ ? SDL_GetGamepads(&count) : nullptr;
        if (gamepads == nullptr || count == 0) {
            if (gamepads != nullptr) {
                SDL_free(gamepads);
            }
            return {"No gamepads detected"};
        }

        const int slots = std::max({minimum_slots, count, gamepad_config_.gamepad_index + 1});
        labels.reserve(static_cast<std::size_t>(std::max(0, slots)));
        for (int index = 0; index < slots; ++index) {
            std::string label = "Gamepad " + std::to_string(index + 1);
            if (gamepads != nullptr && index < count) {
                const char* name = SDL_GetGamepadNameForID(gamepads[index]);
                if (name != nullptr && name[0] != '\0') {
                    label += " - ";
                    label += name;
                }
            } else {
                label += " (not connected)";
            }
            labels.push_back(std::move(label));
        }
        if (gamepads != nullptr) {
            SDL_free(gamepads);
        }
#else
        labels.reserve(static_cast<std::size_t>(std::max(0, minimum_slots)));
        for (int index = 0; index < minimum_slots; ++index) {
            labels.push_back("Gamepad " + std::to_string(index + 1));
        }
#endif
        return labels;
    }

    [[nodiscard]] std::string gamepad_status_text() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        if (!gamepad_config_.enabled) {
            return "Gamepad input is disabled.";
        }

        initialize_gamepad_subsystem();
        if (!gamepad_initialized_) {
            return "Gamepad support could not be initialized.";
        }

        int count = 0;
        SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
        const int target_index = std::max(0, gamepad_config_.gamepad_index);
        std::string status;
        if (gamepads == nullptr || count == 0) {
            status = "No gamepads detected. Connect a gamepad to configure pad bindings.";
        } else if (target_index >= count) {
            status = "Waiting for Gamepad " + std::to_string(target_index + 1) + ". " +
                     std::to_string(count) + " connected.";
        } else {
            const char* name = SDL_GetGamepadNameForID(gamepads[target_index]);
            const std::string device_name =
                name != nullptr && name[0] != '\0' ? std::string(name) : "selected gamepad";
            status = gamepad_ != nullptr ? "Connected: " + device_name
                                         : "Detected: " + device_name + ". Waiting to open.";
        }
        if (gamepads != nullptr) {
            SDL_free(gamepads);
        }
        return status;
#else
        return "Gamepad support is unavailable in this build.";
#endif
    }

    [[nodiscard]] std::optional<platform::GamepadControl> detect_pressed_gamepad_control(
        std::span<const platform::GamepadControl> controls) {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        poll_gamepad();
        if (!gamepad_config_.enabled || gamepad_ == nullptr) {
            return std::nullopt;
        }

        for (const auto& control : controls) {
            if (is_control_pressed(control)) {
                return control;
            }
        }
#else
        (void)controls;
#endif
        return std::nullopt;
    }

    void reset_default_gamepad_bindings() {
        gamepad_config_.bindings = platform::default_gamepad_bindings();
        gamepad_config_.axis_deadzone = 12000;
        gamepad_config_.gamepad_index = 0;
        gamepad_config_.enabled = true;
        initialize_gamepad_subsystem();
        close_gamepad();
        open_configured_gamepad();
    }

    [[nodiscard]] bool uses_default_gamepad_bindings() const {
        const auto defaults = platform::default_gamepad_bindings();
        return gamepad_config_.enabled && gamepad_config_.gamepad_index == 0 &&
               gamepad_config_.axis_deadzone == 12000 && gamepad_config_.bindings == defaults;
    }

    void reset_default_bindings() {
        for (const auto button : buttons()) {
            set_binding(button, default_key_for_button(button));
        }
    }

    [[nodiscard]] bool uses_default_bindings() const {
        for (const auto button : buttons()) {
            if (binding(button) != default_key_for_button(button)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static nk::KeyCode default_key_for_button(core::Button button) {
        switch (button) {
        case core::Button::A:
            return nk::KeyCode::X;
        case core::Button::B:
            return nk::KeyCode::Z;
        case core::Button::Select:
            return nk::KeyCode::RightShift;
        case core::Button::Start:
            return nk::KeyCode::Return;
        case core::Button::Up:
            return nk::KeyCode::Up;
        case core::Button::Down:
            return nk::KeyCode::Down;
        case core::Button::Left:
            return nk::KeyCode::Left;
        case core::Button::Right:
            return nk::KeyCode::Right;
        }

        return nk::KeyCode::Unknown;
    }

  private:
    [[nodiscard]] static constexpr std::array<core::Button, 8> buttons() {
        return {
            core::Button::Up,
            core::Button::Down,
            core::Button::Left,
            core::Button::Right,
            core::Button::A,
            core::Button::B,
            core::Button::Start,
            core::Button::Select,
        };
    }

    [[nodiscard]] static constexpr std::size_t button_index(core::Button button) {
        switch (button) {
        case core::Button::Up:
            return 0;
        case core::Button::Down:
            return 1;
        case core::Button::Left:
            return 2;
        case core::Button::Right:
            return 3;
        case core::Button::A:
            return 4;
        case core::Button::B:
            return 5;
        case core::Button::Start:
            return 6;
        case core::Button::Select:
            return 7;
        }

        return 0;
    }

    void initialize_gamepad_subsystem() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        if (!gamepad_config_.enabled || gamepad_initialized_) {
            return;
        }
        if (SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
            gamepad_initialized_ = true;
        }
#endif
    }

    void shutdown_gamepad_subsystem() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        close_gamepad();
        if (gamepad_initialized_) {
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            gamepad_initialized_ = false;
        }
#endif
    }

    void poll_gamepad() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        if (!gamepad_config_.enabled) {
            close_gamepad();
            return;
        }

        initialize_gamepad_subsystem();
        if (!gamepad_initialized_) {
            return;
        }

        SDL_UpdateGamepads();
        if (gamepad_ != nullptr &&
            SDL_GetGamepadConnectionState(gamepad_) == SDL_JOYSTICK_CONNECTION_INVALID) {
            close_gamepad();
        }
        if (gamepad_ == nullptr) {
            open_configured_gamepad();
        }
#endif
    }

    void open_configured_gamepad() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        if (!gamepad_initialized_ || !gamepad_config_.enabled || gamepad_ != nullptr) {
            return;
        }

        int count = 0;
        SDL_JoystickID* gamepads = SDL_GetGamepads(&count);
        if (gamepads == nullptr) {
            return;
        }

        const int target_index = std::max(0, gamepad_config_.gamepad_index);
        if (target_index < count) {
            gamepad_ = SDL_OpenGamepad(gamepads[target_index]);
            if (gamepad_ != nullptr) {
                gamepad_id_ = SDL_GetGamepadID(gamepad_);
            }
        }

        SDL_free(gamepads);
#endif
    }

    void close_gamepad() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        if (gamepad_ != nullptr) {
            SDL_CloseGamepad(gamepad_);
            gamepad_ = nullptr;
            gamepad_id_ = 0;
        }
#endif
    }

    [[nodiscard]] bool is_gamepad_button_pressed(core::Button button) const {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        if (!gamepad_config_.enabled || gamepad_ == nullptr) {
            return false;
        }

        return is_control_pressed(platform::gamepad_control_for_button(gamepad_config_, button));
#else
        return false;
#endif
    }

#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
    [[nodiscard]] static SDL_GamepadButton to_sdl_button(platform::GamepadButton button) {
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

    [[nodiscard]] static SDL_GamepadAxis to_sdl_axis(platform::GamepadAxis axis) {
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

    [[nodiscard]] bool is_control_pressed(const platform::GamepadControl& control) const {
        switch (control.kind) {
        case platform::GamepadControlKind::Button:
            return SDL_GetGamepadButton(gamepad_, to_sdl_button(control.button));
        case platform::GamepadControlKind::AxisNegative:
            return SDL_GetGamepadAxis(gamepad_, to_sdl_axis(control.axis)) <=
                   -std::max(1, static_cast<int>(gamepad_config_.axis_deadzone));
        case platform::GamepadControlKind::AxisPositive:
            return SDL_GetGamepadAxis(gamepad_, to_sdl_axis(control.axis)) >=
                   std::max(1, static_cast<int>(gamepad_config_.axis_deadzone));
        }

        return false;
    }
#endif

    nk::Window& window_;
    std::array<nk::KeyCode, 8> bindings_{};
    platform::GamepadInputConfig gamepad_config_;
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
    bool gamepad_initialized_ = false;
    SDL_Gamepad* gamepad_ = nullptr;
    SDL_JoystickID gamepad_id_ = 0;
#endif
};

} // namespace mapperbus::frontend
