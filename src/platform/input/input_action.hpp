#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "core/input/controller.hpp"

namespace mapperbus::platform {

// Binding actions are independent of the eight bits on an NES controller.
enum class InputAction : std::uint8_t {
    Up,
    Down,
    Left,
    Right,
    A,
    B,
    Start,
    Select,
    TurboA,
    TurboB,
    Microphone,
};

inline constexpr std::size_t kInputActionCount = 11;

[[nodiscard]] constexpr auto input_actions() {
    return std::array{InputAction::Up,
                      InputAction::Down,
                      InputAction::Left,
                      InputAction::Right,
                      InputAction::A,
                      InputAction::B,
                      InputAction::Start,
                      InputAction::Select,
                      InputAction::TurboA,
                      InputAction::TurboB,
                      InputAction::Microphone};
}

[[nodiscard]] constexpr std::size_t button_index(InputAction action) {
    return static_cast<std::size_t>(action);
}

[[nodiscard]] constexpr InputAction input_action(core::Button button) {
    switch (button) {
    case core::Button::Up:
        return InputAction::Up;
    case core::Button::Down:
        return InputAction::Down;
    case core::Button::Left:
        return InputAction::Left;
    case core::Button::Right:
        return InputAction::Right;
    case core::Button::A:
        return InputAction::A;
    case core::Button::B:
        return InputAction::B;
    case core::Button::Start:
        return InputAction::Start;
    case core::Button::Select:
        return InputAction::Select;
    }
    return InputAction::Up;
}

} // namespace mapperbus::platform
