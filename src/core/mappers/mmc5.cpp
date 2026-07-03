#include "core/mappers/mmc5.hpp"

#include "core/apu/apu.hpp"

namespace mapperbus::core {

// --- MMC5 Pulse ---

void Mmc5Pulse::clock_timer() {
    if (timer == 0) {
        // Same half-CPU-rate clocking as the APU pulses: 2t+1 CPU cycles
        // per sequencer step (f = CPU / (16 * (t + 1))).
        timer = static_cast<uint16_t>(timer_period * 2 + 1);
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
    prg_banks_[4] = static_cast<uint8_t>((num_prg_8k_ - 1) & 0x7F); // $5117: last bank
    chr_mode_ = 3;
    chr_banks_1k_.fill(0);
    pulse1_ = {};
    pulse2_ = {};
    pcm_output_ = 0;
    audio_cycle_ = 0;
    irq_target_ = 0;
    irq_scanline_ = 0;
    irq_enabled_ = false;
    irq_pending_ = false;
    in_frame_ = false;
}

namespace {
// Per PRG mode, for each of the four 8 KB CPU slots ($8000/$A000/$C000/$E000):
// which $511x register controls it, the region size in 8 KB units, and the
// slot's sub-index within that region.
constexpr uint8_t kPrgReg[4][4] = {{4, 4, 4, 4}, {2, 2, 4, 4}, {2, 2, 3, 4}, {1, 2, 3, 4}};
constexpr uint8_t kPrgSize[4][4] = {{4, 4, 4, 4}, {2, 2, 2, 2}, {2, 2, 1, 1}, {1, 1, 1, 1}};
constexpr uint8_t kPrgSub[4][4] = {{0, 1, 2, 3}, {0, 1, 0, 1}, {0, 1, 0, 0}, {0, 0, 0, 0}};
} // namespace

Byte Mmc5::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000) {
        const uint32_t bank = (prg_banks_[0] & 0x07) % num_prg_ram_8k_;
        return prg_ram_[bank * 0x2000 + (addr - 0x6000)];
    }
    if (addr < 0x8000)
        return 0;

    const uint8_t slot = static_cast<uint8_t>((addr - 0x8000) / 0x2000);
    const uint32_t in_bank = (addr - 0x8000) & 0x1FFF;
    const uint8_t reg_index = kPrgReg[prg_mode_][slot];
    const uint8_t size8k = kPrgSize[prg_mode_][slot];
    const uint8_t sub = kPrgSub[prg_mode_][slot];
    const uint8_t reg = prg_banks_[reg_index];

    // $5117 is always ROM; $5114-$5116 use bit7 to select ROM vs PRG-RAM.
    const bool is_rom = (reg_index == 4) || (reg & 0x80) != 0;
    if (is_rom) {
        uint32_t bank8k = (reg & 0x7F) & ~static_cast<uint32_t>(size8k - 1);
        bank8k += sub;
        const uint32_t offset = bank8k * 0x2000 + in_bank;
        return prg_rom_.empty() ? 0 : prg_rom_[offset % prg_rom_.size()];
    }
    const uint32_t bank = (reg & 0x07) % num_prg_ram_8k_;
    return prg_ram_[bank * 0x2000 + in_bank];
}

void Mmc5::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        const uint32_t bank = (prg_banks_[0] & 0x07) % num_prg_ram_8k_;
        prg_ram_[bank * 0x2000 + (addr - 0x6000)] = value;
        return;
    }
    if (addr < 0x8000)
        return;

    // Writes to a ROM-mapped window are ignored; a RAM-mapped window is writable.
    const uint8_t slot = static_cast<uint8_t>((addr - 0x8000) / 0x2000);
    const uint8_t reg_index = kPrgReg[prg_mode_][slot];
    if (reg_index == 4) {
        return; // $5117 region is always ROM
    }
    const uint8_t reg = prg_banks_[reg_index];
    if ((reg & 0x80) != 0) {
        return; // ROM-mapped
    }
    const uint32_t in_bank = (addr - 0x8000) & 0x1FFF;
    const uint32_t bank = (reg & 0x07) % num_prg_ram_8k_;
    prg_ram_[bank * 0x2000 + in_bank] = value;
}

void Mmc5::clock_irq_counter() {
    // Driven once per rendered scanline (PPU dot 260). The in-frame flag is
    // armed by on_ppu_frame_start at the top of each rendered frame.
    in_frame_ = true;
    ++irq_scanline_;
    if (irq_scanline_ == irq_target_ && irq_target_ != 0) {
        if (irq_enabled_) {
            irq_pending_ = true;
        }
    }
}

void Mmc5::on_ppu_frame_start() {
    irq_scanline_ = 0;
    in_frame_ = true;
}

bool Mmc5::maps_prg(Address addr) const {
    return addr >= 0x6000;
}

bool Mmc5::maps_expansion(Address addr) const {
    return addr >= 0x5000 && addr <= 0x5FFF;
}

Byte Mmc5::read_chr(Address addr) {
    if (use_chr_ram_)
        return chr_ram_[addr % 0x2000];
    if (chr_rom_.empty())
        return 0;

    addr &= 0x1FFF;
    uint32_t offset = 0;
    switch (chr_mode_) {
    case 0: { // single 8 KB bank ($5127)
        offset = static_cast<uint32_t>(chr_banks_1k_[7]) * 0x2000 + addr;
        break;
    }
    case 1: { // two 4 KB banks ($5123 / $5127)
        const uint16_t bank = (addr < 0x1000) ? chr_banks_1k_[3] : chr_banks_1k_[7];
        offset = static_cast<uint32_t>(bank) * 0x1000 + (addr & 0x0FFF);
        break;
    }
    case 2: { // four 2 KB banks ($5121/$5123/$5125/$5127)
        static constexpr std::array<uint8_t, 4> kRegs = {1, 3, 5, 7};
        const uint16_t bank = chr_banks_1k_[kRegs[addr / 0x0800]];
        offset = static_cast<uint32_t>(bank) * 0x0800 + (addr & 0x07FF);
        break;
    }
    default: { // mode 3: eight 1 KB banks
        const uint8_t idx = static_cast<uint8_t>((addr / 0x0400) & 0x07);
        offset = static_cast<uint32_t>(chr_banks_1k_[idx]) * 0x0400 + (addr & 0x03FF);
        break;
    }
    }
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
    // $5204: Scanline IRQ status (bit7 in-frame, bit6 pending); read clears it.
    if (addr == 0x5204) {
        Byte status = 0;
        status |= in_frame_ ? 0x40 : 0x00;
        status |= irq_pending_ ? 0x80 : 0x00;
        irq_pending_ = false;
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

    // PRG/CHR banking modes
    case 0x5100:
        prg_mode_ = value & 0x03;
        break;
    case 0x5101:
        chr_mode_ = value & 0x03;
        break;

    // PRG bank registers ($5114-$5117 keep bit7 = ROM/RAM select)
    case 0x5113:
        prg_banks_[0] = value;
        break;
    case 0x5114:
        prg_banks_[1] = value;
        break;
    case 0x5115:
        prg_banks_[2] = value;
        break;
    case 0x5116:
        prg_banks_[3] = value;
        break;
    case 0x5117:
        prg_banks_[4] = value;
        break;

    // Scanline IRQ
    case 0x5203:
        irq_target_ = value;
        break;
    case 0x5204:
        irq_enabled_ = (value & 0x80) != 0;
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
