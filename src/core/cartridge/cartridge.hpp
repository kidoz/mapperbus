#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "core/cartridge/ines_header.hpp"
#include "core/cartridge/rom_sha256.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Fds;
class StateWriter;
class StateReader;

/// Largest accepted iNES/NES 2.0 image, including header, trainer, PRG, and CHR.
/// The cap applies before allocation and to both linear and exponent/multiplier sizes.
inline constexpr std::size_t kMaxRomBytes = 16 * 1024 * 1024;
inline constexpr std::size_t kMaxDcsOptionBytes = 128UZ * 1024UZ;

class Cartridge {
  public:
    [[nodiscard]] static Result<Cartridge> from_file(const std::string& path,
                                                     std::span<const Byte> dcs_option = {});
    [[nodiscard]] static Result<Cartridge> from_data(std::span<const Byte> rom_data,
                                                     std::string_view filename = {},
                                                     std::span<const Byte> dcs_option = {});

    [[nodiscard]] static Result<Cartridge> from_fds_bios(std::span<const Byte> bios,
                                                         const Fds& drive);

    Cartridge(Cartridge&&) = default;
    Cartridge& operator=(Cartridge&&) = default;

    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader, std::uint32_t version = 20U);

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
    void write_prg_cpu(Address addr, Byte value, bool consecutive);
    [[nodiscard]] bool maps_prg(Address addr) const;
    [[nodiscard]] Byte prg_read_mask(Address addr) const {
        return mapper_->prg_read_mask(addr);
    }
    [[nodiscard]] Byte read_chr(Address addr);
    void write_chr(Address addr, Byte value);

    [[nodiscard]] const INesHeader& header() const {
        return header_;
    }
    [[nodiscard]] const Sha256Digest& content_hash() const {
        return content_hash_;
    }
    /// Battery saves belong to the base cartridge, independently of its option.
    [[nodiscard]] const Sha256Digest& base_content_hash() const {
        return base_content_hash_;
    }
    void reset() {
        mapper_->reset();
    }
    [[nodiscard]] MirrorMode mirror_mode() const {
        return mapper_->mirror_mode();
    }
    [[nodiscard]] std::optional<Address> nametable_address(Address addr) const {
        return mapper_->nametable_address(addr);
    }
    [[nodiscard]] std::optional<Byte> read_nametable(Address addr) const {
        return mapper_->read_nametable(addr);
    }
    bool write_nametable(Address addr, Byte value) {
        return mapper_->write_nametable(addr, value);
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
    void clock_cpu() {
        mapper_->clock_cpu();
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
    [[nodiscard]] Byte expansion_read_mask(Address addr) const {
        return mapper_->expansion_read_mask(addr);
    }
    [[nodiscard]] bool maps_expansion(Address addr) const {
        return mapper_->maps_expansion(addr);
    }
    void write_expansion(Address addr, Byte value) {
        mapper_->write_expansion(addr, value);
    }

  private:
    Cartridge(INesHeader header, std::unique_ptr<Mapper> mapper);

    Sha256Digest content_hash_{};
    Sha256Digest base_content_hash_{};
    INesHeader header_;
    std::unique_ptr<Mapper> mapper_;
};

} // namespace mapperbus::core
