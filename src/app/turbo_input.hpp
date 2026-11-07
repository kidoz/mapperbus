#pragma once

#include <array>

#include "platform/input/input_action.hpp"

namespace mapperbus::app {

// Session policy: phase advances only when a frame consumes input. It never
// depends on UI polls, controller strobes, or elapsed host time.
class TurboInput {
  public:
    void set_period(int frames) {
        const int period = frames == 2 || frames == 6 ? frames : 4;
        if (period_ != period) {
            period_ = period;
            reset();
        }
    }

    void reset() {
        phases_ = {};
    }

    [[nodiscard]] bool next(int player, int button, bool held) {
        auto& phase = phases_[player][button];
        if (!held) {
            phase = 0;
            return false;
        }
        const bool pressed = phase < period_ / 2;
        phase = (phase + 1) % period_;
        return pressed;
    }

  private:
    int period_ = 4;
    std::array<std::array<int, 2>, platform::kInputProfileCount> phases_{};
};

} // namespace mapperbus::app
