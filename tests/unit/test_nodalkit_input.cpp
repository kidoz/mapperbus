#include <array>
#include <catch2/catch_test_macros.hpp>
#include <nk/platform/window.h>

#include "frontends/nodalkit/nodalkit_input.hpp"

namespace mapperbus::frontend {
namespace {

TEST_CASE("NodalKit gamepads route independent mappings and release disconnected inputs",
          "[gui][input][gamepad]") {
    struct VirtualPads {
        std::array<SDL_JoystickID, 4> ids{};
        std::array<SDL_Joystick*, 4> joysticks{};
        ~VirtualPads() {
            for (auto* joystick : joysticks) {
                if (joystick)
                    SDL_CloseJoystick(joystick);
            }
            for (auto id : ids) {
                if (id)
                    SDL_DetachVirtualJoystick(id);
            }
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        }
    } pads;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "MapperBus four profile test";
    std::array<platform::GamepadInputConfig, 4> configs;
    for (int player = 0; player < 4; ++player) {
        pads.ids[player] = SDL_AttachVirtualJoystick(&desc);
        REQUIRE(pads.ids[player] != 0);
        pads.joysticks[player] = SDL_OpenJoystick(pads.ids[player]);
        REQUIRE(pads.joysticks[player] != nullptr);
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        REQUIRE(ids != nullptr);
        const auto* found = std::find(ids, ids + count, pads.ids[player]);
        const auto index = static_cast<int>(found - ids);
        SDL_free(ids);
        REQUIRE(index < count);
        configs[player].gamepad_index = index;
    }
    using platform::InputAction;
    REQUIRE(platform::apply_gamepad_mapping(configs[1], "a=west,turbo_b=north,microphone=north"));
    REQUIRE(platform::apply_gamepad_mapping(configs[2], "a=north,turbo_a=west"));
    REQUIRE(platform::apply_gamepad_mapping(configs[3], "a=leftshoulder"));
    nk::Window window({.title = "Four gamepad profiles", .width = 600, .height = 300});
    NodalKitInput input(window, configs);
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[2], SDL_GAMEPAD_BUTTON_NORTH, true));
    REQUIRE(
        SDL_SetJoystickVirtualButton(pads.joysticks[3], SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[0], SDL_GAMEPAD_BUTTON_EAST, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_WEST, true));
    input.poll();
    CHECK(input.is_button_pressed(0, core::Button::A));
    CHECK(input.is_button_pressed(1, core::Button::A));
    CHECK(input.is_button_pressed(2, core::Button::A));
    CHECK(input.is_button_pressed(3, core::Button::A));
    // A complete unplug/replug between polls must accept the replacement ID.
    SDL_CloseJoystick(pads.joysticks[3]);
    pads.joysticks[3] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[3]));
    pads.ids[3] = SDL_AttachVirtualJoystick(&desc);
    REQUIRE(pads.ids[3] != 0);
    pads.joysticks[3] = SDL_OpenJoystick(pads.ids[3]);
    REQUIRE(pads.joysticks[3] != nullptr);
    REQUIRE(
        SDL_SetJoystickVirtualButton(pads.joysticks[3], SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    input.poll();
    CHECK(input.player_input(3).gamepad_connected());
    CHECK(input.is_button_pressed(3, core::Button::A));
    input.player_input(2).reset_default_gamepad_bindings();
    CHECK(input.player_input(2).uses_default_gamepad_bindings());
    input.player_input(2).set_gamepad_index(configs[2].gamepad_index);
    input.poll();
    CHECK_FALSE(input.is_button_pressed(2, core::Button::A));
    CHECK(input.is_button_pressed(3, core::Button::A));
    CHECK_FALSE(input.is_action_pressed(1, InputAction::TurboA));
    CHECK_FALSE(input.is_button_pressed(0, core::Button::B));
    // Capture must read only the selected player's device.
    const std::array controls{platform::gamepad_button_control(platform::GamepadButton::West)};
    CHECK(input.player_input(0).pressed_gamepad_controls(controls).empty());
    REQUIRE(input.player_input(1).pressed_gamepad_controls(controls).size() == 1);
    input.player_input(0).set_gamepad_enabled(false);
    input.poll();
    CHECK_FALSE(input.is_button_pressed(0, core::Button::A));
    CHECK(input.is_button_pressed(1, core::Button::A));
    input.player_input(1).set_gamepad_binding(InputAction::A,
                                              {.kind = platform::GamepadControlKind::None});
    CHECK_FALSE(input.is_button_pressed(1, core::Button::A));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_NORTH, true));
    input.poll();
    CHECK(input.is_action_pressed(1, InputAction::TurboB));
    CHECK(input.is_action_pressed(1, InputAction::Microphone));
    CHECK_FALSE(input.is_action_pressed(0, InputAction::Microphone));
    SDL_CloseJoystick(pads.joysticks[1]);
    pads.joysticks[1] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[1]));
    pads.ids[1] = 0;
    input.poll();
    CHECK_FALSE(input.is_action_pressed(1, InputAction::TurboB));
    CHECK_FALSE(input.is_action_pressed(1, InputAction::Microphone));
    CHECK_FALSE(input.player_input(1).gamepad_connected());
    CHECK(input.player_input(1).gamepad_status_text().find("disconnected") != std::string::npos);
    // Explicitly selecting the compacted index permits adopting its new device.
    input.player_input(1).set_gamepad_index(configs[1].gamepad_index);
    input.poll();
    CHECK(input.player_input(1).gamepad_connected());
    CHECK(input.is_action_pressed(1, InputAction::Microphone));
    input.player_input(1).set_gamepad_enabled(false);
    CHECK_FALSE(input.is_action_pressed(1, InputAction::Microphone));
    CHECK(input.is_button_pressed(3, core::Button::A));
    SDL_CloseJoystick(pads.joysticks[3]);
    pads.joysticks[3] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[3]));
    pads.ids[3] = 0;
    input.poll();
    CHECK_FALSE(input.is_button_pressed(3, core::Button::A));
    CHECK_FALSE(input.player_input(3).gamepad_connected());
}

