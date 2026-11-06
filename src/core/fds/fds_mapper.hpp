#pragma once

#include <array>

#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Fds;

/// Fixed RAM-adapter mapping. Drive registers/audio remain owned by Fds.
class FdsMapper final : public Mapper {
  public:
    static constexpr std::size_t kBiosBytes = 8192;
    FdsMapper(std::span<const Byte> bios, const Fds& drive);
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void reset() override {}
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    const Fds& drive_;
    std::array<Byte, kBiosBytes> bios_{};
    std::array<Byte, 32768> prg_ram_{};
    std::array<Byte, 8192> chr_ram_{};
};

} // namespace mapperbus::core
