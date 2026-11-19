#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 269: MMC3-compatible multicart banking with outer banks and scrambled CHR data.
class Mapper269 final : public Mapper {
  public:
    Mapper269(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void power_on() override;
    void reset() override;

    bool irq_pending() const override {
        return registers_.irq_pending;
    }
    void acknowledge_irq() override {
        registers_.irq_pending = false;
    }
    void clock_irq_counter() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        std::array<Byte, 4> outer{0U, 0U, 0x0FU, 0U};
        Byte outer_index = 0U;
        Byte bank_select = 0U;
        bool prg_mode = false;
        bool chr_inversion = false;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_latch = 0U;
        bool irq_reload = false;
        bool irq_enabled = false;
        bool irq_pending = false;
        bool ram_enabled = true;
        bool ram_write_protected = false;
    };

    unsigned inner_chr_bank(Address addr) const;
    unsigned chr_outer_mask() const;
    uint16_t state_tag() const;

    Registers registers_;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> wram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    bool battery_backed_ = false;
};

} // namespace mapperbus::core
