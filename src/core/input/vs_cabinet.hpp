#pragma once

#include "core/state/state.hpp"

namespace mapperbus::core {

/// Normal single-CPU Nintendo Vs. cabinet. The two coin chutes share D5
/// on the standard harness. One rising host edge closes the switch for 50 ms.
class VsCabinet {
  public:
    VsCabinet(Byte ppu_type, bool swap_ports) : ppu_type_(ppu_type), swap_ports_(swap_ports) {}

    Byte ppu_type() const {
        return ppu_type_;
    }
    int player_for_port(int port) const {
        return port ^ static_cast<int>(swap_ports_);
    }
    void set_dips(Byte switches) {
        dips_ = switches;
    }
    void set_service(bool pressed) {
        service_ = pressed;
    }
    void set_coin(bool pressed) {
        if (pressed && !coin_pressed_)
            coin_cycles_ = kCoinPulse;
        coin_pressed_ = pressed;
    }
    void clock_cpu() {
        if (coin_cycles_ != 0U)
            --coin_cycles_;
    }
    void reset() {
        counter_line_ = false;
        coin_cycles_ = 0U;
    }
    Byte read_port(int port, Byte serial) const {
        if (port == 1)
            return static_cast<Byte>((dips_ & 0xFCU) | serial);
        return static_cast<Byte>(((dips_ & 3U) << 3U) | (service_ ? 4U : 0U) |
                                 (coin_cycles_ != 0U ? 0x20U : 0U) | serial);
    }
    void observe_counter(Address addr, Byte data) {
        if ((addr & 0xE020U) != 0x4020U)
            return;
        const bool energized = (data & 1U) != 0U;
        if (counter_line_ && !energized)
            ++coins_counted_;
        counter_line_ = energized;
    }
    uint32_t coins_counted() const {
        return coins_counted_;
    }

    void save_state(StateWriter& writer) const {
        writer.write(ppu_type_);
        writer.write(static_cast<Byte>(swap_ports_));
        writer.write(dips_);
        writer.write(coin_cycles_);
        writer.write(coins_counted_);
        writer.write(static_cast<Byte>(coin_pressed_ | (service_ << 1U) | (counter_line_ << 2U)));
    }
    void load_state(StateReader& reader) {
        auto ppu = reader.read<Byte>();
        auto swap = reader.read<Byte>();
        auto dips = reader.read<Byte>();
        auto cycles = reader.read<uint32_t>();
        auto count = reader.read<uint32_t>();
        auto flags = reader.read<Byte>();
        if (!reader.ok() || ppu != ppu_type_ || swap != static_cast<Byte>(swap_ports_) ||
            cycles > kCoinPulse || flags > 7U) {
            reader.fail();
            return;
        }
        dips_ = dips;
        coin_cycles_ = cycles;
        coins_counted_ = count;
        coin_pressed_ = (flags & 1U) != 0;
        service_ = (flags & 2U) != 0;
        counter_line_ = (flags & 4U) != 0;
    }

  private:
    static constexpr uint32_t kCoinPulse = 89489U;
    Byte ppu_type_;
    bool swap_ports_;
    Byte dips_ = 0U;
    bool service_ = false;
    bool coin_pressed_ = false;
    bool counter_line_ = false;
    uint32_t coin_cycles_ = 0U;
    uint32_t coins_counted_ = 0U;
};
} // namespace mapperbus::core
