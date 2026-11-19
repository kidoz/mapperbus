#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 432: Realtec MMC3 multicarts with outer ROM bank pins.
class Mapper432 final : public Mapper {
  public:
    static constexpr std::size_t kWramSize = 0x2000UZ;

    Mapper432(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
        Byte outer = 0U;
        Byte solder_enable = 0U;
        Byte bank_select = 0U;
        Byte bank_mode = 0U;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        Byte irq_flags = 0U;
        Byte ram_flags = 0x80U;
    };

    [[nodiscard]] bool nrom256_mode() const;
    [[nodiscard]] bool solder_read_enabled() const;
    [[nodiscard]] bool prg_rom_disabled() const;
    [[nodiscard]] Byte mmc3_prg_bank(Address addr) const;
    [[nodiscard]] Byte mmc3_chr_bank(Address addr) const;
    [[nodiscard]] std::uint32_t chr_offset(Address addr) const;
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, kWramSize> wram_{};
    std::size_t wram_size_ = 0U;
    std::size_t nvram_size_ = 0U;
    Byte submapper_ = 0U;
    Byte solder_pad_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_;
};

} // namespace mapperbus::core
