#include "core/mappers/mapper515.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kInternalPrgSize = 0x100000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x5150U;
constexpr std::uint32_t kPhaseMask = 0x3FFFFU;
constexpr float kPhaseScale = 1.0F / 262144.0F;
constexpr float kTwoPi = 6.28318530F;
constexpr float kAudioTickSeconds = 36.0F / 1789773.0F;
constexpr float kMaxAttenuationDb = 48.0F;
constexpr float kAttackTauBaseSeconds = 5.0F;
constexpr float kDecayDbPerSecondBase = 0.5F;
constexpr float kAmplitudeLfoHz = 3.7F;
constexpr float kAmplitudeDepthDb = 0.8F;
constexpr float kVibratoLfoHz = 6.4F;
constexpr float kVibratoDepthCents = 7.0F;
constexpr float kModulationIndex = 0.6366198F;
constexpr float kMasterGain = 0.09F;

constexpr std::array<std::array<Byte, 8>, 16> kPatches{{
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
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
constexpr std::array<float, 16> kMultipliers{0.5F,
                                             1.0F,
                                             2.0F,
                                             3.0F,
                                             4.0F,
                                             5.0F,
                                             6.0F,
                                             7.0F,
                                             8.0F,
                                             9.0F,
                                             10.0F,
                                             10.0F,
                                             12.0F,
                                             12.0F,
                                             15.0F,
                                             15.0F};
constexpr std::array<float, 4> kKeyScaleAttenuation{0.0F, 1.5F, 3.0F, 6.0F};

float decibels_to_gain(float decibels) {
    return std::exp2(decibels * -0.16609640F);
}

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 515 oldid 22771.
// Reset is unspecified: internal bank zero and cleared peripheral latches are
// the deterministic policy. Soft reset retains CHR RAM and the live ADC input.
Mapper515::Mapper515(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper515::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 515U) {
        return std::unexpected("Mapper 515 requires mapper ID 515");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 515 requires NES 2.0 submapper 0");
    }
    if (prg_size < kInternalPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 515 requires 1-2 MiB of 16 KiB-banked PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 515 requires only 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 515 requires ordinary H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 515 does not provide trainer RAM");
    }
    return {};
}

void Mapper515::set_microphone_sample(Byte sample) {
    microphone_sample_ = sample;
}

std::size_t Mapper515::internal_rom_offset() const {
    return prg_rom_.size() >= kInternalPrgSize ? prg_rom_.size() - kInternalPrgSize : 0U;
}

std::size_t Mapper515::external_rom_size() const {
    return prg_rom_.size() > kInternalPrgSize ? prg_rom_.size() - kInternalPrgSize : 0U;
}

Byte Mapper515::read_selected_lower(Address addr) const {
    const bool internal = (prg_bank_ & 0x80U) != 0U;
    const auto base = internal ? internal_rom_offset() : 0UZ;
    const auto size = internal ? std::min(kInternalPrgSize, prg_rom_.size()) : external_rom_size();
    if (size < kPrgBankSize) {
        return 0U;
    }
    const auto bank = static_cast<std::size_t>(prg_bank_ & 0x3FU) % (size / kPrgBankSize);
    const auto offset = base + bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return offset < prg_rom_.size() ? prg_rom_[offset] : 0U;
}

Byte Mapper515::read_prg(Address addr) {
    if ((addr & 0xE003U) == 0x6000U) {
        return audio_.registers[0x0FU];
    }
    if ((addr & 0xE003U) == 0x6003U) {
        return microphone_selected_ && microphone_bits_remaining_ != 0U &&
                       (microphone_shift_ & 0x80U) != 0U
                   ? 0x80U
                   : 0U;
    }
    if (addr < 0x8000U) {
        return 0U;
    }
    if (addr < 0xC000U) {
        return read_selected_lower(addr);
    }
    if (prg_rom_.size() < kPrgBankSize) {
        return 0U;
    }
    return prg_rom_[prg_rom_.size() - kPrgBankSize + (addr & (kPrgBankSize - 1U))];
}

void Mapper515::write_prg(Address addr, Byte value) {
    if ((addr & 0xE003U) == 0x6000U) {
        audio_index_ = value & 0x3FU;
        return;
    }
    if ((addr & 0xE003U) == 0x6001U) {
        write_audio_register(value);
        return;
    }
    if (addr == 0xC002U) {
        const bool selected = (value & 0x80U) == 0U;
        if (selected && !microphone_selected_) {
            microphone_shift_ = microphone_sample_;
            microphone_bits_remaining_ = 8U;
        } else if (!selected) {
            microphone_shift_ = 0U;
            microphone_bits_remaining_ = 0U;
        }
        microphone_selected_ = selected;
        return;
    }
    if (addr == 0xC003U) {
        const bool clock = (value & 0x80U) != 0U;
        if (clock && !microphone_clock_ && microphone_selected_ &&
            microphone_bits_remaining_ != 0U) {
            microphone_shift_ = static_cast<Byte>(microphone_shift_ << 1U);
            --microphone_bits_remaining_;
        }
        microphone_clock_ = clock;
        return;
    }
    if ((addr & 0xE00FU) == 0x8000U) {
        prg_bank_ = value;
    }
}

bool Mapper515::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr & 0xE003U) == 0x6000U || (addr & 0xE003U) == 0x6003U;
}

