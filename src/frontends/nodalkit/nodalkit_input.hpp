#pragma once

#include <array>
#include <nk/platform/key_codes.h>
#include <nk/platform/window.h>

#include "platform/input/input_backend.hpp"

namespace mapperbus::frontend {

class NodalKitInput : public platform::InputBackend {
  public:
    explicit NodalKitInput(nk::Window& window) : window_(window) {
        reset_default_bindings();
    }

    void poll() override {}

    bool is_button_pressed(int player, core::Button button) const override {
        if (player != 0) {
            return false;
        }

        return window_.is_key_pressed(binding(button));
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

    nk::Window& window_;
    std::array<nk::KeyCode, 8> bindings_{};
};

} // namespace mapperbus::frontend
