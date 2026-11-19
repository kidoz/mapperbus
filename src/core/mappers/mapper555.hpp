#pragma once
#include "core/mappers/mmc3.hpp"

namespace mapperbus::core {
/// NES-EVENT2 / retroUSB Campus Challenge with a switch-configured tournament timer.
class Mapper555 final : public Mmc3 {
  public:
    using Mmc3::Mmc3;
    static Result<void> validate_geometry(const INesHeader&, std::size_t, std::size_t);
    Byte read_prg(Address addr) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    bool maps_expansion(Address addr) const override;
    Byte read_expansion(Address addr) override;
    Byte expansion_read_mask(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    Result<void> configure_board_jumpers(Byte value) override;
    void clock_cpu() override;
    void reset() override;
    void save_state(StateWriter& w) const override;
    void load_state(StateReader& r) override;

  private:
    std::uint32_t timeout() const;
    bool maps_pattern_ram(Byte chr_bank) const;
    Byte game_ = 0, time_switches_ = 0;
    std::uint32_t elapsed_ = 0;
    std::array<Byte, 2048> scratch_{};
    std::array<Byte, 8192> pattern_ram_{};
};
} // namespace mapperbus::core
