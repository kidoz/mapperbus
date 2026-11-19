#pragma once
#include "core/mappers/mmc3.hpp"

namespace mapperbus::core {
class CoolXLite final : public Mmc3 {
  public:
    CoolXLite(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override {
        return maps_prg(addr) ? 0xFF : 0;
    }
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override {
        return addr >= 0x4FF0 && addr <= 0x4FF5;
    }
    Byte expansion_read_mask(Address) const override {
        return 0;
    }
    void write_expansion(Address addr, Byte value) override;
    std::span<const Byte> battery_ram() const override {
        return {};
    }
    void set_battery_ram(std::span<const Byte>) override {}
    void power_on() override;
    void reset() override;
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    std::size_t pattern_offset(Address addr) const;
    std::vector<Byte> work_ram_;
    std::vector<Byte> pattern_ram_;
    Byte sub_, mirror_reg_ = 0;
    std::array<Byte, 6> outer_{};
    uint32_t prg_bytes_, chr_bytes_;
};
} // namespace mapperbus::core
