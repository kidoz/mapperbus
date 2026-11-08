#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Latch-based cartridge boards without IRQ or audio hardware. Each board's
/// address decoding and wiring is selected explicitly, never by a game hash.
class DiscreteMapper final : public Mapper {
  public:
    DiscreteMapper(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    bool maps_expansion(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    Byte prg_read_mask(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    [[nodiscard]] bool has_prg_ram() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x8000> chr_ram_{};
    std::array<Byte, 0x2000> prg_ram_{};
    const uint16_t mapper_;
    const uint8_t submapper_;
    const MirrorMode initial_mirror_;
    const bool nina_;
    const bool holy_diver_;
    std::array<Byte, 8> registers_{};
    std::array<Byte, 4> nibble_ram_{};
    Address address_latch_ = 0;
    Byte register_select_ = 0;
    MirrorMode mirror_;
};

} // namespace mapperbus::core
