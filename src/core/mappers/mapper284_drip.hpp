#pragma once

#include <array>
#include <optional>
#include <span>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Drip final : public Mapper {
  public:
    Drip(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    std::span<Byte> trainer_ram() override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;

    void power_on() override;
    void reset() override;
    Result<void> configure_board_jumpers(Byte value) override;

    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_cpu() override;

    bool has_expansion_audio() const override;
    void clock_audio() override;
    float audio_output() const override;

    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

    // The PPU must query this with loopy::tile_address(bg_v) instead of using
    // the ordinary packed attribute byte when the returned value has data.
    [[nodiscard]] std::optional<Byte> background_attribute_palette(
        Address tile_address) const override;

  private:
    struct PcmChannel {
        std::array<Byte, 256> fifo{};
        Byte read_index = 0U;
        std::uint16_t count = 0U;
        std::uint16_t period = 0U;
        std::uint16_t active_period = 0U;
        std::uint16_t timer = 0U;
        Byte volume = 0U;
        Byte pwm_phase = 0U;
        bool active = false;
    };

    void clear_channel(PcmChannel& channel);
    void push_sample(PcmChannel& channel, Byte value);
    void clock_channel(PcmChannel& channel);
    [[nodiscard]] float channel_output(const PcmChannel& channel) const;
    void write_register(unsigned reg, Byte value);
    [[nodiscard]] std::uint32_t state_tag() const;
    void save_channel(StateWriter& writer, const PcmChannel& channel) const;
    [[nodiscard]] bool load_channel(StateReader& reader, PcmChannel& channel) const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 8192> wram_{};
    std::array<Byte, 2048> extended_attributes_{};
    std::array<Byte, 4> chr_banks_{};
    std::array<PcmChannel, 2> channels_{};
    MirrorMode mirror_ = MirrorMode::Vertical;
    Byte dip_switch_ = 0U;
    Byte control_ = 0U;
    Byte prg_bank_ = 0U;
    Byte irq_low_ = 0U;
    std::uint16_t irq_counter_ = 0U;
    bool irq_enabled_ = false;
    bool irq_pending_ = false;
    bool battery_ = false;
};

} // namespace mapperbus::core
