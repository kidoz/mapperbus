#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/bandai_irq.hpp"
#include "core/mappers/mapper.hpp"
#include "core/mappers/serial_eeprom.hpp"

namespace mapperbus::core {

/// Bandai FCG-1/2, LZ93D50, JUMP2 and X24C01 boards (16, 153, 159).
class BandaiFcg final : public Mapper {
  public:
    BandaiFcg(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
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
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const bool wram_;
    const bool low_only_;
    const bool dual_decode_;
    const EepromKind eeprom_kind_;
    SerialEeprom eeprom_;
    std::array<Byte, 0x2000> prg_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    std::array<Byte, 8> chr_banks_{};
    Byte prg_bank_ = 0U;
    Byte mirror_ = 0U;
    Byte ppu_slot_ = 0U;
    Byte ram_enabled_ = 0U;
    Byte serial_control_ = 0x60U;
    BandaiIrq irq_;
};

} // namespace mapperbus::core
