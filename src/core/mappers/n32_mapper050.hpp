#pragma once

#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 050: N-32 fixed PRG/CHR layout and CPU-cycle IRQ counter.
class N32Mapper050 final : public Mapper {
  public:
    N32Mapper050(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;

    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode mirror_;
    Byte prg_bank_ = 0U;
    std::uint16_t irq_counter_ = 0U;
    Byte irq_enabled_ = 0U;
};

} // namespace mapperbus::core
