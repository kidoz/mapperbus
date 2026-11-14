#include "core/mappers/golden_game.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kChipSize = 0x100000U;
constexpr Address kLatchMask = 0x3F1FU;
constexpr std::array<Byte, 5> kDefaultSockets{0U, 1U, 5U, 7U, 15U};
constexpr std::array<std::pair<std::string_view, Byte>, 8> kProfiles{{{"slots-0", 1U},
                                                                      {"slots-0-1", 3U},
                                                                      {"slots-0-2", 5U},
                                                                      {"slots-0-1-2", 7U},
                                                                      {"slots-0-3", 9U},
                                                                      {"slots-0-1-3", 11U},
                                                                      {"slots-0-2-3", 13U},
                                                                      {"slots-0-1-2-3", 15U}}};
} // namespace

// Original decoding from NESdev r22514 and published board/dumping reports.
// R polarity follows the corrected 2025 report, not Kevtris's older text.
// Packed sockets are ordered by physical socket number; defaults are explicit
// compatibility choices. The coverage ledger records the ambiguous layouts.
GoldenGame::GoldenGame(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), sockets_(kDefaultSockets[prg_rom_.size() / kChipSize]) {}

Result<void> GoldenGame::validate_geometry(const INesHeader& h,
                                           std::size_t prg_size,
                                           std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 235 supports submapper 0");
    if (prg_size < kChipSize || prg_size > 4U * kChipSize || prg_size % kChipSize != 0U ||
        chr_size != 0U)
        return std::unexpected("Mapper 235 requires 1-4 packed 1 MiB PRG chips and no CHR ROM");
    if (h.has_battery || (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U ||
                                        h.chr_ram_size != 8192U || h.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 235 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 235 supports ordinary H/V CIRAM metadata");
    return {};
}

std::optional<unsigned> GoldenGame::chip_index() const {
    const unsigned socket = (latch_ >> 8U) & 3U;
    if ((sockets_ & (1U << socket)) == 0U)
        return std::nullopt;
    return static_cast<unsigned>(
        std::popcount(static_cast<unsigned>(sockets_) & ((1U << socket) - 1U)));
}

Byte GoldenGame::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && !chip_index() ? 0U : 0xFFU;
}

Byte GoldenGame::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto chip = chip_index();
    if (!chip)
        return 0U;
    const unsigned half = (latch_ & 0x800U) != 0U ? (latch_ >> 12U) & 1U : (addr >> 14U) & 1U;
    const auto offset =
        *chip * kChipSize + (latch_ & 31U) * 0x8000U + half * 0x4000U + (addr & 0x3FFFU);
    return prg_rom_[offset];
}

void GoldenGame::write_prg(Address addr, Byte /*value*/) {
    if (addr >= 0x8000U)
        latch_ = addr & kLatchMask;
}

Byte GoldenGame::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void GoldenGame::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode GoldenGame::mirror_mode() const {
    if ((latch_ & 0x400U) != 0U)
        return MirrorMode::SingleLower;
    return (latch_ & 0x2000U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

Result<void> GoldenGame::configure_board_profile(std::string_view name) {
    for (const auto& [profile, sockets] : kProfiles) {
        if (name != profile)
            continue;
        if (static_cast<std::size_t>(std::popcount(static_cast<unsigned>(sockets))) * kChipSize !=
            prg_rom_.size())
            return std::unexpected("Mapper 235 socket profile does not match the packed PRG size");
        sockets_ = sockets;
        power_on(); // Select physical wiring before installing the cartridge.
        return {};
    }
    return std::unexpected(
        "Unknown mapper 235 profile; use slots- followed by populated sockets (0 required)");
}

void GoldenGame::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void GoldenGame::reset() {
    latch_ = 0U; // Retain volatile pattern memory on console soft reset.
}

uint16_t GoldenGame::state_tag() const {
    return static_cast<uint16_t>(0xEB00U | sockets_);
}

void GoldenGame::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(latch_);
    writer.write_array(chr_ram_);
}

void GoldenGame::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto latch = reader.read<Address>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || (latch & ~kLatchMask) != 0U) {
        reader.fail();
        return;
    }
    latch_ = latch;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
