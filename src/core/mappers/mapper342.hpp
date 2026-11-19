#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Native COOLGIRL outer-register and NROM component for NES 2.0 mapper 342.
/// Simulated non-NROM ASICs, special banking modes and flash writes are not implemented.
class Mapper342 final : public Mapper {
  public:
    Mapper342(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void write_expansion(Address addr, Byte value) override;
    void power_on() override;
    void reset() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    struct RuntimeState {
        std::array<Byte, 8> registers{};
        std::array<Byte, 4> prg_banks{};
        std::array<std::uint16_t, 8> chr_banks{};
    };

    void sync_native_banks();
    bool wram_mapped(Address addr) const;
    std::optional<std::uint32_t> mapped_prg_offset(Address addr) const;
    std::optional<std::uint32_t> mapped_chr_offset(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> wram_;
    std::vector<Byte> chr_ram_;
    RuntimeState state_;
};

} // namespace mapperbus::core
