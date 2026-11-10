#include "core/mappers/txc.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {
// Original implementation: NESdev 36 r4196, 132 r4967, 136 r21611,
// 147 r5057, 172 r5257 and 173 r5274. No title/hash-based mapper hacks.
Txc::Txc(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), id_(h.mapper_number),
      simple_147_(id_ == 147U && prg_rom_.size() <= 65536U),
      initial_vertical_(h.mirror_mode == MirrorMode::Vertical ? 1U : 0U),
      vertical_(initial_vertical_) {}
bool Txc::supports_mapper(uint16_t id) {
    return id == 36U || id == 132U || id == 136U || id == 147U || id == 172U || id == 173U;
}
Result<void> Txc::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    const auto id = h.mapper_number;
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Unsupported TXC/JV001 submapper");
    const std::size_t max_prg = id == 36U || id == 147U    ? 131072U
                                : id == 172U || id == 173U ? 32768U
                                                           : 65536U;
    const std::size_t max_chr = id == 36U || id == 147U    ? 131072U
                                : id == 132U || id == 172U ? 32768U
                                                           : 65536U;
    if (prg < 16384U || prg > max_prg || prg % 16384U != 0U || chr < 8192U || chr > max_chr ||
        chr % 8192U != 0U)
        return std::unexpected("Unsupported TXC/JV001 ROM geometry");
    if ((h.is_nes2 && h.prg_ram_size != 0U) || h.prg_nvram_size != 0U || h.has_battery ||
        h.chr_ram_size != 0U || h.chr_nvram_size != 0U)
        return std::unexpected("TXC/JV001 boards have no RAM or NVRAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("TXC/JV001 requires an H/V header arrangement");
    return {};
}
bool Txc::jv001() const {
    return id_ == 136U || id_ == 147U || id_ == 172U;
}
Byte Txc::reverse_six(Byte value) {
    unsigned result = 0U;
    for (unsigned bit = 0U; bit < 6U; ++bit)
        result |= ((static_cast<unsigned>(value) >> bit) & 1U) << (5U - bit);
    return static_cast<Byte>(result);
}
Byte Txc::data_input(Byte value) const {
    if (id_ == 36U)
        return (static_cast<unsigned>(value) >> 4U) & 3U;
    if (id_ == 147U)
        return value >> 2U;
    if (id_ == 172U)
        return reverse_six(value);
    return value;
}
Byte Txc::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    unsigned bank = 0U;
    if (id_ == 36U)
        bank = output_;
    else if (id_ == 132U)
        bank = (static_cast<unsigned>(output_) >> 2U) & 1U;
    else if (id_ == 136U)
        bank = (static_cast<unsigned>(output_) >> 4U) & 1U;
    else if (id_ == 147U)
        bank = (output_ & 1U) | ((static_cast<unsigned>(output_) >> 4U) & 2U);
    return prg_rom_[(bank * 32768U + (addr & 0x7FFFU)) % prg_rom_.size()];
}
void Txc::write_prg(Address addr, Byte value) {
    if (simple_147_) {
        if ((addr & 0x4103U) == 0x4102U) {
            if (addr >= 0xC000U)
                value &= read_prg(addr);
            output_ = value >> 2U;
        }
    } else if (addr >= 0x8000U) {
        output_ = reg_;
        if (id_ == 172U)
            vertical_ = invert_;
    } else
        write_expansion(addr, value);
}
bool Txc::maps_expansion(Address addr) const {
    if (simple_147_)
        return (addr & 0x4103U) == 0x4102U;
    return (addr & 0xE100U) == 0x4100U || (id_ == 36U && (addr & 0xE200U) == 0x4200U);
}
Byte Txc::expansion_read_mask(Address addr) const {
    if (simple_147_ || (addr & 0xE100U) != 0x4100U)
        return 0U;
    if (id_ == 36U)
        return 0x30U;
    if (id_ == 147U)
        return 0xFCU;
    return jv001() ? 0x3FU : 0x0FU;
}
Byte Txc::read_expansion(Address addr) {
    if (expansion_read_mask(addr) == 0U)
        return 0U;
    if (id_ == 36U)
        return static_cast<Byte>(reg_ << 4U);
    if (!jv001())
        return static_cast<Byte>(reg_ | ((static_cast<unsigned>(s_) ^ invert_) << 3U));
    const Byte data = reg_ ^ (invert_ != 0U ? 0x30U : 0U);
    if (id_ == 147U)
        return static_cast<Byte>(data << 2U);
    return id_ == 172U ? reverse_six(data) : data;
}
void Txc::write_expansion(Address addr, Byte value) {
    if (simple_147_) {
        write_prg(addr, value);
        return;
    }
    // These are independent decoders. $430x can write the CHR latch and a
    // protection register on the very same bus strobe.
    if (id_ == 36U && (addr & 0xE200U) == 0x4200U)
        chr_latch_ = value & 15U;
    if ((addr & 0xE100U) != 0x4100U)
        return;
    const Byte data = data_input(value);
    const Byte mask = jv001() ? 0x3FU : id_ == 36U ? 3U : 7U;
    switch (addr & 3U) {
    case 0U:
        if (jv001()) {
            if (mode_ != 0U)
                reg_ = static_cast<Byte>((reg_ & 0x30U) | ((reg_ + 1U) & 15U));
            else
                reg_ = input_ ^ (invert_ != 0U ? 15U : 0U);
        } else
            reg_ = mode_ != 0U ? static_cast<Byte>((reg_ + 1U) & mask)
                               : static_cast<Byte>(input_ ^ (invert_ != 0U ? mask : 0U));
        break;
    case 1U:
        invert_ = data & 1U;
        break;
    case 2U:
        input_ = data & mask;
        if (!jv001() && id_ != 36U)
            s_ = (static_cast<unsigned>(data) >> 3U) & 1U;
        break;
    case 3U:
        mode_ = data & 1U;
        break;
    default:
        break;
    }
}
Byte Txc::read_chr(Address addr) {
    // An 8 KiB UVEPROM on mapper 173 reuses A14 as /PGM. Model disabled
    // reads as $FF, not as an analog floating-bus measurement.
    if (id_ == 173U && chr_rom_.size() == 8192U && invert_ != 0U)
        return 0xFFU;
    unsigned bank = 0U;
    if (id_ == 36U)
        bank = chr_latch_;
    else if (id_ == 132U)
        bank = output_ & 3U;
    else if (id_ == 136U)
        bank = output_ & 7U;
    else if (id_ == 147U)
        bank = (static_cast<unsigned>(output_) >> 1U) & 15U;
    else if (id_ == 172U)
        bank = reverse_six(output_) & 3U;
    else if (id_ == 173U)
        bank = ((output_ & 2U) << 1U) | ((invert_ ^ 1U) << 1U) | (output_ & 1U);
    return chr_rom_[(bank * 8192U + (addr & 0x1FFFU)) % chr_rom_.size()];
}
void Txc::write_chr(Address, Byte) {}
MirrorMode Txc::mirror_mode() const {
    return vertical_ != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}
