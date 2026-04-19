#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/input/controller.hpp"
#include "core/types.hpp"

namespace mapperbus::platform {

enum class GamepadControlKind {
    Button,
    AxisNegative,
    AxisPositive,
};

enum class GamepadButton {
    South,
    East,
    West,
    North,
    Back,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
};

enum class GamepadAxis {
    LeftX,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
};

struct GamepadControl {
    GamepadControlKind kind = GamepadControlKind::Button;
    GamepadButton button = GamepadButton::South;
    GamepadAxis axis = GamepadAxis::LeftX;

    friend bool operator==(const GamepadControl&, const GamepadControl&) = default;
};

struct GamepadInputConfig {
    bool enabled = true;
    int gamepad_index = 0;
    std::int16_t axis_deadzone = 12000;
    std::array<GamepadControl, 8> bindings{};

    GamepadInputConfig();
};

[[nodiscard]] constexpr std::array<core::Button, 8> controller_buttons() {
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

[[nodiscard]] constexpr std::size_t button_index(core::Button button) {
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

[[nodiscard]] constexpr GamepadControl gamepad_button_control(GamepadButton button) {
    return {.kind = GamepadControlKind::Button, .button = button};
}

[[nodiscard]] constexpr GamepadControl gamepad_axis_control(GamepadAxis axis,
                                                            GamepadControlKind kind) {
    return {.kind = kind, .axis = axis};
}

[[nodiscard]] inline std::array<GamepadControl, 8> default_gamepad_bindings() {
    std::array<GamepadControl, 8> bindings{};
    bindings[button_index(core::Button::Up)] = gamepad_button_control(GamepadButton::DpadUp);
    bindings[button_index(core::Button::Down)] = gamepad_button_control(GamepadButton::DpadDown);
    bindings[button_index(core::Button::Left)] = gamepad_button_control(GamepadButton::DpadLeft);
    bindings[button_index(core::Button::Right)] = gamepad_button_control(GamepadButton::DpadRight);
    bindings[button_index(core::Button::A)] = gamepad_button_control(GamepadButton::East);
    bindings[button_index(core::Button::B)] = gamepad_button_control(GamepadButton::South);
    bindings[button_index(core::Button::Start)] = gamepad_button_control(GamepadButton::Start);
    bindings[button_index(core::Button::Select)] = gamepad_button_control(GamepadButton::Back);
    return bindings;
}

inline GamepadInputConfig::GamepadInputConfig() : bindings(default_gamepad_bindings()) {}

[[nodiscard]] inline const GamepadControl& gamepad_control_for_button(
    const GamepadInputConfig& config, core::Button button) {
    return config.bindings[button_index(button)];
}

[[nodiscard]] inline std::string trim_copy(std::string_view text) {
    auto begin = text.begin();
    auto end = text.end();

    while (begin != end && std::isspace(static_cast<unsigned char>(*begin)) != 0) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1))) != 0) {
        --end;
    }

    return std::string(begin, end);
}

[[nodiscard]] inline std::string normalized_token(std::string_view text) {
    std::string normalized;
    normalized.reserve(text.size());

    for (const char value : trim_copy(text)) {
        if (value == '-' || value == '_' || std::isspace(static_cast<unsigned char>(value)) != 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(value))));
    }

    return normalized;
}

[[nodiscard]] inline core::Result<core::Button> parse_controller_button(std::string_view text) {
    const std::string name = normalized_token(text);

    if (name == "a") {
        return core::Button::A;
    }
    if (name == "b") {
        return core::Button::B;
    }
    if (name == "select") {
        return core::Button::Select;
    }
    if (name == "start") {
        return core::Button::Start;
    }
    if (name == "up") {
        return core::Button::Up;
    }
    if (name == "down") {
        return core::Button::Down;
    }
    if (name == "left") {
        return core::Button::Left;
    }
    if (name == "right") {
        return core::Button::Right;
    }

    return std::unexpected("unknown NES button '" + trim_copy(text) + "'");
}

