#include "core/mappers/decathlon.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
unsigned exchange(unsigned value, unsigned a, unsigned b) {
    const unsigned difference = ((value >> a) ^ (value >> b)) & 1U;
    return value ^ (difference << a) ^ (difference << b);
}
} // namespace
// Original bit-routing implementation from Everynes. The undefined CHR mode 6
// follows the inversion used by published Mesen and Nintendulator references.
Decathlon::Decathlon(const INesHeader& h, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_(h.mirror_mode) {}
Result<void> Decathlon::validate_geometry(const INesHeader& h,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 244 supports submapper 0");
    if (prg_size != 131072U || chr_size != 65536U)
        return std::unexpected("Mapper 244 requires 128 KiB PRG and 64 KiB CHR ROM");
    if (h.has_battery || h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
        (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 244 supports ROM-only board memory");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 244 requires fixed H/V mirroring");
    return {};
}
Byte Decathlon::read_prg(Address addr) {
    return addr >= 0x8000U ? prg_rom_[static_cast<unsigned>(prg_bank_) * 32768U + (addr & 0x7FFFU)]
                           : 0U;
}
void Decathlon::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    // The old PRG mapping drives the bus before the destination latch is chosen.
    const unsigned bus = value & read_prg(addr);
    if ((bus & 8U) == 0U) {
        unsigned bank = bus & 3U;
        if ((bus & 0x10U) != 0U)
            bank ^= 3U;
        if ((bus & 0x20U) != 0U)
            bank = exchange(bank, 0U, 1U);
        prg_bank_ = static_cast<Byte>(bank);
    } else {
        unsigned bank = bus & 7U;
        const unsigned mode = (bus >> 4U) & 7U;
        if (mode >= 6U)
            bank ^= 7U;
        else {
            if ((mode & 1U) != 0U)
                bank = exchange(bank, 0U, 1U);
            if ((mode & 2U) != 0U)
                bank = exchange(bank, 1U, 2U);
            if ((mode & 4U) != 0U)
                bank = exchange(bank, 2U, 0U);
        }
        chr_bank_ = static_cast<Byte>(bank);
    }
}
Byte Decathlon::read_chr(Address addr) {
    return chr_rom_[static_cast<unsigned>(chr_bank_) * 8192U + (addr & 0x1FFFU)];
}
void Decathlon::write_chr(Address, Byte) {}
MirrorMode Decathlon::mirror_mode() const {
    return mirror_;
}
void Decathlon::reset() {
    // Published compatibility policy: console reset retains both latches.
}
void Decathlon::power_on() {
    prg_bank_ = chr_bank_ = 0U;
}
uint16_t Decathlon::state_tag() const {
    return static_cast<uint16_t>(0xF400U | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}
void Decathlon::save_state(StateWriter& w) const {
    w.write(state_tag());
    w.write(prg_bank_);
    w.write(chr_bank_);
}
void Decathlon::load_state(StateReader& r) {
    const auto tag = r.read<uint16_t>();
    const auto prg = r.read<Byte>(), chr = r.read<Byte>();
    if (!r.ok() || tag != state_tag() || prg > 3U || chr > 7U) {
        r.fail();
        return;
    }
    prg_bank_ = prg;
    chr_bank_ = chr;
}
} // namespace mapperbus::core
