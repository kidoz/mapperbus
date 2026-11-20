#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <bitset>
#include <string>
#include <utility>
#include <vector>

namespace mapperbus::frontend {

// Captures instance IDs, never indices or model names: identical controllers
// can be distinguished for this session. No live configuration is modified.
class Sdl3GamepadAssignment {
  public:
    explicit Sdl3GamepadAssignment(std::vector<std::string> roles)
        : roles_(std::move(roles)), initialized_(SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        poll();
    }
    Sdl3GamepadAssignment(const Sdl3GamepadAssignment&) = delete;
    Sdl3GamepadAssignment& operator=(const Sdl3GamepadAssignment&) = delete;
    ~Sdl3GamepadAssignment() {
        for (auto& device : devices_)
            SDL_CloseGamepad(device.gamepad);
        if (initialized_)
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }

    void poll() {
        ready_ = false;
        if (roles_.empty()) {
            status_ = "No controller roles selected. Cancel and try again.";
            return;
        }
        if (!initialized_) {
            status_ = "Gamepad support could not be initialized. Cancel and try again.";
            return;
        }
        SDL_UpdateGamepads();
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        if (!ids) {
            status_ = "Gamepads could not be read. Cancel and try again.";
            return;
        }
        std::vector<SDL_JoystickID> current(ids, ids + count);
        SDL_free(ids);
        for (auto it = devices_.begin(); it != devices_.end();) {
            if (std::ranges::find(current, it->id) == current.end() ||
                !SDL_GamepadConnected(it->gamepad)) {
                SDL_CloseGamepad(it->gamepad);
                it = devices_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto id : current) {
            if (std::ranges::find(devices_, id, &Device::id) == devices_.end()) {
                if (auto* pad = SDL_OpenGamepad(id))
                    devices_.push_back({.id = id, .gamepad = pad});
            }
        }
        for (std::size_t player = 0; player < selected_.size(); ++player) {
            if (std::ranges::find(devices_, selected_[player], &Device::id) == devices_.end()) {
                selected_.resize(player);
                status_ =
                    "A selected controller disconnected. Assign " + roles_[player] + " again.";
                break;
            }
        }
        std::vector<SDL_JoystickID> fresh;
        bool selected_held = false;
        for (auto& device : devices_) {
            std::bitset<SDL_GAMEPAD_BUTTON_COUNT> pressed;
            for (int button = 0; button < SDL_GAMEPAD_BUTTON_COUNT; ++button)
                pressed[button] =
                    SDL_GetGamepadButton(device.gamepad, static_cast<SDL_GamepadButton>(button));
            if (device.primed && (pressed & ~device.previous).any())
                fresh.push_back(device.id);
            device.previous = pressed;
            device.primed = true;
            if (std::ranges::find(selected_, device.id) != selected_.end())
                selected_held |= pressed.any();
        }
        ready_ = selected_.size() == roles_.size() && !selected_held;
        if (selected_.size() == roles_.size()) {
            status_ =
                ready_ ? "All controllers are ready." : "Release the controller buttons to finish.";
            return;
        }
        if (fresh.size() > 1) {
            status_ = "Press a button on one controller at a time.";
        } else if (fresh.size() == 1) {
            const auto previous = std::ranges::find(selected_, fresh.front());
            if (previous != selected_.end()) {
                status_ = "That controller is already " + roles_[previous - selected_.begin()] +
                          ". Use a different controller.";
            } else {
                selected_.push_back(fresh.front());
                status_ = selected_.size() == roles_.size()
                              ? "Release the controller buttons to finish."
                              : prompt();
            }
        } else if (status_.empty()) {
            status_ = prompt();
        }
    }

    [[nodiscard]] bool ready() const {
        return ready_;
    }
    [[nodiscard]] std::size_t assigned_count() const {
        return selected_.size();
    }
    [[nodiscard]] const std::string& status() const {
        return status_;
    }
    [[nodiscard]] const std::vector<SDL_JoystickID>& assignments() const {
        return selected_;
    }
    [[nodiscard]] const std::string& role_name(std::size_t player) const {
        return roles_.at(player);
    }
    [[nodiscard]] std::string device_name(std::size_t player) const {
        if (player >= selected_.size())
            return "Not assigned";
        const auto found = std::ranges::find(devices_, selected_[player], &Device::id);
        if (found == devices_.end())
            return "Disconnected";
        const auto* name = SDL_GetGamepadName(found->gamepad);
        return name && *name ? std::string(name).substr(0, 256) : "Gamepad";
    }

  private:
    struct Device {
        SDL_JoystickID id = 0;
        SDL_Gamepad* gamepad = nullptr;
        std::bitset<SDL_GAMEPAD_BUTTON_COUNT> previous{};
        bool primed = false;
    };
    [[nodiscard]] std::string prompt() const {
        return "Press a button for " + roles_.at(selected_.size()) +
               ". Release any buttons already held.";
    }
    std::vector<std::string> roles_;
    bool initialized_ = false;
    bool ready_ = false;
    std::vector<Device> devices_;
    std::vector<SDL_JoystickID> selected_;
    std::string status_;
};
} // namespace mapperbus::frontend
