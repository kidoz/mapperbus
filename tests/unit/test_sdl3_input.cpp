#include <catch2/catch_test_macros.hpp>
#include <string>

#include "frontends/sdl3/sdl3_input.hpp"

namespace mapperbus::frontend {
namespace {

TEST_CASE("SDL3 gamepad control parser accepts buttons, aliases, and axes", "[sdl3][input]") {
    auto east = parse_gamepad_control("east");
    REQUIRE(east);
    REQUIRE(east->kind == GamepadControlKind::Button);
    REQUIRE(east->button == platform::GamepadButton::East);

    auto dpad_up = parse_gamepad_control("dpad-up");
    REQUIRE(dpad_up);
    REQUIRE(dpad_up->kind == GamepadControlKind::Button);
    REQUIRE(dpad_up->button == platform::GamepadButton::DpadUp);

    auto left = parse_gamepad_control("leftx-");
    REQUIRE(left);
    REQUIRE(left->kind == GamepadControlKind::AxisNegative);
    REQUIRE(left->axis == platform::GamepadAxis::LeftX);

    auto right = parse_gamepad_control("+leftx");
    REQUIRE(right);
    REQUIRE(right->kind == GamepadControlKind::AxisPositive);
    REQUIRE(right->axis == platform::GamepadAxis::LeftX);
}

TEST_CASE("SDL3 gamepad mapping parser updates NES bindings", "[sdl3][input]") {
    Sdl3InputConfig config;

    auto result = apply_gamepad_mapping(config, "a=east,b=south,up=lefty-,down=lefty+,select=back");
    REQUIRE(result);

    const auto& a = gamepad_control_for_button(config, core::Button::A);
    REQUIRE(a.kind == GamepadControlKind::Button);
    REQUIRE(a.button == platform::GamepadButton::East);

    const auto& b = gamepad_control_for_button(config, core::Button::B);
    REQUIRE(b.kind == GamepadControlKind::Button);
    REQUIRE(b.button == platform::GamepadButton::South);

    const auto& up = gamepad_control_for_button(config, core::Button::Up);
    REQUIRE(up.kind == GamepadControlKind::AxisNegative);
    REQUIRE(up.axis == platform::GamepadAxis::LeftY);

    const auto& down = gamepad_control_for_button(config, core::Button::Down);
    REQUIRE(down.kind == GamepadControlKind::AxisPositive);
    REQUIRE(down.axis == platform::GamepadAxis::LeftY);

    const auto& select = gamepad_control_for_button(config, core::Button::Select);
    REQUIRE(select.kind == GamepadControlKind::Button);
    REQUIRE(select.button == platform::GamepadButton::Back);
}

TEST_CASE("SDL3 gamepad mapping parser reports invalid entries", "[sdl3][input]") {
    Sdl3InputConfig config;

    auto unknown_button = apply_gamepad_mapping(config, "jump=a");
    REQUIRE_FALSE(unknown_button);

    auto missing_direction = apply_gamepad_mapping(config, "up=lefty");
    REQUIRE_FALSE(missing_direction);
    REQUIRE(missing_direction.error().find("+ or - direction") != std::string::npos);

    auto unknown_control = apply_gamepad_mapping(config, "a=launch");
    REQUIRE_FALSE(unknown_control);
}

TEST_CASE("SDL3 scaler hotkeys map to runtime commands", "[sdl3][input]") {
    auto native = Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_0);
    REQUIRE(native.kind == Sdl3ScalerCommandKind::Native);

    auto factor = Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_4);
    REQUIRE(factor.kind == Sdl3ScalerCommandKind::SetFactor);
    REQUIRE(factor.factor == 4);

    auto cycle = Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_F9);
    REQUIRE(cycle.kind == Sdl3ScalerCommandKind::CycleMode);

    auto ignored = Sdl3Input::scaler_command_for_scancode(SDL_SCANCODE_A);
    REQUIRE_FALSE(ignored);
}

TEST_CASE("SDL3 save-state hotkeys map to session commands", "[sdl3][input]") {
    auto save = Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F5, false, 0);
    REQUIRE(save.kind == Sdl3SessionCommandKind::SaveState);
    REQUIRE(save.slot == 0);

    auto load = Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F7, false, 0);
    REQUIRE(load.kind == Sdl3SessionCommandKind::LoadState);

    // Shift+F5 targets slot 1.
    auto save_slot1 = Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F5, true, 0);
    REQUIRE(save_slot1.kind == Sdl3SessionCommandKind::SaveState);
    REQUIRE(save_slot1.slot == 1);

    // F9 belongs to the scaler hotkeys and must stay out of the session set.
    REQUIRE_FALSE(Sdl3Input::session_command_for_scancode(SDL_SCANCODE_F9, false, 0));
    REQUIRE_FALSE(Sdl3Input::session_command_for_scancode(SDL_SCANCODE_A, false, 0));
}

TEST_CASE("SDL3 window hotkeys map to fullscreen/vsync", "[sdl3][input]") {
    auto fullscreen = Sdl3Input::window_command_for_scancode(SDL_SCANCODE_F11);
    REQUIRE(fullscreen.kind == Sdl3WindowCommandKind::ToggleFullscreen);

    auto vsync = Sdl3Input::window_command_for_scancode(SDL_SCANCODE_V);
    REQUIRE(vsync.kind == Sdl3WindowCommandKind::ToggleVsync);

    REQUIRE_FALSE(Sdl3Input::window_command_for_scancode(SDL_SCANCODE_A));
}

} // namespace
} // namespace mapperbus::frontend
