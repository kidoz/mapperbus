#include "core/mappers/mapper307.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomBytes = 131072U;
constexpr std::size_t kRamBankBytes = 4096U;
constexpr std::size_t kPrgBankBytes = 8192U;
constexpr std::uint16_t kStateTag = 0x3070U;

} // namespace

// Original implementation from NESdev r7791 and the linked KS-7037 schematic
// report (posts 242196, 242631 and 242644).
Mapper307::Mapper307(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper307::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2) {
        return std::unexpected("Mapper 307 requires a NES 2.0 header");
    }
    if (header.submapper != 0U) {
        return std::unexpected("Mapper 307 supports submapper 0");
    }
    if (prg_size != kPrgRomBytes || chr_size != 0U) {
        return std::unexpected("Mapper 307 requires 128 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 8192U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 8192U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 307 requires 8 KiB volatile PRG RAM and 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 307 requires H/V CIRAM metadata");
    }
    return {};
}

bool Mapper307::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper307::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x7000U) {
        return prg_ram_[addr - 0x6000U];
    }
    if (addr >= 0xB000U && addr < 0xC000U) {
        return prg_ram_[kRamBankBytes + addr - 0xB000U];
    }

    std::size_t offset = 0U;
    if (addr >= 0x7000U && addr < 0x8000U) {
        offset = 15U * kRamBankBytes + (addr & 0x0FFFU);
    } else if (addr >= 0x8000U && addr < 0xA000U) {
        offset = static_cast<std::size_t>(banks_[4U]) * kPrgBankBytes + (addr & 0x1FFFU);
    } else if (addr >= 0xA000U && addr < 0xB000U) {
        offset = 28U * kRamBankBytes + (addr & 0x0FFFU);
    } else if (addr >= 0xC000U && addr < 0xE000U) {
        offset = static_cast<std::size_t>(banks_[5U]) * kPrgBankBytes + (addr & 0x1FFFU);
    } else if (addr >= 0xE000U) {
        offset = 15U * kPrgBankBytes + (addr & 0x1FFFU);
    } else {
        return 0U;
    }
    return prg_rom_[offset];
}

void Mapper307::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x7000U) {
        prg_ram_[addr - 0x6000U] = value;
        return;
    }
    if (addr >= 0xB000U && addr < 0xC000U) {
        prg_ram_[kRamBankBytes + addr - 0xB000U] = value;
        return;
    }
    if (addr < 0x8000U || addr >= 0xA000U) {
        return;
    }
    if ((addr & 1U) == 0U) {
        bank_select_ = value & 7U;
        return;
    }

    switch (bank_select_) {
    case 2U:
    case 3U:
    case 4U:
    case 5U:
        banks_[bank_select_ - 2U] = value & 1U;
        break;
    case 6U:
    case 7U:
        banks_[bank_select_ - 2U] = value & 0x0FU;
        break;
    default:
        break;
    }
}

Byte Mapper307::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper307::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper307::mirror_mode() const {
    return MirrorMode::SingleLower;
}

std::optional<Address> Mapper307::nametable_address(Address addr) const {
    if (addr < 0x2000U || addr >= 0x3F00U) {
        return std::nullopt;
    }
    constexpr std::array<unsigned, 4> kRegisterSlots{0U, 2U, 1U, 3U};
    const auto table = static_cast<unsigned>((addr - 0x2000U) & 0x0FFFU) >> 10U;
    return static_cast<Address>(banks_[kRegisterSlots[table]] * 0x0400U + (addr & 0x03FFU));
}

void Mapper307::power_on() {
    // Cold RAM contents are a deterministic policy; the reset input clears no RAM.
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
    reset();
}

void Mapper307::reset() {
    bank_select_ = 0U;
    banks_.fill(0U);
}

void Mapper307::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(bank_select_);
    writer.write_array(banks_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper307::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 6> banks{};
    std::array<Byte, 8192> prg_ram{};
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(banks);
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);

    const bool valid_ciram =
        std::all_of(banks.begin(), banks.begin() + 4, [](Byte bank) { return bank <= 1U; });
    const bool valid_prg =
        std::all_of(banks.begin() + 4, banks.end(), [](Byte bank) { return bank <= 15U; });
    if (!reader.ok() || tag != kStateTag || bank_select > 7U || !valid_ciram || !valid_prg) {
        reader.fail();
        return;
    }

    bank_select_ = bank_select;
    banks_ = banks;
    prg_ram_ = std::move(prg_ram);
    chr_ram_ = std::move(chr_ram);
}

} // namespace mapperbus::core
