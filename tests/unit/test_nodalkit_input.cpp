#include <array>
#include <catch2/catch_test_macros.hpp>
#include <nk/platform/window.h>

#include "frontends/nodalkit/nodalkit_input.hpp"

namespace mapperbus::frontend {
namespace {

TEST_CASE("NodalKit gamepads route independent mappings and release disconnected inputs",
          "[gui][input][gamepad]") {
    struct VirtualPads {
        std::array<SDL_JoystickID, 2> ids{};
        std::array<SDL_Joystick*, 2> joysticks{};
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
    desc.name = "MapperBus two player test";
    std::array<platform::GamepadInputConfig, 2> configs;
    for (int player = 0; player < 2; ++player) {
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
    REQUIRE(platform::apply_gamepad_mapping(configs[1], "a=west,turbo_b=north"));
    nk::Window window({.title = "Two player gamepads", .width = 600, .height = 300});
    NodalKitInput input(window, configs[0], configs[1]);
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[0], SDL_GAMEPAD_BUTTON_EAST, true));
    REQUIRE(SDL_SetJoystickVirtualButton(pads.joysticks[1], SDL_GAMEPAD_BUTTON_WEST, true));
    input.poll();
    CHECK(input.is_button_pressed(0, core::Button::A));
    CHECK(input.is_button_pressed(1, core::Button::A));
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
    SDL_CloseJoystick(pads.joysticks[1]);
    pads.joysticks[1] = nullptr;
    REQUIRE(SDL_DetachVirtualJoystick(pads.ids[1]));
    pads.ids[1] = 0;
    input.poll();
    CHECK_FALSE(input.is_action_pressed(1, InputAction::TurboB));
    CHECK_FALSE(input.player_input(1).gamepad_connected());
}

} // namespace
} // namespace mapperbus::frontend
