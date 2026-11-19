#include "core/mappers/mapper224.hpp"

#include <algorithm>
#include <bit>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kWramSize = 8192U;
constexpr std::size_t kMaxChrRamSize = 262144U;
constexpr std::size_t kMaxPrgRomSize = 33554432U;
constexpr uint16_t kStateTag = 0xE000U;

void set_pin(uint32_t& page, unsigned pin, bool high) {
    const auto mask = uint32_t{1U} << (pin - 13U);
    page = (page & ~mask) | (high ? mask : 0U);
}

bool valid_ram_declaration(std::size_t size) {
    return size == 0U || size == kWramSize;
}
} // namespace

// NESdev mapper 224 r5752 selects the KT-008 wiring documented by NES 2.0
// mapper 268 r24663 submapper 1 and the AA6023A v1.2 datasheet. The board has
// no CHR ROM, expansion audio, or bus conflicts. Undefined $xxx5 helper-pin
// functions are retained as register state but have no invented behavior.
Kt008::Kt008(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)),
      chr_ram_(header.is_nes2 ? header.chr_ram_size : kMaxChrRamSize, 0U),
      prg_ram_(header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size : kWramSize, 0U),
      nvram_size_(header.is_nes2 ? header.prg_nvram_size : (header.has_battery ? kWramSize : 0U)),
      initial_mirror_(header.mirror_mode), nes2_(header.is_nes2), mirror_mode_(initial_mirror_) {
    chr_rom.clear();
    reset();
}

Result<void> Kt008::validate_geometry(const INesHeader& header,
                                      std::size_t prg_size,
                                      std::size_t chr_size) {
    if (header.mapper_number != 224U)
        return std::unexpected("KT-008 requires mapper 224");
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 224 supports submapper 0");
    if (prg_size < 16384U || prg_size > kMaxPrgRomSize || prg_size % 16384U != 0U || chr_size != 0U)
        return std::unexpected("Mapper 224 requires 16 KiB-32 MiB PRG ROM and no CHR ROM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 224 supports ordinary H/V CIRAM");
    if (header.is_nes2) {
        if (header.chr_ram_size < kWramSize || header.chr_ram_size > kMaxChrRamSize ||
            !std::has_single_bit(header.chr_ram_size) || header.chr_nvram_size != 0U)
            return std::unexpected("Mapper 224 requires power-of-two 8-256 KiB volatile CHR RAM");
        if (!valid_ram_declaration(header.prg_ram_size) ||
            !valid_ram_declaration(header.prg_nvram_size) ||
            (header.prg_ram_size != 0U && header.prg_nvram_size != 0U) ||
            header.has_battery != (header.prg_nvram_size == kWramSize))
            return std::unexpected("Mapper 224 permits one optional 8 KiB PRG RAM/NVRAM chip");
    }
    return {};
}

bool Kt008::gnrom_mode() const {
    return (supervisor_[3U] & 0x10U) != 0U;
}

bool Kt008::supervisor_locked() const {
    return !gnrom_mode() && (supervisor_[3U] & 0x80U) != 0U;
}

bool Kt008::low_ram_mapped(Address addr) const {
    return addr >= 0x5000U && addr < 0x6000U && prg_ram_enabled_ && !prg_ram_.empty() &&
           (supervisor_[3U] & 0x20U) != 0U;
}

bool Kt008::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && prg_ram_enabled_ && !prg_ram_.empty());
}

Byte Kt008::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (!maps_prg(addr))
            return 0U;
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U)
        return 0U;
    const auto offset = prg_page(addr) * 0x2000U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

