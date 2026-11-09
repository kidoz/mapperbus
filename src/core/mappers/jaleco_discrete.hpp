#pragma once

#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// JF-17/JF-19 (72/92), plus the JF-10 legacy dump ordering (101).
/// Optional external ADPCM playback is not part of this implementation.
class JalecoDiscrete final : public Mapper {
  public:
    JalecoDiscrete(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const uint16_t mapper_;
    const MirrorMode mirror_;
    const Byte prg_bank_mask_;
    Byte prg_bank_ = 0U;
    Byte chr_bank_ = 0U;
    Byte command_ = 0U;
};

} // namespace mapperbus::core
