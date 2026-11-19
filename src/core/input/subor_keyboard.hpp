#pragma once

#include <array>

#include "core/state/state.hpp"
#include "core/types.hpp"

namespace mapperbus::core {

/// Parallel row view of the 13-by-8 Subor keyboard used in Asder computers.
class SuborKeyboard {
  public:
    void set_row(Byte row, Byte pressed_mask) {
        if (row < pressed_.size())
            pressed_[row] = pressed_mask;
    }
    [[nodiscard]] Byte read_row(Byte row) const {
        return row < pressed_.size() ? static_cast<Byte>(~pressed_[row]) : 0xFFU;
    }
    void save_state(StateWriter& writer) const {
        writer.write_array(pressed_);
    }
    void load_state(StateReader& reader) {
        decltype(pressed_) next{};
        reader.read_array(next);
        if (reader.ok())
            pressed_ = next;
    }

  private:
    std::array<Byte, 13U> pressed_{};
};
} // namespace mapperbus::core
