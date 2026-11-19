#include "core/mappers/mapper283.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kPrgBankBytes = 32768U;
constexpr std::size_t kPrgUnitBytes = 16384U;
constexpr std::size_t kLowBankBytes = 8192U;
constexpr std::size_t kFirstBlockBytes = 262144U;
constexpr std::size_t kMaxMainRomBytes = 256U * kPrgBankBytes;

bool auxiliary_rom_present(std::size_t prg_size) {
    return prg_size >= 3U * kPrgUnitBytes && std::has_single_bit(prg_size - kPrgUnitBytes);
}

std::size_t main_rom_size(std::size_t prg_size) {
    return auxiliary_rom_present(prg_size) ? prg_size - kPrgUnitBytes : prg_size;
}
} // namespace

// Original implementation from the immutable NESdev mapper 283 article, r7724.
BlockFamily283::BlockFamily283(const INesHeader& header,
                               std::vector<Byte> prg_rom,
                               const std::vector<Byte>&)
    : prg_rom_(std::move(prg_rom)), mirror_(header.mirror_mode),
      main_rom_size_(main_rom_size(prg_rom_.size())),
      has_auxiliary_rom_(auxiliary_rom_present(prg_rom_.size())) {}

Result<void> BlockFamily283::validate_geometry(const INesHeader& header,
                                               std::size_t prg_size,
                                               std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U) {
        return std::unexpected("Mapper 283 supports submapper 0");
    }

    const std::size_t main_size = main_rom_size(prg_size);
    const bool valid_main = std::has_single_bit(main_size) && main_size >= kPrgBankBytes &&
                            main_size <= kMaxMainRomBytes;
    const bool valid_shape = std::has_single_bit(prg_size) || auxiliary_rom_present(prg_size);
    if (!valid_main || !valid_shape || chr_size != 0U) {
        return std::unexpected(
            "Mapper 283 requires power-of-two 32 KiB-8 MiB main PRG, optional repeated "
            "8 KiB auxiliary PRG, and no CHR ROM");
    }
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.chr_ram_size != kLowBankBytes))) {
        return std::unexpected(
            "Mapper 283 requires 8 KiB volatile CHR RAM and no persistent or CPU RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 283 requires fixed H/V mirroring");
    }
    return {};
}

bool BlockFamily283::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte BlockFamily283::read_prg(Address addr) {
    if (addr >= 0x8000U) {
        const std::size_t offset =
            static_cast<std::size_t>(prg_bank_) * kPrgBankBytes + (addr & 0x7FFFU);
        return prg_rom_[offset % main_rom_size_];
    }
    if (addr >= 0x6000U) {
        const std::size_t base = has_auxiliary_rom_
                                     ? prg_rom_.size() - kLowBankBytes
                                     : std::min(main_rom_size_, kFirstBlockBytes) - kLowBankBytes;
        return prg_rom_[base + (addr & 0x1FFFU)];
    }
    return 0U;
}

void BlockFamily283::write_prg(Address addr, Byte value) {
    if (addr >= 0x8000U) {
        prg_bank_ = value;
    }
}

Byte BlockFamily283::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void BlockFamily283::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode BlockFamily283::mirror_mode() const {
    return mirror_;
}

void BlockFamily283::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void BlockFamily283::reset() {
    // Deterministic latch policy; the source does not specify electrical reset behavior.
    prg_bank_ = 0U;
}

uint16_t BlockFamily283::state_tag() const {
    const uint16_t wiring = static_cast<uint16_t>(has_auxiliary_rom_ ? 2U : 0U) |
                            static_cast<uint16_t>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
    return static_cast<uint16_t>(0xFD00U | wiring);
}

void BlockFamily283::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(main_rom_size_));
    writer.write(prg_bank_);
    writer.write_array(chr_ram_);
}

void BlockFamily283::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto saved_main_size = reader.read<uint32_t>();
    const auto bank = reader.read<Byte>();
    std::array<Byte, kLowBankBytes> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || saved_main_size != main_rom_size_) {
        reader.fail();
        return;
    }
    prg_bank_ = bank;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
