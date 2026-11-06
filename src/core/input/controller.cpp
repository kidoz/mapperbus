#include "core/input/controller.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {

void Controller::save_state(StateWriter& writer) const {
    writer.write_array(button_state_);
    writer.write_array(shift_register_);
    writer.write(static_cast<Byte>(strobe_));
}

void Controller::load_state(StateReader& reader) {
    reader.read_array(button_state_);
    reader.read_array(shift_register_);
    const auto strobe = reader.read<Byte>();
    strobe_ = strobe == 1;
    if (strobe > 1)
        reader.fail();
}

void Controller::set_button_state(int player, Button button, bool pressed) {
    if (player < 0 || player > 1)
        return;
    auto bit = static_cast<Byte>(button);
    if (pressed) {
        button_state_[player] |= bit;
    } else {
        button_state_[player] &= ~bit;
    }
    if (strobe_)
        shift_register_[player] = button_state_[player];
}

Byte Controller::read(int player) {
    const auto value = peek(player);
    clock(player);
    return value;
}

Byte Controller::peek(int player) const {
    if (player < 0 || player > 1)
        return 0;
    if (strobe_) {
        return button_state_[player] & 0x01U;
    }
    return shift_register_[player] & 0x01U;
}

void Controller::clock(int player) {
    if (player < 0 || player > 1 || strobe_)
        return;
    shift_register_[player] =
        static_cast<Byte>((static_cast<unsigned>(shift_register_[player]) >> 1U) | 0x80U);
}

void Controller::write(Byte value) {
    strobe_ = (value & 0x01U) != 0;
    if (strobe_) {
        shift_register_[0] = button_state_[0];
        shift_register_[1] = button_state_[1];
    }
}

} // namespace mapperbus::core
