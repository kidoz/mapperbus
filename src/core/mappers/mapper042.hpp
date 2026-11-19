#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 42: FDS-to-cartridge conversions with an M2-cycle IRQ counter.
class Mapper042 final : public Mapper {
  public:
    Mapper042(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
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
    void power_on() override;
    bool irq_pending() const override;
    void clock_cpu() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> chr_ram_{};
    Byte prg_bank_ = 0U;
    Byte chr_bank_ = 0U;
    MirrorMode mirror_ = MirrorMode::Vertical;
    uint16_t irq_counter_ = 0U;
    bool irq_enabled_ = false;
};

} // namespace mapperbus::core
