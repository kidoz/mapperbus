#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/fk23c.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 523: Jncota's hard-wired FS005 board with shifted CHR pins.
class Mapper523 final : public Mapper {
  public:
    Mapper523(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

  private:
    struct ShadowState {
        Byte mode = 0U;
        Byte chr_base = 0U;
        Byte extended = 0U;
        Byte bank_select = 0U;
        std::array<Byte, 12> banks = {0U, 2U, 4U, 5U, 6U, 7U, 0U, 1U, 0xFEU, 0xFFU, 0xFFU, 0xFFU};
        Byte ram_register = 0U;
    };

    [[nodiscard]] static std::optional<ShadowState> parse_shadow(std::span<const Byte> bytes);
    [[nodiscard]] bool extended_mode() const;
    [[nodiscard]] bool ram_configuration() const;
    [[nodiscard]] bool outer_registers_enabled() const;
    [[nodiscard]] std::optional<unsigned> outer_register(Address addr) const;
    [[nodiscard]] std::uint32_t chr_address(Address addr) const;
    void write_outer_register(unsigned index, Byte value);
    void reset_shadow();

    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Fk23c fs005_;
    std::vector<Byte> chr_rom_;
    Byte solder_pad_ = 0U;
    ShadowState shadow_{};
};

} // namespace mapperbus::core
