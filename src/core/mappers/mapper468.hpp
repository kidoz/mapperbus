#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 468: BlazePro multi-ASIC multicart with supervisor banking.
class Mapper468 final : public Mapper {
  public:
    Mapper468(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    ~Mapper468() override;

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
    void on_ppu_hblank() override;
    void on_ppu_frame_start() override;

    bool has_expansion_audio() const override;
    void clock_audio() override;
    float audio_output() const override;

    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    enum class Mode : Byte {
        Invalid,
        Sxrom,
        Surom,
        Axrom,
        Bxrom,
        Misc,
        Lf36,
        Fxrom,
        Uxrom,
        If12,
        Pnrom,
        Unrom512,
        Gnrom,
        Nanjing,
        Txrom,
        Txsrom,
        Vrc24,
        Vrc6,
        Vrc1,
        Vrc7,
        Vrc3,
        Fme7,
    };

    struct Mmc1State {
        Byte shift = 0x10U;
        Byte write_count = 0U;
        Byte control = 0x0CU;
        Byte chr0 = 0U;
        Byte chr1 = 0U;
        Byte prg = 0U;
    };

    struct Mmc24State {
        Byte prg = 0U;
        std::array<Byte, 4> chr{};
        std::array<Byte, 2> latches{};
    };

    struct Mmc3State {
        Byte bank_select = 0U;
        std::array<Byte, 8> banks{};
        Byte mirror = 0U;
    };

    struct VrcState {
        std::array<Byte, 4> prg{};
        std::array<uint16_t, 8> chr{};
        Byte command = 0U;
        Byte mode = 0U;
        Byte microwire = 0U;
    };

    struct IrqState {
        uint16_t latch = 0U;
        uint16_t counter = 0U;
        uint16_t prescaler = 341U;
        Byte control = 0U;
        Byte enabled = 0U;
        Byte after_ack = 0U;
        Byte pending = 0U;
        Byte counter_enabled = 0U;
        Byte reload = 0U;
    };

    struct Vrc6PulseState {
        Byte duty = 0U;
        Byte volume = 0U;
        Byte mode = 0U;
        Byte enabled = 0U;
        Byte halted = 0U;
        Byte frequency_shift = 0U;
        uint16_t period = 0U;
        uint16_t timer = 0U;
        Byte step = 0U;
    };

    struct Vrc6SawState {
        Byte rate = 0U;
        Byte enabled = 0U;
        Byte halted = 0U;
        Byte frequency_shift = 0U;
        uint16_t period = 0U;
        uint16_t timer = 0U;
        Byte accumulator = 0U;
        Byte step = 0U;
    };

    struct AyChannelState {
        uint16_t period = 0U;
        uint16_t timer = 0U;
        Byte volume = 0U;
        Byte use_envelope = 0U;
        Byte tone_enabled = 1U;
        Byte noise_enabled = 1U;
        Byte output = 0U;
    };

    struct Fme7AudioState {
        std::array<AyChannelState, 3> channels{};
        Byte ay_register = 0U;
        Byte divider = 0U;
        Byte noise_period = 0U;
        uint16_t noise_timer = 0U;
        uint32_t noise_lfsr = 1U;
        uint16_t envelope_period = 0U;
        uint16_t envelope_timer = 0U;
        Byte envelope_shape = 0U;
        Byte envelope_step = 0U;
        Byte envelope_level = 0U;
        Byte envelope_attack = 0U;
        Byte envelope_holding = 0U;
    };

    struct InnerState {
        std::array<Byte, 4> custom{};
        Byte latch = 0U;
        MirrorMode mirror = MirrorMode::Vertical;
        Mmc1State mmc1{};
        Mmc24State mmc24{};
        Mmc3State mmc3{};
        VrcState vrc{};
        IrqState irq{};
        Byte ram_enabled = 0U;
        Byte ram_write_protected = 0U;
        Vrc6PulseState pulse1{};
        Vrc6PulseState pulse2{};
        Vrc6SawState saw{};
        Fme7AudioState fme7{};
    };

    struct EepromState {
        std::array<Byte, 16> memory{};
        Byte clock = 1U;
        Byte phase = 0U;
        Byte command = 0U;
        Byte output = 1U;
    };

    struct RuntimeState {
        std::array<Byte, 4> supervisor{};
        InnerState inner{};
        EepromState eeprom{};
        uint16_t ppu_scanline = 0U;
        Byte nanjing_upper_half = 0U;
    };

    [[nodiscard]] Mode mode() const;
    [[nodiscard]] uint32_t outer_bank() const;
    [[nodiscard]] std::size_t chr_ram_size() const;
    [[nodiscard]] Byte read_prg8(uint32_t bank, Address addr, uint32_t and_mask) const;
    [[nodiscard]] Byte read_chr1k(uint32_t bank, Address addr, uint32_t and_mask) const;
    [[nodiscard]] Byte read_chr_ram(std::size_t offset) const;
    [[nodiscard]] Byte mmc3_chr_bank(Address addr) const;
    [[nodiscard]] MirrorMode vrc_mirror(Byte value) const;
    [[nodiscard]] bool valid_mode_state(const RuntimeState& state) const;

    void activate_mode();
    void write_mmc1(Address addr, Byte value);
    void write_mmc24(Address addr, Byte value);
    void write_mmc3(Address addr, Byte value);
    void write_vrc24(Address addr, Byte value);
    void write_vrc6(Address addr, Byte value);
    void write_vrc7(Address addr, Byte value);
    void write_fme7(Address addr, Byte value);
    void tick_vrc_irq();
    void tick_vrc3_irq();
    void tick_vrc6_audio();
    void tick_fme7_audio();
    void clock_fme7_envelope();
    void set_eeprom_pins(Byte select, Byte clock, Byte data);

    const INesHeader header_;
    const std::size_t prg_size_;
    const std::size_t chr_size_;
    const std::size_t chr_ram_size_;
    const bool persistent_ram_;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 0x2000> wram_{};
    std::array<Byte, 0x8000> chr_ram_{};
    RuntimeState runtime_{};
    std::unique_ptr<Mapper> fm_audio_;
};

} // namespace mapperbus::core
