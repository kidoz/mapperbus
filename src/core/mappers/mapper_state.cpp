// Save-state serialization for all built-in mappers.
//
// Defined in a single translation unit so each mapper's save_state/load_state
// can access its private members without spreading boilerplate (and an extra
// include of state.hpp) across every mapper .cpp file. Only mutable runtime
// state is serialized; immutable ROM (prg_rom_/chr_rom_) and ROM-derived
// constants (num_*_banks_, use_chr_ram_) are reconstructed at load time.

#include <algorithm>
#include <cstddef>

#include "core/mappers/axrom.hpp"
#include "core/mappers/cnrom.hpp"
#include "core/mappers/color_dreams.hpp"
#include "core/mappers/mmc1.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/mmc5.hpp"
#include "core/mappers/namco163.hpp"
#include "core/mappers/nrom.hpp"
#include "core/mappers/sunsoft5b.hpp"
#include "core/mappers/uxrom.hpp"
#include "core/mappers/vrc6.hpp"
#include "core/mappers/vrc7.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {

// --- NROM (0) ---
void Nrom::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
}
void Nrom::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
}

// --- UxROM (2) ---
void Uxrom::save_state(StateWriter& w) const {
    w.write_array(chr_ram_);
    w.write(bank_select_);
}
void Uxrom::load_state(StateReader& r) {
    r.read_array(chr_ram_);
    r.read(bank_select_);
}

// --- CNROM (3) ---
void Cnrom::save_state(StateWriter& w) const {
    w.write(bank_select_);
}
void Cnrom::load_state(StateReader& r) {
    r.read(bank_select_);
}

// --- AxROM (7) ---
void Axrom::save_state(StateWriter& w) const {
    w.write_array(chr_ram_);
    w.write(mirror_mode_);
    w.write(bank_select_);
}
void Axrom::load_state(StateReader& r) {
    r.read_array(chr_ram_);
    r.read(mirror_mode_);
    r.read(bank_select_);
}

// --- Color Dreams (11) ---
void ColorDreams::save_state(StateWriter& w) const {
    w.write_array(chr_ram_);
    w.write(prg_bank_);
    w.write(chr_bank_);
}
void ColorDreams::load_state(StateReader& r) {
    r.read_array(chr_ram_);
    r.read(prg_bank_);
    r.read(chr_bank_);
}

// --- MMC1 (1) ---
void Mmc1::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    w.write(shift_register_);
    w.write(write_count_);
    w.write(control_);
    w.write(chr_bank0_);
    w.write(chr_bank1_);
    w.write(prg_bank_);
    w.write(prg_offset0_);
    w.write(prg_offset1_);
    w.write(chr_offset0_);
    w.write(chr_offset1_);
    w.write(mirror_mode_);
}
void Mmc1::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read(shift_register_);
    r.read(write_count_);
    r.read(control_);
    r.read(chr_bank0_);
    r.read(chr_bank1_);
    r.read(prg_bank_);
    r.read(prg_offset0_);
    r.read(prg_offset1_);
    r.read(chr_offset0_);
    r.read(chr_offset1_);
    r.read(mirror_mode_);
}

// --- MMC3 (4) ---
void Mmc3::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    w.write(bank_select_);
    w.write(prg_mode_);
    w.write(chr_inversion_);
    w.write_array(bank_registers_);
    w.write_array(prg_offsets_);
    w.write_array(chr_offsets_);
    w.write(mirror_mode_);
    w.write(irq_counter_);
    w.write(irq_reload_);
    w.write(irq_reload_flag_);
    w.write(irq_enabled_);
    w.write(irq_pending_);
    w.write(irq_zero_latch_repeats_);
    w.write(prg_ram_enabled_);
    w.write(prg_ram_write_protected_);
}
void Mmc3::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read(bank_select_);
    r.read(prg_mode_);
    r.read(chr_inversion_);
    r.read_array(bank_registers_);
    r.read_array(prg_offsets_);
    r.read_array(chr_offsets_);
    r.read(mirror_mode_);
    r.read(irq_counter_);
    r.read(irq_reload_);
    r.read(irq_reload_flag_);
    r.read(irq_enabled_);
    r.read(irq_pending_);
    r.read(irq_zero_latch_repeats_);
    r.read(prg_ram_enabled_);
    r.read(prg_ram_write_protected_);
}

// --- MMC5 (5) ---
void Mmc5::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    w.write(prg_mode_);
    w.write_array(prg_banks_);
    w.write(chr_mode_);
    w.write_array(chr_banks_1k_);
    w.write(mirror_mode_);
    w.write(pulse1_);
    w.write(pulse2_);
    w.write(pcm_output_);
    w.write(audio_cycle_);
    w.write(irq_target_);
    w.write(irq_scanline_);
    w.write(irq_enabled_);
    w.write(irq_pending_);
    w.write(in_frame_);
}
void Mmc5::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read(prg_mode_);
    r.read_array(prg_banks_);
    r.read(chr_mode_);
    r.read_array(chr_banks_1k_);
    r.read(mirror_mode_);
    r.read(pulse1_);
    r.read(pulse2_);
    r.read(pcm_output_);
    r.read(audio_cycle_);
    r.read(irq_target_);
    r.read(irq_scanline_);
    r.read(irq_enabled_);
    r.read(irq_pending_);
    r.read(in_frame_);
}

