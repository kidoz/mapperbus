#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 056: Kaiser KS202 with external PRG/CHR banking and mirroring logic.
class Mapper056Ks202 final : public Mapper {
  public:
    Mapper056Ks202(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> prg_ram_{};
    std::array<Byte, 3> prg_banks_{};
    std::array<Byte, 4> prg_upper_{};
    std::array<Byte, 8> chr_banks_{};
    const MirrorMode initial_mirror_;
    MirrorMode mirror_ = MirrorMode::Horizontal;
    Byte bank_select_ = 0U;
    uint16_t irq_reload_ = 0U;
    uint16_t irq_counter_ = 0U;
    Byte irq_control_ = 0U;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
