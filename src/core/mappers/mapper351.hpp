#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 351: Techline XB-xxx multi-ASIC multicarts.
class Mapper351 final : public Mapper {
  public:
    Mapper351(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void power_on() override;
    void reset() override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void clock_cpu() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    enum class Asic : Byte { Mmc3 = 0U, Mmc1 = 2U, Vrc4 = 3U };

    struct Registers {
        Byte chr_base = 0U;
        Byte prg_base = 0U;
        Byte banking = 0U;
        Byte nrom_mirror = 0U;

        Byte mmc3_select = 0U;
        std::array<Byte, 8> mmc3_banks{};
        Byte mmc3_mirror = 0U;
        Byte mmc3_irq_counter = 0U;
        Byte mmc3_irq_reload = 0U;
        Byte mmc3_irq_flags = 0U;

        Byte mmc1_shift = 0x10U;
        Byte mmc1_count = 0U;
        Byte mmc1_control = 0x0CU;
        Byte mmc1_chr0 = 0U;
        Byte mmc1_chr1 = 0U;
        Byte mmc1_prg = 0U;

        std::array<Byte, 2> vrc_prg{0U, 1U};
        std::array<std::uint16_t, 8> vrc_chr{};
        Byte vrc_mirror = 0U;
        Byte vrc_mode = 0U;
        Byte vrc_irq_latch = 0U;
        Byte vrc_irq_counter = 0U;
        std::uint16_t vrc_irq_prescaler = 341U;
        Byte vrc_irq_control = 0U;
        Byte vrc_irq_enabled = 0U;
        Byte vrc_irq_pending = 0U;
    };

    [[nodiscard]] Asic asic() const;
    [[nodiscard]] std::size_t prg_span() const;
    [[nodiscard]] std::size_t chr_span() const;
    [[nodiscard]] std::size_t prg_inner_offset(Address addr) const;
    [[nodiscard]] std::size_t chr_inner_offset(Address addr) const;
    [[nodiscard]] Byte read_combined_prg(std::size_t offset) const;
    void write_mmc1(Address addr, Byte value);
    void write_mmc1_register(Address addr, Byte value);
    void write_vrc4(Address addr, Byte value);
    void vrc_irq_control(Byte value);
    void vrc_irq_tick();
    [[nodiscard]] std::uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> chr_ram_{};
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    Registers registers_;
};

} // namespace mapperbus::core
