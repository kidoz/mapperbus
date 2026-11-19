#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Kaiser KS-7017 FDS conversion with a staged bank selector and M2 timer.
class Mapper303 final : public Mapper {
  public:
    Mapper303(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader&,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    bool maps_prg(Address addr) const override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> prg_ram_{};
    std::array<Byte, 8192> chr_ram_{};
    Byte bank_ = 0;
    Byte latch_ = 0;
    bool horizontal_ = false;
    std::uint16_t counter_ = 0;
    bool counting_ = false;
    bool irq_ = false;
};

} // namespace mapperbus::core
