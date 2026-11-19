#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {
/// Double Dragon II bootleg, with independently wired CIRAM pages and an M2 timer.
class Mapper127 final : public Mapper {
  public:
    Mapper127(const INesHeader&, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    std::optional<Address> nametable_address(Address addr) const override;
    bool irq_pending() const override;
    void clock_cpu() override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& w) const override;
    void load_state(StateReader& r) override;

  private:
    std::vector<Byte> prg_, chr_;
    std::array<Byte, 4> prg_banks_{};
    std::array<Byte, 8> chr_banks_{};
    std::array<Byte, 4> nametables_{};
    Byte counter_ = 0;
    bool enabled_ = false;
    bool pending_ = false;
};
} // namespace mapperbus::core