void Txc::reset() {}
void Txc::save_state(StateWriter& w) const {
    w.write(input_);
    w.write(reg_);
    w.write(output_);
    w.write(s_);
    w.write(mode_);
    w.write(invert_);
    w.write(chr_latch_);
    w.write(vertical_);
}
void Txc::load_state(StateReader& r) {
    r.read(input_);
    r.read(reg_);
    r.read(output_);
    r.read(s_);
    r.read(mode_);
    r.read(invert_);
    r.read(chr_latch_);
    r.read(vertical_);
    const Byte mask = jv001() ? 0x3FU : id_ == 36U ? 3U : 7U;
    if (!r.ok() || input_ > mask || reg_ > mask || output_ > mask || s_ > 1U || mode_ > 1U ||
        invert_ > 1U || chr_latch_ > (id_ == 36U ? 15U : 0U) || vertical_ > 1U ||
        (id_ != 172U && vertical_ != initial_vertical_) || ((jv001() || id_ == 36U) && s_ != 0U) ||
        (simple_147_ && (input_ != 0U || reg_ != 0U || s_ != 0U || mode_ != 0U || invert_ != 0U))) {
        input_ = 0U;
        reg_ = 0U;
        output_ = 0U;
        s_ = 0U;
        mode_ = 0U;
        invert_ = 0U;
        chr_latch_ = 0U;
        vertical_ = initial_vertical_;
        r.fail();
    }
}
} // namespace mapperbus::core
