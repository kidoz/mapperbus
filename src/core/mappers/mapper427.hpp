#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 427: normal OneBus banking, GPIO protection and optional 24C04.
/// This is the documented 2bpp cartridge component, not a complete VT369 console.
class Mapper427 final : public Mapper {
  public:
    static constexpr std::size_t kEepromSize = 512U;

    Mapper427(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    std::optional<Address> ciram_address(Address addr) const override;

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    std::optional<Byte> read_ppu_register(Address addr, Byte open_bus) override;
    bool write_ppu_register(Address addr, Byte value) override;

    void reset() override;
    bool irq_pending() const override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    void clock_cpu() override;
    void on_ppu_hblank() override;

    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte control = 0U;
        Byte mirror = 0U;
        std::array<Byte, 3> prg{0U, 1U, 0xFEU};
        Byte middle = 0U;
        Byte mode = 0U;
        std::array<Byte, 6> chr{4U, 5U, 6U, 7U, 0U, 2U};
        Byte intermediate = 0U;
        Byte chr_middle = 0U;
        Byte latch = 0U;
        Byte counter = 0U;
        bool enabled = false;
        bool pending = false;
        bool a12 = false;
        Byte low_cycles = 0U;
    };

    struct GpioRegisters {
        Byte mask = 0U;
        Byte latch = 0U;
        Byte state = 0xFFU;
    };

    struct ProtectionRegisters {
        Byte stage = 0U;
        Byte command = 0U;
        Byte result = 0U;
        bool clock = true;
        bool data = true;
        bool output = true;
    };

    struct EepromRegisters {
        Byte stage = 0U;
        bool clock = true;
        bool data = true;
        bool output = true;
        bool read_mode = false;
        Word address = 0U;
        Byte bit = 0U;
        Byte latch = 0U;
    };

    void write_system(unsigned reg, Byte value);
    void write_gpio(GpioRegisters& gpio, unsigned port, Address addr, Byte value);
    Byte read_gpio(GpioRegisters& gpio, unsigned port, Address addr);
    void set_protection_pins(bool clock, bool data);
    void set_eeprom_pins(bool clock, bool data);
    void receive_eeprom_bit();
    void clock_counter();
    [[nodiscard]] std::size_t prg_address(Address addr) const;
    [[nodiscard]] std::size_t chr_address(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Byte submapper_ = 0U;
    bool has_eeprom_ = false;
    Registers registers_;
    GpioRegisters gpio1_;
    GpioRegisters gpio2_;
    ProtectionRegisters protection_;
    EepromRegisters eeprom_;
    std::array<Byte, kEepromSize> eeprom_data_{};
};

} // namespace mapperbus::core
