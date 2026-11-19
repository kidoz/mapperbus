#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Mapper 014: Gouder SL-1632, with independent MMC3 and VRC2b register files.
class Sl1632 final : public Mapper {
  public:
    Sl1632(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    void reset() override;
    void power_on() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Mmc3State {
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

    struct Vrc2State {
        std::array<Byte, 2> prg_banks{0U, 1U};
        std::array<Byte, 8> chr_banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        bool microwire = false;
    };

    bool mmc3_mode() const;
    void write_mmc3(Address addr, Byte value);
    void write_vrc2(Address addr, Byte value);
    Byte mmc3_chr_bank(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    MirrorMode initial_mirror_;
    Byte supervisor_ = 1U;
    Mmc3State mmc3_;
    Vrc2State vrc2_;
};

} // namespace mapperbus::core
