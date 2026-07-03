#include "core/mappers/vrc7.hpp"

#include <algorithm>

namespace mapperbus::core {

namespace {
// VRC7 internal instrument ROM (patch 0 is the user-defined "custom" patch and
// is filled at runtime). Each row is the 8 register bytes $00-$07.
constexpr std::array<std::array<uint8_t, 8>, 16> kVrc7Patches = {{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, // 0: custom (placeholder)
    {0x03, 0x21, 0x05, 0x06, 0xE8, 0x81, 0x42, 0x27},
    {0x13, 0x41, 0x14, 0x0D, 0xD8, 0xF6, 0x23, 0x12},
    {0x11, 0x11, 0x08, 0x08, 0xFA, 0xB2, 0x20, 0x12},
    {0x31, 0x61, 0x0C, 0x07, 0xA8, 0x64, 0x61, 0x27},
    {0x32, 0x21, 0x1E, 0x06, 0xE1, 0x76, 0x01, 0x28},
    {0x02, 0x01, 0x06, 0x00, 0xA3, 0xE2, 0xF4, 0xF4},
    {0x21, 0x61, 0x1D, 0x07, 0x82, 0x81, 0x11, 0x07},
    {0x23, 0x21, 0x22, 0x17, 0xA2, 0x72, 0x01, 0x17},
    {0x35, 0x11, 0x25, 0x00, 0x40, 0x73, 0x72, 0x01},
    {0xB5, 0x01, 0x0F, 0x0F, 0xA8, 0xA5, 0x51, 0x02},
    {0x17, 0xC1, 0x24, 0x07, 0xF8, 0xF8, 0x22, 0x12},
    {0x71, 0x23, 0x11, 0x06, 0x65, 0x74, 0x18, 0x16},
    {0x01, 0x02, 0xD3, 0x05, 0xC9, 0x95, 0x03, 0x02},
    {0x61, 0x63, 0x0C, 0x00, 0x94, 0xC0, 0x33, 0xF6},
    {0x21, 0x72, 0x0D, 0x00, 0xC1, 0xD5, 0x56, 0x06},
}};

// YM2413 frequency-multiplier factors indexed by the patch's low nibble.
constexpr std::array<float, 16> kMultiplier = {
    0.5f, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15};

constexpr float kTwoPi = 6.28318530f;
// FM synthesis tick rate: NTSC CPU / 36 (~49716 Hz).
constexpr float kAudioTickSeconds = 36.0f / 1789773.0f;
// Operator phase accumulators hold one period in 18 bits.
constexpr uint32_t kPhaseMask = 0x3FFFF;
constexpr float kPhaseScale = 1.0f / 262144.0f;
// Envelope ceiling: a carrier attenuated past this is treated as silent.
constexpr float kMaxAttenuationDb = 48.0f;

// Envelope pacing. Effective rate = 4*R + key-scale offset, so speed doubles
// per +1 of the 4-bit register rate R (the OPL convention). Calibration:
// attack tau(4*R=32, i.e. AR=8) ~ 20 ms (perceived ~100 ms rise) and
// decay(4*R=32, i.e. DR=8) ~ 128 dB/s -> 48 dB in ~375 ms, both in the
// right ballpark for a real OPLL without chasing bit-exact tables.
constexpr float kAttackTauBaseSeconds = 5.0f; // attack time constant at effective rate 0
constexpr float kDecayDbPerSecBase = 0.5f;    // decay/release speed at effective rate 0

// Global LFOs (OPLL-ish): ~3.7 Hz tremolo, ~6.4 Hz vibrato at ~+/-7 cents.
constexpr float kAmLfoHz = 3.7f;
constexpr float kAmDepthDb = 0.8f;
constexpr float kVibLfoHz = 6.4f;
constexpr float kVibDepthCents = 7.0f;

// Carrier phase-modulation index: full-scale modulator output swings the
// carrier phase by ~4 radians (4 / 2pi of a period), a conventional 2-op
// FM depth that gets bright OPLL-like timbres at TL=0 without aliasing mush.
constexpr float kModulationIndex = 0.6366198f;

// KSL attenuation slopes in dB per octave, applied above ~octave 4.
constexpr std::array<float, 4> kKslDbPerOctave = {0.0f, 1.5f, 3.0f, 6.0f};

// One channel at full volume peaks around 0.09, close to an APU pulse
// (~0.15 peak) once the softer FM waveform is taken into account; the old
// 0.02 was ~14 dB too quiet.
constexpr float kMasterGain = 0.09f;

// dB attenuation to linear gain: 10^(-db/20) == 2^(-db * log2(10)/20).
float db_to_gain(float db) {
    return std::exp2(db * -0.16609640f);
}

// Decay/release: attenuation rises linearly in dB (exponential amplitude
// fall), doubling in speed per +4 of effective rate.
float decay_db_per_tick(uint8_t rate, uint8_t rate_offset) {
    const auto effective = static_cast<float>(4 * rate + rate_offset);
    return kDecayDbPerSecBase * std::exp2(effective * 0.25f) * kAudioTickSeconds;
}

// Attack: exponential approach of the attenuation toward 0 dB (fast rise
// that flattens out, like the hardware's non-linear-in-dB attack).
float attack_fraction_per_tick(uint8_t rate, uint8_t rate_offset) {
    const auto effective = static_cast<float>(4 * rate + rate_offset);
    const float tau = kAttackTauBaseSeconds * std::exp2(effective * -0.25f);
    return std::min(1.0f, kAudioTickSeconds / tau);
}
} // namespace

Vrc7::Vrc7(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mirror_mode_(header.mirror_mode) {
    num_prg_8k_ = static_cast<uint8_t>(prg_rom_.size() / 0x2000);
    reset();
}

void Vrc7::reset() {
    prg_banks_.fill(0);
    prg_banks_[2] = num_prg_8k_ - 1;
    chr_banks_.fill(0);
    irq_latch_ = 0;
    irq_counter_ = 0;
    irq_enabled_ = false;
    irq_enabled_after_ack_ = false;
    irq_pending_ = false;
    irq_prescaler_ = 0;
    audio_register_ = 0;
    audio_halted_ = false;
    fm_channels_ = {};
    custom_patch_.fill(0);
    audio_divider_ = 0;
    am_lfo_phase_ = 0.0f;
    fm_lfo_phase_ = 0.0f;
}

const uint8_t* Vrc7::patch_for(uint8_t instrument) const {
    if (instrument == 0) {
        return custom_patch_.data();
    }
    return kVrc7Patches[instrument & 0x0F].data();
}

Byte Vrc7::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000)
        return prg_ram_[addr - 0x6000];
    if (addr < 0x8000)
        return 0;

