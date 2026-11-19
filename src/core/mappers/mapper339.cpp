#include "core/mappers/mapper339.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankSize = 8192UZ;
constexpr std::size_t kChrBankSize = 1024UZ;
constexpr std::size_t kMinPrgSize = 32768UZ;
constexpr std::size_t kMaxPrgSize = 131072UZ;
constexpr std::size_t kMinChrSize = 8192UZ;
constexpr std::size_t kMaxChrSize = 524288UZ;
constexpr std::size_t kPrgRamSize = 8192UZ;
constexpr std::uint16_t kStateTag = 0x3390U;

} // namespace

// Board decoding follows NESdev mapper 339 r22997. Ordinary MMC3 behavior
// follows r24445. ROM bounds describe the connected address pins, not a survey
// of physical populations. Reset defaults and Sharp-style repeated zero-latch
// IRQs are deterministic compatibility policies; the board article specifies neither.
Mapper339::Mapper339(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.prg_ram_size + header.prg_nvram_size, 0U), nvram_size_(header.prg_nvram_size),
      submapper_(header.submapper), initial_mirror_(header.mirror_mode),
      mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper339::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 339U) {
        return std::unexpected("Mapper 339 requires mapper ID 339");
    }
    if (!header.is_nes2 || header.submapper > 4U) {
        return std::unexpected("Mapper 339 requires NES 2.0 submapper 0 through 4");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize ||
        !std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected(
            "Mapper 339 requires power-of-two 32-128 KiB PRG and 8-512 KiB CHR ROM");
    }
    const bool valid_ram = (header.prg_ram_size == 0U || header.prg_ram_size == kPrgRamSize) &&
                           (header.prg_nvram_size == 0U || header.prg_nvram_size == kPrgRamSize) &&
                           !(header.prg_ram_size != 0U && header.prg_nvram_size != 0U);
    if (!valid_ram || header.has_battery != (header.prg_nvram_size != 0U) ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 339 supports one optional 8 KiB PRG RAM/NVRAM chip and no CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 339 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

bool Mapper339::nrom256_selected() const {
    switch (submapper_) {
    case 0U:
        return (outer_ & 0x06U) == 0x06U;
    case 1U:
        return (outer_ & 0x04U) != 0U;
    case 2U:
        return (outer_ & 0x11U) != 0U;
    case 3U:
        return (outer_ & 0x18U) != 0U;
    default:
        return (outer_ & 0x14U) != 0U;
    }
}

Byte Mapper339::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!prg_ram_enabled_ || prg_ram_.empty()) {
            return 0U;
        }
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U) {
        return 0U;
    }

    std::size_t offset = 0U;
    if ((outer_ & 0x20U) == 0U) {
        const std::size_t outer = static_cast<std::size_t>((outer_ >> 1U) & 3U) * 32768UZ;
        const std::size_t a14 =
            nrom256_selected() ? addr & 0x4000U : static_cast<std::size_t>(outer_ & 1U) << 14U;
        offset = outer + a14 + (addr & 0x3FFFU);
    } else {
        const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
        const bool mode = (bank_select_ & 0x40U) != 0U;
        unsigned bank = 0U;
        switch (slot) {
        case 0U:
            bank = mode ? 14U : bank_registers_[6U] & 0x0FU;
            break;
        case 1U:
            bank = bank_registers_[7U] & 0x0FU;
            break;
        case 2U:
            bank = mode ? bank_registers_[6U] & 0x0FU : 14U;
            break;
        default:
            bank = 15U;
            break;
        }
        offset = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    if ((outer_ & 0x80U) != 0U) {
        offset = (offset & ~3UZ) | menu_pad_;
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper339::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_) {
            outer_ = addr & 0xBFU;
            if (!prg_ram_.empty()) {
                prg_ram_[addr & 0x1FFFU] = value;
            }
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 0xC7U;
        break;
    case 0x8001U: {
        const unsigned index = bank_select_ & 7U;
        bank_registers_[index] = index >= 6U ? value & 0x3FU : value;
        break;
    }
    case 0xA000U:
        mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        prg_ram_enabled_ = (value & 0x80U) != 0U;
        prg_ram_write_protected_ = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        irq_latch_ = value;
        break;
    case 0xC001U:
        irq_counter_ = 0U;
        irq_reload_ = true;
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

bool Mapper339::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return true;
    }
    return addr >= 0x6000U && addr < 0x8000U && prg_ram_enabled_ && !prg_ram_.empty();
}