uint32_t Kt008::prg_page(Address addr) const {
    const auto address = static_cast<unsigned>(addr);
    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const bool prg_mode = (bank_select_ & 0x40U) != 0U;
    const bool weird = (supervisor_[3U] & 0x40U) != 0U;
    uint32_t page = 0U;
    if (gnrom_mode()) {
        page = (supervisor_[3U] & 6U) << 2U;
        const bool cpu_a14 = (supervisor_[1U] & 2U) != 0U;
        page |= (cpu_a14 ? ((address >> 14U) & 1U)
                         : ((static_cast<unsigned>(supervisor_[3U]) >> 3U) & 1U))
                << 1U;
        page |= (address >> 13U) & 1U;
    } else {
        switch (slot) {
        case 0U:
            page = prg_mode ? 0xFEU : bank_registers_[6U];
            break;
        case 1U:
            page = bank_registers_[7U];
            break;
        case 2U:
            page = prg_mode ? bank_registers_[6U] : (weird ? 0U : 0xFEU);
            break;
        default:
            page = weird && !prg_mode ? 0U : 0xFFU;
            break;
        }
    }

    set_pin(page,
            17U,
            (supervisor_[0U] & 0x40U) != 0U ? (supervisor_[0U] & 1U) != 0U : (page & 0x10U) != 0U);
    set_pin(page,
            18U,
            (supervisor_[0U] & 0x80U) != 0U ? (supervisor_[0U] & 2U) != 0U : (page & 0x20U) != 0U);
    set_pin(page,
            19U,
            (supervisor_[1U] & 0x40U) == 0U ? (supervisor_[0U] & 4U) != 0U : (page & 0x40U) != 0U);
    set_pin(page,
            20U,
            (supervisor_[1U] & 0x80U) == 0U ? (supervisor_[1U] & 0x10U) != 0U
                                            : (page & 0x80U) != 0U);
    set_pin(page, 21U, (supervisor_[1U] & 4U) != 0U);
    set_pin(page, 22U, (supervisor_[1U] & 8U) != 0U);
    set_pin(page, 23U, (supervisor_[0U] & 0x20U) != 0U);
    set_pin(page, 24U, (supervisor_[0U] & 0x40U) != 0U);

    if ((supervisor_[5U] & 0x10U) != 0U) {
        const auto chr_bank = effective_chr_bank(ppu_chr_slot_);
        set_pin(page, 18U, (chr_bank & 0x40U) != 0U);
        set_pin(page, 19U, (chr_bank & 0x80U) != 0U);
    }
    return page;
}

void Kt008::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_ && !prg_ram_.empty())
            prg_ram_[addr & 0x1FFFU] = value;
        return;
    }
    if (addr < 0x8000U)
        return;
    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            bank_select_ = value & 0xC7U;
        } else {
            bank_registers_[bank_select_ & 7U] = (bank_select_ & 7U) >= 6U ? value & 0x3FU : value;
        }
    } else if (addr < 0xC000U) {
        if (even)
            mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        else {
            prg_ram_enabled_ = (value & 0x80U) != 0U;
            prg_ram_write_protected_ = (value & 0x40U) != 0U;
        }
    } else if (addr < 0xE000U) {
        if (even)
            irq_reload_ = value;
        else {
            irq_counter_ = 0U;
            irq_reload_flag_ = true;
        }
    } else if (even) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

Byte Kt008::mmc3_chr_bank(unsigned slot) const {
    const auto mapped = slot ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    if (mapped < 4U) {
        const auto value = bank_registers_[mapped / 2U];
        if ((supervisor_[3U] & 0x40U) != 0U)
            return (mapped & 1U) != 0U ? 0U : value;
        return (value & 0xFEU) | (mapped & 1U);
    }
    return bank_registers_[mapped - 2U];
}

Byte Kt008::effective_chr_bank(unsigned slot) const {
    Byte bank = mmc3_chr_bank(slot);
    if (gnrom_mode()) {
        const auto control = static_cast<unsigned>(supervisor_[2U]);
        const auto masked = static_cast<Byte>(control & ~((control >> 4U) & 7U) & 0xFU);
        bank = static_cast<Byte>((static_cast<unsigned>(masked) << 3U) | (slot & 7U) |
                                 (static_cast<unsigned>(bank) & 0x80U));
    }
    if ((supervisor_[0U] & 0x80U) != 0U)
        bank = static_cast<Byte>((bank & 0x7FU) | ((supervisor_[0U] & 0x10U) << 3U));
    return bank;
}

std::size_t Kt008::chr_offset(Address addr) const {
    return (static_cast<std::size_t>(
                effective_chr_bank((static_cast<unsigned>(addr) >> 10U) & 7U)) *
                0x400U +
            (addr & 0x3FFU)) %
           chr_ram_.size();
}

Byte Kt008::read_chr(Address addr) {
    return chr_ram_[chr_offset(addr & 0x1FFFU)];
}

void Kt008::write_chr(Address addr, Byte value) {
    chr_ram_[chr_offset(addr & 0x1FFFU)] = value;
}

MirrorMode Kt008::mirror_mode() const {
    return mirror_mode_;
}

bool Kt008::maps_expansion(Address addr) const {
    return low_ram_mapped(addr);
}

Byte Kt008::read_expansion(Address addr) {
    return low_ram_mapped(addr) ? prg_ram_[addr & 0x1FFFU] : 0U;
}

