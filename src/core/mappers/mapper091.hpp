#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Mapper091 final : public Mapper {
  public:
    Mapper091(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    bool irq_pending() const override;
    void clock_cpu() override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::size_t prg_offset(Address addr) const;
    std::size_t chr_offset(Address addr) const;
    uint16_t state_tag() const;

    const Byte submapper_;
    const MirrorMode initial_mirror_;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 2> prg_banks_{};
    std::array<Byte, 4> chr_banks_{};
    Byte outer_bank_ = 0U;
    MirrorMode mirror_;
    uint16_t irq_counter_ = 0U;
    Byte irq_phase_ = 0U;
    Byte irq_position_ = 0U;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool ppu_a12_ = false;
};

} // namespace mapperbus::core
