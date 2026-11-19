#pragma once

#include <array>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {
/// BATMAP-SRR-X, including the separate serial sample ROM.
class Mapper413 final : public Mapper {
  public:
    static constexpr std::size_t kSerialSize = 8UZ << 20;
    Mapper413(const INesHeader&,
              std::vector<Byte> prg_rom,
              std::vector<Byte> chr_rom,
              std::span<const Byte> serial);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    bool maps_prg(Address addr) const override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address, Byte) override {}
    bool maps_expansion(Address addr) const override;
    Byte read_expansion(Address addr) override;
    MirrorMode mirror_mode() const override {
        return mirror_;
    }
    bool observes_ppu_a12() const override {
        return true;
    }
    void observe_ppu_a12(bool high) override;
    void clock_cpu() override;
    bool irq_pending() const override {
        return state_.pending;
    }
    void reset() override {
        state_ = {};
    }
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    Byte read_sample();
    struct Registers {
        std::array<Byte, 4> banks{};
        std::uint32_t address = 0;
        Byte increment = 0;
        Byte counter = 0;
        Byte latch = 0;
        Byte enabled = 0;
        Byte pending = 0;
        Byte a12 = 0;
        Byte low_cycles = 0;
    } state_;
    std::vector<Byte> prg_, chr_, samples_;
    MirrorMode mirror_;
};
} // namespace mapperbus::core
