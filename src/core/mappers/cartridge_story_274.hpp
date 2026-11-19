#pragma once

#include <array>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class CartridgeStory274 final : public Mapper {
  public:
    CartridgeStory274(const INesHeader& header,
                      std::vector<Byte> prg_rom,
                      const std::vector<Byte>& chr_rom);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    Byte prg_read_mask(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::array<Byte, 8192> chr_ram_{};
    std::size_t extra_size_ = 0U;
    Byte inner_bank_ = 0U;
    Byte outer_bank_ = 0U;
};

} // namespace mapperbus::core