TEST_CASE("NodalKit owns six independent gamepad profiles for Hori mode", "[gui][input][hori]") {
    struct VirtualPads {
        std::array<SDL_JoystickID, 6> ids{};
        std::array<SDL_Joystick*, 6> joysticks{};
        ~VirtualPads() {
            for (auto* joystick : joysticks)
                if (joystick)
                    SDL_CloseJoystick(joystick);
            for (auto id : ids)
                if (id)
                    SDL_DetachVirtualJoystick(id);
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
        }
    } pads;
    REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
    SDL_VirtualJoystickDesc desc{};
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    desc.name = "MapperBus Hori profiles";
    std::array<platform::GamepadInputConfig, 6> profiles;
    for (int player = 0; player < 6; ++player) {
        pads.ids[player] = SDL_AttachVirtualJoystick(&desc);
        REQUIRE(pads.ids[player] != 0);
        pads.joysticks[player] = SDL_OpenJoystick(pads.ids[player]);
        REQUIRE(pads.joysticks[player] != nullptr);
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        REQUIRE(ids != nullptr);
        const int index = static_cast<int>(std::find(ids, ids + count, pads.ids[player]) - ids);
        SDL_free(ids);
        REQUIRE(index < count);
        profiles[player].gamepad_index = index;
    }
    REQUIRE(platform::apply_gamepad_mapping(profiles[4], "a=back"));
    REQUIRE(platform::apply_gamepad_mapping(profiles[5], "a=start,microphone=leftshoulder"));
    nk::Window window({.title = "Six input profiles", .width = 600, .height = 300});
    NodalKitInput input(window, profiles);
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[4], SDL_GAMEPAD_BUTTON_BACK, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[5], SDL_GAMEPAD_BUTTON_START, true));
    REQUIRE(
        SDL_SetJoystickVirtualButton(pads.joysticks[5], SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, true));
    input.poll();
    CHECK(input.is_button_pressed(4, core::Button::A));
    CHECK(input.is_button_pressed(5, core::Button::A));
    CHECK_FALSE(input.is_button_pressed(0, core::Button::A));
    CHECK(input.is_action_pressed(5, platform::InputAction::Microphone));
    CHECK(input.player_input(5).uses_default_bindings());
    input.player_input(5).reset_default_gamepad_bindings();
    CHECK(input.player_input(5).uses_default_gamepad_bindings());
    input.player_input(5).set_gamepad_index(profiles[5].gamepad_index);
    input.poll();
    CHECK_FALSE(input.is_button_pressed(5, core::Button::A));
    CHECK_FALSE(input.is_action_pressed(5, platform::InputAction::Microphone));
    CHECK(input.is_button_pressed(4, core::Button::A));
    SDL_CloseJoystick(pads.joysticks[5]);
    pads.joysticks[5] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[5]));
    pads.ids[5] = 0;
    input.poll();
    CHECK_FALSE(input.player_input(5).gamepad_connected());
    CHECK(input.player_input(4).gamepad_connected());
}

} // namespace
} // namespace mapperbus::frontend
