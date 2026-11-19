#pragma once

#include <memory>

#include "core/mappers/namco163.hpp"

namespace mapperbus::core {
class Ppu;

/// VirtuaNES EX CHINA_ER_SAN2 uses an N163 board and a separate 16×16 font ROM.
class San2 final : public Namco163 {
  public:
    static Result<std::unique_ptr<Mapper>> create(const INesHeader&,
                                                  std::vector<Byte>,
                                                  std::vector<Byte>,
                                                  std::span<const Byte>);
    San2(const INesHeader&, std::vector<Byte>, std::vector<Byte>, std::span<const Byte>);
    void reset() override;
    void write_prg(Address, Byte) override;
    bool maps_expansion(Address address) const override {
        return address >= 0x4800 && address < 0x6000;
    }
    Byte read_expansion(Address address) override {
        return Namco163::read_expansion(address & 0xF800U);
    }
    void write_expansion(Address address, Byte value) override {
        Namco163::write_expansion(address & 0xF800U, value);
    }
    float audio_output() const override {
        return mute_ ? 0.0F : Namco163::audio_output();
    }
    std::optional<Address> ciram_address(Address) const override;
    std::optional<Byte> read_nametable(Address) const override;
    bool write_nametable(Address, Byte) override {
        return true;
    }
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;
    const FrameBuffer& translated_frame(const Ppu&, const std::array<Byte, kRamSize>&) const;

  private:
    std::vector<Byte> font_;
    std::array<Byte, 12> banks_{};
    Byte protect_ = 0xFF;
    Byte force_rom_ = 0;
    bool mute_ = false;
    mutable FrameBuffer translated_{};
};
} // namespace mapperbus::core
