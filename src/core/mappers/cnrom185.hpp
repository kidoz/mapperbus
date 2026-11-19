#pragma once

#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 185: CNROM CHR chip-select protection and mandatory AND bus conflicts.
class Cnrom185 final : public Mapper {
  public:
    Cnrom185(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void observe_cpu_read(Address addr, Byte data) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] bool chr_enabled() const;
    [[nodiscard]] uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Byte submapper_;
    MirrorMode mirror_mode_;
    Byte latch_ = 0U;
    Byte ppudata_reads_ = 0U;
};

} // namespace mapperbus::core
