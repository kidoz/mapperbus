#include "core/mappers/mapper326.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomBytes = 131072U;
constexpr std::size_t kChrRomBytes = 131072U;
constexpr std::size_t kPrgBankBytes = 8192U;
constexpr std::size_t kChrBankBytes = 1024U;
constexpr std::uint16_t kStateTag = 0x3260U;

} // namespace

// Original implementation from NESdev r7837 and its linked PCB report, post
// 218722. Zeroed reset latches are a deterministic policy; reset is undocumented.
Mapper326::Mapper326(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> Mapper326::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 326U) {
        return std::unexpected("Mapper 326 requires mapper ID 326");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 326 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgRomBytes || chr_size != kChrRomBytes) {
        return std::unexpected("Mapper 326 requires 128 KiB PRG and 128 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 326 supports ROM-only board memory");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 326 requires ordinary H/V nametable metadata");
    }
    return {};
}

Byte Mapper326::read_prg(Address addr) {
    if (addr < 0x8000U) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const unsigned bank = slot < 3U ? prg_banks_[slot] & 0x0FU : 0x0FU;
    const std::size_t offset =
        static_cast<std::size_t>(bank) * kPrgBankBytes + (addr & (kPrgBankBytes - 1U));
    return prg_rom_[offset];
}

void Mapper326::write_prg(Address addr, Byte value) {
    const Address low_register = addr & 0x801FU;
    if (low_register >= 0x8010U && low_register <= 0x801BU) {
        const auto index = static_cast<unsigned>(low_register & 0x0FU);
        if (index < chr_banks_.size()) {
            chr_banks_[index] = value & 0x7FU;
        } else {
            nametable_banks_[index - chr_banks_.size()] = value & 1U;
        }
        return;
    }

    switch (addr & 0xE010U) {
    case 0x8000U:
        prg_banks_[0] = value & 0x7FU;
        break;
    case 0xA000U:
        prg_banks_[1] = value & 0x7FU;
        break;
    case 0xC000U:
        prg_banks_[2] = value & 0x7FU;
        break;
    default:
        break;
    }
}

Byte Mapper326::read_chr(Address addr) {
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    const std::size_t offset =
        static_cast<std::size_t>(chr_banks_[slot]) * kChrBankBytes + (addr & (kChrBankBytes - 1U));
    return chr_rom_[offset];
}

void Mapper326::write_chr(Address, Byte) {}

MirrorMode Mapper326::mirror_mode() const {
    return MirrorMode::SingleLower;
}

std::optional<Address> Mapper326::nametable_address(Address addr) const {
    if (addr < 0x2000U || addr >= 0x3F00U) {
        return std::nullopt;
    }
    const auto table = static_cast<unsigned>(((addr - 0x2000U) >> 10U) & 3U);
    return static_cast<Address>(nametable_banks_[table] * 0x0400U + (addr & 0x03FFU));
}

void Mapper326::power_on() {
    reset();
}

void Mapper326::reset() {
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    nametable_banks_.fill(0U);
}

void Mapper326::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write_array(nametable_banks_);
}

void Mapper326::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    std::array<Byte, 3> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    std::array<Byte, 4> nametable_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    reader.read_array(nametable_banks);

    const auto valid_latch = [](Byte value) { return value <= 0x7FU; };
    const auto valid_ciram = [](Byte value) { return value <= 1U; };
    if (!reader.ok() || tag != kStateTag ||
        !std::all_of(prg_banks.begin(), prg_banks.end(), valid_latch) ||
        !std::all_of(chr_banks.begin(), chr_banks.end(), valid_latch) ||
        !std::all_of(nametable_banks.begin(), nametable_banks.end(), valid_ciram)) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    nametable_banks_ = nametable_banks;
}

} // namespace mapperbus::core
