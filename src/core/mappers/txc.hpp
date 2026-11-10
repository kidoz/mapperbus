#pragma once

#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {
/// TXC 05-00002-010 / JV001 protection chips and the small mapper-147 latch PCB.
class Txc final : public Mapper {
  public:
    Txc(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static bool supports_mapper(uint16_t id);
    static Result<void> validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    bool maps_expansion(Address addr) const override;
    Byte expansion_read_mask(Address addr) const override;
    Byte read_expansion(Address addr) override;
    void write_expansion(Address addr, Byte value) override;
    void reset() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    static Byte reverse_six(Byte value);
    bool jv001() const;
    Byte data_input(Byte value) const;
    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    const uint16_t id_;
    const bool simple_147_;
    const Byte initial_vertical_;
    Byte input_ = 0U;
    Byte reg_ = 0U;
    Byte output_ = 0U;
    Byte s_ = 0U;
    Byte mode_ = 0U;
    Byte invert_ = 0U;
    Byte chr_latch_ = 0U;
    Byte vertical_;
};
} // namespace mapperbus::core