    if (addr < 0xA000) {
        uint32_t off = static_cast<uint32_t>(prg_banks_[0]) * 0x2000 + (addr - 0x8000);
        return prg_rom_[off % prg_rom_.size()];
    }
    if (addr < 0xC000) {
        uint32_t off = static_cast<uint32_t>(prg_banks_[1]) * 0x2000 + (addr - 0xA000);
        return prg_rom_[off % prg_rom_.size()];
    }
    if (addr < 0xE000) {
        uint32_t off = static_cast<uint32_t>(prg_banks_[2]) * 0x2000 + (addr - 0xC000);
        return prg_rom_[off % prg_rom_.size()];
    }
    uint32_t off = static_cast<uint32_t>(num_prg_8k_ - 1) * 0x2000 + (addr - 0xE000);
    return prg_rom_[off % prg_rom_.size()];
}

void Vrc7::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        prg_ram_[addr - 0x6000] = value;
        return;
    }
    if (addr < 0x8000)
        return;

    switch (addr) {
    case 0x8000:
        prg_banks_[0] = value & 0x3F;
        break;
    case 0x8010:
        prg_banks_[1] = value & 0x3F;
        break;
    case 0x9000:
        prg_banks_[2] = value & 0x3F;
        break;
    case 0x9010:
        audio_register_ = value;
        break;
    case 0x9030:
        // FM register write
        if (audio_register_ < 0x08) {
            // Custom instrument patch registers $00-$07.
            custom_patch_[audio_register_] = value;
        } else if (audio_register_ >= 0x10 && audio_register_ <= 0x15) {
            uint8_t ch = audio_register_ - 0x10;
            fm_channels_[ch].frequency = (fm_channels_[ch].frequency & 0x100) | value;
        } else if (audio_register_ >= 0x20 && audio_register_ <= 0x25) {
            FmChannel& ch = fm_channels_[audio_register_ - 0x20];
            ch.frequency = (ch.frequency & 0xFF) | ((value & 0x01) << 8);
            ch.octave = (value >> 1) & 0x07;
            ch.sustain = (value & 0x20) != 0;
            const bool key = (value & 0x10) != 0;
            if (key && !ch.key_on && !audio_halted_) {
                // Key-on: hardware restarts both operators from phase 0 and
                // puts their envelopes back into the attack stage.
                ch.modulator.phase = 0;
                ch.modulator.stage = EnvStage::Attack;
                ch.carrier.phase = 0;
                ch.carrier.stage = EnvStage::Attack;
                ch.mod_out1 = 0.0f;
                ch.mod_out2 = 0.0f;
            } else if (!key && ch.key_on) {
                if (ch.modulator.stage != EnvStage::Idle)
                    ch.modulator.stage = EnvStage::Release;
                if (ch.carrier.stage != EnvStage::Idle)
                    ch.carrier.stage = EnvStage::Release;
            }
            ch.key_on = key;
        } else if (audio_register_ >= 0x30 && audio_register_ <= 0x35) {
            uint8_t ch = audio_register_ - 0x30;
            fm_channels_[ch].instrument = (value >> 4) & 0x0F;
            fm_channels_[ch].volume = value & 0x0F;
        }
        break;
    case 0xA000:
    case 0xA010:
    case 0xB000:
    case 0xB010:
    case 0xC000:
    case 0xC010:
    case 0xD000:
    case 0xD010:
        chr_banks_[(addr - 0xA000) / 0x0010] = value;
        break;
    case 0xE000: {
        uint8_t m = value & 0x03;
        if (m == 0)
            mirror_mode_ = MirrorMode::Vertical;
        else if (m == 1)
            mirror_mode_ = MirrorMode::Horizontal;
        else if (m == 2)
            mirror_mode_ = MirrorMode::SingleLower;
        else
            mirror_mode_ = MirrorMode::SingleUpper;
        // Bit 6 halts and resets the sound chip; while it stays set the FM
        // section is silent and key-ons are ignored (register latches still
        // accept writes so patch uploads made during reset are not lost).
        const bool halt = (value & 0x40) != 0;
        if (halt && !audio_halted_)
            silence_audio();
        audio_halted_ = halt;
        break;
    }
    case 0xE010:
        irq_latch_ = value;
        break;
    case 0xF000:
        irq_enabled_after_ack_ = (value & 0x01) != 0;
        irq_enabled_ = (value & 0x02) != 0;
        if (irq_enabled_) {
            irq_counter_ = irq_latch_;
            irq_prescaler_ = 341;
        }
        irq_pending_ = false;
        break;
    case 0xF010:
        irq_enabled_ = irq_enabled_after_ack_;
        irq_pending_ = false;
        break;
    default:
        break;
    }
}

