#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class FaridSlrom323 final : public Mapper {
  public:
    FaridSlrom323(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);

    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    void write_prg_cpu(Address addr, Byte value, bool consecutive) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    void write_register(Address addr, Byte value);

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;

    Byte shift_register_ = 0x10U;
    uint8_t write_count_ = 0U;
    Byte control_ = 0x0CU;
    Byte chr_bank0_ = 0U;
    Byte chr_bank1_ = 0U;
    Byte prg_bank_ = 0U;
    Byte outer_bank_ = 0U;
};

} // namespace mapperbus::core
