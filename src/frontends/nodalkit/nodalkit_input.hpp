#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <nk/platform/key_codes.h>
#include <nk/platform/window.h>
#include <optional>
#include <span>
#include <string>
#include <vector>

#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
#include <SDL3/SDL.h>

#include "frontends/sdl3/gamepad_selection.hpp"
#endif

#include "frontends/nodalkit/input_monitor.hpp"
#include "platform/input/gamepad_config.hpp"
#include "platform/input/input_backend.hpp"

namespace mapperbus::frontend {

class NodalKitPlayerInput {
  public:
    explicit NodalKitPlayerInput(nk::Window& window,
                                 platform::GamepadInputConfig gamepad_config,
                                 int player)
        : window_(window), default_gamepad_index_(player), gamepad_config_(gamepad_config) {
        reset_default_bindings();
        initialize_gamepad_subsystem();
    }

    NodalKitPlayerInput(const NodalKitPlayerInput&) = delete;
    NodalKitPlayerInput& operator=(const NodalKitPlayerInput&) = delete;

    ~NodalKitPlayerInput() {
        shutdown_gamepad_subsystem();
    }

    void poll() {
        poll_gamepad();
    }

    /// Keys the window uses for pause, reset, save, load, and fullscreen.
    /// They never count as a held game button, including after a rebind.
    [[nodiscard]] static bool is_reserved_game_key(nk::KeyCode key) {
        switch (key) {
        case nk::KeyCode::P:
        case nk::KeyCode::R:
        case nk::KeyCode::F5:
        case nk::KeyCode::F6:
        case nk::KeyCode::F8:
        case nk::KeyCode::F7:
        case nk::KeyCode::F11:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool is_action_pressed(platform::InputAction button) const {
        return keyboard_button_pressed(binding(button)) || is_gamepad_button_pressed(button);
    }

    void set_binding(platform::InputAction button, nk::KeyCode key) {
        bindings_[button_index(button)] = key;
    }

    [[nodiscard]] nk::KeyCode binding(platform::InputAction button) const {
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
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        initialize_gamepad_subsystem();
        selection_.assign(gamepad_config_, index);
#else
        gamepad_config_.gamepad_index = std::max(0, index);
        gamepad_config_.identity = {};
#endif
    }

#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
    bool prepare_gamepad_assignment() {
        if (!gamepad_initialized_)
            gamepad_initialized_ = SDL_InitSubSystem(SDL_INIT_GAMEPAD);
        return gamepad_initialized_;
    }
    void commit_gamepad_assignment(Sdl3GamepadSelection& staged,
                                   platform::GamepadInputConfig&& configuration) noexcept {
        selection_.swap(staged);
        gamepad_config_ = std::move(configuration);
    }
#endif

    bool consume_gamepad_assignment_changes() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        return selection_.consume_changed();
#else
        return false;
#endif
    }
    bool consume_gamepad_inventory_changes() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        return selection_.consume_inventory_changed();
#else
        return false;
#endif
    }

    void set_gamepad_deadzone(std::int16_t deadzone) {
        gamepad_config_.axis_deadzone =
            static_cast<std::int16_t>(std::clamp(static_cast<int>(deadzone), 1, 32767));
    }

    void set_gamepad_binding(platform::InputAction button, platform::GamepadControl control) {
        gamepad_config_.bindings[platform::button_index(button)] = control;
    }

    [[nodiscard]] const platform::GamepadControl& gamepad_binding(
        platform::InputAction button) const {
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
        return selection_.gamepad() != nullptr && SDL_GamepadConnected(selection_.gamepad());
#else
        return false;
#endif
    }

