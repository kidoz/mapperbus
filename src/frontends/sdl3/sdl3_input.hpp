#pragma once

#include <SDL3/SDL.h>
#include <array>
#include <cstdint>

#include "platform/input/input_backend.hpp"

namespace mapperbus::frontend {

class Sdl3Input : public platform::InputBackend {
  public:
    void poll() override;
    bool is_button_pressed(int player, core::Button button) const override;
    bool should_quit() const override;

  private:
    bool quit_requested_ = false;
    std::array<std::uint8_t, 2> button_state_{};
};

} // namespace mapperbus::frontend
