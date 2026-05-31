#include "core/mappers/vrc7.hpp"

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
    fm_channels_ = {};
    custom_patch_.fill(0);
    audio_divider_ = 0;
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
            uint8_t ch = audio_register_ - 0x20;
            fm_channels_[ch].frequency =
                (fm_channels_[ch].frequency & 0xFF) | ((value & 0x01) << 8);
            fm_channels_[ch].octave = (value >> 1) & 0x07;
            fm_channels_[ch].key_on = (value & 0x10) != 0;
            fm_channels_[ch].sustain = (value & 0x20) != 0;
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

void Vrc7::clock_audio() {
    // FM synthesis runs at CPU/36 (approximately 49716 Hz).
    ++audio_divider_;
    if (audio_divider_ < 36)
        return;
    audio_divider_ = 0;

    for (auto& ch : fm_channels_) {
        if (!ch.key_on)
            continue;
        const uint8_t* patch = patch_for(ch.instrument);
        const float mod_mult = kMultiplier[patch[0] & 0x0F];
        const float car_mult = kMultiplier[patch[1] & 0x0F];
        const uint32_t base = static_cast<uint32_t>(ch.frequency) << ch.octave;
        // Two operators advance at instrument-dependent multiples of the note.
        ch.mod_phase += static_cast<uint32_t>(static_cast<float>(base) * mod_mult);
        ch.phase += static_cast<uint32_t>(static_cast<float>(base) * car_mult);
    }
}

float Vrc7::audio_output() const {
    constexpr float kTwoPi = 6.2831853f;
    float output = 0.0f;

    for (const auto& ch : fm_channels_) {
        if (!ch.key_on || ch.volume == 0x0F)
            continue;

        const uint8_t* patch = patch_for(ch.instrument);
        const float mod_tl = static_cast<float>(patch[2] & 0x3F); // modulator total level
        const uint8_t feedback = patch[3] & 0x07;                 // modulator feedback

        // Modulator: a sine attenuated by its total level, scaled by feedback
        // depth — this is what gives each instrument its distinct timbre.
        const float mod_phase = static_cast<float>(ch.mod_phase & 0x3FFFF) / 262144.0f;
        const float mod_level = (63.0f - mod_tl) / 63.0f;
        const float fb_depth = static_cast<float>(feedback) / 7.0f;
        const float modulator = std::sin(mod_phase * kTwoPi) * mod_level * fb_depth;

        // Carrier: phase-modulated by the modulator output.
        const float car_phase = static_cast<float>(ch.phase & 0x3FFFF) / 262144.0f;
        const float carrier = std::sin((car_phase + modulator) * kTwoPi);

        const float vol = static_cast<float>(15 - ch.volume) / 15.0f;
        output += carrier * vol;
    }

    return output * 0.02f; // Normalize toward APU level
}

} // namespace mapperbus::core
