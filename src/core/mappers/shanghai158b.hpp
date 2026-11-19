#pragma once
#include "core/mappers/mmc3.hpp"

namespace mapperbus::core {
/// Shanghai Paradise 158B uses the 823x bank/scramble logic and an additional protection matrix.
class Shanghai158B final : public Mmc3 {
  public:
    Shanghai158B(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    bool maps_expansion(Address addr) const override;
    Byte read_expansion(Address addr) override;
    Byte expansion_read_mask(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    void reset() override;
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    Byte table_ = 7;
};
} // namespace mapperbus::core
