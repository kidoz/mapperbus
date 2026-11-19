#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 417: Fine Studio bootleg PRG/CHR/CIRAM banking and cycle IRQ.
class Mapper417 final : public Mapper {
  public:
    Mapper417(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    std::optional<Address> nametable_address(Address addr) const override;

    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] std::uint16_t irq_mask() const;
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const Byte submapper_;
    const MirrorMode initial_mirror_;
    std::array<Byte, 3> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    std::array<Byte, 4> nametable_banks_{};
    std::uint16_t irq_counter_ = 0U;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
