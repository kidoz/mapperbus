#pragma once

#include <memory>
#include <string>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Cartridge {
  public:
    static Result<Cartridge> from_file(const std::string& path);
    static Result<Cartridge> from_data(std::span<const Byte> rom_data);

    Cartridge(Cartridge&&) = default;
    Cartridge& operator=(Cartridge&&) = default;

    Byte read_prg(Address addr);
    void write_prg(Address addr, Byte value);
    Byte read_chr(Address addr);
    void write_chr(Address addr, Byte value);

    const INesHeader& header() const {
        return header_;
    }
    void reset() {
        mapper_->reset();
    }
    MirrorMode mirror_mode() const {
        return mapper_->mirror_mode();
    }
    bool irq_pending() const {
        return mapper_->irq_pending();
    }
    void acknowledge_irq() {
        mapper_->acknowledge_irq();
    }
    void clock_irq_counter() {
        mapper_->clock_irq_counter();
    }
    bool has_expansion_audio() const {
        return mapper_->has_expansion_audio();
    }
    void clock_audio() {
        mapper_->clock_audio();
    }
    float audio_output() const {
        return mapper_->audio_output();
    }
    Byte read_expansion(Address addr) {
        return mapper_->read_expansion(addr);
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
