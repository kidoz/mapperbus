#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 164: Yancheng cy2000-3 / FS304 banking, 1 bpp CHR and 93C66 EEPROM.
class Fs304 final : public Mapper {
  public:
    Fs304(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    Byte read_expansion(Address addr) override;
    Byte expansion_read_mask(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    void observe_cpu_read(Address addr, Byte data) override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    void clock_cpu() override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    struct EepromState {
        enum class Phase : Byte { Idle, Command, Read, WriteData, Erase, Ignore };
        enum class Operation : Byte { None, Write, Erase, WriteAll, EraseAll };

        std::array<Byte, 512> data{};
        Phase phase = Phase::Idle;
        Word shift = 0U;
        Byte bits = 0U;
        Byte read_bits = 0U;
        Word address = 0U;
        Byte write_data = 0U;
        bool write_enabled = false;
        bool valid = true;
        Operation pending = Operation::None;
        Word pending_address = 0U;
        Byte pending_data = 0U;
        bool chip_select = false;
        bool clock = false;
        bool data_in = false;
        bool data_out = true;
        Word busy = 0U;
    };

    std::size_t prg_bank(Address addr) const;
    Address chr_address(Address addr) const;
    uint16_t state_tag() const;
    void eeprom_set_lines(bool chip_select, bool clock, bool data_in);
    void eeprom_decode_command();
    void eeprom_power_on();
    [[nodiscard]] bool eeprom_output() const;
    void eeprom_save_state(StateWriter& writer) const;
    void eeprom_load_state(StateReader& reader, EepromState& state) const;

    std::vector<Byte> prg_rom_;
    std::size_t wram_size_;
    std::array<Byte, 8192> wram_{};
    std::array<Byte, 8192> chr_ram_{};
    Byte bank_low_ = 0U;
    Byte bank_high_ = 0U;
    Byte microwire_ = 0U;
    Byte mirror_control_ = 0U;
    bool latched_a0_ = false;
    bool latched_a9_ = false;
    bool previous_a13_ = false;
    EepromState eeprom_;
};

} // namespace mapperbus::core
