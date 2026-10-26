// Save-state serialization for all built-in mappers.
//
// Defined in a single translation unit so each mapper's save_state/load_state
// can access its private members without spreading boilerplate (and an extra
// include of state.hpp) across every mapper .cpp file. Only mutable runtime
// state is serialized; immutable ROM (prg_rom_/chr_rom_) and ROM-derived
// constants (num_*_banks_, use_chr_ram_) are reconstructed at load time.

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "core/mappers/axrom.hpp"
#include "core/mappers/cnrom.hpp"
#include "core/mappers/color_dreams.hpp"
#include "core/mappers/mmc1.hpp"
#include "core/mappers/mmc2.hpp"
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
namespace {

[[nodiscard]] bool valid_mirror_mode(MirrorMode mode) {
    return mode <= MirrorMode::SingleUpper;
}

void validate_mirror_mode(MirrorMode& mode, StateReader& reader) {
    if (!valid_mirror_mode(mode)) {
        mode = MirrorMode::Horizontal;
        reader.fail();
    }
}

[[nodiscard]] bool valid_mmc5_pulse(const Mmc5Pulse& pulse) {
    return pulse.duty <= 0x03 && pulse.sequence_pos < 8 && pulse.timer_period <= 0x07FF &&
           pulse.volume <= 0x0F && pulse.envelope_period <= 0x0F && pulse.envelope_decay <= 0x0F;
}

[[nodiscard]] bool valid_vrc6_frequency_shift(uint8_t shift) {
    return shift == 0 || shift == 4 || shift == 8;
}

} // namespace

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
    validate_mirror_mode(mirror_mode_, r);
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
    validate_mirror_mode(mirror_mode_, r);
    if (shift_register_ > 0x1F || write_count_ > 4 || control_ > 0x1F || chr_bank0_ > 0x1F ||
        chr_bank1_ > 0x1F || prg_bank_ > 0x1F) {
        shift_register_ = 0x10;
        write_count_ = 0;
        control_ = 0x0C;
        chr_bank0_ = 0;
        chr_bank1_ = 0;
        prg_bank_ = 0;
        r.fail();
    }
    update_banks();
}