[[nodiscard]] inline core::Result<GamepadButton> parse_gamepad_button(std::string_view text) {
    const std::string name = normalized_token(text);

    if (name == "south" || name == "cross" || name == "a") {
        return GamepadButton::South;
    }
    if (name == "east" || name == "circle" || name == "b") {
        return GamepadButton::East;
    }
    if (name == "west" || name == "square" || name == "x") {
        return GamepadButton::West;
    }
    if (name == "north" || name == "triangle" || name == "y") {
        return GamepadButton::North;
    }
    if (name == "back" || name == "select") {
        return GamepadButton::Back;
    }
    if (name == "start") {
        return GamepadButton::Start;
    }
    if (name == "leftstick") {
        return GamepadButton::LeftStick;
    }
    if (name == "rightstick") {
        return GamepadButton::RightStick;
    }
    if (name == "leftshoulder" || name == "leftbumper" || name == "l1" || name == "lb") {
        return GamepadButton::LeftShoulder;
    }
    if (name == "rightshoulder" || name == "rightbumper" || name == "r1" || name == "rb") {
        return GamepadButton::RightShoulder;
    }
    if (name == "dpadup" || name == "dpup") {
        return GamepadButton::DpadUp;
    }
    if (name == "dpaddown" || name == "dpdown") {
        return GamepadButton::DpadDown;
    }
    if (name == "dpadleft" || name == "dpleft") {
        return GamepadButton::DpadLeft;
    }
    if (name == "dpadright" || name == "dpright") {
        return GamepadButton::DpadRight;
    }

    return std::unexpected("unknown gamepad button '" + trim_copy(text) + "'");
}

[[nodiscard]] inline core::Result<GamepadAxis> parse_gamepad_axis_name(std::string_view text) {
    const std::string name = normalized_token(text);

    if (name == "leftx") {
        return GamepadAxis::LeftX;
    }
    if (name == "lefty") {
        return GamepadAxis::LeftY;
    }
    if (name == "rightx") {
        return GamepadAxis::RightX;
    }
    if (name == "righty") {
        return GamepadAxis::RightY;
    }
    if (name == "lefttrigger" || name == "lt" || name == "l2") {
        return GamepadAxis::LeftTrigger;
    }
    if (name == "righttrigger" || name == "rt" || name == "r2") {
        return GamepadAxis::RightTrigger;
    }

    return std::unexpected("unknown gamepad axis '" + trim_copy(text) + "'");
}

[[nodiscard]] inline core::Result<GamepadControl> parse_gamepad_axis_control(
    std::string_view text) {
    std::string token = trim_copy(text);
    if (token.empty()) {
        return std::unexpected(std::string("empty gamepad control"));
    }

    GamepadControlKind kind = GamepadControlKind::AxisPositive;
    bool has_polarity = false;
    if (token.front() == '+' || token.front() == '-') {
        kind = token.front() == '-' ? GamepadControlKind::AxisNegative
                                    : GamepadControlKind::AxisPositive;
        has_polarity = true;
        token.erase(token.begin());
    } else if (token.back() == '+' || token.back() == '-') {
        kind = token.back() == '-' ? GamepadControlKind::AxisNegative
                                   : GamepadControlKind::AxisPositive;
        has_polarity = true;
        token.pop_back();
    }

    auto axis = parse_gamepad_axis_name(token);
    if (!axis) {
        return std::unexpected(axis.error());
    }

    const bool trigger = *axis == GamepadAxis::LeftTrigger || *axis == GamepadAxis::RightTrigger;
    if (!has_polarity && !trigger) {
        return std::unexpected("axis control '" + trim_copy(text) + "' needs + or - direction");
    }

    return gamepad_axis_control(*axis, kind);
}

