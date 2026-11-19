#pragma once

#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 040: NTDEC 2722 and the submapper-1 NTDEC 2752 multicart.
class Ntdec2722 final : public Mapper {
  public:
    Ntdec2722(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] bool nrom_mode() const;
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_;
    bool multicart_ = false;
    Byte prg_bank_ = 0U;
    Byte outer_bank_ = 0U;
    std::uint16_t irq_counter_ = 0U;
    bool irq_enabled_ = false;
};

} // namespace mapperbus::core
