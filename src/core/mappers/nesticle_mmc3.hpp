#pragma once

#include "core/mappers/mmc3.hpp"

namespace mapperbus::core {

/// NESticle's software-defined MMC3 bank ports, used by the trained Akira translation.
class NesticleMmc3 final : public Mmc3 {
  public:
    NesticleMmc3(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    Byte read_chr(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    Byte index_ = 0;
    std::array<Byte, 4> prg_{};
    std::array<Byte, 8> chr_{};
};

} // namespace mapperbus::core
