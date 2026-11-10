#pragma once

#include <array>
#include <string_view>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/bandai_irq.hpp"
#include "core/mappers/mapper.hpp"
#include "core/mappers/serial_eeprom.hpp"

namespace mapperbus::core {

/// Datach reader and its inserted game cartridge (NES 2.0 mapper 157).
class Datach final : public Mapper {
  public:
    Datach(const INesHeader& header, std::vector<Byte> prg, const std::vector<Byte>& chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void clock_cpu() override;
    bool irq_pending() const override;
    bool observes_ppu_address() const override;
    void observe_ppu_address(Address addr) override;
    Result<void> scan_barcode(std::string_view digits, uint32_t module_cycles) override;
    std::span<const Byte> shared_nvram() const override;
    void set_shared_nvram(std::span<const Byte> data) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    void update_serial();
    std::vector<Byte> prg_rom_;
    const bool external_;
    std::array<Byte, 8192> chr_ram_{};
    std::array<Byte, 4> clocks_{};
    Byte prg_bank_ = 0U;
    Byte mirror_ = 0U;
    Byte ppu_slot_ = 0U;
    Byte serial_control_ = 0x60U;
    Byte bus_sda_ = 1U;
    BandaiIrq irq_;
    SerialEeprom internal_eeprom_{EepromKind::X24C02};
    SerialEeprom external_eeprom_;
    std::array<Byte, 128> barcode_{};
    Byte barcode_size_ = 0U;
    Byte barcode_position_ = 0U;
    uint32_t module_cycles_ = 1000U;
    uint32_t module_phase_ = 0U;
};

} // namespace mapperbus::core
