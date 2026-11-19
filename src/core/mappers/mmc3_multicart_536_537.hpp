#pragma once
#include "core/mappers/mmc3.hpp"

namespace mapperbus::core {

/// N42S-2 and JY4M4 multicarts with WRAM-select outer registers and an AX5202P.
class Mmc3Multicart536537 final : public Mmc3 {
  public:
    Mmc3Multicart536537(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    bool maps_prg(Address addr) const override {
        return addr >= 0x6000;
    }
    Byte prg_read_mask(Address addr) const override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    std::span<const Byte> battery_ram() const override {
        return {};
    }
    void set_battery_ram(std::span<const Byte>) override {}
    void power_on() override;
    void reset() override;
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    std::size_t prg_offset(Address addr) const;
    uint16_t id_;
    uint32_t prg_size_;
    std::array<Byte, 2> outer_{};
};
} // namespace mapperbus::core
