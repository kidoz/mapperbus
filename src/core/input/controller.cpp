#include "core/input/controller.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {

void Controller::save_state(StateWriter& writer) const {
    writer.write_array(button_state_);
    writer.write_array(shift_register_);
    writer.write(strobe_);
}

void Controller::load_state(StateReader& reader) {
    reader.read_array(button_state_);
    reader.read_array(shift_register_);
    reader.read(strobe_);
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
}

Byte Controller::read(int player) {
    if (player < 0 || player > 1)
        return 0;
    if (strobe_) {
        return button_state_[player] & 0x01;
    }
    Byte value = shift_register_[player] & 0x01;
    shift_register_[player] >>= 1;
    return value;
}

void Controller::write(Byte value) {
    strobe_ = (value & 0x01) != 0;
    if (strobe_) {
        shift_register_[0] = button_state_[0];
        shift_register_[1] = button_state_[1];
    }
}

} // namespace mapperbus::core
