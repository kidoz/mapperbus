#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 370: F600 MMC3 multicart with outer ROM pins and a solder pad.
class Mapper370 final : public Mapper {
  public:
    Mapper370(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    std::optional<Address> nametable_address(Address addr) const override;

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;

    Result<void> configure_board_jumpers(Byte value) override;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;

    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte bank_select = 0U;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_latch = 0U;
        bool irq_reload = false;
        bool irq_enabled = false;
        bool irq_pending = false;
        bool ram_enabled = true;
        bool ram_write_protected = false;
    };

    [[nodiscard]] bool uses_tlsrom() const;
    [[nodiscard]] unsigned prg_bank(Address addr) const;
    [[nodiscard]] unsigned chr_bank(Address addr) const;
    [[nodiscard]] unsigned prg_rom_bank(unsigned bank) const;
    [[nodiscard]] unsigned chr_rom_bank(unsigned bank) const;
    [[nodiscard]] std::uint16_t memory_state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> prg_ram_;
    std::size_t nvram_size_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Byte solder_pad_ = 0U;
    Registers registers_;
};

} // namespace mapperbus::core
