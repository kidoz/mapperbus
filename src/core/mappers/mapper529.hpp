#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 529: YY0807/J-2148/T-230 VRC4e and optional 93C56 EEPROM.
class Mapper529 final : public Mapper {
  public:
    static constexpr std::size_t kEepromSize = 0x100UZ;

    Mapper529(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_cpu() override;

    void reset() override;
    void power_on() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    enum class EepromPhase : Byte { Command, Ignore, Read, Write, WaitChipSelect, Busy };
    enum class EepromOperation : Byte { None, Write, Erase, WriteAll, EraseAll };

    struct VrcState {
        Byte prg_bank = 0U;
        std::array<std::uint16_t, 8> chr_banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_latch = 0U;
        Byte irq_counter = 0U;
        std::uint16_t irq_prescaler = 341U;
        Byte irq_control = 0U;
        Byte irq_enabled = 0U;
        Byte irq_pending = 0U;
    };

    struct EepromState {
        Byte chip_select = 0U;
        Byte clock = 0U;
        Byte input = 0U;
        Byte output = 1U;
        Byte write_enabled = 0U;
        EepromPhase phase = EepromPhase::Command;
        EepromOperation pending = EepromOperation::None;
        std::uint16_t command = 0U;
        Byte command_bits = 0U;
        Byte address = 0U;
        std::uint16_t shift = 0U;
        Byte data_bits = 0U;
        Word busy = 0U;
        Word pending_data = 0U;
        Byte pending_address = 0U;

        bool operator==(const EepromState&) const = default;
    };

    void set_microwire(bool chip_select, bool clock, bool input);
    void execute_microwire_command();
    void begin_microwire_write();
    void clock_microwire();
    [[nodiscard]] Word read_eeprom_word(Byte address) const;
    void write_eeprom_word(Byte address, Word value);
    void irq_control(Byte value);
    void irq_tick();

    const MirrorMode initial_mirror_;
    const bool has_eeprom_;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    std::array<Byte, kEepromSize> eeprom_data_{};
    VrcState vrc_;
    EepromState eeprom_;
};

} // namespace mapperbus::core
