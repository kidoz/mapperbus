#pragma once

#include <array>
#include <memory>
#include <optional>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/input/vs_cabinet.hpp"
#include "core/mappers/konami_vrc.hpp"

namespace mapperbus::core {

/// Vs. Unisystem ROM sockets, shared RAM and cabinet I/O, with optional VRC1 banking.
class VsSystem final : public Mapper {
  public:
    VsSystem(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom);
    static Result<std::unique_ptr<Mapper>> create_vrc1(const INesHeader& header,
                                                       std::vector<Byte> prg,
                                                       std::vector<Byte> chr,
                                                       std::span<const Byte> misc);
    static Result<void> validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size);

    VsCabinet* vs_cabinet() override {
        return cabinet_ ? &*cabinet_ : nullptr;
    }
    void clock_cpu() override {
        if (cabinet_)
            cabinet_->clock_cpu();
    }
    void observe_cpu_read(Address addr, Byte data) override {
        if (cabinet_)
            cabinet_->observe_counter(addr, data);
    }
    Result<void> configure_board_jumpers(Byte value) override {
        if (!cabinet_)
            return Mapper::configure_board_jumpers(value);
        cabinet_->set_dips(value);
        return {};
    }
    Byte read_prg(Address addr) override;
    void write_prg(Address addr, Byte value) override;
    bool maps_prg(Address addr) const override;
    Byte prg_read_mask(Address addr) const override;
    void observe_cpu_write(Address addr, Byte value) override;
    Byte read_chr(Address addr) override;
    void write_chr(Address addr, Byte value) override;
    MirrorMode mirror_mode() const override;
    void power_on() override;
    void reset() override;
    std::span<const Byte> battery_ram() const override;
    void set_battery_ram(std::span<const Byte> data) override;
    void save_state(StateWriter& writer) const override;
    void load_state(StateReader& reader) override;

  private:
    std::optional<std::size_t> prg_offset(Address addr) const;
    uint16_t state_tag() const;

    std::vector<Byte> prg_rom_;
    std::vector<Byte> chr_rom_;
    std::array<Byte, 2048> shared_ram_{};
    MirrorMode mirror_mode_;
    std::optional<VsCabinet> cabinet_;
    std::unique_ptr<KonamiVrc> vrc_;
    bool battery_;
    Byte bank_select_ = 0U;
};

} // namespace mapperbus::core
