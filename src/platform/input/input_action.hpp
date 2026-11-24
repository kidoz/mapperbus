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
    Rewind,
};

inline constexpr std::size_t kInputActionCount = static_cast<std::size_t>(InputAction::Rewind) + 1;
// Two built-in pads plus up to four Famicom adapter pads.
inline constexpr int kInputProfileCount = 6;

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
                      InputAction::Microphone,
                      InputAction::Rewind};
}

[[nodiscard]] constexpr std::size_t button_index(InputAction action) {
    return static_cast<std::size_t>(action);
}

/// A gameplay binding takes priority over a shared rewind shortcut.
template <typename Key>
[[nodiscard]] constexpr bool has_gameplay_key(const std::array<Key, kInputActionCount>& bindings,
                                              Key key) {
    for (auto action : input_actions())
        if (action != InputAction::Rewind && bindings[button_index(action)] == key)
            return true;
    return false;
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
