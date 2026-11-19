#pragma once

#include <array>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// 8025 enhanced MMC3, including FK23C/FK23CA and the NES 2.0 board variants.
class Fk23c final : public Mapper {
  public:
    Fk23c(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    Result<void> configure_board_jumpers(Byte value) override;
    Result<void> configure_board_profile(std::string_view profile) override;

    void power_on() override;
    void reset() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;

    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    void load_state_version(StateReader& reader, std::uint32_t version) override;

  private:
    struct RuntimeState;
    void load_registers(StateReader& reader, bool exact_size);

    std::optional<unsigned> outer_register(Address addr) const;
    void write_outer_register(unsigned index, Byte value);
    bool outer_registers_enabled() const;
    bool extended_mode() const;
    bool ram_configuration() const;
    bool ram_enabled() const;
    bool expansion_ram_enabled() const;
    std::size_t wram_offset(unsigned bank, Address addr) const;
    uint32_t prg_base() const;
    uint32_t prg_address(Address addr) const;
    uint32_t chr_address(Address addr) const;
    bool chr_uses_ram(uint32_t offset) const;
    Byte read_wram(Address addr) const;
    void write_wram(Address addr, Byte value);
    uint16_t state_tag() const;
    void apply_state(RuntimeState state);

    const Byte submapper_;
    Byte solder_pad_ = 0U;
    const MirrorMode initial_mirror_;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> chr_ram_;
    std::vector<Byte> volatile_wram_;
    std::vector<Byte> nvram_;

    Byte mode_ = 0U;
    Byte prg_base_ = 0U;
    Byte chr_base_ = 0U;
    Byte prg_base_msb_ = 0U;
    Byte chr_base_msb_ = 0U;
    Byte extended_ = 0U;
    Byte unrom_latch_ = 0U;
    Byte cnrom_latch_ = 0U;
    Byte bank_select_ = 0U;
    std::array<Byte, 12> banks_{};
    Byte mirror_register_ = 0U;
    Byte ram_register_ = 0U;
    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_flag_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
};

} // namespace mapperbus::core
