#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 400: retroUSB 8-BIT XMAS 2017.
class Mapper400 final : public Mapper {
  public:
    static constexpr std::size_t kLcdDataSize = 163840UZ;

    Mapper400(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void observe_cpu_write(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Byte> led_outputs() const override;

    [[nodiscard]] std::size_t lcd_data_count() const {
        return registers_.lcd_data_count;
    }
    [[nodiscard]] std::span<const Byte> lcd_data() const {
        return lcd_data_;
    }

    void power_on() override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte inner_prg_bank = 0U;
        Byte lower_outer_bank = 0U;
        Byte upper_outer_bank = 0U;
        Byte chr_bank = 0U;
        MirrorMode mirror = MirrorMode::Vertical;
        Byte leds = 0xFFU;
        Byte lcd_control = 3U;
        Byte lcd_command = 0U;
        std::uint32_t lcd_data_count = 0U;
    };

    void write_lcd_data(Byte value);
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 0x8000> chr_ram_{};
    std::array<Byte, kLcdDataSize> lcd_data_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_;
};

} // namespace mapperbus::core
