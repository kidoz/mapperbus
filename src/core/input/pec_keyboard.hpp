#pragma once

#include "core/input/subor_keyboard.hpp"

namespace mapperbus::core {

// PEC-586 and Golden Key keyboards scan a row serially, MSB first. The
// computer wires the expansion-port result to both D1 and D6 on $4017.
class PecKeyboard {
  public:
    explicit PecKeyboard(Byte rows = 13) : rows_(rows) {}
    void set_row(Byte row, Byte mask) {
        matrix_.set_row(row, mask);
    }
    void write(Byte value) {
        if ((value & ~strobe_) & 2U)
            row_ = 0;
        if ((strobe_ & ~value) & 1U)
            column_ = 0;
        if ((strobe_ & ~value) & 4U)
            row_ = (row_ + 1U) % rows_;
        strobe_ = value & 7U;
    }
    [[nodiscard]] Byte read(int port, Byte bus) const {
        if (port != 1)
            return bus;
        const bool pressed = !(matrix_.read_row(row_) & (0x80U >> column_));
        return static_cast<Byte>((bus & ~0x42U) | (pressed ? 0x42U : 0U));
    }
    void clock(int port) {
        if (port == 1)
            column_ = (column_ + 1U) & 7U;
    }
    void reset() {
        row_ = column_ = strobe_ = 0;
    }
    void save_state(StateWriter& w) const {
        matrix_.save_state(w);
        w.write(row_);
        w.write(column_);
        w.write(strobe_);
    }
    void load_state(StateReader& r) {
        auto next = *this;
        next.matrix_.load_state(r);
        r.read(next.row_);
        r.read(next.column_);
        r.read(next.strobe_);
        if (!r.ok() || next.row_ >= rows_ || next.column_ >= 8 || next.strobe_ >= 8) {
            r.fail();
            return;
        }
        *this = next;
    }

  private:
    SuborKeyboard matrix_;
    Byte rows_, row_ = 0, column_ = 0, strobe_ = 0;
};
} // namespace mapperbus::core
