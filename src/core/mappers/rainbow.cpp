#include "core/mappers/rainbow.hpp"

#include <algorithm>
#include <bit>

#include "core/apu/expansion_gains.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
std::vector<Byte> flash_image(std::vector<Byte> prg, std::vector<Byte> chr) {
    prg.insert(prg.end(), chr.begin(), chr.end());
    return prg;
}
bool between(unsigned n, unsigned first, unsigned last) {
    return first <= last ? n >= first && n <= last : n >= first || n <= last;
}
constexpr unsigned page_size = 4096;
} // namespace
Result<void> Rainbow::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 682 || !h.is_nes2 || h.submapper || h.has_trainer || h.misc_rom_count ||
        prg < 32768 || prg > 8388608 || !std::has_single_bit(prg) ||
        (chr && (chr < 8192 || chr > 8388608 || !std::has_single_bit(chr))) ||
        h.prg_ram_size + h.prg_nvram_size > 262144 || (h.prg_ram_size && h.prg_nvram_size) ||
        h.has_battery != (h.prg_nvram_size != 0) || h.chr_ram_size > 262144 || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected(
            "Rainbow requires power-of-two ROMs up to 8 MiB each and up to 256 KiB RAM per chip");
    return {};
}
Rainbow::Rainbow(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_size_(prg.size()), chr_size_(chr.size()), nvram_size_(h.prg_nvram_size),
      original_(flash_image(std::move(prg), std::move(chr))), storage_(original_),
      dirty_(original_.size() / page_size), work_(h.prg_ram_size), chr_ram_(h.chr_ram_size) {
    storage_.resize(original_.size() + nvram_size_);
    reset();
}
Rainbow::Location Rainbow::cpu_location(Address a) const {
    unsigned size, reg;
    if (a < 0x8000) {
        size = reg_[0] & 0x80U ? 4096U : 8192U;
        reg = 6U + (a - 0x6000U) / size;
    } else {
        unsigned mode = reg_[0] & 7U;
        size = mode == 0                                 ? 32768U
               : mode == 1 || (mode == 2 && a < 0xC000U) ? 16384U
               : mode <= 3                               ? 8192U
                                                         : 4096U;
        reg = 8U + (((a - 0x8000U) & ~(size - 1U)) >> 12U);
    }
    const unsigned high = reg_[reg];
    Chip chip = !(high & 0x80U)              ? Chip::PrgFlash
                : a < 0x8000 && (high & 64U) ? Chip::Fpga
                                             : Chip::WorkRam;
    return {chip, (((high & 7U) << 8U) | reg_[reg + 16]) * size + (a & (size - 1U))};
}
Byte Rainbow::prg_read_mask(Address a) const {
    if (a < 0x6000)
        return 0;
    return cpu_location(a).chip == Chip::WorkRam && !nvram_size_ && work_.empty() ? 0 : 255;
}
Byte Rainbow::read_prg(Address a) {
    if (a < 0x6000)
        return 0;
    if ((a & 0xFFFEU) == 0xFFFA && (reg_[0x6B] & 1U))
        return reg_[0x6D - (a & 1U)];
    if ((a & 0xFFFEU) == 0xFFFE && (reg_[0x6B] & 2U))
        return reg_[0x6F - (a & 1U)];
    return read(cpu_location(a));
}
void Rainbow::write_prg(Address a, Byte v) {
    if (a >= 0x6000)
        write(cpu_location(a), v);
}
Rainbow::Location Rainbow::pattern_location(unsigned off) const {
    const unsigned source = reg_[0x20] >> 6U;
    return {source == 0   ? Chip::ChrFlash
            : source == 1 ? Chip::ChrRam
                          : Chip::Fpga,
            source >= 2 ? off & 4095U : off};
}
Rainbow::Location Rainbow::chr_location(Address a) const {
    if (reg_[0x20] >= 128)
        return pattern_location(a);
    const unsigned size = 8192U >> std::min(4U, reg_[0x20] & 7U);
    const unsigned slot = (a & 8191U) / size;
    const unsigned bank = ((reg_[0x30 + slot] & 63U) << 8U) | reg_[0x40 + slot];
    return pattern_location(bank * size + (a & (size - 1U)));
}
Byte Rainbow::read_chr(Address a) {
    return read(chr_location(a));
}
void Rainbow::write_chr(Address a, Byte v) {
    write(chr_location(a), v);
}
std::optional<Address> Rainbow::ciram_address(Address a) const {
    if (a < 0x2000)
        return (reg_[0x20] >> 6U) == 3 ? std::optional<Address>(a & 2047U) : std::nullopt;
    const unsigned nt = (a >> 10U) & 3U;
    if (a >= 0x3F00 || (reg_[0x2A + nt] & 0xC0U))
        return {};
    return (reg_[0x26 + nt] & 1U) * 1024U + (a & 1023U);
}
Rainbow::Location Rainbow::nt_location(Address a) const {
    const unsigned slot = (a >> 10U) & 3U, bank = reg_[0x26 + slot],
                   source = reg_[0x2A + slot] >> 6U;
    return {source == 1   ? Chip::ChrRam
            : source == 2 ? Chip::Fpga
                          : Chip::ChrFlash,
            (source == 2 ? bank & 3U : bank) * 1024U + (a & 1023U)};
}
std::optional<Byte> Rainbow::read_nametable(Address a) const {
    if (ciram_address(a))
        return {};
    return read(nt_location(a));
}
bool Rainbow::write_nametable(Address a, Byte v) {
    if (ciram_address(a))
        return false;
    write(nt_location(a), v);
    return true;
}
std::optional<BackgroundFetch> Rainbow::background_fetch(Address nt,
                                                         Address pattern,
                                                         unsigned x,
                                                         unsigned y) const {
    const bool window = (reg_[0x20] & 16U) && between(x, reg_[0x70] & 31U, reg_[0x71] & 31U) &&
                        between(y, reg_[0x72], reg_[0x73]);
    const unsigned slot = (nt >> 10U) & 3U;
    const Byte control = reg_[window ? 0x2F : 0x2A + slot];
    if (!window && !(control & 0x23U))
        return {};
    unsigned pos = nt & 1023U;
    Byte tile = static_cast<Byte>(pattern >> 4U), row = pattern & 7U;
    std::optional<Byte> palette;
    if (window) {
        const unsigned col = (x + reg_[0x74]) & 31U,
                       line = (y + std::min<unsigned>(reg_[0x75], 239)) % 240U;
        pos = (line >> 3U) * 32U + col;
        row = line & 7U;
        unsigned base = (reg_[0x2E] & 3U) * 1024U;
        tile = fpga_[base + pos];
        unsigned attr = fpga_[base + 960U + ((line >> 5U) * 8U) + (col >> 2U)];
        palette = (attr >> (((line >> 4U) & 1U) * 4U + (col & 2U))) & 3U;
    }
    const Byte flags = fpga_[((control >> 2U) & 3U) * 1024U + pos];
    if (control & 1U)
        palette = flags >> 6U;
    if (control & 32U) {
        tile = reg_[0x24];
        palette = reg_[0x25] & 3U;
    }
    BackgroundFetch result{
        static_cast<Address>((pattern & 0x1000U) + tile * 16U + row), palette, {}};
    if ((control & 2U) && (reg_[0x20] >> 6U) < 2) {
        const unsigned off =
            ((reg_[0x21] & 31U) << 18U) | ((flags & 63U) << 12U) | (result.pattern & 4095U);
        result.planes = std::array{read(pattern_location(off)), read(pattern_location(off + 8))};
    }
    return result;
}
std::optional<std::array<Byte, 2>> Rainbow::sprite_pattern(unsigned idx,
                                                           Address pattern,
                                                           bool tall) const {
    if (!(reg_[0x20] & 32U) || idx >= 64 || (reg_[0x20] >> 6U) >= 2)
        return {};
    const unsigned size = tall ? 8192 : 4096;
    const unsigned bank = ((reg_[0x140] & (tall ? 3U : 7U)) << 8U) | reg_[0x100 + idx];
    const auto offset = bank * size + (pattern & (size - 1U));
    return std::array{read(pattern_location(offset)), read(pattern_location(offset + 8U))};
}
Byte Rainbow::read(Location loc) const {
    if (loc.chip == Chip::Fpga)
        return fpga_[loc.offset & 8191U];
    if (loc.chip == Chip::ChrRam)
        return chr_ram_.empty() ? 0 : chr_ram_[loc.offset % chr_ram_.size()];
    if (loc.chip == Chip::WorkRam)
        return nvram_size_     ? storage_[original_.size() + loc.offset % nvram_size_]
               : work_.empty() ? 0
                               : work_[loc.offset % work_.size()];
    const bool chr = loc.chip == Chip::ChrFlash;
    const auto size = chr ? chr_size_ : prg_size_;
    if (!size)
        return 0;
    if (flash_id_[chr]) {
        switch (loc.offset & 511U) {
        case 0:
            return 1;
        case 2:
            return size == 1048576 ? 0x5B : size == 2097152 ? 0x49 : size >= 4194304 ? 0x7E : 0xFF;
        case 0x1C:
            return size == 4194304 ? 0x0A : size == 8388608 ? 0x10 : 0xFF;
        case 0x1E:
            return size >= 4194304 ? 0 : 0xFF;
        default:
            return 0xFF;
        }
    }
    return storage_[(chr ? prg_size_ : 0) + loc.offset % size];
}
void Rainbow::write(Location loc, Byte value) {
    switch (loc.chip) {
    case Chip::Fpga:
        fpga_[loc.offset & 8191U] = value;
        break;
    case Chip::WorkRam:
        if (nvram_size_)
            storage_[original_.size() + loc.offset % nvram_size_] = value;
        else if (!work_.empty())
            work_[loc.offset % work_.size()] = value;
        break;
    case Chip::ChrRam:
        if (!chr_ram_.empty())
            chr_ram_[loc.offset % chr_ram_.size()] = value;
        break;
    case Chip::PrgFlash:
    case Chip::ChrFlash:
        write_flash(loc.chip == Chip::ChrFlash, loc.offset, value);
        break;
    }
}
void Rainbow::write_flash(bool chr, unsigned offset, Byte value) {
    const unsigned size = chr ? chr_size_ : prg_size_, base = chr ? prg_size_ : 0;
    if (!size)
        return;
    offset %= size;
    auto &phase = flash_phase_[chr], &id = flash_id_[chr], &bypass = bypass_[chr];
    const auto cmd = offset & 4095U;
    if (phase == 3) {
        storage_[base + offset] &= value;
        dirty_[(base + offset) / page_size] = 1;
        phase = 0;
        return;
    }
    if (bypass) {
        if (phase == 7) {
            if (!value)
                bypass = 0;
            phase = 0;
        } else if (value == 0xA0)
            phase = 3;
        else if (value == 0x90)
            phase = 7;
        return;
    }
    if (value == 0xF0) {
        id = phase = 0;
        return;
    }
    switch (phase) {
    case 0:
        phase = (cmd == 0xAAA && value == 0xAA) ? 1 : 0;
        break;
    case 1:
        phase = (cmd == 0x555 && value == 0x55) ? 2 : 0;
        break;
    case 2:
        phase = 0;
        if (cmd == 0xAAA) {
            if (value == 0x90)
                id = 1;
            else if (value == 0x20)
                bypass = 1;
            else if (value == 0xA0)
                phase = 3;
            else if (value == 0x80)
                phase = 4;
        }
        break;
    case 4:
        phase = (cmd == 0xAAA && value == 0xAA) ? 5 : 0;
        break;
    case 5:
        phase = (cmd == 0x555 && value == 0x55) ? 6 : 0;
        break;
    case 6: {
        phase = 0;
        unsigned start = 0, length = size;
        if (value == 0x30) {
            length = std::min(65536U, size);
            if (size >= 65536 && size < 8388608 && offset >= size - 65536) {
                if (size >= 4194304)
                    length = 8192;
                else {
                    const unsigned in_sector = offset & 65535U;
                    length = in_sector < 32768 ? 32768 : in_sector < 49152 ? 8192 : 16384;
                }
            }
            start = offset & ~(length - 1U);
        } else if (value != 0x10 || cmd != 0xAAA)
            return;
        std::fill_n(storage_.begin() + base + start, length, 0xFF);
        std::fill_n(dirty_.begin() + (base + start) / page_size, length / page_size, 1);
        break;
    }
    default:
        phase = 0;
        break;
    }
}
void Rainbow::set_battery_ram(std::span<const Byte> data) {
    if (data.size() != storage_.size())
        return;
    std::ranges::copy(data, storage_.begin());
    for (unsigned i = 0; i < dirty_.size(); ++i)
        dirty_[i] = !std::equal(original_.begin() + i * page_size,
                                original_.begin() + (i + 1) * page_size,
                                storage_.begin() + i * page_size);
}
bool Rainbow::maps_expansion(Address a) const {
    if (a >= 0x4800 && a < 0x6000)
        return true;
    if (a >= 0x4280 && a < 0x4800)
        return a == 0x4280 || a == 0x4282 || oam_proc_ != 0;
    switch (a) {
    case 0x4011:
        return (reg_[0xA9] & 4U) != 0;
    case 0x4100:
    case 0x4120:
    case 0x4150:
    case 0x4151:
    case 0x4154:
    case 0x4157:
    case 0x415F:
    case 0x4160:
    case 0x4161:
    case 0x4190:
    case 0x4191:
    case 0x4192:
        return true;
    default:
        return false;
    }
}
Byte Rainbow::expansion_read_mask(Address a) const {
    return maps_expansion(a) ? 255 : 0;
}
unsigned Rainbow::fpga_ptr() const {
    return ((reg_[0x5C] & 31U) << 8U) | reg_[0x5D];
}
void Rainbow::step_fpga_ptr() {
    const unsigned addr = (fpga_ptr() + reg_[0x5E]) & 8191U;
    reg_[0x5C] = addr >> 8U;
    reg_[0x5D] = static_cast<Byte>(addr);
}
Byte Rainbow::read_expansion(Address a) {
    if (a >= 0x5000 && a < 0x6000)
        return fpga_[(reg_[0x15] & 1U) * 4096U + (a & 4095U)];
    if (a >= 0x4800 && a < 0x5000)
        return fpga_[6144U + (a & 2047U)];
    if (a >= 0x4280 && a < 0x4800) {
        if (!oam_proc_) {
            if (a == 0x4280)
                oam_proc_ = 1;
            else if (a == 0x4282)
                oam_proc_ = 2;
            else
                return 0;
        }
        // Generated application code is LDA #imm; STA abs for each byte, then RTS.
        const bool extended = oam_proc_ == 2;
        unsigned start = extended ? 0x4282 : 0x4280;
        if (a < start)
            return 0;
        unsigned displacement = a - start, index = displacement / 5U;
        unsigned count = ((reg_[0x143] & 63U) + 1U) * (extended ? 1U : 4U);
        if (index >= count)
            return 0x60;
        const unsigned page = reg_[extended ? 0x142 : 0x141] & 7U;
        const unsigned source = 6144U + page * 256U + index * (extended ? 4U : 1U);
        const unsigned target = extended ? 0x4200U + index : 0x2004U;
        const std::array<Byte, 5> code{
            0xA9, fpga_[source], 0x8D, static_cast<Byte>(target), static_cast<Byte>(target >> 8U)};
        return code[displacement % 5U];
    }
    switch (a) {
    case 0x4011:
        return mixed_sample() << 1U;
    case 0x4100:
        return reg_[0] & 0x87U;
    case 0x4120:
        return reg_[0x20] & 0xF7U;
    case 0x4150:
        return scanline_;
    case 0x4151:
        pending_ &= ~2U;
        return (scanline_ != 255 ? 64U : 0U) | (hblank_ ? 128U : 0U);
    case 0x4154:
        return jitter_;
    case 0x4157:
        return parity_ << 7U;
    case 0x415F: {
        Byte value = fpga_[fpga_ptr()];
        step_fpga_ptr();
        return value;
    }
    case 0x4160:
        return 0x21; // emulator platform, revision 1
    case 0x4161:
        return ((pending_ & 1U) && (reg_[0x5A] & 1U) ? 64U : 0U) |
               ((pending_ & 2U) && scan_enable_ ? 128U : 0U);
    case 0x4190:
        return reg_[0x90] & 3U;
    case 0x4191:
        return 0; // No ESP module mounted.
    case 0x4192:
        return 0x80;
    default:
        return 0;
    }
}
void Rainbow::observe_cpu_read(Address a, Byte) {
    if (a == 0x4011 && (reg_[0x5A] & 4U))
        ack_cpu_irq();
    if (oam_proc_) {
        const unsigned start = oam_proc_ == 2 ? 0x4282U : 0x4280U;
        const unsigned count = ((reg_[0x143] & 63U) + 1U) * (oam_proc_ == 2 ? 1U : 4U);
        if (a == start + count * 5U)
            oam_proc_ = 0;
    }
}
void Rainbow::write_expansion(Address a, Byte v) {
    if (a >= 0x5000 && a < 0x6000) {
        fpga_[(reg_[0x15] & 1U) * 4096U + (a & 4095U)] = v;
        return;
    }
    if (a >= 0x4800 && a < 0x5000) {
        fpga_[6144U + (a & 2047U)] = v;
        return;
    }
    if (a < 0x4100 || a > 0x4243)
        return;
    const unsigned reg = a - 0x4100U;
    if (reg == 0x5F) {
        fpga_[fpga_ptr()] = v;
        step_fpga_ptr();
        return;
    }
    if (reg == 0x5B) {
        ack_cpu_irq();
        return;
    }
    if (reg == 0x53)
        v = std::clamp<Byte>(v, 1, 170);
    if (reg == 0x51)
        scan_enable_ = 1;
    else if (reg == 0x52) {
        scan_enable_ = 0;
        pending_ &= ~2U;
    } else if (reg == 0x57)
        parity_ = 0;
    else if (reg == 0x5A) {
        v &= 7U;
        pending_ &= ~1U;
        if (v & 1U)
            cpu_count_ = (reg_[0x58] << 8U) | reg_[0x59];
    }
    reg_[reg] = v;
    if (reg >= 0xA0 && reg <= 0xA8)
        update_voice((reg - 0xA0U) / 3U);
}
void Rainbow::ack_cpu_irq() {
    pending_ &= ~1U;
    reg_[0x5A] = (reg_[0x5A] & 6U) | ((reg_[0x5A] >> 1U) & 1U);
}
void Rainbow::clock_cpu_falling_edge() {
    parity_ ^= 1U;
    if (pending_ & 2U)
        ++jitter_;
    else
        jitter_ = 0;
    if ((reg_[0x5A] & 1U) && cpu_count_ && !--cpu_count_) {
        pending_ |= 1U;
        cpu_count_ = (reg_[0x58] << 8U) | reg_[0x59];
    }
}
bool Rainbow::irq_pending() const {
    return ((pending_ & 1U) && (reg_[0x5A] & 1U)) || ((pending_ & 2U) && scan_enable_);
}
void Rainbow::on_render_dot(unsigned line, unsigned dot, bool rendering) {
    if (!rendering || line > 241) {
        scanline_ = 255;
        hblank_ = 0;
        pending_ &= ~2U;
        return;
    }
    if (scanline_ == 255)
        scanline_ = 0;
    else if (dot == 1 && line)
        ++scanline_;
    hblank_ = dot >= 257U;
    if (scanline_ == reg_[0x50] && dot == 2U * reg_[0x53] - 1U)
        pending_ |= 2U;
}
void Rainbow::update_voice(unsigned i) {
    unsigned base = 0xA0U + i * 3U;
    unsigned period = reg_[base + 1] | ((reg_[base + 2] & 15U) << 8U);
    const bool enabled = (reg_[base + 2] & 128U) != 0;
    if (i < 2) {
        auto& ch = pulse_[i];
        ch.duty = (reg_[base] >> 4U) & 7U;
        ch.volume = reg_[base] & 15U;
        ch.mode = (reg_[base] & 0x80U) != 0;
        ch.timer_period = period;
        ch.enabled = enabled;
        if (!enabled) {
            ch.step = 0;
            ch.timer = period;
        }
    } else {
        saw_.accumulator_rate = reg_[base] & 63U;
        saw_.timer_period = period;
        saw_.enabled = enabled;
        if (!enabled) {
            saw_.step = saw_.accumulator = 0;
            saw_.timer = period;
        }
    }
}
Byte Rainbow::mixed_sample() const {
    return pulse_[0].output() + pulse_[1].output() + saw_.output();
}
void Rainbow::clock_audio() {
    for (auto& p : pulse_)
        p.clock();
    saw_.clock();
}
float Rainbow::audio_output() const {
    return (reg_[0xA9] & 3U) ? mixed_sample() * expansion_gains::kVrc6 * (reg_[0xAA] & 15U) / 15.0F
                             : 0;
}
void Rainbow::reset() {
    reg_.fill(0);
    reg_[0x28] = reg_[0x29] = 1;
    reg_[0x2F] = 0x80;
    reg_[0x53] = 135;
    reg_[0xAA] = 15;
    reg_[0x141] = 7;
    reg_[0x142] = 6;
    scanline_ = 255;
    hblank_ = scan_enable_ = pending_ = jitter_ = parity_ = oam_proc_ = 0;
    cpu_count_ = 0;
    saw_ = {};
    pulse_ = {};
    flash_id_ = {};
    flash_phase_ = {};
    bypass_ = {};
}
void Rainbow::power_on() {
    reset();
    fpga_.fill(0);
    std::ranges::fill(work_, 0);
    std::ranges::fill(chr_ram_, 0);
}
void Rainbow::save_state(StateWriter& w) const {
    w.write<Word>(682);
    w.write<std::uint32_t>(prg_size_);
    w.write<std::uint32_t>(chr_size_);
    w.write_array(reg_);
    w.write_array(flash_phase_);
    w.write_array(flash_id_);
    w.write_array(bypass_);
    w.write_array(
        std::array{scanline_, hblank_, scan_enable_, pending_, parity_, jitter_, oam_proc_});
    w.write(cpu_count_);
    for (const auto& channel : pulse_) {
        w.write(channel.timer);
        w.write(channel.step);
    }
    w.write(saw_.timer);
    w.write(saw_.step);
    w.write(saw_.accumulator);
    w.write_array(fpga_);
    w.write_bytes(work_);
    w.write_bytes(chr_ram_);
    w.write_bytes(std::span{storage_}.subspan(original_.size()));
    w.write<Word>(std::ranges::count(dirty_, 1));
    for (unsigned i = 0; i < dirty_.size(); ++i)
        if (dirty_[i]) {
            w.write<Word>(i);
            w.write_bytes(std::span{storage_}.subspan(i * page_size, page_size));
        }
}
void Rainbow::load_state(StateReader& r) {
    const auto tag = r.read<Word>();
    const auto prg = r.read<std::uint32_t>(), chr = r.read<std::uint32_t>();
    decltype(reg_) regs{};
    decltype(flash_phase_) phases{}, ids{}, bypasses{};
    r.read_array(regs);
    r.read_array(phases);
    r.read_array(ids);
    r.read_array(bypasses);
    std::array<Byte, 7> flags{};
    r.read_array(flags);
    const auto cpu_count = r.read<Word>();
    std::array<Word, 3> timers{};
    std::array<Byte, 3> steps{};
    for (unsigned i = 0; i < 3; ++i) {
        r.read(timers[i]);
        r.read(steps[i]);
    }
    auto accum = r.read<Byte>();
    decltype(fpga_) fpga{};
    r.read_array(fpga);
    std::vector<Byte> work, patterns, nvram;
    r.read_bytes(work, work_.size());
    r.read_bytes(patterns, chr_ram_.size());
    r.read_bytes(nvram, nvram_size_);
    const unsigned n = r.read<Word>();
    if (!r.ok() || tag != 682 || prg != prg_size_ || chr != chr_size_ || !regs[0x53] ||
        regs[0x53] > 170 || (flags[0] != 255 && flags[0] > 241) || flags[1] > 1 || flags[2] > 1 ||
        flags[3] > 3 || flags[4] > 1 || flags[6] > 2 || steps[0] > 15 || steps[1] > 15 ||
        steps[2] > 13 || std::ranges::max(timers) > 4095 || std::ranges::max(phases) > 7 ||
        std::ranges::max(ids) > 1 || std::ranges::max(bypasses) > 1 ||
        work.size() != work_.size() || patterns.size() != chr_ram_.size() ||
        nvram.size() != nvram_size_ || n > dirty_.size()) {
        r.fail();
        return;
    }
    struct Patch {
        Word page;
        std::vector<Byte> data;
    };
    std::vector<Patch> patches;
    for (unsigned i = 0; i < n; ++i) {
        auto page = r.read<Word>();
        std::vector<Byte> bytes;
        r.read_bytes(bytes, page_size);
        if (!r.ok() || page >= dirty_.size() || (!patches.empty() && patches.back().page >= page) ||
            bytes.size() != page_size) {
            r.fail();
            return;
        }
        patches.push_back({page, std::move(bytes)});
    }
    for (unsigned i = 0; i < dirty_.size(); ++i)
        if (dirty_[i])
            std::copy_n(
                original_.begin() + i * page_size, page_size, storage_.begin() + i * page_size);
    std::ranges::fill(dirty_, 0);
    for (const auto& patch : patches) {
        std::ranges::copy(patch.data, storage_.begin() + patch.page * page_size);
        dirty_[patch.page] = 1;
    }
    std::ranges::copy(nvram, storage_.begin() + original_.size());
    fpga_ = fpga;
    work_ = std::move(work);
    chr_ram_ = std::move(patterns);
    reg_ = regs;
    flash_phase_ = phases;
    flash_id_ = ids;
    bypass_ = bypasses;
    scanline_ = flags[0];
    hblank_ = flags[1];
    scan_enable_ = flags[2];
    pending_ = flags[3];
    parity_ = flags[4];
    jitter_ = flags[5];
    oam_proc_ = flags[6];
    cpu_count_ = cpu_count;
    for (unsigned i = 0; i < 3; ++i)
        update_voice(i);
    for (unsigned i = 0; i < 2; ++i) {
        pulse_[i].step = steps[i];
        pulse_[i].timer = timers[i];
    }
    saw_.step = steps[2];
    saw_.timer = timers[2];
    saw_.accumulator = accum;
}
} // namespace mapperbus::core
