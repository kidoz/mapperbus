#pragma once

#include "core/input/controller.hpp"

namespace mapperbus::platform {

class InputBackend {
  public:
    virtual ~InputBackend() = default;

    virtual void poll() = 0;
    virtual bool is_button_pressed(int player, core::Button button) const = 0;
    virtual bool should_quit() const = 0;
};

} // namespace mapperbus::platform
