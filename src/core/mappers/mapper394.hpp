#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 394: Realtec HSK007 BNROM/MMC3/J.Y. Company multicart.
class Mapper394 final : public Mapper {
  public:
    Mapper394(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    std::optional<Byte> read_nametable(Address addr) const override;
    bool write_nametable(Address addr, Byte value) override;

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
    void clock_cpu() override;
    void observe_cpu_write(Address addr, Byte data) override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    bool observes_ppu_reads() const override;
    void observe_ppu_read(Address addr) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Mmc3State {
        Byte bank_select = 0U;
        std::array<Byte, 8> banks{};
        MirrorMode mirror = MirrorMode::Vertical;
        Byte irq_counter = 0U;
        Byte irq_reload = 0U;
        Byte irq_flags = 0U;
        Byte ram_flags = 0x80U;
    };

    struct JyState {
        std::array<Byte, 4> prg_banks{};
        std::array<std::uint16_t, 8> chr_banks{};
        std::array<std::uint16_t, 4> nt_banks{};
        std::array<Byte, 4> config{};
        std::array<Byte, 2> latches{};
        Byte jumpers = 0U;
        Byte multiplicand = 0U;
        Byte multiplier = 0U;
        std::uint16_t product = 0U;
        std::uint16_t next_product = 0U;
        Byte multiply_remaining = 0U;
        Byte accumulator = 0U;
        Byte test = 0U;
        Byte irq_mode = 0U;
        Byte irq_config = 0U;
        Byte prescaler = 0U;
        Byte counter = 0U;
        Byte irq_xor = 0U;
        Byte irq_enabled = 0U;
        Byte irq_pending = 0U;
        Byte a12 = 0U;
    };

    struct State {
        Byte mode = 0x0FU;
        Byte outer = 0x90U;
        Mmc3State mmc3{};
        JyState jy{};
    };

    [[nodiscard]] unsigned mapper_mode() const;
    [[nodiscard]] bool jy_mode() const;
    [[nodiscard]] std::size_t prg_outer_base() const;
    [[nodiscard]] std::size_t chr_outer_base() const;
    [[nodiscard]] std::size_t mmc3_prg_offset(Address addr) const;
    [[nodiscard]] std::size_t mmc3_chr_offset(Address addr) const;
    [[nodiscard]] std::size_t jy_prg_offset(Address addr) const;
    [[nodiscard]] std::size_t jy_chr_offset(Address addr) const;
    [[nodiscard]] std::size_t jy_nametable_chr_offset(Address addr) const;
    [[nodiscard]] bool jy_chr_nametable(Address addr) const;
    void write_mmc3(Address addr, Byte value);
    void write_jy(Address addr, Byte value);
    void clock_jy_irq(Byte source);
    void disable_jy_irq();
    void clock_jy_a12(bool high);

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    Byte submapper_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    State state_;
};

} // namespace mapperbus::core
