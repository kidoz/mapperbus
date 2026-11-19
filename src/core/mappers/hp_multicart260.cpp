#include "core/mappers/hp_multicart260.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kMaxPrgBytes = 1024U * 1024U;
constexpr std::size_t kMaxChrBytes = 1024U * 1024U;
} // namespace

// NESdev mapper 260 r22170 and its linked MMC3/FK23C hardware descriptions.
HpMulticart260::HpMulticart260(const INesHeader& header,
                               std::vector<Byte> prg_rom,
                               std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode), mmc3_mirror_(header.mirror_mode),
      battery_backed_(header.has_battery) {
    reset();
}

Result<void> HpMulticart260::validate_geometry(const INesHeader& header,
                                               std::size_t prg_size,
                                               std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 260 supports submapper 0");
    if (prg_size < 16384U || prg_size > kMaxPrgBytes || prg_size % 16384U != 0U ||
        chr_size < 8192U || chr_size > kMaxChrBytes || chr_size % 8192U != 0U)
        return std::unexpected(
            "Mapper 260 requires 16 KiB-aligned PRG and 8 KiB-aligned CHR ROM up to 1 MiB each");
    const auto ram_size = header.prg_ram_size + header.prg_nvram_size;
    if (ram_size != 8192U || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && ((header.prg_nvram_size != 0U) != header.has_battery)))
        return std::unexpected("Mapper 260 requires exactly 8 KiB PRG RAM and no CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 260 requires ordinary H/V CIRAM");
    return {};
}

unsigned HpMulticart260::outer_prg_bank(unsigned inner) const {
    const unsigned mask = (mode_ & 2U) != 0U ? 0x0FU : 0x1FU;
    return (inner & mask) | ((static_cast<unsigned>(prg_base_) << 1U) & ~mask);
}

unsigned HpMulticart260::outer_chr_bank(unsigned inner) const {
    const unsigned mask = (mode_ & 1U) != 0U ? 0x7FU : 0xFFU;
    return (inner & mask) | ((static_cast<unsigned>(chr_base_) << 3U) & ~mask);
}

unsigned HpMulticart260::prg_bank(unsigned slot) const {
    if ((mode_ & 4U) != 0U) {
        if ((mode_ & 3U) == 0U)
            return (static_cast<unsigned>(prg_base_) << 1U) | (slot & 1U);
        return ((static_cast<unsigned>(prg_base_) & 0x3EU) << 1U) | (slot >> 1U);
    }
    const unsigned mask = (mode_ & 2U) != 0U ? 0x0FU : 0x1FU;
    unsigned inner = 0U;
    switch (slot) {
    case 0U:
        inner = prg_mode_ ? mask - 1U : bank_registers_[6U];
        break;
    case 1U:
        inner = bank_registers_[7U];
        break;
    case 2U:
        inner = prg_mode_ ? bank_registers_[6U] : mask - 1U;
        break;
    default:
        inner = mask;
        break;
    }
    return outer_prg_bank(inner);
}

unsigned HpMulticart260::chr_bank(Address addr) const {
    const unsigned slot = (static_cast<unsigned>(addr) >> 10U) & 7U;
    if ((mode_ & 4U) != 0U) {
        unsigned base = chr_base_;
        if ((mode_ & 3U) >= 2U) {
            const unsigned mask = (mode_ & 3U) == 2U ? 1U : 3U;
            base = (cnrom_latch_ & mask) | (chr_base_ & ~mask);
        }
        return (base << 3U) | slot;
    }

    unsigned logical_slot = slot;
    if (chr_inversion_)
        logical_slot ^= 4U;
    unsigned inner = 0U;
    if (logical_slot < 4U)
        inner = (bank_registers_[logical_slot / 2U] & 0xFEU) | (logical_slot & 1U);
    else
        inner = bank_registers_[logical_slot - 2U];
    return outer_chr_bank(inner);
}

Byte HpMulticart260::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return ram_enabled_ ? prg_ram_[addr - 0x6000U] : 0U;
    if (addr < 0x8000U)
        return 0U;
    const auto slot = static_cast<unsigned>(addr - 0x8000U) / 8192U;
    const auto offset = prg_bank(slot) * 8192U + ((addr - 0x8000U) & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

void HpMulticart260::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (ram_enabled_ && !ram_write_protected_)
            prg_ram_[addr - 0x6000U] = value;
        return;
    }
    if (addr < 0x8000U)
        return;

    if ((mode_ & 4U) != 0U)
        cnrom_latch_ = value & 7U;

    switch (addr & 0xE001U) {
    case 0x8000U:
        bank_select_ = value & 7U;
        prg_mode_ = (value & 0x40U) != 0U;
        chr_inversion_ = (value & 0x80U) != 0U;
        break;
    case 0x8001U:
        bank_registers_[bank_select_] = bank_select_ >= 6U ? value & 0x3FU : value;
        break;
    case 0xA000U:
        mmc3_mirror_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        ram_enabled_ = (value & 0x80U) != 0U;
        ram_write_protected_ = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        irq_reload_ = value;
        break;
    case 0xC001U:
        irq_counter_ = 0U;
        irq_reload_pending_ = true;
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

bool HpMulticart260::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && ram_enabled_);
}