void Kt008::write_expansion(Address addr, Byte value) {
    if (addr >= 0x5000U && addr < 0x6000U) {
        const auto index = static_cast<unsigned>(addr & 7U);
        if (index < supervisor_.size() && (!supervisor_locked() || index == 2U)) {
            if (index == 2U && (supervisor_[2U] & 0x80U) != 0U)
                supervisor_[2U] = (supervisor_[2U] & 0xF0U) | (value & 0x0FU);
            else
                supervisor_[index] = value;
        }
        if (low_ram_mapped(addr) && !prg_ram_write_protected_)
            prg_ram_[addr & 0x1FFFU] = value;
    }
}

bool Kt008::observes_ppu_address() const {
    return true;
}

void Kt008::observe_ppu_address(Address addr) {
    ppu_chr_slot_ = static_cast<Byte>((static_cast<unsigned>(addr) >> 10U) & 7U);
}

bool Kt008::irq_pending() const {
    return irq_pending_;
}

void Kt008::acknowledge_irq() {
    irq_pending_ = false;
}

void Kt008::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_reload_ != 0U && irq_enabled_)
        irq_pending_ = true;
}

void Kt008::reset() {
    supervisor_.fill(0U);
    bank_select_ = 0U;
    bank_registers_.fill(0U);
    mirror_mode_ = initial_mirror_;
    irq_counter_ = irq_reload_ = 0U;
    irq_reload_flag_ = irq_enabled_ = irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
    ppu_chr_slot_ = 0U;
}

void Kt008::power_on() {
    reset();
    std::ranges::fill(chr_ram_, 0U);
    if (nvram_size_ == 0U)
        std::ranges::fill(prg_ram_, 0U);
}

void Kt008::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_ram_.size()));
    writer.write<Byte>((nes2_ ? 1U : 0U) | (initial_mirror_ == MirrorMode::Vertical ? 2U : 0U) |
                       (nvram_size_ != 0U ? 4U : 0U));
    writer.write_array(supervisor_);
    writer.write(bank_select_);
    writer.write_array(bank_registers_);
    writer.write(mirror_mode_);
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
    writer.write(ppu_chr_slot_);
    writer.write_bytes(prg_ram_);
    writer.write_bytes(chr_ram_);
}

void Kt008::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto geometry = reader.read<Byte>();
    std::array<Byte, 6> supervisor{};
    reader.read_array(supervisor);
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<MirrorMode>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();
    const auto ppu_slot = reader.read<Byte>();
    std::vector<Byte> prg_ram;
    std::vector<Byte> chr_ram;
    reader.read_bytes(prg_ram, kWramSize);
    reader.read_bytes(chr_ram, kMaxChrRamSize);

    const auto expected_geometry =
        static_cast<Byte>((nes2_ ? 1U : 0U) | (initial_mirror_ == MirrorMode::Vertical ? 2U : 0U) |
                          (nvram_size_ != 0U ? 4U : 0U));
    const bool valid_irq =
        (irq_flags & ~7U) == 0U && ((irq_flags & 4U) == 0U || (irq_flags & 2U) != 0U);
    const bool valid_ram = (ram_flags & ~0xC0U) == 0U;
    const bool valid_banks =
        (bank_select & 0x38U) == 0U && banks[6U] <= 0x3FU && banks[7U] <= 0x3FU;
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_ram_.size() || geometry != expected_geometry ||
        (mirror != MirrorMode::Horizontal && mirror != MirrorMode::Vertical) || !valid_irq ||
        !valid_ram || !valid_banks || ppu_slot > 7U || prg_ram.size() != prg_ram_.size() ||
        chr_ram.size() != chr_ram_.size()) {
        reader.fail();
        return;
    }

    supervisor_ = supervisor;
    bank_select_ = bank_select;
    bank_registers_ = banks;
    mirror_mode_ = mirror;
    irq_counter_ = counter;
    irq_reload_ = reload;
    irq_reload_flag_ = (irq_flags & 1U) != 0U;
    irq_enabled_ = (irq_flags & 2U) != 0U;
    irq_pending_ = (irq_flags & 4U) != 0U;
    prg_ram_enabled_ = (ram_flags & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram_flags & 0x40U) != 0U;
    ppu_chr_slot_ = ppu_slot;
    prg_ram_ = std::move(prg_ram);
    chr_ram_ = std::move(chr_ram);
}

std::span<const Byte> Kt008::battery_ram() const {
    return nvram_size_ == 0U ? std::span<const Byte>{} : std::span<const Byte>(prg_ram_);
}

void Kt008::set_battery_ram(std::span<const Byte> data) {
    if (nvram_size_ != 0U && data.size() == nvram_size_)
        std::ranges::copy(data, prg_ram_.begin());
}

} // namespace mapperbus::core
