#pragma once

#include "platform/input/input_backend.hpp"

namespace mapperbus::platform {

class NullInput : public InputBackend {
  public:
    void poll() override {}
    bool is_button_pressed(int /*player*/, core::Button /*button*/) const override {
        return false;
    }
    bool should_quit() const override {
        return false;
    }
};

} // namespace mapperbus::platform
