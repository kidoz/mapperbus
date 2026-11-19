#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Sachen 3013, NROM-128 with a constant readback in the lower PRG window.
class Mapper553 final : public Mapper {
  public:
    Mapper553(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    const MirrorMode mirror_;
    std::vector<Byte> prg_;
    std::vector<Byte> chr_;
};
} // namespace mapperbus::core
