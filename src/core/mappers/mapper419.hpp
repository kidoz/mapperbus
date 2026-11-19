#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// NES 2.0 mapper 419: OneBus banking and the Taikee TK-8007 ADPCM device.
/// Opcode descrambling and console-side $4017 read merging are outside this
/// cartridge component; its protocol status is available through read_expansion.
class Mapper419 final : public Mapper {
  public:
    Mapper419(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

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
    std::optional<Address> ciram_address(Address addr) const override;

    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void observe_cpu_write(Address addr, Byte value) override;
    std::optional<Byte> read_ppu_register(Address addr, Byte open_bus) override;
    bool write_ppu_register(Address addr, Byte value) override;

    void reset() override;
    bool irq_pending() const override;
    bool observes_ppu_a12() const override;
    void observe_ppu_a12(bool high) override;
    void clock_cpu() override;
    void on_ppu_hblank() override;

    bool has_expansion_audio() const override;
    void set_region(Region region) override;
    void clock_audio() override;
    float audio_output() const override;

    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    struct Registers {
        Byte outer = 0U;
        Byte control = 0U;
        Byte mirror = 0U;
        std::array<Byte, 3> prg{0U, 1U, 0xFEU};
        Byte middle = 0U;
        Byte mode = 0U;
        std::array<Byte, 6> chr{4U, 5U, 6U, 7U, 0U, 2U};
        Byte intermediate = 0U;
        Byte chr_middle = 0U;
        Byte latch = 0U;
        Byte counter = 0U;
        bool enabled = false;
        bool pending = false;
        bool a12 = false;
        Byte low_cycles = 0U;
    };

    [[nodiscard]] unsigned canonical_chr_register(Address ppu_base) const;
    [[nodiscard]] Address native_ppu_base(unsigned raw_index) const;
    [[nodiscard]] Address forwarded_ppu_base(unsigned bank_index) const;
    [[nodiscard]] std::size_t prg_address(Address addr) const;
    [[nodiscard]] std::size_t chr_address(Address addr) const;
    [[nodiscard]] std::uint16_t state_tag() const;
    void write_system(unsigned reg, Byte value);
    void clock_counter();

    void receive_audio_byte(Byte value);
    void append_audio_byte(Byte value);
    void reset_audio_chip();
    void reset_decoder();
    void clock_sample();
    [[nodiscard]] std::uint64_t audio_threshold() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const Byte submapper_;
    Registers registers_;
    Byte opcode_control_ = 0U;

    std::array<Byte, 96> audio_buffer_{};
    Byte audio_read_ = 0U;
    Byte audio_size_ = 0U;
    std::uint64_t audio_frame_ = 0U;
    Byte audio_frame_sample_ = 21U;
    std::int32_t predictor_ = 0;
    std::int32_t output_ = 0;
    Byte audio_index_ = 0U;
    std::uint16_t playback_period_ = 0U;
    std::uint64_t audio_phase_ = 0U;
    std::uint32_t cpu_clock_hz_ = 1789773U;

    Byte audio_command_ = 0U;
    Byte audio_command_offset_ = 0U;
    Byte period_low_ = 0U;
    bool reset_prefix_ = false;
    bool io_enabled_ = false;
    Byte audio_nibble_ = 0U;
    Byte audio_high_nibble_ = 0U;
    bool io_clock_high_ = false;
    bool audio_acknowledge_ = true;
    bool audio_ready_valid_ = false;
    bool audio_ready_ = false;
};

} // namespace mapperbus::core
