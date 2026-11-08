#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Jaleco SS88006 (18), without the optional external ADPCM speech chip.
class JalecoSs88006 final : public Mapper {
  public:
    JalecoSs88006(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
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
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    const std::size_t prg_ram_size_;
    const std::size_t nvram_size_;
    const Byte initial_mirror_;
    std::array<Byte, 3> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    Byte mirror_ = 0U;
    Byte ram_control_ = 0U;
    Word irq_reload_ = 0U;
    Word irq_counter_ = 0U;
    Byte irq_control_ = 0U;
    Byte irq_pending_ = 0U;
};

} // namespace mapperbus::core
