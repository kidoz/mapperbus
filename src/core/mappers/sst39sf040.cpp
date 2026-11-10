#include "core/mappers/sst39sf040.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
// Fixed M2 durations within DS20005022F maximum times at NES NTSC/PAL/Dendy
// clock rates. Analog programming, wear and completion races are not modeled.
constexpr uint32_t kProgramCycles = 32U;
constexpr uint32_t kSectorCycles = 36000U;
constexpr uint32_t kChipCycles = 144000U;
} // namespace
Sst39sf040::Sst39sf040(std::vector<Byte> image) : data_(std::move(image)) {
    const auto source_size = data_.size();
    data_.resize(kSize, 0xFFU);
    // Short power-of-two dumps mirror their fitted address lines initially.
    if (source_size != 0U)
        for (std::size_t i = source_size; i < kSize; ++i)
            data_[i] = data_[i % source_size];
}
Byte Sst39sf040::read(uint32_t address) {
    address &= 0x7FFFFU;
    if (remaining_ != 0U) {
        const Byte value = static_cast<Byte>((operation_ == Operation::Program
                                                  ? (~static_cast<unsigned>(pending_data_) & 0x80U)
                                                  : 0U) |
                                             (toggle_ == 0U ? 0x40U : 0U));
        toggle_ ^= 1U;
        return value;
    }
    if (id_mode_ != 0U)
        return address == 0U ? 0xBFU : address == 1U ? 0xB7U : 0xFFU;
    return data_[address];
}
void Sst39sf040::begin(Operation operation, uint32_t address, Byte data, uint32_t cycles) {
    operation_ = operation;
    pending_address_ = address;
    pending_data_ = data;
    remaining_ = cycles;
    toggle_ = 0U;
    id_mode_ = 0U;
    command_ = Command::Read;
}
void Sst39sf040::write(uint32_t address, Byte data) {
    if (remaining_ != 0U)
        return; // Includes F0: reset cannot abort programming.
    address &= 0x7FFFFU;
    const auto decoded = address & 0x7FFFU; // Command address pins A14..A0.
    if (command_ == Command::Program) {
        begin(Operation::Program, address, data, kProgramCycles);
        return;
    }
    if (data == 0xF0U) {
        command_ = Command::Read;
        id_mode_ = 0U;
        return;
    }
    switch (command_) {
    case Command::Read:
        if (decoded == 0x5555U && data == 0xAAU)
            command_ = Command::Unlock1;
        break;
    case Command::Unlock1:
        command_ = decoded == 0x2AAAU && data == 0x55U ? Command::Unlock2 : Command::Read;
        break;
    case Command::Unlock2:
        command_ = Command::Read;
        if (decoded == 0x5555U) {
            if (data == 0xA0U)
                command_ = Command::Program;
            else if (data == 0x80U)
                command_ = Command::Erase1;
            else if (data == 0x90U)
                id_mode_ = 1U;
        }
        break;
    case Command::Erase1:
        command_ = decoded == 0x5555U && data == 0xAAU ? Command::Erase2 : Command::Read;
        break;
    case Command::Erase2:
        command_ = decoded == 0x2AAAU && data == 0x55U ? Command::Erase3 : Command::Read;
        break;
    case Command::Erase3:
        command_ = Command::Read;
        if (data == 0x30U)
            begin(Operation::Sector, address & 0x7F000U, 0xFFU, kSectorCycles);
        else if (decoded == 0x5555U && data == 0x10U)
            begin(Operation::Chip, 0U, 0xFFU, kChipCycles);
        break;
    case Command::Program:
        break;
    }
}
void Sst39sf040::clock_cpu() {
    if (remaining_ == 0U)
        return;
    --remaining_;
    if (remaining_ != 0U)
        return;
    if (operation_ == Operation::Program)
        data_[pending_address_] &= pending_data_;
    else if (operation_ == Operation::Sector)
        std::fill_n(data_.begin() + pending_address_, 4096U, 0xFFU);
    else if (operation_ == Operation::Chip)
        std::ranges::fill(data_, 0xFFU);
    operation_ = Operation::None;
    toggle_ = 0U;
}
void Sst39sf040::restore_nvram(std::span<const Byte> data) {
    if (data.size() == kSize)
        std::ranges::copy(data, data_.begin());
}
void Sst39sf040::save_state(StateWriter& w) const {
    w.write_bytes(data_);
    w.write(command_);
    w.write(operation_);
    w.write(id_mode_);
    w.write(toggle_);
    w.write(pending_data_);
    w.write(pending_address_);
    w.write(remaining_);
}
void Sst39sf040::load_state(StateReader& r) {
    r.read_bytes(data_, kSize);
    r.read(command_);
    r.read(operation_);
    r.read(id_mode_);
    r.read(toggle_);
    r.read(pending_data_);
    r.read(pending_address_);
    r.read(remaining_);
    const auto maximum = operation_ == Operation::Program  ? kProgramCycles
                         : operation_ == Operation::Sector ? kSectorCycles
                                                           : kChipCycles;
    if (!r.ok() || data_.size() != kSize || command_ > Command::Erase3 ||
        operation_ > Operation::Chip || id_mode_ > 1U || toggle_ > 1U ||
        pending_address_ >= kSize || remaining_ > maximum ||
        ((remaining_ == 0U) != (operation_ == Operation::None)) ||
        (remaining_ != 0U && (id_mode_ != 0U || command_ != Command::Read)) ||
        (operation_ == Operation::Sector && (pending_address_ & 0xFFFU) != 0U) ||
        (operation_ == Operation::Chip && pending_address_ != 0U)) {
        data_.resize(kSize, 0xFFU);
        command_ = Command::Read;
        operation_ = Operation::None;
        id_mode_ = toggle_ = 0U;
        remaining_ = 0U;
        pending_address_ = 0U;
        r.fail();
    }
}
} // namespace mapperbus::core
