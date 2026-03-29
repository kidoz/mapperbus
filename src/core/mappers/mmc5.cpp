#include "core/mappers/mmc5.hpp"

#include "core/apu/apu.hpp"

namespace mapperbus::core {

// --- MMC5 Pulse ---

void Mmc5Pulse::clock_timer() {
    if (timer == 0) {
        timer = timer_period;
        sequence_pos = (sequence_pos + 1) & 0x07;
    } else {
        --timer;
    }
}

void Mmc5Pulse::clock_envelope() {
    if (envelope_start) {
        envelope_start = false;
        envelope_decay = 15;
        envelope_divider = envelope_period;
    } else if (envelope_divider == 0) {
        envelope_divider = envelope_period;
        if (envelope_decay > 0) {
            --envelope_decay;
        } else if (halt_length) {
            envelope_decay = 15;
        }
    } else {
        --envelope_divider;
    }
}

void Mmc5Pulse::clock_length() {
    if (length_counter > 0 && !halt_length) {
        --length_counter;
    }
}

uint8_t Mmc5Pulse::output() const {
    if (!enabled || length_counter == 0)
        return 0;
    if (timer_period < 8)
        return 0;
    static constexpr std::array<std::array<uint8_t, 8>, 4> kDuty = {{
        {0, 1, 0, 0, 0, 0, 0, 0},
        {0, 1, 1, 0, 0, 0, 0, 0},
        {0, 1, 1, 1, 1, 0, 0, 0},
        {1, 0, 0, 1, 1, 1, 1, 1},
    }};
    if (kDuty[duty & 3][sequence_pos] == 0)
        return 0;
    return constant_volume ? volume : envelope_decay;
}

// --- MMC5 Mapper ---

Mmc5::Mmc5(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mirror_mode_(header.mirror_mode) {
    num_prg_8k_ = static_cast<uint8_t>(prg_rom_.size() / 0x2000);
    reset();
}

void Mmc5::reset() {
    prg_mode_ = 3;
    prg_banks_.fill(0);
    prg_banks_[4] = num_prg_8k_ - 1; // Last bank at $E000
    chr_banks_1k_.fill(0);
    pulse1_ = {};
    pulse2_ = {};
    pcm_output_ = 0;
    audio_cycle_ = 0;
}

Byte Mmc5::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000) {
        return exram_[addr - 0x6000];
    }
    if (addr < 0x8000)
        return 0;

    // Mode 3: four 8KB banks
    uint8_t bank_idx = (addr - 0x8000) / 0x2000;
    uint8_t bank = prg_banks_[bank_idx + 1] % num_prg_8k_;
    uint32_t offset = static_cast<uint32_t>(bank) * 0x2000 + ((addr - 0x8000) % 0x2000);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mmc5::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        exram_[addr - 0x6000] = value;
    }
}

Byte Mmc5::read_chr(Address addr) {
    if (use_chr_ram_)
        return chr_ram_[addr % 0x2000];
    uint8_t bank_idx = (addr / 0x0400) & 0x07;
    uint32_t offset = static_cast<uint32_t>(chr_banks_1k_[bank_idx]) * 0x0400 + (addr % 0x0400);
    if (chr_rom_.empty())
        return 0;
    return chr_rom_[offset % chr_rom_.size()];
}

void Mmc5::write_chr(Address addr, Byte value) {
    if (use_chr_ram_)
        chr_ram_[addr % 0x2000] = value;
}

MirrorMode Mmc5::mirror_mode() const {
    return mirror_mode_;
}

Byte Mmc5::read_expansion(Address addr) {
    // $5015: Audio status
    if (addr == 0x5015) {
        Byte status = 0;
        if (pulse1_.length_counter > 0)
            status |= 0x01;
        if (pulse2_.length_counter > 0)
            status |= 0x02;
        return status;
    }
    return 0;
}

