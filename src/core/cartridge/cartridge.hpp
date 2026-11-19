#pragma once

#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/cartridge/rom_sha256.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

class Fds;
class Ppu;
class StateWriter;
class StateReader;

/// Largest accepted iNES/NES 2.0 image, including header, trainer, PRG, and CHR.
/// The cap applies before allocation and to both linear and exponent/multiplier sizes.
inline constexpr std::size_t kMaxRomBytes =
    128 * 1024 * 1024 + 16 + 512; // ROM payload plus header and trainer.
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

    const FrameBuffer& translated_frame(const Ppu&, const std::array<Byte, kRamSize>&) const;
    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader, std::uint32_t version = 20U);

    /// True for header-marked cartridge NVRAM or self-flashable storage.
    [[nodiscard]] bool has_battery() const {
        return header_.has_battery || mapper_->has_flash_storage();
    }
    /// Cartridge-owned persistent RAM/EEPROM bytes (empty if absent).
    [[nodiscard]] std::span<const Byte> battery_ram() const {
        return mapper_->battery_ram();
    }
    void set_battery_ram(std::span<const Byte> data) {
        mapper_->set_battery_ram(data);
    }

    [[nodiscard]] std::span<const Byte> shared_nvram() const {
        return mapper_->shared_nvram();
    }
    void set_shared_nvram(std::span<const Byte> data) {
        mapper_->set_shared_nvram(data);
    }
    Byte controller_port_bits(int port, Byte bus) const {
        return mapper_->controller_port_bits(port, bus);
    }
    void clock_controller_port(int port) {
        mapper_->clock_controller_port(port);
    }
    VsCabinet* vs_cabinet() {
        return mapper_->vs_cabinet();
    }
    void send_keyboard_scancode(Byte code) {
        mapper_->send_keyboard_scancode(code);
    }
    void set_keyboard_row(Byte row, Byte pressed_mask) {
        mapper_->set_keyboard_row(row, pressed_mask);
    }
    Result<void> scan_barcode(std::string_view digits, uint32_t module_cycles) {
        return mapper_->scan_barcode(digits, module_cycles);
    }

    [[nodiscard]] bool has_flash_storage() const {
        return mapper_->has_flash_storage();
    }
    void observe_cpu_read(Address addr, Byte data) {
        mapper_->observe_cpu_read(addr, data);
    }
    void observe_cpu_write(Address addr, Byte data) {
        mapper_->observe_cpu_write(addr, data);
    }
    Result<void> configure_board_jumpers(Byte value) {
        return mapper_->configure_board_jumpers(value);
    }
    bool uses_render_context() const {
        return mapper_->uses_render_context();
    }
    void on_render_dot(unsigned scanline, unsigned dot, bool rendering) {
        mapper_->on_render_dot(scanline, dot, rendering);
    }
    std::optional<BackgroundFetch> background_fetch(Address nt,
                                                    Address pattern,
                                                    unsigned column,
                                                    unsigned scanline) const {
        return mapper_->background_fetch(nt, pattern, column, scanline);
    }
    std::optional<std::array<Byte, 2>> sprite_pattern(unsigned oam,
                                                      Address pattern,
                                                      bool tall) const {
        return mapper_->sprite_pattern(oam, pattern, tall);
    }
    Result<void> configure_game_doctor_bios(std::span<const Byte> bios);
    Result<void> configure_board_profile(std::string_view profile) {
        return mapper_->configure_board_profile(profile);
    }
    [[nodiscard]] bool unmirrored_nametables() const {
        return mapper_->unmirrored_nametables();
    }
    [[nodiscard]] std::optional<Byte> led_outputs() const {
        return mapper_->led_outputs();
    }
    [[nodiscard]] Byte read_prg(Address addr);
    void write_prg(Address addr, Byte value);
    void write_prg_cpu(Address addr, Byte value, bool consecutive);
    [[nodiscard]] std::optional<Byte> read_ppu_register(Address addr, Byte open_bus) {
        return mapper_->read_ppu_register(addr, open_bus);
    }
    bool write_ppu_register(Address addr, Byte value) {
        return mapper_->write_ppu_register(addr, value);
    }
    [[nodiscard]] bool maps_prg(Address addr) const;
    [[nodiscard]] Byte prg_read_mask(Address addr) const {
        return mapper_->prg_read_mask(addr);
    }
    [[nodiscard]] Byte cpu_open_bus_mask() const {
        return mapper_->cpu_open_bus_mask();
    }
    [[nodiscard]] Byte read_chr(Address addr);
    void write_chr(Address addr, Byte value);
    [[nodiscard]] std::optional<std::array<Byte, 2>> background_pattern(Address tile,
                                                                        Address pattern) const {
        return mapper_->background_pattern(tile, pattern);
    }
    [[nodiscard]] std::optional<Byte> background_attribute_palette(Address tile_address) const {
        return mapper_->background_attribute_palette(tile_address);
    }

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
    void power_on();
    void reset() {
        mapper_->reset();
    }
    [[nodiscard]] MirrorMode mirror_mode() const {
        return mapper_->mirror_mode();
    }
    [[nodiscard]] std::optional<Address> ciram_address(Address addr) const {
        return mapper_->ciram_address(addr);
    }
    [[nodiscard]] std::optional<Address> nametable_address(Address addr) const {
        return mapper_->nametable_address(addr);
    }
    [[nodiscard]] bool maps_nametable(Address addr) const {
        return mapper_->maps_nametable(addr);
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
    void on_ppu_hblank() {
        mapper_->on_ppu_hblank();
    }
    void clock_irq_counter() {
        mapper_->clock_irq_counter();
    }
    void clock_cpu() {
        mapper_->clock_cpu();
    }
    [[nodiscard]] bool observes_ppu_a12() const {
        return mapper_->observes_ppu_a12();
    }
    void observe_ppu_a12(bool high) {
        mapper_->observe_ppu_a12(high);
    }
    [[nodiscard]] bool observes_ppu_address() const {
        return mapper_->observes_ppu_address();
    }
    void observe_ppu_address(Address addr) {
        mapper_->observe_ppu_address(addr);
    }
    [[nodiscard]] bool observes_ppu_reads() const {
        return mapper_->observes_ppu_reads();
    }
    void observe_ppu_read(Address addr) {
        mapper_->observe_ppu_read(addr);
    }
    void clock_cpu_falling_edge() {
        mapper_->clock_cpu_falling_edge();
    }
    void on_ppu_frame_start() {
        mapper_->on_ppu_frame_start();
    }
    [[nodiscard]] bool has_expansion_audio() const {
        return mapper_->has_expansion_audio();
    }
    void set_region(Region region) {
        mapper_->set_region(region);
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
    [[nodiscard]] static Result<Cartridge> from_data_impl(std::span<const Byte> rom_data,
                                                          std::string_view filename,
                                                          std::span<const Byte> dcs_option,
                                                          std::vector<Byte>* owned_data);
    Cartridge(INesHeader header, std::unique_ptr<Mapper> mapper);
    void seed_trainer();

    static constexpr std::size_t kTrainerSize = 512U;
    std::array<Byte, kTrainerSize> trainer_{};
    Sha256Digest content_hash_{};
    Sha256Digest base_content_hash_{};
    INesHeader header_;
    std::unique_ptr<Mapper> mapper_;
};

} // namespace mapperbus::core
