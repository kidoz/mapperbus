#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <catch2/catch_test_macros.hpp>

namespace mapperbus::tests {
struct IdentityGamepads {
    std::array<SDL_JoystickID, 4> ids{};
    std::array<SDL_Joystick*, 4> joysticks{};
    IdentityGamepads() {
        REQUIRE(SDL_InitSubSystem(SDL_INIT_GAMEPAD));
        int count = 0;
        SDL_free(SDL_GetGamepads(&count));
        if (count) {
            SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
            SKIP("Identity fixture needs isolated virtual gamepad slots 0..3");
        }
    }
    ~IdentityGamepads() {
        for (int pad = 0; pad < 4; ++pad)
            detach(pad);
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }
    void attach(int pad, int model) {
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
        desc.button_mask = (1U << SDL_GAMEPAD_BUTTON_COUNT) - 1;
        desc.vendor_id = 0x1234;
        desc.product_id = static_cast<Uint16>(model + 1);
        desc.name = "MapperBus identity fixture";
        ids[pad] = SDL_AttachVirtualJoystick(&desc);
        REQUIRE(ids[pad] != 0);
        joysticks[pad] = SDL_OpenJoystick(ids[pad]);
        REQUIRE(joysticks[pad] != nullptr);
    }
    void detach(int pad) {
        if (joysticks[pad])
            SDL_CloseJoystick(joysticks[pad]);
        joysticks[pad] = nullptr;
        if (ids[pad])
            SDL_DetachVirtualJoystick(ids[pad]);
        ids[pad] = 0;
    }
    void press(int pad, bool active) {
        REQUIRE(SDL_SetJoystickVirtualButton(joysticks[pad], SDL_GAMEPAD_BUTTON_EAST, active));
    }
};
} // namespace mapperbus::tests
