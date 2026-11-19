#include "core/mappers/multicart51_53.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kMenuSize = 32768U;
constexpr std::size_t kGameRomSize = 2U * 1024U * 1024U;
} // namespace
Multicart51_53::Multicart51_53(const INesHeader& h,
                               std::vector<Byte> prg,
                               std::vector<Byte> /*chr*/)
    : prg_(std::move(prg)), mapper_(h.mapper_number), submapper_(h.submapper) {
    power_on();
}

Result<void> Multicart51_53::validate_geometry(const INesHeader& h,
                                               std::size_t prg,
                                               std::size_t chr) {
    if (h.mapper_number == 51U) {
        if (h.submapper > 1U || prg != 512U * 1024U || h.misc_rom_count != 0U)
            return std::unexpected("11-in-1 requires 512 KiB PRG and submapper 0 or 1");
    } else if (h.mapper_number == 53U) {
        if (h.submapper != 0U || !((prg == kGameRomSize + kMenuSize && h.misc_rom_count == 0U) ||
                                   (h.is_nes2 && prg == kGameRomSize && h.misc_rom_count == 1U)))
            return std::unexpected("Supervision requires 2 MiB PRG and one 32 KiB menu ROM");
    } else
        return std::unexpected("Expected mapper 51 or 53");
    if (chr != 0U || h.has_trainer || h.has_battery || h.prg_nvram_size != 0U ||
        (h.is_nes2 && h.prg_ram_size != 0U) || h.chr_ram_size != 8192U || h.chr_nvram_size != 0U ||
        (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal))
        return std::unexpected("11-in-1 and Supervision require 8 KiB CHR RAM and ordinary CIRAM");
    return {};
}
Result<std::unique_ptr<Mapper>> Multicart51_53::create_with_menu(const INesHeader& h,
                                                                 std::vector<Byte> prg,
                                                                 std::vector<Byte> chr,
                                                                 std::span<const Byte> menu) {
    if (h.misc_rom_count != 0U) {
        if (menu.size() != kMenuSize)
            return std::unexpected("Supervision menu ROM must be exactly 32 KiB");
        // Normalize a separate NES 2.0 menu to the historical menu-first PRG layout.
        prg.insert(prg.begin(), menu.begin(), menu.end());
    } else if (!menu.empty())
        return std::unexpected("Unexpected miscellaneous ROM for Supervision");
    return std::make_unique<Multicart51_53>(h, std::move(prg), std::move(chr));
}

std::size_t Multicart51_53::ball_games_offset(Address addr) const {
    const bool nrom = (mode_ & 2U) != 0U;
    const unsigned bank = bank_;
    if (addr < 0x8000U) {
        const unsigned page =
            submapper_ == 1U ? ((bank << 1U) | 0x23U) : ((bank << 2U) | (nrom ? 0x23U : 0x2FU));
        return ((page & 63U) * 8192U) + (addr & 0x1FFFU);
    }
    if (nrom) {
        const unsigned page = submapper_ == 1U ? bank >> 1U : bank;
        return (page & 15U) * 32768U + (addr & 0x7FFFU);
    }
    const bool fixed = addr >= 0xC000U;
    unsigned page;
    if (submapper_ == 1U) {
        // 850828C uses latch bits 4 and 6 for the outer PRG address lines.
        page = ((bank >> 1U) & 8U) | ((bank >> 2U) & 16U) | (fixed ? 7U : (bank & 7U));
    } else {
        page = (bank << 1U) | (fixed ? 7U : ((bank >> 4U) & 1U));
    }
    return (page & 31U) * 16384U + (addr & 0x3FFFU);
}

std::size_t Multicart51_53::supervision_offset(Address addr) const {
    const unsigned block = mode_ & 15U;
    if (addr < 0x8000U)
        return kMenuSize + block * 131072U + 15U * 8192U + (addr & 0x1FFFU);
    if ((mode_ & 0x10U) == 0U)
        return addr & 0x7FFFU;
    return kMenuSize + block * 131072U + (addr < 0xC000U ? bank_ : 7U) * 16384U + (addr & 0x3FFFU);
}
Byte Multicart51_53::read_prg(Address addr) {
    if (!maps_prg(addr))
        return 0U;
    return prg_[mapper_ == 51U ? ball_games_offset(addr) : supervision_offset(addr)];
}
void Multicart51_53::write_prg(Address addr, Byte value) {
    if (addr < 0x6000U)
        return;
    if (addr >= 0x8000U) {
        bank_ = mapper_ == 53U ? value & 7U : value;
    } else if (mapper_ == 51U) {
        mode_ = value & 0x12U;
    } else if ((mode_ & 0x10U) == 0U) {
        mode_ = value & 0x3FU;
    }
}
bool Multicart51_53::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}
Byte Multicart51_53::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}
void Multicart51_53::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}
MirrorMode Multicart51_53::mirror_mode() const {
    return (mode_ & (mapper_ == 51U ? 0x10U : 0x20U)) != 0U ? MirrorMode::Horizontal
                                                            : MirrorMode::Vertical;
}
void Multicart51_53::reset() {
    mode_ = mapper_ == 51U ? 2U : 0U;
    bank_ = 0U;
}
void Multicart51_53::power_on() {
    reset();
    chr_ram_.fill(0U);
}
void Multicart51_53::save_state(StateWriter& w) const {
    w.write(mapper_);
    w.write(submapper_);
    w.write(mode_);
    w.write(bank_);
    w.write_array(chr_ram_);
}
void Multicart51_53::load_state(StateReader& r) {
    const auto mapper = r.read<uint16_t>();
    const auto submapper = r.read<Byte>();
    const auto mode = r.read<Byte>();
    const auto bank = r.read<Byte>();
    std::array<Byte, 8192> ram{};
    r.read_array(ram);
    if (!r.ok() || mapper != mapper_ || submapper != submapper_ ||
        (mapper_ == 51U ? (mode & ~0x12U) != 0U : mode > 0x3FU || bank > 7U)) {
        r.fail();
        return;
    }
    mode_ = mode;
    bank_ = bank;
    chr_ram_ = ram;
}
} // namespace mapperbus::core
