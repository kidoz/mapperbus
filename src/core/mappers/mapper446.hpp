#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 446: Mindkids SMD172B FPGA multicart.
class Mapper446 final : public Mapper {
  public:
    Mapper446(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
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

    void power_on() override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_irq_counter() override;
    void clock_cpu() override;
    bool has_expansion_audio() const override;
    void clock_audio() override;
    float audio_output() const override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;

  private:
    enum class Inner : Byte {
        Unrom,
        Tkrom,
        Bnrom,
        Cnrom,
        Anrom,
        Skrom,
        Snrom,
        Surom,
        Gnrom,
        Pnrom,
        Tlrom,
        Hkrom,
        Bandai152,
        Tksrom,
        Mapper189,
        Vrc6,
        Vrc2,
        Vrc4,
        Vrc23,
        Vrc1,
        H3001
    };

    struct Pulse {
        Byte control = 0U;
        std::uint16_t period = 0U;
        std::uint16_t timer = 0U;
        Byte step = 0U;
        bool enabled = false;
    };

    struct Saw {
        Byte rate = 0U;
        std::uint16_t period = 0U;
        std::uint16_t timer = 0U;
        Byte accumulator = 0U;
        Byte step = 0U;
        bool enabled = false;
    };

    struct Registers {
        Byte selection = 0U;
        Byte outer_prg_lsb = 0U;
        Byte outer_prg_msb = 0U;
        Byte prg_mask = 0U;
        Byte flags = 0U;
        Byte chr_protect = 0U;
        Byte outer_chr = 0U;
        bool locked = false;

        Byte discrete_bank = 0U;
        Byte discrete_mirror = 0U;

        Byte mmc1_shift = 0x10U;
        Byte mmc1_count = 0U;
        Byte mmc1_control = 0x0CU;
        Byte mmc1_chr0 = 0U;
        Byte mmc1_chr1 = 0U;
        Byte mmc1_prg = 0U;

        Byte mmc3_select = 0U;
        std::array<Byte, 8> mmc3_banks{};
        Byte mmc3_mirror = 0U;
        Byte mmc3_ram_control = 0x80U;
        Byte mmc3_irq_counter = 0U;
        Byte mmc3_irq_reload = 0U;
        Byte mmc3_irq_flags = 0U;

        Byte mmc2_prg = 0U;
        std::array<Byte, 4> mmc2_chr{};
        std::array<Byte, 2> mmc2_latches{};

        std::array<Byte, 3> vrc_prg{};
        std::array<std::uint16_t, 8> vrc_chr{};
        Byte vrc_mirror = 0U;
        Byte vrc_mode = 0U;
        Byte microwire = 0U;
        Byte vrc_irq_latch = 0U;
        Byte vrc_irq_counter = 0U;
        std::uint16_t vrc_irq_prescaler = 341U;
        Byte vrc_irq_control = 0U;
        Byte vrc_irq_enabled = 0U;
        Byte vrc_irq_pending = 0U;

        std::array<Byte, 2> h3001_prg{0U, 1U};
        std::array<Byte, 8> h3001_chr{};
        Byte h3001_swap = 0U;
        Word h3001_irq_reload = 0U;
        Word h3001_irq_counter = 0U;
        Byte h3001_irq_enabled = 0U;
        Byte h3001_irq_pending = 0U;

        std::array<Pulse, 2> pulses{};
        Saw saw{};
        Byte audio_control = 0U;
    };

    [[nodiscard]] std::optional<Inner> inner() const;
    [[nodiscard]] bool is_mmc1() const;
    [[nodiscard]] bool is_mmc3() const;
    [[nodiscard]] bool is_vrc() const;
    [[nodiscard]] bool ram_enabled(Address addr) const;
    [[nodiscard]] bool ram_writable() const;
    [[nodiscard]] std::size_t inner_prg_offset(Address addr) const;
    [[nodiscard]] std::size_t inner_chr_offset(Address addr) const;
    [[nodiscard]] std::size_t outer_prg_offset(Address addr) const;
    [[nodiscard]] std::size_t outer_chr_offset(Address addr) const;
    [[nodiscard]] unsigned mmc3_chr_bank(Address addr) const;
    [[nodiscard]] Byte vrc_port(Address addr) const;
    void write_mmc1(Address addr, Byte value);
    void write_mmc1_register(Address addr, Byte value);
    void write_mmc3(Address addr, Byte value);
    void write_vrc(Address addr, Byte value);
    void write_vrc6(Address addr, Byte value);
    void write_h3001(Address addr, Byte value);
    void vrc_irq_control(Byte value);
    void vrc_irq_acknowledge();
    void vrc_irq_tick();
    void clock_pulse(Pulse& pulse);
    void clock_saw();
    [[nodiscard]] Byte pulse_output(const Pulse& pulse) const;
    [[nodiscard]] Byte saw_output() const;
    [[nodiscard]] std::uint32_t geometry_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_ram_;
    std::array<Byte, 0x2000> prg_ram_{};
    Byte submapper_ = 0U;
    MirrorMode initial_mirror_ = MirrorMode::Vertical;
    std::size_t prg_ram_size_ = 0U;
    std::size_t nvram_size_ = 0U;
    Registers registers_{};
};

} // namespace mapperbus::core
