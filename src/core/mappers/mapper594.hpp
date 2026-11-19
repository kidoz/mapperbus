#pragma once
#include "core/mappers/mmc3.hpp"
namespace mapperbus::core {
/// Rinco Talkman RS-7600 with an Am7202 FIFO feeding the MSM6585 ADPCM decoder.
class Mapper594 final : public Mmc3 {
  public:
    Mapper594(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t, std::size_t);
    bool maps_prg(Address addr) const override;
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    bool has_expansion_audio() const override;
    void set_region(Region region) override;
    void clock_audio() override;
    float audio_output() const override;
    void reset() override;
    void save_state(StateWriter& w) const override;
    void load_state(StateReader& r) override;

  private:
    void decode_nibble(Byte nibble);
    std::array<Byte, 1024> fifo_{};
    Byte extra_ = 0, mode_ = 0, rate_ = 0, step_index_ = 0;
    Word head_ = 0, count_ = 0;
    std::int16_t sample_ = 0;
    bool low_nibble_ = false;
    std::uint32_t phase_ = 0, cpu_hz_ = 1789773;
};
} // namespace mapperbus::core