Byte Mapper515::prg_read_mask(Address addr) const {
    if ((addr & 0xE003U) == 0x6003U) {
        return 0x80U;
    }
    if ((addr & 0xE003U) == 0x6000U) {
        return 0xFFU;
    }
    if (addr >= 0x8000U && addr < 0xC000U && (prg_bank_ & 0x80U) == 0U &&
        external_rom_size() == 0U) {
        return 0U;
    }
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper515::read_chr(Address addr) {
    return addr < kChrRamSize ? chr_ram_[addr] : 0U;
}

void Mapper515::write_chr(Address addr, Byte value) {
    if (addr < kChrRamSize) {
        chr_ram_[addr] = value;
    }
}

MirrorMode Mapper515::mirror_mode() const {
    return initial_mirror_;
}

void Mapper515::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper515::reset() {
    prg_bank_ = 0x80U;
    audio_index_ = 0U;
    audio_ = AudioState{};
    microphone_shift_ = 0U;
    microphone_bits_remaining_ = 0U;
    microphone_selected_ = false;
    microphone_clock_ = false;
}

bool Mapper515::has_expansion_audio() const {
    return true;
}

void Mapper515::write_audio_register(Byte value) {
    audio_.registers[audio_index_] = value;
    if (audio_index_ < 0x20U || audio_index_ > 0x28U) {
        return;
    }

    auto& voice = audio_.voices[audio_index_ - 0x20U];
    const bool key_on = (value & 0x10U) != 0U;
    if (key_on && !voice.key_on) {
        voice.modulator.phase = 0U;
        voice.carrier.phase = 0U;
        voice.modulator.stage = EnvelopeStage::Attack;
        voice.carrier.stage = EnvelopeStage::Attack;
        voice.modulator_previous = 0.0F;
        voice.modulator_current = 0.0F;
        voice.output = 0.0F;
    } else if (!key_on && voice.key_on) {
        if (voice.modulator.stage != EnvelopeStage::Idle) {
            voice.modulator.stage = EnvelopeStage::Release;
        }
        if (voice.carrier.stage != EnvelopeStage::Idle) {
            voice.carrier.stage = EnvelopeStage::Release;
        }
    }
    voice.key_on = key_on;
}

void Mapper515::step_envelope(FmOperator& fm_operator,
                              Byte attack_decay,
                              Byte sustain_release,
                              bool sustained,
                              Byte rate_offset) const {
    const Byte attack = attack_decay >> 4U;
    const Byte decay = attack_decay & 0x0FU;
    const float sustain_db = 3.0F * static_cast<float>(sustain_release >> 4U);
    const Byte release = sustain_release & 0x0FU;
    const auto decay_per_tick = [rate_offset](Byte rate) {
        const auto effective = static_cast<float>(4U * rate + rate_offset);
        return kDecayDbPerSecondBase * std::exp2(effective * 0.25F) * kAudioTickSeconds;
    };

    switch (fm_operator.stage) {
    case EnvelopeStage::Idle:
        break;
    case EnvelopeStage::Attack:
        if (attack == 15U) {
            fm_operator.envelope_db = 0.0F;
        } else if (attack != 0U) {
            const auto effective = static_cast<float>(4U * attack + rate_offset);
            const float tau = kAttackTauBaseSeconds * std::exp2(effective * -0.25F);
            const float fraction = std::min(1.0F, kAudioTickSeconds / tau);
            fm_operator.envelope_db -= fm_operator.envelope_db * fraction;
        }
        if (attack != 0U && fm_operator.envelope_db < 0.1F) {
            fm_operator.envelope_db = 0.0F;
            fm_operator.stage = EnvelopeStage::Decay;
        }
        break;
    case EnvelopeStage::Decay:
        if (decay != 0U) {
            fm_operator.envelope_db += decay_per_tick(decay);
        }
        if (fm_operator.envelope_db >= sustain_db) {
            fm_operator.envelope_db = sustain_db;
            fm_operator.stage = EnvelopeStage::Sustain;
        }
        break;
    case EnvelopeStage::Sustain:
        if (!sustained && release != 0U) {
            fm_operator.envelope_db += decay_per_tick(release);
            if (fm_operator.envelope_db >= kMaxAttenuationDb) {
                fm_operator.envelope_db = kMaxAttenuationDb;
                fm_operator.stage = EnvelopeStage::Idle;
            }
        }
        break;
    case EnvelopeStage::Release:
        if (release != 0U) {
            fm_operator.envelope_db += decay_per_tick(release);
            if (fm_operator.envelope_db >= kMaxAttenuationDb) {
                fm_operator.envelope_db = kMaxAttenuationDb;
                fm_operator.stage = EnvelopeStage::Idle;
            }
        }
        break;
    }
}

void Mapper515::tick_voice(unsigned index, float amplitude_attenuation, float vibrato_multiplier) {
    auto& voice = audio_.voices[index];
    const Byte instrument = audio_.registers[0x30U + index] >> 4U;
    const Byte* patch = instrument == 0U ? audio_.registers.data() : kPatches[instrument].data();
    const auto frequency = static_cast<std::uint16_t>(
        audio_.registers[0x10U + index] | ((audio_.registers[0x20U + index] & 1U) << 8U));
    const Byte octave = (audio_.registers[0x20U + index] >> 1U) & 7U;
    const auto pitch_key = static_cast<Byte>((octave << 1U) | ((frequency >> 8U) & 1U));

    step_envelope(voice.modulator,
                  patch[4],
                  patch[6],
                  (patch[0] & 0x20U) != 0U,
                  (patch[0] & 0x10U) != 0U ? pitch_key : pitch_key >> 2U);
    step_envelope(voice.carrier,
                  patch[5],
                  patch[7],
                  (patch[1] & 0x20U) != 0U,
                  (patch[1] & 0x10U) != 0U ? pitch_key : pitch_key >> 2U);

    if (voice.carrier.envelope_db >= kMaxAttenuationDb) {
        voice.modulator_previous = 0.0F;
        voice.modulator_current = 0.0F;
        voice.output = 0.0F;
        return;
    }

    const auto key_scale = [&](Byte level) {
        if (level == 0U) {
            return 0.0F;
        }
        const float pitch_octave =
            static_cast<float>(octave) + static_cast<float>((frequency >> 6U) & 7U) * 0.125F;
        return kKeyScaleAttenuation[level] * std::max(0.0F, pitch_octave - 4.0F);
    };
    const auto base = (static_cast<std::uint32_t>(frequency) << octave) >> 1U;
    auto modulator_increment =
        static_cast<std::uint32_t>(static_cast<float>(base) * kMultipliers[patch[0] & 0x0FU]);
    if ((patch[0] & 0x40U) != 0U) {
        modulator_increment = static_cast<std::uint32_t>(static_cast<float>(modulator_increment) *
                                                         vibrato_multiplier);
    }
    voice.modulator.phase = (voice.modulator.phase + modulator_increment) & kPhaseMask;

    float modulator_phase = static_cast<float>(voice.modulator.phase) * kPhaseScale;
    const Byte feedback = patch[3] & 7U;
    if (feedback != 0U) {
        modulator_phase += (voice.modulator_current + voice.modulator_previous) * 0.5F *
                           std::exp2(static_cast<float>(feedback) - 8.0F);
    }
    float modulator_wave = std::sin(kTwoPi * modulator_phase);
    if ((patch[3] & 0x08U) != 0U) {
        modulator_wave = std::max(modulator_wave, 0.0F);
    }
    float modulator_attenuation = voice.modulator.envelope_db +
                                  0.75F * static_cast<float>(patch[2] & 0x3FU) +
                                  key_scale(patch[2] >> 6U);
    if ((patch[0] & 0x80U) != 0U) {
        modulator_attenuation += amplitude_attenuation;
    }
    const float modulator_output = modulator_wave * decibels_to_gain(modulator_attenuation);
    voice.modulator_previous = voice.modulator_current;
    voice.modulator_current = modulator_output;

    auto carrier_increment =
        static_cast<std::uint32_t>(static_cast<float>(base) * kMultipliers[patch[1] & 0x0FU]);
    if ((patch[1] & 0x40U) != 0U) {
        carrier_increment =
            static_cast<std::uint32_t>(static_cast<float>(carrier_increment) * vibrato_multiplier);
    }
    voice.carrier.phase = (voice.carrier.phase + carrier_increment) & kPhaseMask;
    const float carrier_phase =
        static_cast<float>(voice.carrier.phase) * kPhaseScale + modulator_output * kModulationIndex;
    float carrier_wave = std::sin(kTwoPi * carrier_phase);
    if ((patch[3] & 0x10U) != 0U) {
        carrier_wave = std::max(carrier_wave, 0.0F);
    }
    float carrier_attenuation = voice.carrier.envelope_db +
                                3.0F * static_cast<float>(audio_.registers[0x30U + index] & 0x0FU) +
                                key_scale(patch[3] >> 6U);
    if ((patch[1] & 0x80U) != 0U) {
        carrier_attenuation += amplitude_attenuation;
    }
    voice.output = carrier_wave * decibels_to_gain(carrier_attenuation);
}

void Mapper515::clock_audio() {
    ++audio_.divider;
    if (audio_.divider < 36U) {
        return;
    }
    audio_.divider = 0U;
    audio_.amplitude_lfo_phase += kAmplitudeLfoHz * kAudioTickSeconds;
    audio_.amplitude_lfo_phase -= std::floor(audio_.amplitude_lfo_phase);
    audio_.vibrato_lfo_phase += kVibratoLfoHz * kAudioTickSeconds;
    audio_.vibrato_lfo_phase -= std::floor(audio_.vibrato_lfo_phase);
    const float amplitude_attenuation =
        0.5F * kAmplitudeDepthDb * (1.0F - std::cos(kTwoPi * audio_.amplitude_lfo_phase));
    const float vibrato_multiplier =
        std::exp2(std::sin(kTwoPi * audio_.vibrato_lfo_phase) * (kVibratoDepthCents / 1200.0F));
    for (unsigned index = 0U; index < audio_.voices.size(); ++index) {
        tick_voice(index, amplitude_attenuation, vibrato_multiplier);
    }
}

float Mapper515::audio_output() const {
    float output = 0.0F;
    for (const auto& voice : audio_.voices) {
        output += voice.output;
    }
    return output * kMasterGain;
}

std::uint16_t Mapper515::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper515::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(prg_bank_);
    writer.write(audio_index_);
    writer.write_array(audio_.registers);
    writer.write_array(audio_.voices);
    writer.write(audio_.divider);
    writer.write(audio_.amplitude_lfo_phase);
    writer.write(audio_.vibrato_lfo_phase);
    writer.write_array(chr_ram_);
    writer.write(microphone_shift_);
    writer.write(microphone_bits_remaining_);
    writer.write<Byte>((microphone_selected_ ? 1U : 0U) | (microphone_clock_ ? 2U : 0U));
}