Byte HpMulticart260::read_chr(Address addr) {
    const auto offset = chr_bank(addr) * 1024U + (addr & 0x3FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void HpMulticart260::write_chr(Address, Byte) {}

MirrorMode HpMulticart260::mirror_mode() const {
    if ((mode_ & 4U) != 0U)
        return (cnrom_latch_ & 4U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    return mmc3_mirror_;
}

bool HpMulticart260::maps_expansion(Address addr) const {
    return addr == 0x5000U;
}

Byte HpMulticart260::expansion_read_mask(Address addr) const {
    return addr == 0x5000U ? 0x03U : 0U;
}

Byte HpMulticart260::read_expansion(Address addr) {
    return addr == 0x5000U ? dip_switches_ : 0U;
}

void HpMulticart260::write_expansion(Address addr, Byte value) {
    if ((mode_ & 0x80U) != 0U)
        return;
    switch (addr & 0xF003U) {
    case 0x5000U:
        mode_ = value & 0x87U;
        break;
    case 0x5001U:
        prg_base_ = value & 0x3FU;
        break;
    case 0x5002U:
        chr_base_ = value & 0x7FU;
        break;
    default:
        break;
    }
}

Result<void> HpMulticart260::configure_board_jumpers(Byte value) {
    if (value > 3U)
        return std::unexpected("Mapper 260 DIP switches accept values 0 through 3");
    dip_switches_ = value;
    return {};
}

bool HpMulticart260::irq_pending() const {
    return irq_pending_;
}

void HpMulticart260::acknowledge_irq() {
    irq_pending_ = false;
}

void HpMulticart260::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_pending_) {
        irq_counter_ = irq_reload_;
        irq_reload_pending_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

void HpMulticart260::power_on() {
    reset();
    if (!battery_backed_)
        prg_ram_.fill(0U);
}

void HpMulticart260::reset() {
    mode_ = 0U;
    prg_base_ = 0U;
    chr_base_ = 0U;
    cnrom_latch_ = 0U;
    bank_select_ = 0U;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0U);
    mmc3_mirror_ = initial_mirror_;
    ram_enabled_ = true;
    ram_write_protected_ = false;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_pending_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

uint16_t HpMulticart260::state_tag() const {
    return static_cast<uint16_t>(0x2600U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U) |
                                 (battery_backed_ ? 2U : 0U) | (dip_switches_ << 2U));
}

void HpMulticart260::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write_array(prg_ram_);
    writer.write<Byte>((chr_inversion_ ? 0x80U : 0U) | (prg_mode_ ? 0x40U : 0U) | bank_select_);
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mmc3_mirror_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_pending_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((ram_enabled_ ? 0x80U : 0U) | (ram_write_protected_ ? 0x40U : 0U));
    writer.write(mode_);
    writer.write(prg_base_);
    writer.write(chr_base_);
    writer.write(cnrom_latch_);
}

void HpMulticart260::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    std::array<Byte, 8192> ram{};
    reader.read_array(ram);
    const auto control = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_reload = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    const auto prg_base = reader.read<Byte>();
    const auto chr_base = reader.read<Byte>();
    const auto cnrom_latch = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || (control & 0x38U) != 0U ||
        (banks[6U] & 0xC0U) != 0U || (banks[7U] & 0xC0U) != 0U ||
        mirror > static_cast<Byte>(MirrorMode::Vertical) || irq_flags > 7U ||
        ((irq_flags & 4U) != 0U && (irq_flags & 2U) == 0U) || (ram_flags & 0x3FU) != 0U ||
        (mode & 0x78U) != 0U || prg_base > 0x3FU || chr_base > 0x7FU || cnrom_latch > 7U) {
        reader.fail();
        return;
    }

    prg_ram_ = ram;
    bank_select_ = control & 7U;
    prg_mode_ = (control & 0x40U) != 0U;
    chr_inversion_ = (control & 0x80U) != 0U;
    bank_registers_ = banks;
    mmc3_mirror_ = static_cast<MirrorMode>(mirror);
    irq_counter_ = irq_counter;
    irq_reload_ = irq_reload;
    irq_reload_pending_ = (irq_flags & 1U) != 0U;
    irq_enabled_ = (irq_flags & 2U) != 0U;
    irq_pending_ = (irq_flags & 4U) != 0U;
    ram_enabled_ = (ram_flags & 0x80U) != 0U;
    ram_write_protected_ = (ram_flags & 0x40U) != 0U;
    mode_ = mode;
    prg_base_ = prg_base;
    chr_base_ = chr_base;
    cnrom_latch_ = cnrom_latch;
}

std::span<const Byte> HpMulticart260::battery_ram() const {
    return battery_backed_ ? std::span<const Byte>(prg_ram_) : std::span<const Byte>{};
}

void HpMulticart260::set_battery_ram(std::span<const Byte> data) {
    if (battery_backed_ && data.size() == prg_ram_.size())
        std::ranges::copy(data, prg_ram_.begin());
}

} // namespace mapperbus::core
