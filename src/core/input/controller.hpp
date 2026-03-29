#pragma once

#include <array>
#include <cstdint>

#include "core/types.hpp"

namespace mapperbus::core {

enum class Button : std::uint8_t {
    A = 0x01,
    B = 0x02,
    Select = 0x04,
    Start = 0x08,
    Up = 0x10,
    Down = 0x20,
    Left = 0x40,
    Right = 0x80,
};

class Controller {
  public:
    void set_button_state(int player, Button button, bool pressed);
    Byte read(int player);
    void write(Byte value);

  private:
    std::array<Byte, 2> button_state_{};
    std::array<Byte, 2> shift_register_{};
    bool strobe_ = false;
};

} // namespace mapperbus::core
