#pragma once

#include "platform/input/input_action.hpp"

namespace mapperbus::platform {

class InputBackend {
  public:
    virtual ~InputBackend() = default;

    virtual void poll() = 0;
    virtual bool is_button_pressed(int player, core::Button button) const = 0;
    // Backends without extra actions (e.g. headless input) need no turbo support.
    virtual bool is_action_pressed(int player, InputAction action) const {
        switch (action) {
        case InputAction::TurboA:
        case InputAction::TurboB:
        case InputAction::Microphone:
            return false;
        default:
            for (auto button : {core::Button::A,
                                core::Button::B,
                                core::Button::Select,
                                core::Button::Start,
                                core::Button::Up,
                                core::Button::Down,
                                core::Button::Left,
                                core::Button::Right}) {
                if (input_action(button) == action)
                    return is_button_pressed(player, button);
            }
            return false;
        }
    }
    virtual bool should_quit() const = 0;
};

} // namespace mapperbus::platform
