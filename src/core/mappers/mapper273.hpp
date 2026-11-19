#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 273: VRC2-compatible banking with a 74'393/74'191 cycle IRQ.
class Mapper273 final : public Mapper {
  public:
    Mapper273(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void clock_cpu_falling_edge() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const MirrorMode initial_mirror_;
    MirrorMode mirror_;
    std::array<Byte, 2> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    Byte irq_counter_ = 0U;
    Byte irq_prescaler_ = 0U;
    bool irq_enabled_ = false;
    bool load_suppresses_count_ = false;
};

} // namespace mapperbus::core
