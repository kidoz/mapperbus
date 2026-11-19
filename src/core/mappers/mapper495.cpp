#include "core/mappers/mapper495.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kRomSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::uint16_t kStateTag = 0x4950U;
constexpr Byte kPrgBankMask = 0x0FU;
constexpr Byte kChrRegisterMask = 0xDFU;

} // namespace

// Original implementation from NESdev NES 2.0 Mapper 495 oldid 22800.
// Reset is unspecified; zeroed banks and FD latches are a deterministic policy.
Mapper495::Mapper495(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper495::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 495U) {
        return std::unexpected("Mapper 495 requires mapper ID 495");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 495 requires NES 2.0 submapper 0");
    }
    if (prg_size != kRomSize || chr_size != kRomSize) {
        return std::unexpected("Mapper 495 requires exactly 128 KiB PRG ROM and 128 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 495 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 495 requires ordinary H/V initial CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 495 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper495::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    std::size_t offset = 0UZ;
    if (slot < 3U) {
        offset = static_cast<std::size_t>(registers_.prg_banks[slot]) * kPrgBankSize +
                 (addr & (kPrgBankSize - 1U));
    } else {
        if (prg_rom_.size() < kPrgBankSize) {
            return 0U;
        }
        offset = prg_rom_.size() - kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    return offset < prg_rom_.size() ? prg_rom_[offset] : 0U;
}

void Mapper495::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        return;
    }
    switch (addr & 0xE000U) {
    case 0x8000U:
        registers_.prg_banks[0] = value & kPrgBankMask;
        return;
    case 0xA000U:
        registers_.prg_banks[1] = value & kPrgBankMask;
        return;
    case 0xC000U:
        registers_.prg_banks[2] = value & kPrgBankMask;
        return;
    default:
        break;
    }

    const auto bank = value & kChrRegisterMask;
    switch (addr & 0xEC00U) {
    case 0xE000U:
        registers_.chr_banks[0] = bank;
        break;
    case 0xE400U:
        registers_.chr_banks[2] = bank;
        break;
    case 0xE800U:
        registers_.chr_banks[1] = bank;
        break;
    case 0xEC00U:
        registers_.chr_banks[3] = bank;
        break;
    default:
        break;
    }
}

bool Mapper495::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mapper495::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper495::selected_chr_bank(unsigned half) const {
    const auto index = static_cast<std::size_t>(registers_.latches[half]) * 2UZ + half;
    return registers_.chr_banks[index] & 0x1FU;
}

Byte Mapper495::read_chr(Address addr) {
    if (addr >= 0x2000U) {
        return 0U;
    }
    const auto half = static_cast<unsigned>((addr >> 12U) & 1U);
    const auto offset = static_cast<std::size_t>(selected_chr_bank(half)) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return offset < chr_rom_.size() ? chr_rom_[offset] : 0U;
}

void Mapper495::write_chr(Address, Byte) {}

Byte Mapper495::selected_ciram_mode(bool upper) const {
    const auto half = static_cast<unsigned>(upper);
    const auto index = static_cast<std::size_t>(registers_.latches[half]) * 2UZ + half;
    return registers_.chr_banks[index] >> 6U;
}

MirrorMode Mapper495::mirror_mode() const {
    constexpr std::array<MirrorMode, 4> kModes{MirrorMode::SingleLower,
                                               MirrorMode::Vertical,
                                               MirrorMode::Horizontal,
                                               MirrorMode::SingleUpper};
    return kModes[selected_ciram_mode(false)];
}

std::optional<Address> Mapper495::nametable_address(Address addr) const {
    if (addr < 0x2000U || addr >= 0x3F00U) {
        return std::nullopt;
    }
    const auto table = static_cast<unsigned>((addr >> 10U) & 3U);
    unsigned page = 0U;
    switch (selected_ciram_mode(addr >= 0x3000U)) {
    case 0U:
        page = table == 3U ? 1U : 0U;
        break;
    case 1U:
        page = table & 1U;
        break;
    case 2U:
        page = table >> 1U;
        break;
    default:
        page = 1U;
        break;
    }
    return static_cast<Address>(page * 0x0400U + (addr & 0x03FFU));
}

bool Mapper495::observes_ppu_reads() const {
    return true;
}

void Mapper495::observe_ppu_read(Address addr) {
    const auto trigger = static_cast<Address>(addr & 0x1FF0U);
    if (trigger != 0x0FD0U && trigger != 0x0FE0U && trigger != 0x1FD0U && trigger != 0x1FE0U) {
        return;
    }
    const auto half = static_cast<unsigned>((trigger >> 12U) & 1U);
    registers_.latches[half] = trigger == 0x0FE0U || trigger == 0x1FE0U ? 1U : 0U;
}

void Mapper495::reset() {
    registers_ = Registers{};
}

void Mapper495::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write_array(registers_.prg_banks);
    writer.write_array(registers_.chr_banks);
    writer.write_array(registers_.latches);
}

void Mapper495::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read_array(next.prg_banks);
    reader.read_array(next.chr_banks);
    reader.read_array(next.latches);

    const auto valid_prg_bank = [](Byte bank) { return (bank & ~kPrgBankMask) == 0U; };
    const auto valid_chr_bank = [](Byte bank) { return (bank & ~kChrRegisterMask) == 0U; };
    const auto valid_latch = [](Byte latch) { return latch <= 1U; };
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !std::ranges::all_of(next.prg_banks, valid_prg_bank) ||
        !std::ranges::all_of(next.chr_banks, valid_chr_bank) ||
        !std::ranges::all_of(next.latches, valid_latch)) {
        reader.fail();
        return;
    }

    registers_ = next;
}

} // namespace mapperbus::core