void Mapper515::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto prg_bank = reader.read<Byte>();
    const auto audio_index = reader.read<Byte>();
    AudioState audio;
    reader.read_array(audio.registers);
    for (auto& voice : audio.voices)
        reader.read_record(voice, {offsetof(FmVoice, key_on)});
    reader.read(audio.divider);
    reader.read(audio.amplitude_lfo_phase);
    reader.read(audio.vibrato_lfo_phase);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);
    const auto microphone_shift = reader.read<Byte>();
    const auto microphone_bits = reader.read<Byte>();
    const auto microphone_flags = reader.read<Byte>();

    const auto valid_operator = [](const FmOperator& fm_operator) {
        return fm_operator.phase <= kPhaseMask && fm_operator.stage <= EnvelopeStage::Release &&
               std::isfinite(fm_operator.envelope_db) && fm_operator.envelope_db >= 0.0F &&
               fm_operator.envelope_db <= kMaxAttenuationDb;
    };
    bool valid_voices = true;
    for (unsigned index = 0U; index < audio.voices.size(); ++index) {
        const auto& voice = audio.voices[index];
        valid_voices = valid_voices && valid_operator(voice.modulator) &&
                       valid_operator(voice.carrier) && std::isfinite(voice.modulator_previous) &&
                       std::isfinite(voice.modulator_current) && std::isfinite(voice.output) &&
                       voice.key_on == ((audio.registers[0x20U + index] & 0x10U) != 0U);
    }
    const bool valid_lfo = std::isfinite(audio.amplitude_lfo_phase) &&
                           audio.amplitude_lfo_phase >= 0.0F && audio.amplitude_lfo_phase < 1.0F &&
                           std::isfinite(audio.vibrato_lfo_phase) &&
                           audio.vibrato_lfo_phase >= 0.0F && audio.vibrato_lfo_phase < 1.0F;
    const bool valid_microphone =
        microphone_bits <= 8U && microphone_flags <= 3U &&
        ((microphone_flags & 1U) != 0U || (microphone_shift == 0U && microphone_bits == 0U));
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U ||
        audio_index >= audio.registers.size() || audio.divider >= 36U || !valid_voices ||
        !valid_lfo || !valid_microphone) {
        reader.fail();
        return;
    }

    prg_bank_ = prg_bank;
    audio_index_ = audio_index;
    audio_ = audio;
    chr_ram_ = chr_ram;
    microphone_shift_ = microphone_shift;
    microphone_bits_remaining_ = microphone_bits;
    microphone_selected_ = (microphone_flags & 1U) != 0U;
    microphone_clock_ = (microphone_flags & 2U) != 0U;
}

} // namespace mapperbus::core
