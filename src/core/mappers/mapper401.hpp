#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// KC885 multicart: mapper-45 banking with configurable PRG A18/A19 and /CE wiring.
class Mapper401 final : public Mapper {
  public:
    Mapper401(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;

    /// Bit 0 selects A18 from register 1 bit 6, bit 1 selects A19 from register 1
    /// bit 5, and bit 2 connects register 1 bit 7 to PRG /CE. Zero selects both
    /// high address bits from register 2 and leaves PRG /CE disconnected.
    Result<void> configure_board_jumpers(Byte value) override;

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        std::array<Byte, 4> outer{};
        Byte outer_index = 0U;
        Byte bank_select = 0U;
        bool prg_mode = false;
        bool chr_inversion = false;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        bool irq_reload_flag = false;
        bool irq_enabled = false;
        bool irq_pending = false;
        bool wram_enabled = true;
        bool wram_write_protected = false;
    };

    [[nodiscard]] unsigned mapped_prg_bank(Address addr) const;
    [[nodiscard]] Byte raw_chr_bank(Address addr) const;
    [[nodiscard]] unsigned mapped_chr_bank(Address addr) const;
    [[nodiscard]] bool prg_rom_disabled() const;
    void reset_outer_registers();
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> wram_{};
    std::size_t wram_size_ = 0U;
    bool persistent_wram_ = false;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Byte solder_pads_ = 0U;
    Registers registers_;
};

} // namespace mapperbus::core
