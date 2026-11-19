#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 392: 00202650 MMC3 multicart with menu CHR-RAM.
class Mapper392 final : public Mapper {
  public:
    Mapper392(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    unsigned inner_prg_bank(Address addr) const;
    unsigned inner_chr_bank(Address addr) const;
    bool game_mode() const;
    std::uint16_t memory_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    Byte outer_bank_ = 0U;
    Byte bank_select_ = 0U;
    bool prg_mode_ = false;
    bool chr_inversion_ = false;
    std::array<Byte, 8> bank_registers_{};

    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_pending_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
