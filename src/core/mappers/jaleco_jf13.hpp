#pragma once

#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Jaleco JF-13 (mapper 86): 32 KiB PRG/8 KiB CHR banking and speech control.
/// The µPD7756C's internal sample ROM is unavailable, so no waveform is produced.
class JalecoJf13 final : public Mapper {
  public:
    JalecoJf13(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void clock_cpu() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode mirror_;
    Byte bank_ = 0U;
    Byte audio_control_ = 0U;
    uint16_t audio_reset_low_cycles_ = 0U;
};

} // namespace mapperbus::core
