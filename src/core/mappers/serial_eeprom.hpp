#pragma once

#include <array>
#include <span>

#include "core/types.hpp"

namespace mapperbus::core {

class StateWriter;
class StateReader;

enum class EepromKind : Byte { None, X24C01, X24C02 };

/// Xicor's original four-byte-page two-wire EEPROMs. Bits are MSB first.
class SerialEeprom {
  public:
    explicit SerialEeprom(EepromKind kind);
    void set_lines(bool scl, bool released_sda);
    [[nodiscard]] bool sda() const;
    /// Open-drain output only, for devices sharing SDA with separate clocks.
    [[nodiscard]] bool releases_sda() const {
        return pull_low_ == 0U;
    }
    void set_bus_lines(bool scl, bool released_sda, bool previous_sda, bool next_sda);
    void clock_cpu();
    [[nodiscard]] std::span<const Byte> nvram() const;
    void restore_nvram(std::span<const Byte> data);
    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);

  private:
    enum class State : Byte { Idle, Receive, Ack, Send, HostAck, Ignore };
    enum class Stage : Byte { Header, Address, Data };
    void receive_byte();
    void drive_read_bit();
    [[nodiscard]] unsigned size() const;
    const EepromKind kind_;
    std::array<Byte, 256> data_{};
    std::array<Byte, 4> page_{};
    Byte address_ = 0U;
    Byte page_base_ = 0U;
    Byte dirty_ = 0U;
    State state_ = State::Idle;
    State next_ = State::Idle;
    Stage stage_ = Stage::Header;
    Byte shift_ = 0U;
    Byte bits_ = 0U;
    Byte ack_ = 0U;
    Byte ack_phase_ = 0U;
    Byte scl_ = 1U;
    Byte host_sda_ = 1U;
    Byte pull_low_ = 0U;
    Word busy_ = 0U;
};

} // namespace mapperbus::core
