#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Future Media mapper 117: three PRG banks, eight CHR banks and scanline IRQs.
class Mapper117 final : public Mapper {
  public:
    Mapper117(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    [[nodiscard]] static Result<void> validate_geometry(const INesHeader& header,
                                                        std::size_t prg_size,
                                                        std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void set_region(Region region) override;
    void reset() override;
    bool irq_pending() const override;
    void on_ppu_hblank() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const MirrorMode initial_mirror_;
    Region region_;
    uint16_t frame_scanlines_;
    std::array<Byte, 3> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    Byte irq_counter_ = 0U;
    Byte irq_latch_ = 0U;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    uint16_t scanline_ = 0U;
};

} // namespace mapperbus::core
