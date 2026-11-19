#pragma once

#include "core/input/pec_keyboard.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class PecComputer : public Mapper {
  public:
    void set_keyboard_row(Byte row, Byte mask) override {
        keyboard_.set_row(row, mask);
    }
    void observe_cpu_write(Address addr, Byte value) override {
        if (addr == 0x4016)
            keyboard_.write(value);
    }
    Byte controller_port_bits(int port, Byte bus) const override {
        return keyboard_.read(port, bus);
    }
    void clock_controller_port(int port) override {
        keyboard_.clock(port);
    }

  protected:
    explicit PecComputer(Byte rows = 13) : keyboard_(rows) {}
    PecKeyboard keyboard_;
};
} // namespace mapperbus::core
