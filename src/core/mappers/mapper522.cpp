#include "core/mappers/mapper522.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kPrgRamSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::uint16_t kStateTag = 0x5220U;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 522 oldid 8112.
// Reset is unspecified: clear registers like the shared MMC3 policy, retain
// volatile RAM on soft reset, and clear both RAM arrays on cold power.
Mapper522::Mapper522(const INesHeader& header,
                     std::vector<Byte> prg_rom,
                     std::vector<Byte> /*chr_rom*/)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper522::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 522U) {
        return std::unexpected("Mapper 522 requires mapper ID 522");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 522 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxPrgSize || prg_size % kPrgBankSize != 0U ||
        chr_size != 0U) {
        return std::unexpected(
            "Mapper 522 requires 32 KiB-2 MiB of 8 KiB-banked PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kPrgRamSize || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 522 requires exactly 8 KiB volatile PRG RAM and 8 KiB volatile CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 522 requires ordinary hard-wired H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 522 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper522::read_prg(Address addr) {
    if (addr >= 0xC000U && addr < 0xE000U) {
        return prg_ram_[addr & (kPrgRamSize - 1U)];
    }
    if (addr < 0x6000U || prg_rom_.empty()) {
        return 0U;
    }

    std::size_t offset = 0U;
    if (addr < 0x8000U) {
        offset = prg_rom_.size() - 2UZ * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    } else if (addr < 0xC000U) {
        const auto bank = prg_banks_[(addr >> 13U) & 1U];
        offset = (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U))) %
                 prg_rom_.size();
    } else if (addr >= 0xE000U) {
        offset = prg_rom_.size() - kPrgBankSize + (addr & (kPrgBankSize - 1U));
    } else {
        return 0U;
    }
    return prg_rom_[offset];
}

void Mapper522::write_prg(Address addr, Byte value) {
    if (addr >= 0xC000U && addr < 0xE000U) {
        prg_ram_[addr & (kPrgRamSize - 1U)] = value;
        return;
    }
    if (addr >= 0x8000U && addr < 0xA000U) {
        if ((addr & 1U) == 0U) {
            bank_select_ = value;
        } else if (bank_select_ == 6U || bank_select_ == 7U) {
            prg_banks_[bank_select_ - 6U] = value;
        }
    }
}

bool Mapper522::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper522::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper522::read_chr(Address addr) {
    return addr < kChrRamSize ? chr_ram_[addr] : 0U;
}

void Mapper522::write_chr(Address addr, Byte value) {
    if (addr < kChrRamSize) {
        chr_ram_[addr] = value;
    }
}

MirrorMode Mapper522::mirror_mode() const {
    return initial_mirror_;
}

void Mapper522::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper522::reset() {
    bank_select_ = 0U;
    prg_banks_.fill(0U);
}

std::uint16_t Mapper522::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper522::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write<std::uint32_t>(0U);
    writer.write(bank_select_);
    writer.write_array(prg_banks_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper522::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 2> prg_banks{};
    reader.read_array(prg_banks);
    std::array<Byte, kPrgRamSize> prg_ram{};
    reader.read_array(prg_ram);
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || chr_size != 0U) {
        reader.fail();
        return;
    }

    bank_select_ = bank_select;
    prg_banks_ = prg_banks;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
