#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Co Tung CTC-15, with address latch and divider-driven level IRQ.
class Mapper548 final : public Mapper {
  public:
    Mapper548(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;
    bool maps_prg(Address addr) const override;
    void write_expansion(Address addr, Byte value) override;
    void clock_cpu_falling_edge() override;
    bool irq_pending() const override;
    void power_on() override;

  private:
    std::vector<Byte> prg_;
    std::array<Byte, 8192> wram_{};
    std::array<Byte, 8192> chr_ram_{};
    Byte latch_ = 7;
    Byte bank_ = 7;
    Word phase_ = 0;
    bool counting_ = false;
};
} // namespace mapperbus::core