[[nodiscard]] inline bool is_gamepad_axis_token(std::string_view text) {
    std::string token = trim_copy(text);
    if (token.empty()) {
        return false;
    }
    if (token.front() == '+' || token.front() == '-') {
        token.erase(token.begin());
    } else if (token.back() == '+' || token.back() == '-') {
        token.pop_back();
    }

    return parse_gamepad_axis_name(token).has_value();
}

[[nodiscard]] inline core::Result<GamepadControl> parse_gamepad_control(std::string_view text) {
    const std::string token = trim_copy(text);
    if (token.empty()) {
        return std::unexpected(std::string("empty gamepad control"));
    }

    auto axis_result = parse_gamepad_axis_control(token);
    if (axis_result) {
        return axis_result;
    }
    if (is_gamepad_axis_token(token)) {
        return axis_result;
    }

    auto button = parse_gamepad_button(token);
    if (!button) {
        return std::unexpected(button.error());
    }

    return gamepad_button_control(*button);
}

[[nodiscard]] inline core::Result<void> apply_gamepad_mapping(GamepadInputConfig& config,
                                                              std::string_view mapping) {
    std::size_t offset = 0;
    while (offset < mapping.size()) {
        const std::size_t separator = mapping.find_first_of(",;", offset);
        const std::size_t end = separator == std::string_view::npos ? mapping.size() : separator;
        const std::string pair = trim_copy(mapping.substr(offset, end - offset));

        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            if (equals == std::string::npos) {
                return std::unexpected("gamepad mapping entry '" + pair + "' is missing '='");
            }

            auto nes_button = parse_controller_button(std::string_view(pair).substr(0, equals));
            if (!nes_button) {
                return std::unexpected(nes_button.error());
            }

            auto control = parse_gamepad_control(std::string_view(pair).substr(equals + 1));
            if (!control) {
                return std::unexpected(control.error());
            }

            config.bindings[button_index(*nes_button)] = *control;
        }

        if (separator == std::string_view::npos) {
            break;
        }
        offset = separator + 1;
    }

    return {};
}

[[nodiscard]] inline std::string gamepad_button_token(GamepadButton button) {
    switch (button) {
    case GamepadButton::South:
        return "south";
    case GamepadButton::East:
        return "east";
    case GamepadButton::West:
        return "west";
    case GamepadButton::North:
        return "north";
    case GamepadButton::Back:
        return "back";
    case GamepadButton::Start:
        return "start";
    case GamepadButton::LeftStick:
        return "leftstick";
    case GamepadButton::RightStick:
        return "rightstick";
    case GamepadButton::LeftShoulder:
        return "leftshoulder";
    case GamepadButton::RightShoulder:
        return "rightshoulder";
    case GamepadButton::DpadUp:
        return "dpup";
    case GamepadButton::DpadDown:
        return "dpdown";
    case GamepadButton::DpadLeft:
        return "dpleft";
    case GamepadButton::DpadRight:
        return "dpright";
    }

    return "south";
}

[[nodiscard]] inline std::string gamepad_axis_token(GamepadAxis axis) {
    switch (axis) {
    case GamepadAxis::LeftX:
        return "leftx";
    case GamepadAxis::LeftY:
        return "lefty";
    case GamepadAxis::RightX:
        return "rightx";
    case GamepadAxis::RightY:
        return "righty";
    case GamepadAxis::LeftTrigger:
        return "lefttrigger";
    case GamepadAxis::RightTrigger:
        return "righttrigger";
    }

    return "leftx";
}

[[nodiscard]] inline std::string gamepad_control_token(const GamepadControl& control) {
    if (control.kind == GamepadControlKind::Button) {
        return gamepad_button_token(control.button);
    }

    std::string token = gamepad_axis_token(control.axis);
    if (control.kind == GamepadControlKind::AxisNegative) {
        token.push_back('-');
    } else {
        token.push_back('+');
    }
    return token;
}

} // namespace mapperbus::platform
