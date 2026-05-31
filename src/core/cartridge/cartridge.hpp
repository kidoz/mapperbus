#pragma once

#include <memory>
#include <string>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class StateWriter;
class StateReader;

class Cartridge {
  public:
    [[nodiscard]] static Result<Cartridge> from_file(const std::string& path);
    [[nodiscard]] static Result<Cartridge> from_data(std::span<const Byte> rom_data);

    Cartridge(Cartridge&&) = default;
    Cartridge& operator=(Cartridge&&) = default;

    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);

    /// True when the iNES/NES 2.0 header marks the board as battery-backed.
    [[nodiscard]] bool has_battery() const {
        return header_.has_battery;
    }
    /// Battery-backed PRG-RAM contents (empty if the mapper has none).
    [[nodiscard]] std::span<const Byte> battery_ram() const {
        return mapper_->battery_ram();
    }
    void set_battery_ram(std::span<const Byte> data) {
        mapper_->set_battery_ram(data);
    }

    [[nodiscard]] Byte read_prg(Address addr);
    void write_prg(Address addr, Byte value);
    [[nodiscard]] bool maps_prg(Address addr) const;
    [[nodiscard]] Byte read_chr(Address addr);
    void write_chr(Address addr, Byte value);

    [[nodiscard]] const INesHeader& header() const {
        return header_;
    }
    void reset() {
        mapper_->reset();
    }
    [[nodiscard]] MirrorMode mirror_mode() const {
        return mapper_->mirror_mode();
    }
    [[nodiscard]] bool irq_pending() const {
        return mapper_->irq_pending();
    }
    void acknowledge_irq() {
        mapper_->acknowledge_irq();
    }
    void clock_irq_counter() {
        mapper_->clock_irq_counter();
    }
    void on_ppu_frame_start() {
        mapper_->on_ppu_frame_start();
    }
    [[nodiscard]] bool has_expansion_audio() const {
        return mapper_->has_expansion_audio();
    }
    void clock_audio() {
        mapper_->clock_audio();
    }
    [[nodiscard]] float audio_output() const {
        return mapper_->audio_output();
    }
    [[nodiscard]] Byte read_expansion(Address addr) {
        return mapper_->read_expansion(addr);
    }
    [[nodiscard]] bool maps_expansion(Address addr) const {
        return mapper_->maps_expansion(addr);
    }
    void write_expansion(Address addr, Byte value) {
        mapper_->write_expansion(addr, value);
    }

  private:
    Cartridge(INesHeader header, std::unique_ptr<Mapper> mapper);

    INesHeader header_;
    std::unique_ptr<Mapper> mapper_;
};

} // namespace mapperbus::core
