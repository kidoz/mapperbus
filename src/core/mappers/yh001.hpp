#pragma once

#include "core/mappers/mmc3.hpp"

namespace mapperbus::core {

/// NT-994 multicart. The YH-001 routes an AX5202P through a lockable set of outer bank pins.
class Yh001 final : public Mmc3 {
  public:
    Yh001(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    Byte read_chr(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Result<void> configure_board_jumpers(Byte value) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::array<Byte, 4> outer_{};
    Byte pads_ = 0;
};

} // namespace mapperbus::core
