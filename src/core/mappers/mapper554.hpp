#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Kaiser KS-7010, with PRG and CHR banking triggered by CPU read strobes.
class Mapper554 final : public Mapper {
  public:
    Mapper554(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
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
    bool maps_prg(Address addr) const override;
    void observe_cpu_read(Address addr, Byte data) override;

  private:
    std::vector<Byte> prg_;
    std::vector<Byte> chr_;
    Byte bank_ = 0;
};
} // namespace mapperbus::core