void Mmc5::write_expansion(Address addr, Byte value) {
    switch (addr) {
    // Pulse 1
    case 0x5000:
        pulse1_.duty = (value >> 6) & 0x03;
        pulse1_.halt_length = (value & 0x20) != 0;
        pulse1_.constant_volume = (value & 0x10) != 0;
        pulse1_.volume = value & 0x0F;
        pulse1_.envelope_period = value & 0x0F;
        break;
    case 0x5002:
        pulse1_.timer_period = (pulse1_.timer_period & 0x0700) | value;
        break;
    case 0x5003:
        pulse1_.timer_period =
            (pulse1_.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 0x07) << 8);
        if (pulse1_.enabled)
            pulse1_.length_counter = kLengthTable[(value >> 3) & 0x1F];
        pulse1_.sequence_pos = 0;
        pulse1_.envelope_start = true;
        break;

    // Pulse 2
    case 0x5004:
        pulse2_.duty = (value >> 6) & 0x03;
        pulse2_.halt_length = (value & 0x20) != 0;
        pulse2_.constant_volume = (value & 0x10) != 0;
        pulse2_.volume = value & 0x0F;
        pulse2_.envelope_period = value & 0x0F;
        break;
    case 0x5006:
        pulse2_.timer_period = (pulse2_.timer_period & 0x0700) | value;
        break;
    case 0x5007:
        pulse2_.timer_period =
            (pulse2_.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 0x07) << 8);
        if (pulse2_.enabled)
            pulse2_.length_counter = kLengthTable[(value >> 3) & 0x1F];
        pulse2_.sequence_pos = 0;
        pulse2_.envelope_start = true;
        break;

    // PCM
    case 0x5011:
        pcm_output_ = value;
        break;

    // Channel enable
    case 0x5015:
        pulse1_.enabled = (value & 0x01) != 0;
        pulse2_.enabled = (value & 0x02) != 0;
        if (!pulse1_.enabled)
            pulse1_.length_counter = 0;
        if (!pulse2_.enabled)
            pulse2_.length_counter = 0;
        break;

    // PRG banking
    case 0x5100:
        prg_mode_ = value & 0x03;
        break;
    case 0x5113:
        prg_banks_[0] = value & 0x7F;
        break;
    case 0x5114:
        prg_banks_[1] = value & 0x7F;
        break;
    case 0x5115:
        prg_banks_[2] = value & 0x7F;
        break;
    case 0x5116:
        prg_banks_[3] = value & 0x7F;
        break;
    case 0x5117:
        prg_banks_[4] = value & 0x7F;
        break;

    // CHR banking
    case 0x5120:
    case 0x5121:
    case 0x5122:
    case 0x5123:
    case 0x5124:
    case 0x5125:
    case 0x5126:
    case 0x5127:
        chr_banks_1k_[addr - 0x5120] = value;
        break;

    // Mirroring
    case 0x5105: {
        // Simplified: use bits 0-1 for primary mirroring
        uint8_t m = value & 0x03;
        if (m == 0)
            mirror_mode_ = MirrorMode::SingleLower;
        else if (m == 1)
            mirror_mode_ = MirrorMode::SingleUpper;
        else if (m == 2)
            mirror_mode_ = MirrorMode::Vertical;
        else
            mirror_mode_ = MirrorMode::Horizontal;
        break;
    }

    default:
        break;
    }
}

void Mmc5::clock_audio() {
    pulse1_.clock_timer();
    pulse2_.clock_timer();

    // Frame counter equivalent (~240 Hz quarter, ~120 Hz half)
    ++audio_cycle_;
    if (audio_cycle_ % 7457 == 0) {
        pulse1_.clock_envelope();
        pulse2_.clock_envelope();
    }
    if (audio_cycle_ % 14913 == 0) {
        pulse1_.clock_length();
        pulse2_.clock_length();
    }
}

float Mmc5::audio_output() const {
    float p1 = static_cast<float>(pulse1_.output());
    float p2 = static_cast<float>(pulse2_.output());
    float pcm = static_cast<float>(pcm_output_);
    return (p1 + p2) * 0.00752f + pcm * 0.002f;
}

} // namespace mapperbus::core