    [[nodiscard]] InputMonitorProfile monitor_profile() const {
        InputMonitorProfile result;
        result.device_index = gamepad_config_.gamepad_index;
        result.device_name = gamepad_config_.identity.name;
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        result.connection = !gamepad_config_.enabled ? InputMonitorConnection::Disabled
                            : !gamepad_initialized_  ? InputMonitorConnection::Unavailable
                            : gamepad_connected()    ? InputMonitorConnection::Connected
                            : gamepad_config_.identity.ambiguous
                                ? InputMonitorConnection::NeedsAssignment
                                : InputMonitorConnection::Disconnected;
        if (result.connection == InputMonitorConnection::Connected) {
            result.device_id = SDL_GetGamepadID(selection_.gamepad());
            if (const auto* name = SDL_GetGamepadName(selection_.gamepad()))
                result.device_name = std::string(name).substr(0, 256);
        }
#endif
        for (auto action : platform::input_actions())
            result.held[button_index(action)] = is_action_pressed(action);
        return result;
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
        if (selection_.gamepad() == nullptr && !gamepad_config_.identity.guid.empty()) {
            status = gamepad_config_.identity.ambiguous
                         ? "Identical gamepads need reassignment. Choose Preferred device, then "
                           "Use Selected Gamepad."
                         : "Saved gamepad disconnected: " + gamepad_config_.identity.name +
                               ". Reconnect it or choose Preferred device to reassign.";
        } else if (gamepads == nullptr || count == 0) {
            status = "No gamepads detected. Connect a gamepad to configure pad bindings.";
        } else if (target_index >= count) {
            status = "Waiting for Gamepad " + std::to_string(target_index + 1) + ". " +
                     std::to_string(count) + " connected.";
        } else {
            const char* name = selection_.gamepad() != nullptr
                                   ? SDL_GetGamepadName(selection_.gamepad())
                                   : SDL_GetGamepadNameForID(gamepads[target_index]);
            const std::string device_name =
                name != nullptr && name[0] != '\0' ? std::string(name) : "selected gamepad";
            status = selection_.gamepad() != nullptr
                         ? "Connected: " + device_name
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
        const auto pressed = pressed_gamepad_controls(controls);
        if (!pressed.empty()) {
            return pressed.front();
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<platform::GamepadControl> pressed_gamepad_controls(
        std::span<const platform::GamepadControl> controls) {
        std::vector<platform::GamepadControl> pressed;
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        poll_gamepad();
        if (!gamepad_config_.enabled || selection_.gamepad() == nullptr) {
            return pressed;
        }

        for (const auto& control : controls) {
            if (is_control_pressed(control)) {
                pressed.push_back(control);
            }
        }
#else
        (void)controls;
#endif
        return pressed;
    }

    void reset_default_gamepad_bindings() {
        gamepad_config_.bindings = platform::default_gamepad_bindings();
        gamepad_config_.axis_deadzone = 12000;
        gamepad_config_.gamepad_index = default_gamepad_index_;
        gamepad_config_.identity = {};
        gamepad_config_.enabled = true;
        initialize_gamepad_subsystem();
        set_gamepad_index(default_gamepad_index_);
    }

    [[nodiscard]] bool uses_default_gamepad_bindings() const {
        const auto defaults = platform::default_gamepad_bindings();
        return gamepad_config_.enabled && gamepad_config_.gamepad_index == default_gamepad_index_ &&
               gamepad_config_.axis_deadzone == 12000 && gamepad_config_.bindings == defaults;
    }

    void reset_default_bindings() {
        for (const auto button : buttons()) {
            set_binding(button, default_key_for_button(button, default_gamepad_index_));
        }
    }

    [[nodiscard]] bool uses_default_bindings() const {
        for (const auto button : buttons()) {
            if (binding(button) != default_key_for_button(button, default_gamepad_index_)) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static nk::KeyCode default_key_for_button(platform::InputAction button,
                                                            int player = 0) {
        if (player != 0) {
            return nk::KeyCode::Unknown;
        }
        switch (button) {
        case platform::InputAction::Microphone:
            return nk::KeyCode::M;
        case platform::InputAction::TurboA:
            return nk::KeyCode::S;
        case platform::InputAction::TurboB:
            return nk::KeyCode::A;
        case platform::InputAction::A:
            return nk::KeyCode::X;
        case platform::InputAction::B:
            return nk::KeyCode::Z;
        case platform::InputAction::Select:
            return nk::KeyCode::RightShift;
        case platform::InputAction::Start:
            return nk::KeyCode::Return;
        case platform::InputAction::Up:
            return nk::KeyCode::Up;
        case platform::InputAction::Down:
            return nk::KeyCode::Down;
        case platform::InputAction::Left:
            return nk::KeyCode::Left;
        case platform::InputAction::Right:
            return nk::KeyCode::Right;
        }

        return nk::KeyCode::Unknown;
    }

  private:
    [[nodiscard]] bool keyboard_button_pressed(nk::KeyCode key) const {
        if (key == nk::KeyCode::Unknown || is_reserved_game_key(key)) {
            return false;
        }
        // Ctrl+O, Ctrl+Q, and Ctrl+, open, quit, and show settings. Those
        // chords must not also hold a game button bound to the same key.
        const bool ctrl = window_.is_key_pressed(nk::KeyCode::LeftCtrl) ||
                          window_.is_key_pressed(nk::KeyCode::RightCtrl);
        if (ctrl && (key == nk::KeyCode::O || key == nk::KeyCode::Q || key == nk::KeyCode::Comma)) {
            return false;
        }
        return window_.is_key_pressed(key);
    }

    [[nodiscard]] static constexpr std::array<platform::InputAction, platform::kInputActionCount>
    buttons() {
        return platform::input_actions();
    }

    [[nodiscard]] static constexpr std::size_t button_index(platform::InputAction button) {
        return platform::button_index(button);
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
        initialize_gamepad_subsystem();
        if (gamepad_initialized_) {
            SDL_UpdateGamepads();
            selection_.poll(gamepad_config_);
        }
#endif
    }

    void open_configured_gamepad() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        if (gamepad_initialized_)
            selection_.poll(gamepad_config_);
#endif
    }

    void close_gamepad() {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        selection_.close();
#endif
    }

    [[nodiscard]] bool is_gamepad_button_pressed(platform::InputAction button) const {
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
        if (!gamepad_config_.enabled || selection_.gamepad() == nullptr) {
            return false;
        }

        return is_control_pressed(platform::gamepad_control_for_button(gamepad_config_, button));
#else
        (void)button;
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
        case platform::GamepadControlKind::None:
            return false;
        case platform::GamepadControlKind::Button:
            return SDL_GetGamepadButton(selection_.gamepad(), to_sdl_button(control.button));
        case platform::GamepadControlKind::AxisNegative:
            return SDL_GetGamepadAxis(selection_.gamepad(), to_sdl_axis(control.axis)) <=
                   -std::max(1, static_cast<int>(gamepad_config_.axis_deadzone));
        case platform::GamepadControlKind::AxisPositive:
            return SDL_GetGamepadAxis(selection_.gamepad(), to_sdl_axis(control.axis)) >=
                   std::max(1, static_cast<int>(gamepad_config_.axis_deadzone));
        }

        return false;
    }
#endif

    nk::Window& window_;
    int default_gamepad_index_ = 0;
    std::array<nk::KeyCode, platform::kInputActionCount> bindings_{};
    platform::GamepadInputConfig gamepad_config_;
#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
    bool gamepad_initialized_ = false;
    Sdl3GamepadSelection selection_;
#endif
};

/// Own independent bindings and gamepad handles for the two NES ports.
class NodalKitInput : public platform::InputBackend {
  public:
    NodalKitInput(nk::Window& window,
                  platform::GamepadInputConfig player1,
                  platform::GamepadInputConfig player2)
        : NodalKitInput(window,
                        std::array{player1, player2, disabled_profile(2), disabled_profile(3)}) {}

    NodalKitInput(nk::Window& window, std::array<platform::GamepadInputConfig, 4> profiles)
        : NodalKitInput(window,
                        std::array{profiles[0],
                                   profiles[1],
                                   profiles[2],
                                   profiles[3],
                                   disabled_profile(4),
                                   disabled_profile(5)}) {}

    NodalKitInput(nk::Window& window,
                  std::array<platform::GamepadInputConfig, platform::kInputProfileCount> profiles)
        : players_{NodalKitPlayerInput(window, profiles[0], 0),
                   NodalKitPlayerInput(window, profiles[1], 1),
                   NodalKitPlayerInput(window, profiles[2], 2),
                   NodalKitPlayerInput(window, profiles[3], 3),
                   NodalKitPlayerInput(window, profiles[4], 4),
                   NodalKitPlayerInput(window, profiles[5], 5)} {}

    void poll() override {
        if (assignment_capture_)
            return;
        for (auto& player : players_) {
            player.poll();
        }
    }

    bool is_button_pressed(int player, core::Button button) const override {
        return is_action_pressed(player, platform::input_action(button));
    }

    bool is_action_pressed(int player, platform::InputAction action) const override {
        return !assignment_capture_ && player >= 0 && player < platform::kInputProfileCount &&
               players_[player].is_action_pressed(action);
    }

    [[nodiscard]] auto monitor_profiles() const {
        std::array<InputMonitorProfile, platform::kInputProfileCount> result;
        for (int player = 0; player < platform::kInputProfileCount; ++player) {
            result[player] = players_[player].monitor_profile();
            if (assignment_capture_)
                result[player].held.fill(false);
        }
        return result;
    }

    bool consume_gamepad_assignment_changes() {
        if (assignment_capture_)
            return false;
        bool changed = false;
        for (auto& player : players_)
            changed |= player.consume_gamepad_assignment_changes();
        return changed;
    }

    bool consume_gamepad_inventory_changes() {
        if (assignment_capture_)
            return false;
        bool changed = false;
        for (auto& player : players_)
            changed |= player.consume_gamepad_inventory_changes();
        return changed;
    }

    bool should_quit() const override {
        return false;
    }

    /// Host roles in capture order, independent of each profile's enabled flag.
    [[nodiscard]] static std::vector<int> gamepad_assignment_profiles(core::ControllerMode mode,
                                                                      core::Byte attached) {
        if (mode == core::ControllerMode::HoriFourPlayer)
            return {2, 3, 4, 5};
        return active_controller_profiles(mode, attached);
    }

    void set_assignment_capture(bool active) {
        assignment_capture_ = active;
    }

#ifdef MAPPERBUS_HAVE_SDL3_GAMEPAD
    // Stage every handle and config first. The callback may atomically save the
    // proposal; rejecting it leaves all live assignments and bindings intact.
    bool assign_gamepads(
        std::span<const int> profiles,
        std::span<const SDL_JoystickID> chosen,
        const std::function<bool(std::span<const platform::GamepadInputConfig>)>& confirm = {}) {
        if (profiles.empty() || profiles.size() > players_.size() ||
            profiles.size() != chosen.size())
            return false;
        for (std::size_t pad = 0; pad < profiles.size(); ++pad)
            if (profiles[pad] < 0 || profiles[pad] >= platform::kInputProfileCount ||
                std::ranges::find(profiles.first(pad), profiles[pad]) != profiles.first(pad).end())
                return false;
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        if (!ids)
            return false;
        const std::vector<SDL_JoystickID> connected(ids, ids + count);
        SDL_free(ids);
        std::array<Sdl3GamepadSelection, platform::kInputProfileCount> staged;
        std::array<platform::GamepadInputConfig, platform::kInputProfileCount> configurations;
        for (std::size_t pad = 0; pad < profiles.size(); ++pad) {
            const auto found = std::ranges::find(connected, chosen[pad]);
            if (!chosen[pad] || found == connected.end() ||
                std::ranges::find(chosen.first(pad), chosen[pad]) != chosen.first(pad).end())
                return false;
            auto& player = players_[profiles[pad]];
            if (!player.prepare_gamepad_assignment())
                return false;
            configurations[pad] = player.gamepad_config();
            configurations[pad].enabled = true;
            staged[pad].assign(configurations[pad], static_cast<int>(found - connected.begin()));
            if (!staged[pad].gamepad() || SDL_GetGamepadID(staged[pad].gamepad()) != chosen[pad])
                return false;
        }
        for (std::size_t pad = 0; pad < profiles.size(); ++pad)
            if (!SDL_GamepadConnected(staged[pad].gamepad()))
                return false;
        if (confirm && !confirm(std::span(configurations).first(profiles.size())))
            return false;
        for (std::size_t pad = 0; pad < profiles.size(); ++pad)
            players_[profiles[pad]].commit_gamepad_assignment(staged[pad],
                                                              std::move(configurations[pad]));
        return true;
    }
#endif

    [[nodiscard]] NodalKitPlayerInput& player_input(int player) {
        return players_.at(player);
    }

    [[nodiscard]] const NodalKitPlayerInput& player_input(int player) const {
        return players_.at(player);
    }

    [[nodiscard]] static bool is_reserved_game_key(nk::KeyCode key) {
        return NodalKitPlayerInput::is_reserved_game_key(key);
    }

  private:
    static platform::GamepadInputConfig disabled_profile(int index) {
        platform::GamepadInputConfig config;
        config.enabled = false;
        config.gamepad_index = index;
        return config;
    }
    std::array<NodalKitPlayerInput, platform::kInputProfileCount> players_;
    bool assignment_capture_ = false;
};

} // namespace mapperbus::frontend
