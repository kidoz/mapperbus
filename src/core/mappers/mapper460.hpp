#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 460: FC-29-40/K-3101 MMC3 multicarts with CHR ROM and CHR RAM.
class Mapper460 final : public Mapper {
  public:
    Mapper460(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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

    Result<void> configure_board_jumpers(Byte value) override;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Address extra = 0U;
        Byte bank_select = 0U;
        Byte bank_mode = 0U;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        Byte irq_flags = 0U;
        Byte ram_flags = 0x80U;
    };

    [[nodiscard]] bool nrom_mode() const;
    [[nodiscard]] bool nrom256_mode() const;
    [[nodiscard]] bool prg_rom_disabled() const;
    [[nodiscard]] bool chr_ram_selected() const;
    [[nodiscard]] Byte inner_prg_bank(Address addr) const;
    [[nodiscard]] Byte inner_chr_bank(Address addr) const;
    [[nodiscard]] std::size_t prg_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_offset(Address addr) const;
    [[nodiscard]] std::uint16_t configuration_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> chr_ram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    bool solder_pad_connected_ = false;
    Registers registers_{};
};

} // namespace mapperbus::core
