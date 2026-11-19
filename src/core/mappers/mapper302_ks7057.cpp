#include "core/mappers/mapper302_ks7057.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomSize = 128U * 1024U;
constexpr std::size_t kBankSize = 2U * 1024U;
constexpr uint16_t kStateTag = 0xAC00U;

} // namespace

// Original implementation from NESdev mapper 302 r22779 and Krzysiobal's KS7057
// PCB trace. The trace establishes six-bit bank latches and the 128 KiB population.
KaiserKs7057::KaiserKs7057(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), initial_mirror_(header.mirror_mode),
      mirror_(header.mirror_mode) {
    power_on();
}

Result<void> KaiserKs7057::validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (!header.is_nes2 || header.submapper != 0U)
        return std::unexpected("Mapper 302 requires NES 2.0 submapper 0");
    if (prg_size != kPrgRomSize || chr_size != 0U)
        return std::unexpected("Mapper 302 requires 128 KiB PRG ROM and no CHR ROM");
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 302 requires only 8 KiB volatile CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 302 requires H/V nametable wiring");
    return {};
}

Byte KaiserKs7057::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr >= 0xC000U)
        return prg_rom_[7U * 16384U + (addr - 0xC000U)];
    if (addr >= 0xA000U)
        return prg_rom_[13U * 8192U + (addr - 0xA000U)];

    const unsigned slot =
        addr < 0x8000U ? 4U + (addr - 0x6000U) / kBankSize : (addr - 0x8000U) / kBankSize;
    return prg_rom_[prg_banks_[slot] * kBankSize + (addr & (kBankSize - 1U))];
}

void KaiserKs7057::write_prg(Address addr, Byte value) {
    if ((addr & 0xE000U) == 0x8000U) {
        mirror_ = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        return;
    }

    const auto reg = addr & 0xF003U;
    unsigned slot = 0U;
    switch (reg & 0xF002U) {
    case 0xB000U:
        slot = 0U;
        break;
    case 0xB002U:
        slot = 1U;
        break;
    case 0xC000U:
        slot = 2U;
        break;
    case 0xC002U:
        slot = 3U;
        break;
    case 0xD000U:
        slot = 4U;
        break;
    case 0xD002U:
        slot = 5U;
        break;
    case 0xE000U:
        slot = 6U;
        break;
    case 0xE002U:
        slot = 7U;
        break;
    default:
        return;
    }

    if ((reg & 1U) == 0U) {
        prg_banks_[slot] = static_cast<Byte>((prg_banks_[slot] & 0x30U) | (value & 0x0FU));
    } else {
        prg_banks_[slot] = static_cast<Byte>((prg_banks_[slot] & 0x0FU) | ((value & 0x03U) << 4U));
    }
}

bool KaiserKs7057::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte KaiserKs7057::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void KaiserKs7057::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode KaiserKs7057::mirror_mode() const {
    return mirror_;
}

void KaiserKs7057::power_on() {
    // Electrical power-up latch and RAM contents are unknown; use deterministic zeroes.
    prg_banks_.fill(0U);
    chr_ram_.fill(0U);
    mirror_ = initial_mirror_;
}

void KaiserKs7057::reset() {
    // The KS7057 board inhibits KS204 reset while Gyruss holds M2 low.
}

uint16_t KaiserKs7057::state_tag() const {
    return static_cast<uint16_t>(kStateTag | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void KaiserKs7057::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write_array(prg_banks_);
    writer.write(static_cast<Byte>(mirror_));
    writer.write_array(chr_ram_);
}

void KaiserKs7057::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        !std::ranges::all_of(banks, [](Byte bank) { return bank <= 0x3FU; }) ||
        mirror > static_cast<Byte>(MirrorMode::Vertical)) {
        reader.fail();
        return;
    }

    prg_banks_ = banks;
    mirror_ = static_cast<MirrorMode>(mirror);
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
