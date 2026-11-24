#pragma once

#include "platform/input/null_input.hpp"

namespace mapperbus::tests {

struct RewindInput : platform::NullInput {
    bool held = false;
    bool rewind_held() const override {
        return held;
    }
};

} // namespace mapperbus::tests
