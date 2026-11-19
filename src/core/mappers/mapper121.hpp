#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Mapper121 final : public Mapper {
  public:
    Mapper121(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    [[nodiscard]] static Result<void> validate_geometry(const INesHeader& header,
                                                        std::size_t prg_size,
                                                        std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;

    bool maps_expansion(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    static Byte reverse_bank(Byte value);
    uint16_t state_tag() const;
    Byte normal_prg_bank(Address addr) const;
    void apply_protection_index(Byte index);
    void write_mmc3(Address addr, Byte value);

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    bool a9713_ = false;
    bool chr_a18_from_ppu_ = false;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    MirrorMode mirror_mode_ = MirrorMode::Vertical;

    Byte bank_select_ = 0U;
    bool prg_mode_ = false;
    bool chr_inversion_ = false;
    std::array<Byte, 8> bank_registers_{};

    Byte irq_counter_ = 0U;
    Byte irq_reload_ = 0U;
    bool irq_reload_flag_ = false;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;

    Byte protection_latch_ = 0U;
    Byte protection_index_ = 0U;
    std::array<Byte, 3> protected_prg_banks_{};
    Byte protection_read_index_ = 0U;
    bool outer_bank_ = false;
};

} // namespace mapperbus::core
