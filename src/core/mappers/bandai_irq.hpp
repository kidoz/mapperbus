#pragma once

#include "core/state/state.hpp"

namespace mapperbus::core {

/// FCG live-counter and LZ93D50 latched-counter variants. Layout is unchanged
/// from the original BandaiFcg payload (two words followed by two byte flags).
class BandaiIrq {
  public:
    void reset() {
        latch_ = counter_ = 0U;
        enabled_ = pending_ = 0U;
    }
    void control(Byte value, bool latched) {
        enabled_ = value & 1U;
        pending_ = 0U;
        if (latched)
            counter_ = latch_;
        if (enabled_ != 0U && counter_ == 0U)
            pending_ = 1U;
    }
    void write(Byte value, bool high, bool latched) {
        auto& target = latched ? latch_ : counter_;
        const unsigned shift = high ? 8U : 0U;
        target = static_cast<Word>((static_cast<unsigned>(target) & ~(0xFFU << shift)) |
                                   (static_cast<unsigned>(value) << shift));
    }
    void clock() {
        if (enabled_ != 0U) {
            counter_ = static_cast<Word>(counter_ - 1U);
            if (counter_ == 0U)
                pending_ = 1U;
        }
    }
    bool pending() const {
        return pending_ != 0U;
    }
    void save_state(StateWriter& w) const {
        w.write(latch_);
        w.write(counter_);
        w.write(enabled_);
        w.write(pending_);
    }
    void load_state(StateReader& r) {
        r.read(latch_);
        r.read(counter_);
        r.read(enabled_);
        r.read(pending_);
        if (!r.ok() || enabled_ > 1U || pending_ > 1U) {
            reset();
            r.fail();
        }
    }

  private:
    Word latch_ = 0U;
    Word counter_ = 0U;
    Byte enabled_ = 0U;
    Byte pending_ = 0U;
};

} // namespace mapperbus::core