// --- VRC6 (24/26) ---
void Vrc6::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    w.write(prg_bank_16k_);
    w.write(prg_bank_8k_);
    w.write_array(chr_banks_);
    w.write(mirror_mode_);
    w.write(irq_latch_);
    w.write(irq_counter_);
    w.write(irq_enabled_);
    w.write(irq_enabled_after_ack_);
    w.write(irq_pending_);
    w.write(irq_cycle_mode_);
    w.write(irq_prescaler_);
    w.write(pulse1_);
    w.write(pulse2_);
    w.write(sawtooth_);
}
void Vrc6::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read(prg_bank_16k_);
    r.read(prg_bank_8k_);
    r.read_array(chr_banks_);
    r.read(mirror_mode_);
    r.read(irq_latch_);
    r.read(irq_counter_);
    r.read(irq_enabled_);
    r.read(irq_enabled_after_ack_);
    r.read(irq_pending_);
    r.read(irq_cycle_mode_);
    r.read(irq_prescaler_);
    r.read(pulse1_);
    r.read(pulse2_);
    r.read(sawtooth_);
}

// --- VRC7 (85) ---
void Vrc7::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    w.write_array(prg_banks_);
    w.write_array(chr_banks_);
    w.write(mirror_mode_);
    w.write(irq_latch_);
    w.write(irq_counter_);
    w.write(irq_enabled_);
    w.write(irq_enabled_after_ack_);
    w.write(irq_pending_);
    w.write(irq_prescaler_);
    w.write(audio_register_);
    w.write_array(fm_channels_);
    w.write_array(custom_patch_);
    w.write(audio_divider_);
}
void Vrc7::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read_array(prg_banks_);
    r.read_array(chr_banks_);
    r.read(mirror_mode_);
    r.read(irq_latch_);
    r.read(irq_counter_);
    r.read(irq_enabled_);
    r.read(irq_enabled_after_ack_);
    r.read(irq_pending_);
    r.read(irq_prescaler_);
    r.read(audio_register_);
    r.read_array(fm_channels_);
    r.read_array(custom_patch_);
    r.read(audio_divider_);
}

// --- Namco 163 (19) ---
void Namco163::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    w.write_array(prg_banks_);
    w.write_array(chr_banks_);
    w.write(mirror_mode_);
    w.write(irq_counter_);
    w.write(irq_enabled_);
    w.write(irq_pending_);
    w.write_array(sound_ram_);
    w.write(sound_addr_);
    w.write(auto_increment_);
    w.write(num_channels_);
    w.write(audio_timer_);
    w.write(current_channel_);
    w.write_array(channels_);
}
void Namco163::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read_array(prg_banks_);
    r.read_array(chr_banks_);
    r.read(mirror_mode_);
    r.read(irq_counter_);
    r.read(irq_enabled_);
    r.read(irq_pending_);
    r.read_array(sound_ram_);
    r.read(sound_addr_);
    r.read(auto_increment_);
    r.read(num_channels_);
    r.read(audio_timer_);
    r.read(current_channel_);
    r.read_array(channels_);
}

// --- Sunsoft 5B / FME-7 (69) ---
void Sunsoft5b::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    w.write(command_);
    w.write_array(chr_banks_);
    w.write_array(prg_banks_);
    w.write(prg_ram_enabled_);
    w.write(mirror_mode_);
    w.write(irq_counter_);
    w.write(irq_enabled_);
    w.write(irq_counter_enabled_);
    w.write(irq_pending_);
    w.write_array(channels_);
    w.write(ay_register_);
    w.write(audio_divider_);
}
void Sunsoft5b::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read(command_);
    r.read_array(chr_banks_);
    r.read_array(prg_banks_);
    r.read(prg_ram_enabled_);
    r.read(mirror_mode_);
    r.read(irq_counter_);
    r.read(irq_enabled_);
    r.read(irq_counter_enabled_);
    r.read(irq_pending_);
    r.read_array(channels_);
    r.read(ay_register_);
    r.read(audio_divider_);
}

// --- Battery-backed PRG-RAM accessors ---
//
// Every board below exposes an 8 KB ($2000-byte) PRG-RAM window at
// $6000-$7FFF. set_battery_ram copies in at most the available bytes so a
// short or oversized .sav file can never overrun the buffer.
namespace {
template <std::size_t N>
void restore_prg_ram(std::array<Byte, N>& ram, std::span<const Byte> data) {
    const std::size_t count = std::min(data.size(), ram.size());
    std::copy_n(data.begin(), count, ram.begin());
}
} // namespace

std::span<const Byte> Nrom::battery_ram() const {
    return prg_ram_;
}
void Nrom::set_battery_ram(std::span<const Byte> data) {
    restore_prg_ram(prg_ram_, data);
}

std::span<const Byte> Mmc1::battery_ram() const {
    return prg_ram_;
}
void Mmc1::set_battery_ram(std::span<const Byte> data) {
    restore_prg_ram(prg_ram_, data);
}

std::span<const Byte> Mmc3::battery_ram() const {
    return prg_ram_;
}
void Mmc3::set_battery_ram(std::span<const Byte> data) {
    restore_prg_ram(prg_ram_, data);
}

std::span<const Byte> Vrc6::battery_ram() const {
    return prg_ram_;
}
void Vrc6::set_battery_ram(std::span<const Byte> data) {
    restore_prg_ram(prg_ram_, data);
}

std::span<const Byte> Vrc7::battery_ram() const {
    return prg_ram_;
}
void Vrc7::set_battery_ram(std::span<const Byte> data) {
    restore_prg_ram(prg_ram_, data);
}

std::span<const Byte> Namco163::battery_ram() const {
    return prg_ram_;
}
void Namco163::set_battery_ram(std::span<const Byte> data) {
    restore_prg_ram(prg_ram_, data);
}

std::span<const Byte> Sunsoft5b::battery_ram() const {
    return prg_ram_;
}
void Sunsoft5b::set_battery_ram(std::span<const Byte> data) {
    restore_prg_ram(prg_ram_, data);
}

} // namespace mapperbus::core
