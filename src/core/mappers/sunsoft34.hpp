#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Sunsoft-3 (67) and Sunsoft-4 (68), including DCS option ROM and licensing.
class Sunsoft34 final : public Mapper {
  public:
    Sunsoft34(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;
    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    void load_state_version(StateReader& reader, std::uint32_t version) override;
    Result<void> configure_dcs_option_rom(std::span<const Byte> data) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    [[nodiscard]] bool ram_enabled() const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> option_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    const bool sunsoft4_;
    const bool dcs_;
    const Byte initial_mirror_;
    const std::size_t prg_ram_size_;
    const std::size_t nvram_size_;
    const std::size_t chr_ram_size_;
    Byte prg_control_ = 0U;
    std::array<Byte, 4> chr_banks_{};
    std::array<Byte, 2> nametable_banks_{};
    Byte nametable_control_ = 0U;
    Word irq_counter_ = 0U;
    Byte irq_low_next_ = 0U;
    Byte irq_enabled_ = 0U;
    Byte irq_pending_ = 0U;
    std::uint32_t option_cycles_ = 0U;
};

} // namespace mapperbus::core
