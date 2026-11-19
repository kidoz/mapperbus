#include "core/mappers/mapper047.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kBlockSize = 128U * 1024U;
constexpr std::size_t kPrgSize = 2U * kBlockSize;
constexpr std::size_t kChrSize = 2U * kBlockSize;
} // namespace

// Original implementation from NESdev mapper 047 r4284 and MMC3 r24445.
Mmc3Multicart47::Mmc3Multicart47(const INesHeader& header,
                                 std::vector<Byte> prg_rom,
                                 std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode) {
    reset();
}

Result<void> Mmc3Multicart47::validate_geometry(const INesHeader& header,
                                                std::size_t prg_size,
                                                std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 047 supports submapper 0");
    if (prg_size != kPrgSize || chr_size != kChrSize)
        return std::unexpected("Mapper 047 requires 256 KiB PRG and 256 KiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 047 has no PRG RAM, CHR RAM, or battery storage");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 047 requires H/V startup mirroring");
    return {};
}

bool Mmc3Multicart47::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

Byte Mmc3Multicart47::prg_bank(unsigned slot) const {
    const Byte r6 = bank_registers_[6] & 0x0FU;
    const Byte r7 = bank_registers_[7] & 0x0FU;
    const bool swapped = (bank_select_ & 0x40U) != 0U;
    constexpr std::array<Byte, 2> kFixed = {14U, 15U};
    if (slot == 1U)
        return r7;
    if (slot == 3U)
        return kFixed[1];
    if ((slot == 0U) == !swapped)
        return r6;
    return kFixed[0];
}

Byte Mmc3Multicart47::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const unsigned slot = (addr - 0x8000U) >> 13U;
    const std::size_t offset = static_cast<std::size_t>(block_) * kBlockSize +
                               static_cast<std::size_t>(prg_bank(slot)) * 0x2000U +
                               (addr & 0x1FFFU);
    return prg_rom_[offset];
}

void Mmc3Multicart47::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_)
            block_ = value & 1U;
        return;
    }
    if (addr < 0x8000U)
        return;

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 0xC7U;
        break;
    case 0x8001U: {
        const unsigned index = bank_select_ & 7U;
        bank_registers_[index] = index < 6U ? value : value & 0x3FU;
        break;
    }
    case 0xA000U:
        mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        prg_ram_enabled_ = (value & 0x80U) != 0U;
        prg_ram_write_protected_ = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        irq_reload_ = value;
        break;
    case 0xC001U:
        irq_counter_ = 0U;
        irq_reload_flag_ = true;
        break;
    case 0xE000U:
        irq_enabled_ = false;
        irq_pending_ = false;
        break;
    case 0xE001U:
        irq_enabled_ = true;
        break;
    default:
        break;
    }
}

Byte Mmc3Multicart47::chr_bank(Address addr) const {
    unsigned slot = (addr >> 10U) & 7U;
    if ((bank_select_ & 0x80U) != 0U)
        slot ^= 4U;
    if (slot < 4U)
        return (bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U);
    return bank_registers_[slot - 2U];
}

Byte Mmc3Multicart47::read_chr(Address addr) {
    const std::size_t offset = static_cast<std::size_t>(block_) * kBlockSize +
                               static_cast<std::size_t>(chr_bank(addr) & 0x7FU) * 0x400U +
                               (addr & 0x3FFU);
    return chr_rom_[offset];
}

void Mmc3Multicart47::write_chr(Address, Byte) {}

MirrorMode Mmc3Multicart47::mirror_mode() const {
    return mirror_mode_;
}

void Mmc3Multicart47::reset() {
    block_ = 0U;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
}

void Mmc3Multicart47::power_on() {
    reset();
}

bool Mmc3Multicart47::irq_pending() const {
    return irq_pending_;
}

void Mmc3Multicart47::acknowledge_irq() {
    irq_pending_ = false;
}

void Mmc3Multicart47::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

uint16_t Mmc3Multicart47::state_tag() const {
    return static_cast<uint16_t>(0x2F00U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mmc3Multicart47::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(block_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
}

void Mmc3Multicart47::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto block = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_reload = reader.read<Byte>();
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();

    const bool valid = reader.ok() && tag == state_tag() && block <= 1U &&
                       (bank_select & 0x38U) == 0U && bank_registers[6] <= 0x3FU &&
                       bank_registers[7] <= 0x3FU &&
                       mirror <= static_cast<Byte>(MirrorMode::Vertical) && irq <= 7U &&
                       ((irq & 4U) == 0U || (irq & 2U) != 0U) && (ram & 0x3FU) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }

    block_ = block;
    bank_select_ = bank_select;
    bank_registers_ = bank_registers;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = irq_counter;
    irq_reload_ = irq_reload;
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    prg_ram_enabled_ = (ram & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram & 0x40U) != 0U;
}

} // namespace mapperbus::core
