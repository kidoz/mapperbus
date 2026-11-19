#pragma once

#include "core/mappers/vrc6.hpp"

namespace mapperbus::core {
/// Broke Studio's Rainbow FPGA in offline application mode.
class Rainbow final : public Mapper {
  public:
    Rainbow(const INesHeader&, std::vector<Byte>, std::vector<Byte>);
    static Result<void> validate_geometry(const INesHeader&, std::size_t, std::size_t);
    Byte read_prg(Address) override;
    void write_prg(Address, Byte) override;
    bool maps_prg(Address a) const override {
        return a >= 0x6000;
    }
    Byte prg_read_mask(Address) const override;
    Byte read_chr(Address) override;
    void write_chr(Address, Byte) override;
    MirrorMode mirror_mode() const override {
        return MirrorMode::Horizontal;
    }
    std::optional<Address> ciram_address(Address) const override;
    std::optional<Byte> read_nametable(Address) const override;
    bool write_nametable(Address, Byte) override;
    bool uses_render_context() const override {
        return true;
    }
    void on_render_dot(unsigned, unsigned, bool) override;
    std::optional<BackgroundFetch> background_fetch(Address,
                                                    Address,
                                                    unsigned,
                                                    unsigned) const override;
    std::optional<std::array<Byte, 2>> sprite_pattern(unsigned, Address, bool) const override;
    bool maps_expansion(Address) const override;
    Byte expansion_read_mask(Address) const override;
    Byte read_expansion(Address) override;
    void write_expansion(Address, Byte) override;
    void observe_cpu_read(Address, Byte) override;
    void clock_cpu_falling_edge() override;
    bool irq_pending() const override;
    bool has_expansion_audio() const override {
        return true;
    }
    void clock_audio() override;
    float audio_output() const override;
    void reset() override;
    void power_on() override;
    bool has_flash_storage() const override {
        return true;
    }
    std::span<const Byte> battery_ram() const override {
        return storage_;
    }
    void set_battery_ram(std::span<const Byte>) override;
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    enum class Chip { PrgFlash, ChrFlash, WorkRam, ChrRam, Fpga };
    struct Location {
        Chip chip;
        unsigned offset;
    };
    Location cpu_location(Address) const;
    std::optional<Byte> redirected_vector_byte(Address) const;
    Location pattern_location(unsigned offset) const;
    Location chr_location(Address) const;
    Location nt_location(Address) const;
    Byte read(Location) const;
    void write(Location, Byte);
    void write_flash(bool chr, unsigned offset, Byte);
    void update_voice(unsigned channel);
    void ack_cpu_irq();
    Byte mixed_sample() const;
    unsigned fpga_ptr() const;
    void step_fpga_ptr();
    unsigned const prg_size_, chr_size_, nvram_size_;
    const std::vector<Byte> original_;
    std::vector<Byte> storage_, dirty_, work_, chr_ram_;
    std::array<Byte, 8192> fpga_{};
    std::array<Byte, 0x144> reg_{}; // CPU $4100-$4243, including sprite banks.
    std::array<Byte, 2> flash_phase_{}, flash_id_{}, bypass_{};
    Byte scanline_ = 255, hblank_ = 0, scan_enable_ = 0, pending_ = 0, parity_ = 0, jitter_ = 0,
         oam_proc_ = 0;
    Word cpu_count_ = 0;
    std::array<Vrc6Pulse, 2> pulse_{};
    Vrc6Sawtooth saw_{};
};
} // namespace mapperbus::core