Byte Mapper339::chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot = ((pattern_addr >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return bank_registers_[slot - 2U];
}

Byte Mapper339::read_chr(Address addr) {
    const std::size_t outer = static_cast<std::size_t>((outer_ >> 3U) & 3U) * 131072UZ;
    const auto offset = outer + static_cast<std::size_t>(chr_bank(addr) & 0x7FU) * kChrBankSize +
                        (addr & (kChrBankSize - 1U));
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper339::write_chr(Address, Byte) {}

MirrorMode Mapper339::mirror_mode() const {
    return mirror_;
}

Result<void> Mapper339::configure_board_jumpers(Byte value) {
    if (value > 3U) {
        return std::unexpected("Mapper 339 menu solder pads accept values 0 through 3");
    }
    menu_pad_ = value;
    return {};
}

void Mapper339::power_on() {
    reset();
    if (nvram_size_ == 0U) {
        std::ranges::fill(prg_ram_, 0U);
    }
}

void Mapper339::reset() {
    outer_ = 0U;
    mirror_ = initial_mirror_;
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
}

bool Mapper339::irq_pending() const {
    return irq_pending_;
}

void Mapper339::acknowledge_irq() {
    irq_pending_ = false;
}

void Mapper339::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_) {
        irq_counter_ = irq_latch_;
        irq_reload_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_) {
        irq_pending_ = true;
    }
}

std::uint16_t Mapper339::memory_state_tag() const {
    return static_cast<std::uint16_t>(submapper_ | (prg_ram_.empty() ? 0U : 0x08U) |
                                      (nvram_size_ != 0U ? 0x10U : 0U) |
                                      (initial_mirror_ == MirrorMode::Vertical ? 0x20U : 0U) |
                                      (static_cast<unsigned>(menu_pad_) << 6U));
}

void Mapper339::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_state_tag());
    writer.write(outer_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
    writer.write_bytes(prg_ram_);
}

void Mapper339::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory_tag = reader.read<std::uint16_t>();
    const auto outer = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> bank_registers{};
    reader.read_array(bank_registers);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();
    std::vector<Byte> prg_ram;
    reader.read_bytes(prg_ram, kPrgRamSize);

    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory_tag != memory_state_tag() || (outer & 0x40U) != 0U ||
        (bank_select & 0x38U) != 0U || bank_registers[6U] > 0x3FU || bank_registers[7U] > 0x3FU ||
        mirror > 1U || irq_flags > 7U || ((irq_flags & 4U) != 0U && (irq_flags & 2U) == 0U) ||
        (ram_flags & 0x3FU) != 0U || prg_ram.size() != prg_ram_.size()) {
        reader.fail();
        return;
    }

    outer_ = outer;
    bank_select_ = bank_select;
    bank_registers_ = bank_registers;
    mirror_ = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    irq_counter_ = irq_counter;
    irq_latch_ = irq_latch;
    irq_reload_ = (irq_flags & 1U) != 0U;
    irq_enabled_ = (irq_flags & 2U) != 0U;
    irq_pending_ = (irq_flags & 4U) != 0U;
    prg_ram_enabled_ = (ram_flags & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram_flags & 0x40U) != 0U;
    prg_ram_ = std::move(prg_ram);
}

std::span<const Byte> Mapper339::battery_ram() const {
    return std::span<const Byte>(prg_ram_).first(nvram_size_);
}

void Mapper339::set_battery_ram(std::span<const Byte> data) {
    if (data.size() != nvram_size_) {
        return;
    }
    std::ranges::copy(data, prg_ram_.begin());
}

} // namespace mapperbus::core
