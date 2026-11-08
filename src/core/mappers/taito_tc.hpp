#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// TC0190/TC0350 subset (33) and TC0690 (48).
class TaitoTc final : public Mapper {
  public:
    TaitoTc(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void clock_irq_counter() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    const bool has_irq_;
    const bool next_edge_reload_;
    const Byte irq_delay_cycles_;
    const Byte initial_mirror_;
    const std::size_t chr_ram_size_;
    std::array<Byte, 2> prg_banks_{};
    std::array<Byte, 6> chr_banks_{};
    Byte mirror_ = 0U;
    Byte irq_reload_ = 0U;
    Byte irq_counter_ = 0U;
    Byte irq_reload_pending_ = 0U;
    Byte irq_enabled_ = 0U;
    Byte irq_latched_ = 0U;
    Byte irq_delay_ = 0U;
};

} // namespace mapperbus::core
