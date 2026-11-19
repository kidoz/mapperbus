#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 416: four-in-one multicart with an N-32 SMB2J conversion.
class Mapper416 final : public Mapper {
  public:
    Mapper416(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
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
    [[nodiscard]] bool smb2j_mode() const;
    [[nodiscard]] unsigned normal_prg_bank(Address addr) const;
    [[nodiscard]] unsigned smb2j_prg_bank(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Byte data_latch_ = 0U;
    Byte prg_bank_ = 0U;
    std::uint16_t irq_counter_ = 0U;
    bool irq_enabled_ = false;
};

} // namespace mapperbus::core
