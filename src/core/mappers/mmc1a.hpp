#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 155: MMC1A on the documented SxROM board family.
class Mmc1a final : public Mapper {
  public:
    Mmc1a(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    void reset() override;
    void power_on() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    Byte active_chr_register() const;
    bool snrom_ram_disabled() const;
    bool ram_mapped() const;
    std::size_t ram_bank() const;
    std::size_t prg_address(Address addr) const;
    std::size_t chr_address(Address addr) const;
    void reset_serial_port();
    void write_register(Address addr, Byte value);

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> prg_ram_;
    std::vector<Byte> chr_ram_;
    std::size_t prg_nvram_size_ = 0;
    std::size_t prg_nvram_offset_ = 0;
    bool snrom_ = false;
    bool ppu_a12_ = false;

    Byte shift_register_ = 0x10;
    uint8_t write_count_ = 0;
    Byte control_ = 0x0C;
    Byte chr_bank0_ = 0;
    Byte chr_bank1_ = 0;
    Byte prg_bank_ = 0;
};

} // namespace mapperbus::core
