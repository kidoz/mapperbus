#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class SuperGameMegaTypeIII final : public Mapper {
  public:
    SuperGameMegaTypeIII(const INesHeader& header,
                         std::vector<Byte> prg_rom,
                         std::vector<Byte> chr_rom);

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
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    Result<void> configure_board_jumpers(Byte value) override;
    void set_coin_inserted(bool inserted);
    [[nodiscard]] bool audio_enabled() const;
    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte control = 0U;
        Byte mapping = 0U;
        Byte unrom_bank = 0U;
        Byte amrom_bank = 0U;
        Byte mmc1_shift = 0x10U;
        Byte mmc1_control = 0x0CU;
        Byte mmc1_chr0 = 0U;
        Byte mmc1_chr1 = 0U;
        Byte mmc1_prg = 0U;
        Byte mmc3_select = 0U;
        bool mmc3_prg_mode = false;
        bool mmc3_chr_inversion = false;
        std::array<Byte, 8> mmc3_banks{};
        MirrorMode mmc3_mirror = MirrorMode::Vertical;
        Byte mmc3_irq_counter = 0U;
        Byte mmc3_irq_reload = 0U;
        bool mmc3_irq_reload_flag = false;
        bool mmc3_irq_enabled = false;
        bool mmc3_irq_pending = false;
        bool wram_enabled = true;
        bool wram_write_protected = false;
        bool audio_enabled = false;
    };

    void write_serial_mmc1(Address addr, Byte value);
    void commit_mmc1(Address addr, Byte value);
    void write_mmc3(Address addr, Byte value);
    [[nodiscard]] unsigned mapper_select() const;
    [[nodiscard]] unsigned prg_a17(Address addr) const;
    [[nodiscard]] unsigned mmc1_prg_bank(Address addr) const;
    [[nodiscard]] unsigned mmc3_prg_bank(Address addr) const;
    [[nodiscard]] unsigned mmc1_chr_bank(Address addr) const;
    [[nodiscard]] unsigned mmc3_chr_bank(Address addr) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> prg_ram_{};
    std::array<Byte, 8192> chr_ram_{};
    Registers registers_;
    Byte dip_switches_ = 0U;
    bool coin_inserted_ = false;
};

} // namespace mapperbus::core