// --- MMC2 (9) ---
void Mmc2::save_state(StateWriter& w) const {
    w.write_array(prg_ram_);
    w.write_array(chr_ram_);
    w.write(prg_bank_);
    w.write_array(chr_banks_);
    w.write_array(latch_);
    w.write_array(chr_offsets_);
    w.write(mirror_mode_);
}
void Mmc2::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read(prg_bank_);
    r.read_array(chr_banks_);
    r.read_array(latch_);
    r.read_array(chr_offsets_);
    r.read(mirror_mode_);
    validate_mirror_mode(mirror_mode_, r);
    if (prg_bank_ > 0x0F || latch_[0] > 1 || latch_[1] > 1 ||
        std::ranges::any_of(chr_banks_, [](uint8_t bank) { return bank > 0x1F; })) {
        prg_bank_ = 0;
        chr_banks_.fill(0);
        latch_.fill(0);
        r.fail();
    }
    update_chr_banks();
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
    validate_mirror_mode(mirror_mode_, r);
    r.read(irq_counter_);
    r.read(irq_reload_);
    r.read(irq_reload_flag_);
    r.read(irq_enabled_);
    r.read(irq_pending_);
    r.read(irq_zero_latch_repeats_);
    r.read(prg_ram_enabled_);
    r.read(prg_ram_write_protected_);
    if (bank_select_ >= bank_registers_.size()) {
        bank_select_ = 0;
        r.fail();
    }
    update_prg_banks();
    update_chr_banks();
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
    w.write(pcm_read_mode_);
    w.write(pcm_irq_enabled_);
    w.write(pcm_irq_flag_);
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
    // $5100 writes keep only the low two bits. The mode indexes a 4-entry table
    // in read_prg; a raw byte from a blob must not.
    if (prg_mode_ > 3) {
        prg_mode_ = 3;
        r.fail();
    }
    r.read_array(prg_banks_);
    r.read(chr_mode_);
    r.read_array(chr_banks_1k_);
    r.read(mirror_mode_);
    validate_mirror_mode(mirror_mode_, r);
    r.read(pulse1_);
    r.read(pulse2_);
    r.read(pcm_output_);
    r.read(pcm_read_mode_);
    r.read(pcm_irq_enabled_);
    r.read(pcm_irq_flag_);
    r.read(audio_cycle_);
    r.read(irq_target_);
    r.read(irq_scanline_);
    r.read(irq_enabled_);
    r.read(irq_pending_);
    r.read(in_frame_);
    if (chr_mode_ > 3) {
        chr_mode_ = 3;
        r.fail();
    }
    if (!valid_mmc5_pulse(pulse1_)) {
        pulse1_ = {};
        r.fail();
    }
    if (!valid_mmc5_pulse(pulse2_)) {
        pulse2_ = {};
        r.fail();
    }
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
    validate_mirror_mode(mirror_mode_, r);
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
    if (irq_prescaler_ < 0) {
        irq_prescaler_ = 0;
        r.fail();
    }
    const auto valid_pulse = [](const Vrc6Pulse& pulse) {
        return pulse.duty <= 0x07 && pulse.volume <= 0x0F && pulse.step <= 0x0F &&
               pulse.timer_period <= 0x0FFF && valid_vrc6_frequency_shift(pulse.freq_shift);
    };
    if (!valid_pulse(pulse1_)) {
        pulse1_ = {};
        r.fail();
    }
    if (!valid_pulse(pulse2_)) {
        pulse2_ = {};
        r.fail();
    }
    if (sawtooth_.accumulator_rate > 0x3F || sawtooth_.step >= 14 ||
        sawtooth_.timer_period > 0x0FFF || !valid_vrc6_frequency_shift(sawtooth_.freq_shift)) {
        sawtooth_ = {};
        r.fail();
    }
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
    w.write(audio_halted_);
}
void Vrc7::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read_array(prg_banks_);
    r.read_array(chr_banks_);
    r.read(mirror_mode_);
    validate_mirror_mode(mirror_mode_, r);
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
    r.read(audio_halted_);
    if (irq_prescaler_ < 0 || audio_divider_ >= 36) {
        irq_prescaler_ = 0;
        audio_divider_ = 0;
        r.fail();
    }
    for (const auto& channel : fm_channels_) {
        const auto valid_operator = [](const FmOperator& op) {
            return op.stage <= EnvStage::Release && std::isfinite(op.env_db) && op.env_db >= 0.0F;
        };
        if (channel.frequency > 0x01FF || channel.octave > 0x07 || channel.instrument > 0x0F ||
            channel.volume > 0x0F || !valid_operator(channel.modulator) ||
            !valid_operator(channel.carrier) || !std::isfinite(channel.mod_out1) ||
            !std::isfinite(channel.mod_out2) || !std::isfinite(channel.output)) {
            fm_channels_ = {};
            r.fail();
            break;
        }
    }
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
    validate_mirror_mode(mirror_mode_, r);
    r.read(irq_counter_);
    r.read(irq_enabled_);
    r.read(irq_pending_);
    r.read_array(sound_ram_);
    r.read(sound_addr_);
    r.read(auto_increment_);
    r.read(num_channels_);
    // Hardware writes store ((value >> 4) & 7) + 1, so the count is 1..8.
    // Zero reaches `current_channel_ % num_channels_` in clock_audio.
    if (num_channels_ < 1 || num_channels_ > channels_.size()) {
        num_channels_ = 1;
        r.fail();
    }
    r.read(audio_timer_);
    r.read(current_channel_);
    r.read_array(channels_);
    if (sound_addr_ >= sound_ram_.size()) {
        sound_addr_ = 0;
        r.fail();
    }
    if (current_channel_ >= num_channels_) {
        current_channel_ = 0;
        r.fail();
    }
    for (const auto& channel : channels_) {
        if (channel.phase > 0xFFFFFF || channel.frequency > 0x3FFFF || channel.wave_length > 256 ||
            (channel.wave_length != 0 && channel.wave_length % 4 != 0) || channel.volume > 0x0F) {
            channels_ = {};
            r.fail();
            break;
        }
    }
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
    w.write(noise_period_);
    w.write(noise_timer_);
    w.write(noise_lfsr_);
    w.write(env_period_);
    w.write(env_timer_);
    w.write(env_shape_);
    w.write(env_step_);
    w.write(env_level_);
    w.write(env_attack_);
    w.write(env_holding_);
}
void Sunsoft5b::load_state(StateReader& r) {
    r.read_array(prg_ram_);
    r.read_array(chr_ram_);
    r.read(command_);
    r.read_array(chr_banks_);
    r.read_array(prg_banks_);
    r.read(prg_ram_enabled_);
    r.read(mirror_mode_);
    validate_mirror_mode(mirror_mode_, r);
    r.read(irq_counter_);
    r.read(irq_enabled_);
    r.read(irq_counter_enabled_);
    r.read(irq_pending_);
    r.read_array(channels_);
    r.read(ay_register_);
    r.read(audio_divider_);
    r.read(noise_period_);
    r.read(noise_timer_);
    r.read(noise_lfsr_);
    r.read(env_period_);
    r.read(env_timer_);
    r.read(env_shape_);
    r.read(env_step_);
    r.read(env_level_);
    r.read(env_attack_);
    r.read(env_holding_);
    if (command_ > 0x0F || ay_register_ > 0x0F || audio_divider_ >= 16 || noise_period_ > 0x1F ||
        env_shape_ > 0x0F || env_step_ > 31 || env_level_ > 31) {
        command_ = 0;
        ay_register_ = 0;
        audio_divider_ = 0;
        noise_period_ = 0;
        env_shape_ = 0;
        env_step_ = 0;
        env_level_ = 0;
        r.fail();
    }
    for (const auto& channel : channels_) {
        if (channel.period > 0x0FFF || channel.volume > 0x0F) {
            channels_ = {};
            r.fail();
            break;
        }
    }
}

// --- Battery-backed PRG-RAM accessors ---
//
// Every board below exposes a PRG-RAM window at $6000-$7FFF (MMC5 banks its
// 64 KB through the same window). set_battery_ram copies in at most the
// available bytes so a short or oversized .sav file can never overrun the
// buffer.
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

std::span<const Byte> Mmc5::battery_ram() const {
    return prg_ram_;
}
void Mmc5::set_battery_ram(std::span<const Byte> data) {
    restore_prg_ram(prg_ram_, data);
}

std::span<const Byte> Mmc2::battery_ram() const {
    return prg_ram_;
}
void Mmc2::set_battery_ram(std::span<const Byte> data) {
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
