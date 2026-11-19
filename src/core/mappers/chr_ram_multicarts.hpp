#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// PRG latches with fixed CHR RAM: 09-078, K-3017, Sheng Tian and Croaky Karaoke.
class ChrRamMulticarts final : public Mapper {
  public:
    ChrRamMulticarts(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader&, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    bool maps_expansion(Address addr) const override {
        return id_ == 476 && (addr & 0xF000) == 0x5000;
    }
    Byte expansion_read_mask(Address) const override {
        return 0;
    }
    void write_expansion(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    Result<void> configure_board_jumpers(Byte value) override;
    void power_on() override;
    void reset() override;
    void save_state(StateWriter&) const override;
    void load_state(StateReader&) override;

  private:
    std::size_t prg_page(Address addr) const;
    std::vector<Byte> prg_;
    std::array<Byte, 8192> chr_{};
    std::uint16_t id_;
    Byte submapper_;
    MirrorMode mirror_;
    Byte latch_ = 0;
    Byte pads_ = 0;
    std::uint16_t address_ = 0;
};
} // namespace mapperbus::core
