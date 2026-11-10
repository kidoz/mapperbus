#include "core/mappers/sachen.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
// Original implementation: NESdev 133 r4977, 8259 r23709, 137 r20417,
// 143 r5034, NINA-003-006 r22985, 150 r5081, 243 r5861.
Sachen::Sachen(const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr)
    : prg_rom_(std::move(prg)), chr_rom_(std::move(chr)), id_(h.mapper_number),
      fixed_mirror_(h.mirror_mode) {}
bool Sachen::supports_mapper(uint16_t id) {
    switch (id) {
    case 133U:
    case 135U:
    case 137U:
    case 138U:
    case 139U:
    case 141U:
    case 143U:
    case 146U:
    case 150U:
    case 243U:
        return true;
    default:
        return false;
    }
}
Result<void> Sachen::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Unsupported Sachen submapper");
    std::size_t max_prg = 262144U, max_chr = 262144U;
    switch (h.mapper_number) {
    case 133U:
        max_prg = 65536U;
        max_chr = 32768U;
        break;
    case 143U:
        max_prg = 32768U;
        max_chr = 8192U;
        break;
    case 146U:
        max_prg = 65536U;
        max_chr = 65536U;
        break;
    case 150U:
        max_prg = 131072U;
        max_chr = 65536U;
        break;
    case 243U:
        max_prg = 131072U;
        max_chr = 131072U;
        break;
    case 137U:
        max_chr = 32768U;
        break;
    case 138U:
        max_chr = 131072U;
        break;
    case 139U:
        max_chr = 524288U;
        break;
    default:
        break;
    }
    const bool ram = h.mapper_number == 141U && chr == 0U;
    const std::size_t chr_unit = h.mapper_number == 137U ? 1024U : 2048U;
    if (prg < 16384U || prg > max_prg || prg % 16384U != 0U ||
        (!ram && (chr < (h.mapper_number == 137U ? 4096U : 8192U) || chr > max_chr ||
                  chr % chr_unit != 0U)))
        return std::unexpected("Unsupported Sachen ROM geometry");
    if ((h.is_nes2 && h.prg_ram_size != 0U) || h.prg_nvram_size != 0U || h.has_battery ||
        h.chr_nvram_size != 0U ||
        (ram ? (h.is_nes2 && h.chr_ram_size != 8192U) : h.chr_ram_size != 0U))
        return std::unexpected(
            "Sachen boards have no WRAM or NVRAM; only mapper 141 supports 8 KiB CHR RAM");
    // The Great Wall commonly has an erroneous four-screen header. The chip
    // owns CIRAM routing; it never allocates an extra nametable RAM chip.
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical &&
        !(h.mapper_number == 137U && h.mirror_mode == MirrorMode::FourScreen))
        return std::unexpected("Unsupported Sachen header mirroring");
    return {};
}
bool Sachen::is_8259() const {
    return id_ == 135U || id_ == 137U || id_ == 138U || id_ == 139U || id_ == 141U;
}
bool Sachen::readable_registers() const {
    return id_ == 150U || id_ == 243U;
}
bool Sachen::maps_expansion(Address addr) const {
    if (id_ == 133U || id_ == 143U || id_ == 146U)
        return (addr & 0xE100U) == 0x4100U;
    return (addr & 0xC100U) == 0x4100U;
}
bool Sachen::maps_prg(Address addr) const {
    return addr >= 0x8000U || maps_expansion(addr);
}
Byte Sachen::prg_read_mask(Address addr) const {
    return addr >= 0x8000U ? 0xFFU : expansion_read_mask(addr);
}
Byte Sachen::read_prg(Address addr) {
    if (addr < 0x8000U)
        return read_expansion(addr);
    unsigned bank = 0U;
    if (id_ == 133U)
        bank = latch_ >> 2U;
    else if (id_ == 146U)
        bank = latch_ >> 3U;
    else if (is_8259())
        bank = registers_[5];
    else if (readable_registers())
        bank = registers_[5] & 3U;
    return prg_rom_[(bank * 32768U + (addr & 0x7FFFU)) % prg_rom_.size()];
}
void Sachen::write_prg(Address addr, Byte value) {
    write_expansion(addr, value);
}
Byte Sachen::expansion_read_mask(Address addr) const {
    if (!maps_expansion(addr))
        return 0U;
    if (id_ == 143U)
        return 0x3FU;
    if (readable_registers() && (addr & 1U) != 0U)
        return jumper_ != 0U ? 3U : 7U;
    return 0U;
}
Byte Sachen::read_expansion(Address addr) {
    if (!maps_expansion(addr))
        return 0U;
    if (id_ == 143U)
        return static_cast<Byte>(~static_cast<unsigned>(addr) & 0x3FU);
    return readable_registers() && (addr & 1U) != 0U ? registers_[select_] : 0U;
}
void Sachen::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr) || id_ == 143U)
        return;
    if (id_ == 133U || id_ == 146U) {
        latch_ = value & (id_ == 133U ? 7U : 15U);
        return;
    }
    const Byte data = static_cast<Byte>((value | (jumper_ != 0U ? 4U : 0U)) & 7U);
    if ((addr & 1U) == 0U)
        select_ = data;
    else
        registers_[select_] = data;
}
Result<void> Sachen::configure_board_jumpers(Byte value) {
    if (id_ != 150U || value > 1U)
        return std::unexpected("Only Sachen mapper 150 exposes a 0/1 D2 solder-pad setting");
    jumper_ = value;
    return {};
}
std::size_t Sachen::chr_address(Address addr) const {
    if (chr_rom_.empty())
        return addr & 0x1FFFU;
    unsigned bank = 0U;
    if (id_ == 133U)
        bank = latch_ & 3U;
    else if (id_ == 146U)
        bank = latch_ & 7U;
    else if (id_ == 150U)
        bank = ((registers_[4] & 1U) << 2U) | (registers_[6] & 3U);
    else if (id_ == 243U)
        bank = (registers_[2] & 1U) | ((registers_[4] & 1U) << 1U) | ((registers_[6] & 3U) << 2U);
    else if (id_ == 137U) {
        if ((addr & 0x1000U) != 0U)
            return (chr_rom_.size() - 4096U + (addr & 0xFFFU));
        const unsigned slot = (static_cast<unsigned>(addr) >> 10U) & 3U;
        const unsigned reg = (registers_[7] & 1U) != 0U ? 0U : slot;
        bank = registers_[reg];
        if (slot != 0U)
            bank |= ((static_cast<unsigned>(registers_[4]) >> (slot - 1U)) & 1U) << 4U;
        if (slot == 3U)
            bank |= (registers_[6] & 1U) << 3U;
        return (bank * 1024U + (addr & 0x3FFU)) % chr_rom_.size();
    } else if (is_8259()) {
        const unsigned slot = (static_cast<unsigned>(addr) >> 11U) & 3U;
        const unsigned reg = (registers_[7] & 1U) != 0U ? 0U : slot;
        bank = registers_[reg] | (static_cast<unsigned>(registers_[4]) << 3U);
        const unsigned shift = id_ == 139U ? 2U : (id_ == 135U || id_ == 141U ? 1U : 0U);
        const unsigned unit = 2048U << shift;
        return (bank * unit + (addr & (unit - 1U))) % chr_rom_.size();
    }
    return (bank * 8192U + (addr & 0x1FFFU)) % chr_rom_.size();
}
Byte Sachen::read_chr(Address addr) {
    return chr_rom_.empty() ? chr_ram_[addr & 0x1FFFU] : chr_rom_[chr_address(addr)];
}
void Sachen::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty())
        chr_ram_[addr & 0x1FFFU] = value;
}
unsigned Sachen::mirror_page(unsigned table) const {
    if (readable_registers()) {
        switch ((static_cast<unsigned>(registers_[7]) >> 1U) & 3U) {
        case 0U:
            return table == 3U ? 1U : 0U;
        case 1U:
            return table >> 1U;
        case 2U:
            return table & 1U;
        default:
            return 1U;
        }
    }
    const unsigned mode =
        (registers_[7] & 1U) != 0U ? 0U : static_cast<unsigned>(registers_[7]) >> 1U;
    switch (mode) {
    case 0U:
        return id_ == 137U ? table >> 1U : table & 1U;
    case 1U:
        return id_ == 137U ? table & 1U : table >> 1U;
    case 2U:
        return table == 0U ? 0U : 1U;
    default:
        return 0U;
    }
}
MirrorMode Sachen::mirror_mode() const {
    if (!is_8259() && !readable_registers())
        return fixed_mirror_;
    const unsigned p0 = mirror_page(0U), p1 = mirror_page(1U), p2 = mirror_page(2U),
                   p3 = mirror_page(3U);
    if (p0 == p1 && p1 == p2 && p2 == p3)
        return p0 != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    return p1 == p0 ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
std::optional<Address> Sachen::nametable_address(Address addr) const {
    if (!is_8259() && !readable_registers())
        return std::nullopt;
    return static_cast<Address>((mirror_page((static_cast<unsigned>(addr) >> 10U) & 3U) << 10U) |
                                (addr & 0x3FFU));
}
void Sachen::reset() {} // Cartridge latches retain their state on console reset.
void Sachen::save_state(StateWriter& w) const {
    w.write_array(registers_);
    w.write(select_);
    w.write(latch_);
    w.write(jumper_);
    if (chr_rom_.empty())
        w.write_array(chr_ram_);
}
void Sachen::load_state(StateReader& r) {
    r.read_array(registers_);
    r.read(select_);
    r.read(latch_);
    r.read(jumper_);
    if (chr_rom_.empty())
        r.read_array(chr_ram_);
    if (!r.ok() || select_ > 7U ||
        latch_ > (id_ == 133U   ? 7U
                  : id_ == 146U ? 15U
                                : 0U) ||
        jumper_ > (id_ == 150U ? 1U : 0U) ||
        std::ranges::any_of(registers_, [](Byte v) { return v > 7U; })) {
        select_ = 0U;
        latch_ = 0U;
        jumper_ = 0U;
        registers_ = {};
        r.fail();
    }
}
} // namespace mapperbus::core
