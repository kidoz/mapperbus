#pragma once
#include "core/mappers/nrom.hpp"

namespace mapperbus::core {
/// Russian Keyboard Transformer, UNIF UNL-Transformer, with a PC/AT keyboard receiver.
class TransformerKeyboard final : public Nrom {
  public:
    using Nrom::Nrom;
    static Result<void> validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr);
    void send_keyboard_scancode(Byte code) override {
        scancode_ = code;
        irq_ = true;
    }
    bool maps_expansion(Address addr) const override {
        return addr >= 0x5000 && addr <= 0x5004;
    }
    Byte expansion_read_mask(Address addr) const override {
        return maps_expansion(addr) ? 0xFF : 0;
    }
    Byte read_expansion(Address addr) override;
    bool irq_pending() const override {
        return irq_;
    }
    void reset() override {
        scancode_ = 0;
        irq_ = false;
    }
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    Byte scancode_ = 0;
    bool irq_ = false;
};
} // namespace mapperbus::core