void Vrc7::clock_irq_counter() {
    if (!irq_enabled_)
        return;

    irq_prescaler_ -= 3;
    if (irq_prescaler_ <= 0) {
        irq_prescaler_ += 341;
        if (irq_counter_ == 0xFF) {
            irq_counter_ = irq_latch_;
            irq_pending_ = true;
        } else {
            ++irq_counter_;
        }
    }
}

bool Vrc7::maps_prg(Address addr) const {
    return addr >= 0x6000;
}

Byte Vrc7::read_chr(Address addr) {
    if (use_chr_ram_)
        return chr_ram_[addr % 0x2000];
    uint8_t bank_idx = (addr / 0x0400) & 0x07;
    uint32_t offset = static_cast<uint32_t>(chr_banks_[bank_idx]) * 0x0400 + (addr % 0x0400);
    return chr_rom_[offset % chr_rom_.size()];
}

void Vrc7::write_chr(Address addr, Byte value) {
    if (use_chr_ram_)
        chr_ram_[addr % 0x2000] = value;
}

MirrorMode Vrc7::mirror_mode() const {
    return mirror_mode_;
}

void Vrc7::silence_audio() {
    for (FmChannel& ch : fm_channels_) {
        ch.key_on = false;
        ch.modulator = FmOperator{};
        ch.carrier = FmOperator{};
        ch.mod_out1 = 0.0f;
        ch.mod_out2 = 0.0f;
        ch.output = 0.0f;
    }
    am_lfo_phase_ = 0.0f;
    fm_lfo_phase_ = 0.0f;
}

