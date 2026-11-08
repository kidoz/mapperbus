#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// X1-005 (80) and the CHR-A17-to-CIRAM wiring used by mapper 207.
class TaitoX1005 final : public Mapper {
  public:
    TaitoX1005(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 128> internal_ram_{};
    std::array<Byte, 0x2000> chr_ram_{};
    const bool chr_ciram_;
    const bool battery_;
    const Byte initial_mirror_;
    const std::size_t chr_ram_size_;
    std::array<Byte, 3> prg_banks_{};
    std::array<Byte, 6> chr_banks_{};
    Byte mirror_ = 0U;
    Byte ram_enabled_ = 0U;
};

} // namespace mapperbus::core
