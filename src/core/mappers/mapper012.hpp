#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 012: Gouder SL-5020B (submapper 0) and extracted FFE Magic Card 4M
/// play images (submapper 1). The FFE profile excludes the copier BIOS, floppy,
/// parallel-port and pass-through devices, which are not part of an extracted game.
class Mapper012 final : public Mapper {
  public:
    Mapper012(const INesHeader& header, std::vector<Byte> prg_data, std::vector<Byte> chr_data);

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
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    Result<void> configure_board_jumpers(Byte value) override;
    void observe_cpu_write(Address addr, Byte value) override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    bool observes_ppu_reads() const override;
    void observe_ppu_read(Address addr) override;
    void clock_cpu() override;
    void clock_irq_counter() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    bool ffe_pass_through() const;
    bool ffe_play_mode() const;
    bool ffe_prg_protected() const;
    bool ffe_chr_protected() const;
    bool ffe_extended_prg_mode() const;
    bool ffe_chr_1k_mode() const;
    bool ffe_mmc4_mode() const;
    std::optional<std::size_t> ffe_prg_offset(Address addr) const;
    std::size_t ffe_chr_offset(Address addr) const;
    void clock_ffe_irq();
    void initialize_ffe_registers();
    Byte sl_chr_bank(Address addr) const;
    void load_sl_state(StateReader& reader);
    void load_ffe_state(StateReader& reader);

    const bool ffe_4m_ = false;
    const MirrorMode initial_mirror_ = MirrorMode::Vertical;
    const std::size_t prg_source_size_ = 0U;
    const std::size_t chr_source_size_ = 0U;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Byte sl_bank_select_ = 0U;
    bool sl_prg_mode_ = false;
    bool sl_chr_inversion_ = false;
    std::array<Byte, 8> sl_bank_registers_{};
    Byte sl_chr_outer_ = 0U;
    MirrorMode sl_mirror_mode_ = MirrorMode::Vertical;
    Byte sl_irq_counter_ = 0U;
    Byte sl_irq_reload_ = 0U;
    bool sl_irq_reload_flag_ = false;
    bool sl_irq_reload_blocked_ = false;
    bool sl_irq_enabled_ = false;
    bool sl_irq_pending_ = false;
    bool sl_ram_enabled_ = true;
    bool sl_ram_write_protected_ = false;
    Byte sl_language_bit_ = 0U;

    std::vector<Byte> ffe_prg_ram_;
    std::array<Byte, 32768> ffe_chr_ram_{};
    std::array<Byte, 32768> ffe_wram_{};
    std::array<Byte, 4096> ffe_scratch_ram_{};
    std::array<Byte, 4> ffe_prg_2m_banks_{};
    std::array<Byte, 4> ffe_prg_4m_banks_{};
    std::array<Byte, 12> ffe_chr_banks_{};
    std::array<Byte, 2> ffe_mmc4_latches_{};
    Byte ffe_latch_mode_ = 0U;
    Byte ffe_latch_ = 0U;
    Byte ffe_mode_1m_address_ = 0U;
    Byte ffe_mode_2m_address_ = 0U;
    Byte ffe_config_ = 0U;
    std::uint16_t ffe_irq_counter_ = 0U;
    bool ffe_irq_enabled_ = false;
    bool ffe_irq_pending_ = false;
    bool ffe_ppu_a12_ = false;
};

} // namespace mapperbus::core
