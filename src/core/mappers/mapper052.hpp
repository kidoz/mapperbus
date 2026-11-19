#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Realtec8213 final : public Mapper {
  public:
    Realtec8213(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    unsigned inner_prg_bank(Address addr) const;
    unsigned outer_prg_bank(unsigned inner_bank) const;
    unsigned inner_chr_bank(Address addr) const;
    unsigned outer_chr_bank(unsigned inner_bank) const;
    bool chr_ram_selected() const;
    std::uint16_t memory_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    std::size_t prg_ram_size_ = 0U;
    std::size_t nvram_size_ = 0U;
    Byte submapper_ = 0U;
    bool chr_ram_present_ = false;
    MirrorMode initial_mirror_ = MirrorMode::Horizontal;
    MirrorMode mirror_mode_ = MirrorMode::Horizontal;

    Byte outer_bank_ = 0U;
    Byte bank_select_ = 0U;
    bool prg_mode_ = false;
    bool chr_inversion_ = false;
    std::array<Byte, 8> bank_registers_{};
    bool prg_ram_enabled_ = true;
    bool prg_ram_write_protected_ = false;

    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_pending_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
