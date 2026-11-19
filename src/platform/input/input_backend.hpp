#pragma once

#include "platform/input/input_action.hpp"

namespace mapperbus::platform {

class InputBackend {
  public:
    virtual ~InputBackend() = default;

    virtual void poll() = 0;
    // Profiles 0/1 are built-in pads; 2..5 are external Famicom adapter pads.
    // Unsupported profiles must return false.
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
    /// Direct typing to a keyboard-equipped cartridge while gamepads remain available.
    void set_cartridge_keyboard_active(bool active) {
        cartridge_keyboard_active_ = active;
    }
    [[nodiscard]] bool cartridge_keyboard_active() const {
        return cartridge_keyboard_active_;
    }
    /// A physical keyboard key from USB HID usage page 0x07.
    virtual bool keyboard_key_down(core::Byte /*usage*/) const {
        return false;
    }
    virtual bool should_quit() const = 0;

  private:
    bool cartridge_keyboard_active_ = false;
};

} // namespace mapperbus::platform
