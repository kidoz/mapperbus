#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Tec9719 final : public Mapper {
  public:
    Tec9719(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    Result<void> configure_board_jumpers(Byte value) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void power_on() override;
    void reset() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte misc = 0U;
        Byte chr_bank = 0U;
        Byte mode = 0U;
        Byte bank_select = 0U;
        Byte bank_mode = 0U;
        std::array<Byte, 8> banks{};
        Byte mirroring = 0U;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        Byte irq_flags = 0U;
        Byte ram_flags = 0x80U;
    };

    void write_outer_register(unsigned index, Byte value);
    void write_mmc3(Address addr, Byte value);
    unsigned prg_bank(unsigned slot) const;
    unsigned mmc3_chr_bank(Address addr) const;
    std::size_t chr_offset(Address addr) const;
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::vector<Byte> prg_ram_;
    std::vector<Byte> chr_ram_;
    Registers registers_;
    Byte submapper_ = 0U;
    MirrorMode initial_mirroring_ = MirrorMode::Vertical;
    Byte sl0_ = 0U;
    bool has_battery_ = false;
};

} // namespace mapperbus::core
