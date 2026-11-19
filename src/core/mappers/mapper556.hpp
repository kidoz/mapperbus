#pragma once

#include "core/mappers/mmc3.hpp"

namespace mapperbus::core {
/// JY-215 family: the outer shift-by-write selector connects either an MMC3 or VRC4e.
class Mapper556 final : public Mmc3 {
  public:
    using Mmc3::Mmc3;
    static Result<void> validate_geometry(const INesHeader&, std::size_t, std::size_t);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    MirrorMode mirror_mode() const override;
    void write_expansion(Address addr, Byte value) override;
    void reset() override;
    bool irq_pending() const override;
    void acknowledge_irq() override;
    void clock_cpu() override;
    void clock_irq_counter() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    bool vrc() const {
        return (outer_[2] & 0x80U) != 0U;
    }
    std::array<Byte, 4> outer_{0, 0, 15, 0};
    Byte index_ = 0;
    struct Vrc {
        std::array<Byte, 2> prg{0, 1};
        std::array<Word, 8> chr{};
        Byte mirror = 0, swap = 0, latch = 0, counter = 0, control = 0, pending = 0;
        Word prescaler = 341;
    } chip_;
};
} // namespace mapperbus::core