void Vrc7::clock_audio() {
    // FM synthesis runs at CPU/36 (approximately 49716 Hz).
    ++audio_divider_;
    if (audio_divider_ < 36)
        return;
    audio_divider_ = 0;
    if (audio_halted_)
        return;

    am_lfo_phase_ += kAmLfoHz * kAudioTickSeconds;
    am_lfo_phase_ -= std::floor(am_lfo_phase_);
    fm_lfo_phase_ += kVibLfoHz * kAudioTickSeconds;
    fm_lfo_phase_ -= std::floor(fm_lfo_phase_);
    // Tremolo swings between 0 and kAmDepthDb of extra attenuation; vibrato
    // scales the phase increment by up to +/- kVibDepthCents.
    const float am_att_db = 0.5f * kAmDepthDb * (1.0f - std::cos(kTwoPi * am_lfo_phase_));
    const float vib_mult = std::exp2(std::sin(kTwoPi * fm_lfo_phase_) * (kVibDepthCents / 1200.0f));

    for (FmChannel& ch : fm_channels_)
        tick_fm_channel(ch, am_att_db, vib_mult);
}

void Vrc7::tick_fm_channel(FmChannel& ch, float am_att_db, float vib_mult) const {
    const uint8_t* patch = patch_for(ch.instrument);
    // Key-scale-of-rate offset: coarse pitch (block*2 + F-number MSB),
    // divided by 4 unless the operator's KSR bit requests the steep curve.
    const auto pitch_key = static_cast<uint8_t>((ch.octave << 1) | ((ch.frequency >> 8) & 1));

    // ADSR step for one operator. reg_ar_dr is patch byte [4]/[5]
    // (AR << 4 | DR), reg_sl_rr is patch byte [6]/[7] (SL << 4 | RR).
    const auto step_envelope = [&](FmOperator& op,
                                   uint8_t reg_ar_dr,
                                   uint8_t reg_sl_rr,
                                   bool sustained_eg,
                                   uint8_t rate_offset) {
        const uint8_t attack = reg_ar_dr >> 4;
        const uint8_t decay = reg_ar_dr & 0x0F;
        const float sustain_db = 3.0f * static_cast<float>(reg_sl_rr >> 4);
        const uint8_t release = reg_sl_rr & 0x0F;

        switch (op.stage) {
        case EnvStage::Idle:
            break;
        case EnvStage::Attack:
            if (attack == 0)
                break; // rate 0: envelope never opens
            if (attack == 15) {
                op.env_db = 0.0f; // rate 15: instant attack
            } else {
                op.env_db -= op.env_db * attack_fraction_per_tick(attack, rate_offset);
            }
            if (op.env_db < 0.1f) {
                op.env_db = 0.0f;
                op.stage = EnvStage::Decay;
            }
            break;
        case EnvStage::Decay:
            if (decay != 0)
                op.env_db += decay_db_per_tick(decay, rate_offset);
            if (op.env_db >= sustain_db) {
                op.env_db = sustain_db;
                op.stage = EnvStage::Sustain;
            }
            break;
        case EnvStage::Sustain:
            // EG-type 1 holds at the sustain level while keyed; percussive
            // tones (EG-type 0) keep decaying through it at RR.
            if (!sustained_eg && release != 0) {
                op.env_db += decay_db_per_tick(release, rate_offset);
                if (op.env_db >= kMaxAttenuationDb) {
                    op.env_db = kMaxAttenuationDb;
                    op.stage = EnvStage::Idle;
                }
            }
            break;
        case EnvStage::Release: {
            // Key-off decay: the channel sustain bit substitutes rate 5.
            const uint8_t rate = ch.sustain ? 5 : release;
            if (rate != 0) {
                op.env_db += decay_db_per_tick(rate, rate_offset);
                if (op.env_db >= kMaxAttenuationDb) {
                    op.env_db = kMaxAttenuationDb;
                    op.stage = EnvStage::Idle;
                }
            }
            break;
        }
        }
    };

    step_envelope(ch.modulator,
                  patch[4],
                  patch[6],
                  (patch[0] & 0x20) != 0,
                  (patch[0] & 0x10) != 0 ? pitch_key : pitch_key >> 2);
    step_envelope(ch.carrier,
                  patch[5],
                  patch[7],
                  (patch[1] & 0x20) != 0,
                  (patch[1] & 0x10) != 0 ? pitch_key : pitch_key >> 2);

    // A fully attenuated carrier means the channel is inaudible: skip the
    // synthesis and park the feedback history.
    if (ch.carrier.env_db >= kMaxAttenuationDb) {
        ch.mod_out1 = 0.0f;
        ch.mod_out2 = 0.0f;
        ch.output = 0.0f;
        return;
    }

    // Key-scale-level: attenuation growing with pitch above ~octave 4.
    const auto ksl_db = [&](uint8_t ksl) {
        if (ksl == 0)
            return 0.0f;
        const float pitch_octaves =
            static_cast<float>(ch.octave) + static_cast<float>((ch.frequency >> 6) & 0x07) * 0.125f;
        return kKslDbPerOctave[ksl] * std::max(0.0f, pitch_octaves - 4.0f);
    };

    // YM2413 phase increment is F * 2^(octave - 1): the extra >>1 keeps
    // VRC7 in tune with the (half-CPU-rate) APU pulses.
    const uint32_t base = (static_cast<uint32_t>(ch.frequency) << ch.octave) >> 1;

    // --- Modulator ---
    float mod_inc = static_cast<float>(base) * kMultiplier[patch[0] & 0x0F];
    if ((patch[0] & 0x40) != 0)
        mod_inc *= vib_mult;
    ch.modulator.phase += static_cast<uint32_t>(mod_inc);

    float mod_phase = static_cast<float>(ch.modulator.phase & kPhaseMask) * kPhaseScale;
    const uint8_t feedback = patch[3] & 0x07;
    if (feedback != 0) {
        // Self-feedback: the average of the last two modulator outputs bends
        // the modulator's own phase by 2^(FB-8) periods per unit output, so
        // FB=7 reaches ~pi radians (strongly squared-off) and FB=1 is subtle.
        mod_phase +=
            (ch.mod_out1 + ch.mod_out2) * 0.5f * std::exp2(static_cast<float>(feedback) - 8.0f);
    }
    float mod_wave = std::sin(kTwoPi * mod_phase);
    if ((patch[3] & 0x08) != 0)
        mod_wave = std::max(mod_wave, 0.0f); // half-sine rectification
    float mod_att_db = ch.modulator.env_db +
                       0.75f * static_cast<float>(patch[2] & 0x3F) + // total level
                       ksl_db((patch[2] >> 6) & 0x03);
    if ((patch[0] & 0x80) != 0)
        mod_att_db += am_att_db;
    const float mod_out = mod_wave * db_to_gain(mod_att_db);
    ch.mod_out2 = ch.mod_out1;
    ch.mod_out1 = mod_out;

    // --- Carrier ---
    float car_inc = static_cast<float>(base) * kMultiplier[patch[1] & 0x0F];
    if ((patch[1] & 0x40) != 0)
        car_inc *= vib_mult;
    ch.carrier.phase += static_cast<uint32_t>(car_inc);

    const float car_phase = static_cast<float>(ch.carrier.phase & kPhaseMask) * kPhaseScale +
                            mod_out * kModulationIndex;
    float car_wave = std::sin(kTwoPi * car_phase);
    if ((patch[3] & 0x10) != 0)
        car_wave = std::max(car_wave, 0.0f); // half-sine rectification
    float car_att_db = ch.carrier.env_db +
                       3.0f * static_cast<float>(ch.volume) + // 3 dB per volume step
                       ksl_db((patch[3] >> 6) & 0x03);
    if ((patch[1] & 0x80) != 0)
        car_att_db += am_att_db;
    ch.output = car_wave * db_to_gain(car_att_db);
}

float Vrc7::audio_output() const {
    float output = 0.0f;
    for (const FmChannel& ch : fm_channels_)
        output += ch.output;
    return output * kMasterGain;
}

} // namespace mapperbus::core
