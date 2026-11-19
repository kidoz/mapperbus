#include "core/mappers/bmc_t3h53.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Address kLatchMask = 0x03FFU;
constexpr Address kLock = 0x0200U;
constexpr Address kJumperRead = 0x0100U;
constexpr Address kNrom128 = 0x0080U;
} // namespace

// Original decoding from NESdev mapper 59 revision 22300.
BmcT3h53::BmcT3h53(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {}

Result<void> BmcT3h53::validate_geometry(const INesHeader& header,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 59 supports submapper 0");
    if (prg_size < 0x4000U || prg_size > 0x20000U || prg_size % 0x4000U != 0U ||
        chr_size < 0x2000U || chr_size > 0x10000U || chr_size % 0x2000U != 0U)
        return std::unexpected(
            "Mapper 59 requires 16-128 KiB PRG and 8-64 KiB CHR ROM in bank units");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 59 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 59 supports ordinary H/V CIRAM");
    return {};
}

Byte BmcT3h53::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    if ((latch_ & kJumperRead) != 0U)
        return jumpers_;
    unsigned bank = (static_cast<unsigned>(latch_) >> 4U) & 7U;
    if ((latch_ & kNrom128) == 0U)
        bank = (bank & 6U) | ((static_cast<unsigned>(addr) >> 14U) & 1U);
    const auto offset = bank * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void BmcT3h53::write_prg(Address addr, Byte) {
    if (addr >= 0x8000U && (latch_ & kLock) == 0U)
        latch_ = addr & kLatchMask;
}

Byte BmcT3h53::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && (latch_ & kJumperRead) != 0U ? 0x03U : 0xFFU;
}

Byte BmcT3h53::read_chr(Address addr) {
    const auto offset = (latch_ & 7U) * 0x2000U + (addr & 0x1FFFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void BmcT3h53::write_chr(Address, Byte) {}

MirrorMode BmcT3h53::mirror_mode() const {
    return (latch_ & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

Result<void> BmcT3h53::configure_board_jumpers(Byte value) {
    if (value > 3U)
        return std::unexpected("Mapper 59 jumpers require a value 0..3");
    jumpers_ = value;
    return {};
}

void BmcT3h53::reset() {
    // Deterministic reset policy: clear the lock and select the first menu.
    latch_ = 0U;
}

uint16_t BmcT3h53::state_tag() const {
    return static_cast<uint16_t>(0x3B00U | jumpers_);
}

void BmcT3h53::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
}

void BmcT3h53::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Address>();
    if (!reader.ok() || tag != state_tag() ||
        (static_cast<unsigned>(latch) & ~static_cast<unsigned>(kLatchMask)) != 0U) {
        reader.fail();
        return;
    }
    latch_ = latch;
}

} // namespace mapperbus::core
