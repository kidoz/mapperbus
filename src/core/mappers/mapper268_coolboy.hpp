#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// AA6023 Coolboy/Mindkids multicart ASIC (NES 2.0 mapper 268).
class Coolboy final : public Mapper {
  public:
    Coolboy(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    [[nodiscard]] static Result<void> validate_geometry(const INesHeader& header,
                                                        std::size_t prg_size,
                                                        std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    bool observes_ppu_address() const override {
        return true;
    }
    void observe_ppu_address(Address addr) override;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override {
        return state_.irq_pending;
    }
    void acknowledge_irq() override {
        state_.irq_pending = false;
    }
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    struct RuntimeState {
        std::array<Byte, 6> outer{};
        Byte bank_select = 0U;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        bool irq_reload_flag = false;
        bool irq_enabled = false;
        bool irq_pending = false;
        bool wram_enabled = true;
        bool wram_write_protected = false;
        Address observed_ppu_address = 0U;
    };

    [[nodiscard]] bool submapper_in(unsigned first, unsigned last) const;
    [[nodiscard]] bool gnrom_mode() const;
    [[nodiscard]] bool weird_mode() const;
    [[nodiscard]] bool lockout_active() const;
    [[nodiscard]] unsigned outer_base() const;
    [[nodiscard]] bool is_outer_register(Address addr) const;
    void write_outer(unsigned index, Byte value);
    [[nodiscard]] bool extra_wram_mapped(Address addr) const;
    [[nodiscard]] bool base_wram_readable(Address addr) const;
    [[nodiscard]] Byte mmc3_chr_bank(Address addr) const;
    [[nodiscard]] std::size_t mapped_chr_offset(Address addr) const;
    [[nodiscard]] bool mixed_chr_ram_selected(Address addr) const;
    [[nodiscard]] std::size_t mapped_prg_offset(Address addr) const;
    [[nodiscard]] uint32_t geometry_tag() const;
    void write_ram(Address addr, Byte value);

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> prg_ram_;
    std::vector<Byte> chr_ram_;
    Byte submapper_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    bool persistent_wram_ = false;
    RuntimeState state_;
};

} // namespace mapperbus::core
