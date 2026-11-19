#pragma once

#include <array>
#include <memory>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

/// Ball Games and Supervision multicarts with a ROM window at $6000 and CHR RAM.
class Multicart51_53 final : public Mapper {
  public:
    Multicart51_53(const INesHeader& header, std::vector<Byte> prg, std::vector<Byte> chr);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg,
                                          std::size_t chr);
    static Result<std::unique_ptr<Mapper>> create_with_menu(const INesHeader& header,
                                                            std::vector<Byte> prg,
                                                            std::vector<Byte> chr,
                                                            std::span<const Byte> menu);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override;
    void power_on() override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::size_t ball_games_offset(Address addr) const;
    std::size_t supervision_offset(Address addr) const;
    std::vector<Byte> prg_;
    std::array<Byte, 8192> chr_ram_{};
    uint16_t mapper_;
    Byte submapper_;
    Byte mode_ = 0;
    Byte bank_ = 0;
};
} // namespace mapperbus::core
