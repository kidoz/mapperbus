#pragma once

#include <array>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mappers 006 and 008: extracted Front Fareast Magic Card 1M/2M RAM cartridges.
class FfeMagicCard final : public Mapper {
  public:
    FfeMagicCard(const INesHeader& header, std::vector<Byte> prg_ram, std::vector<Byte> chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    void observe_cpu_write(Address addr, Byte value) override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    bool observes_ppu_reads() const override;
    void observe_ppu_read(Address addr) override;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_cpu() override;
    bool maps_expansion(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::optional<std::size_t> prg_offset(Address addr) const;
    std::size_t chr_offset(Address addr) const;
    bool pass_through() const;
    bool play_mode() const;
    bool prg_protected() const;
    bool chr_protected() const;
    bool extended_prg_mode() const;
    bool chr_1k_mode() const;
    bool mmc4_mode() const;
    void clock_irq();
    void initialize_registers();

    std::vector<Byte> prg_ram_;
    std::array<Byte, 32768> chr_ram_{};
    std::array<Byte, 32768> wram_{};
    std::array<Byte, 4096> scratch_ram_{};
    std::array<Byte, 4> prg_2m_banks_{};
    std::array<Byte, 4> prg_4m_banks_{};
    std::array<Byte, 12> chr_banks_{};
    std::array<Byte, 2> mmc4_latches_{};
    Byte initial_latch_mode_ = 0U;
    Byte initial_mirroring_ = 0U;
    Byte latch_mode_ = 0U;
    Byte latch_ = 0U;
    Byte mode_1m_address_ = 0U;
    Byte mode_2m_address_ = 0U;
    Byte config_ = 0U;
    uint16_t irq_counter_ = 0U;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool ppu_a12_ = false;
};

} // namespace mapperbus::core
