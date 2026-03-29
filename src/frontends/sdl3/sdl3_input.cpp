#include "frontends/sdl3/sdl3_input.hpp"

namespace mapperbus::frontend {

void Sdl3Input::poll() {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) {
            quit_requested_ = true;
        }
    }

    const bool* keys = SDL_GetKeyboardState(nullptr);

    // Player 1 keyboard mapping
    button_state_[0] = 0;
    if (keys[SDL_SCANCODE_Z])
        button_state_[0] |= static_cast<uint8_t>(core::Button::B);
    if (keys[SDL_SCANCODE_X])
        button_state_[0] |= static_cast<uint8_t>(core::Button::A);
    if (keys[SDL_SCANCODE_RSHIFT])
        button_state_[0] |= static_cast<uint8_t>(core::Button::Select);
    if (keys[SDL_SCANCODE_RETURN])
        button_state_[0] |= static_cast<uint8_t>(core::Button::Start);
    if (keys[SDL_SCANCODE_UP])
        button_state_[0] |= static_cast<uint8_t>(core::Button::Up);
    if (keys[SDL_SCANCODE_DOWN])
        button_state_[0] |= static_cast<uint8_t>(core::Button::Down);
    if (keys[SDL_SCANCODE_LEFT])
        button_state_[0] |= static_cast<uint8_t>(core::Button::Left);
    if (keys[SDL_SCANCODE_RIGHT])
        button_state_[0] |= static_cast<uint8_t>(core::Button::Right);

    button_state_[1] = 0;
}

bool Sdl3Input::is_button_pressed(int player, core::Button button) const {
    if (player < 0 || player > 1)
        return false;
    return (button_state_[player] & static_cast<uint8_t>(button)) != 0;
}

bool Sdl3Input::should_quit() const {
    return quit_requested_;
}

} // namespace mapperbus::frontend
