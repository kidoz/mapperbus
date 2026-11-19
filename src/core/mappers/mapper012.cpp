#include "core/mappers/mapper012.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kFfePrgRamSize = 524288U;
constexpr std::size_t kFfeWorkRamSize = 32768U;
constexpr std::size_t kFfeScratchRamSize = 4096U;
constexpr std::uint16_t kSlStateTag = 0x0C00U;
constexpr std::uint16_t kFfeStateTag = 0x0C01U;

bool ordinary_mirroring(MirrorMode mode) {
    return mode == MirrorMode::Horizontal || mode == MirrorMode::Vertical;
}
} // namespace

// NESdev mapper 012 r18954, MMC3 IRQ specifics, and the linked Super Magic
// Card register description. The FFE image is already-loaded volatile DRAM,
// not an immutable ROM plus a separate initial CHR-RAM population.
Mapper012::Mapper012(const INesHeader& header,
                     std::vector<Byte> prg_data,
                     std::vector<Byte> chr_data)
    : ffe_4m_(header.is_nes2 && header.submapper == 1U), initial_mirror_(header.mirror_mode),
      prg_source_size_(prg_data.size()), chr_source_size_(chr_data.size()),
      prg_rom_(std::move(prg_data)), chr_rom_(std::move(chr_data)),
      sl_mirror_mode_(initial_mirror_) {
    if (ffe_4m_) {
        ffe_prg_ram_.assign(kFfePrgRamSize, 0U);
        std::ranges::copy(prg_rom_, ffe_prg_ram_.begin());
        std::ranges::copy(chr_rom_, ffe_prg_ram_.begin() + 0x40000);
        std::vector<Byte>{}.swap(prg_rom_);
        std::vector<Byte>{}.swap(chr_rom_);
    }
    power_on();
}

Result<void> Mapper012::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 12U)
        return std::unexpected("Mapper 012 geometry requires mapper ID 12");
    if (header.is_nes2 && header.submapper > 1U)
        return std::unexpected("Mapper 012 supports submappers 0 and 1");
    if (!ordinary_mirroring(header.mirror_mode))
        return std::unexpected("Mapper 012 requires H/V startup mirroring");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 012 has no battery-backed memory");
    if (header.has_trainer)
        return std::unexpected(
            "Mapper 012 trainer images require cartridge trainer-byte routing to CPU $7000");

    if (!header.is_nes2 || header.submapper == 0U) {
        if (prg_size < 32768U || prg_size > 524288U || prg_size % 8192U != 0U || chr_size < 8192U ||
            chr_size > 524288U || chr_size % 1024U != 0U)
            return std::unexpected(
                "SL-5020B requires 32-512 KiB PRG and 8-512 KiB CHR ROM in native bank units");
        if (header.chr_ram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
            return std::unexpected("SL-5020B is a ROM-only board without WRAM or CHR RAM");
        return {};
    }

    const bool valid_prg = prg_size == 131072U || prg_size == 262144U;
    const bool valid_chr = chr_size == 131072U || chr_size == 262144U;
    if (!valid_prg || !valid_chr || (prg_size == 131072U && chr_size == 131072U))
        return std::unexpected(
            "FFE 4M requires 128+256, 256+128, or 256+256 KiB PRG/CHR image data");
    if (header.is_nes2 &&
        (header.prg_ram_size != kFfeWorkRamSize || header.chr_ram_size != kFfeWorkRamSize))
        return std::unexpected("FFE 4M requires 32 KiB volatile WRAM and CHR RAM declarations");
    return {};
}

bool Mapper012::maps_prg(Address addr) const {
    if (!ffe_4m_)
        return addr >= 0x8000U;
    if (ffe_pass_through())
        return false;
    if (addr >= 0x6000U && addr < 0x8000U)
        return true;
    if (addr < 0x8000U)
        return false;
    return ffe_play_mode() || addr < 0xC000U;
}

Byte Mapper012::read_prg(Address addr) {
    if (!ffe_4m_) {
        if (addr < 0x8000U)
            return 0U;
        const auto slot = static_cast<unsigned>((addr >> 13U) & 3U);
        unsigned bank;
        if (slot == 1U)
            bank = sl_bank_registers_[7U];
        else if (slot == 3U)
            bank = 63U;
        else if ((slot == 0U) != sl_prg_mode_)
            bank = sl_bank_registers_[6U];
        else
            bank = 62U;
        const auto offset = static_cast<std::size_t>(bank) * 8192U + (addr & 0x1FFFU);
        return prg_rom_[offset % prg_rom_.size()];
    }

    if (addr >= 0x6000U && addr < 0x8000U && (ffe_play_mode() || ffe_prg_protected()) &&
        !ffe_pass_through()) {
        const auto bank = static_cast<std::size_t>((ffe_config_ >> 4U) & 3U);
        return ffe_wram_[bank * 8192U + (addr & 0x1FFFU)];
    }
    if (const auto offset = ffe_prg_offset(addr))
        return ffe_prg_ram_[*offset];
    return 0U;
}

void Mapper012::write_prg(Address addr, Byte value) {
    if (!ffe_4m_) {
        if (addr < 0x8000U)
            return;
        switch (addr & 0xE001U) {
        case 0x8000U:
            sl_bank_select_ = value & 7U;
            sl_prg_mode_ = (value & 0x40U) != 0U;
            sl_chr_inversion_ = (value & 0x80U) != 0U;
            break;
        case 0x8001U:
            if (sl_bank_select_ >= 6U)
                sl_bank_registers_[sl_bank_select_] = value & 0x3FU;
            else
                sl_bank_registers_[sl_bank_select_] = value;
            break;
        case 0xA000U:
            sl_mirror_mode_ = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
            break;
        case 0xA001U:
            sl_ram_enabled_ = (value & 0x80U) != 0U;
            sl_ram_write_protected_ = (value & 0x40U) != 0U;
            break;
        case 0xC000U:
            sl_irq_reload_ = value;
            break;
        case 0xC001U:
            sl_irq_counter_ |= 0xF0U;
            if (!sl_irq_reload_blocked_)
                sl_irq_reload_flag_ = true;
            break;
        case 0xE000U:
            sl_irq_enabled_ = false;
            sl_irq_pending_ = false;
            break;
        case 0xE001U:
            sl_irq_enabled_ = true;
            break;
        default:
            break;
        }
        return;
    }

    if (ffe_pass_through())
        return;
    if (addr >= 0x8000U) {
        if (ffe_prg_protected())
            ffe_latch_ = value & 0x3FU;
        ffe_prg_2m_banks_[(addr >> 13U) & 3U] = value & 0x3FU;
    }
    if (addr >= 0x6000U && addr < 0x8000U && (ffe_play_mode() || ffe_prg_protected())) {
        const auto bank = static_cast<std::size_t>((ffe_config_ >> 4U) & 3U);
        ffe_wram_[bank * 8192U + (addr & 0x1FFFU)] = value;
        return;
    }
    if (!ffe_prg_protected())
        if (const auto offset = ffe_prg_offset(addr))
            ffe_prg_ram_[*offset] = value;
}

Byte Mapper012::sl_chr_bank(Address addr) const {
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    if (!sl_chr_inversion_) {
        if (slot < 2U)
            return (sl_bank_registers_[0U] & 0xFEU) | slot;
        if (slot < 4U)
            return (sl_bank_registers_[1U] & 0xFEU) | (slot - 2U);
        return sl_bank_registers_[slot - 2U];
    }
    if (slot < 4U)
        return sl_bank_registers_[slot + 2U];
    if (slot < 6U)
        return (sl_bank_registers_[0U] & 0xFEU) | (slot - 4U);
    return (sl_bank_registers_[1U] & 0xFEU) | (slot - 6U);
}

Byte Mapper012::read_chr(Address addr) {
    if (ffe_4m_)
        return ffe_pass_through() ? 0U : ffe_chr_ram_[ffe_chr_offset(addr) % ffe_chr_ram_.size()];
    const auto outer = (sl_chr_outer_ >> (((addr & 0x1000U) != 0U) ? 4U : 0U)) & 1U;
    const auto offset = static_cast<std::size_t>(outer) * 262144U +
                        static_cast<std::size_t>(sl_chr_bank(addr)) * 1024U + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper012::write_chr(Address addr, Byte value) {
    if (ffe_4m_ && !ffe_pass_through() && !ffe_chr_protected())
        ffe_chr_ram_[ffe_chr_offset(addr) % ffe_chr_ram_.size()] = value;
}

MirrorMode Mapper012::mirror_mode() const {
    if (!ffe_4m_)
        return sl_mirror_mode_;
    if ((ffe_mode_1m_address_ & 1U) == 0U)
        return (ffe_latch_mode_ & 0x10U) != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    return (ffe_latch_mode_ & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::optional<Byte> Mapper012::read_nametable(Address addr) const {
    if (!ffe_4m_ || ffe_pass_through() || (ffe_config_ & 2U) != 0U)
        return std::nullopt;
    const auto slot = static_cast<unsigned>((addr >> 10U) & 3U);
    const auto offset =
        static_cast<std::size_t>(ffe_chr_banks_[8U + slot]) * 1024U + (addr & 0x03FFU);
    return ffe_chr_ram_[offset % ffe_chr_ram_.size()];
}

bool Mapper012::write_nametable(Address addr, Byte value) {
    if (!ffe_4m_ || ffe_pass_through() || (ffe_config_ & 2U) != 0U)
        return false;
    if (!ffe_chr_protected()) {
        const auto slot = static_cast<unsigned>((addr >> 10U) & 3U);
        const auto offset =
            static_cast<std::size_t>(ffe_chr_banks_[8U + slot]) * 1024U + (addr & 0x03FFU);
        ffe_chr_ram_[offset % ffe_chr_ram_.size()] = value;
    }
    return true;
}

bool Mapper012::maps_expansion(Address addr) const {
    if (!ffe_4m_)
        return addr >= 0x4020U && addr < 0x6000U && (addr & 0xE100U) == 0x4100U;
    if (ffe_pass_through())
        return false;
    return addr == 0x4500U || addr == 0x4501U || (addr >= 0x5000U && addr <= 0x5FFFU);
}

Byte Mapper012::expansion_read_mask(Address addr) const {
    if (!ffe_4m_ && maps_expansion(addr))
        return 0x01U;
    return 0xFFU;
}

Byte Mapper012::read_expansion(Address addr) {
    if (!ffe_4m_)
        return maps_expansion(addr) ? sl_language_bit_ : 0U;
    if (addr >= 0x5000U && addr <= 0x5FFFU)
        return ffe_scratch_ram_[addr & 0x0FFFU];
    if (addr == 0x4500U)
        return static_cast<Byte>(0x40U | (ffe_latch_mode_ & 0xF0U) | (ffe_mode_1m_address_ & 3U));
    if (addr == 0x4501U)
        return static_cast<Byte>((ffe_latch_ << 2U) | ffe_mode_2m_address_);
    return 0U;
}

void Mapper012::write_expansion(Address addr, Byte value) {
    if (!ffe_4m_) {
        if (maps_expansion(addr))
            sl_chr_outer_ = value & 0x11U;
        return;
    }
    if (ffe_pass_through())
        return;
    if (addr >= 0x5000U && addr <= 0x5FFFU)
        ffe_scratch_ram_[addr & 0x0FFFU] = value;
    else if (addr >= 0x42FCU && addr <= 0x42FFU) {
        ffe_latch_mode_ = value & 0xF0U;
        ffe_mode_1m_address_ = addr & 3U;
    } else if (addr >= 0x43FCU && addr <= 0x43FFU) {
        ffe_latch_ = (ffe_latch_ & 0xFCU) | (value & 3U);
        ffe_mode_2m_address_ = addr & 3U;
    } else if (addr == 0x4500U)
        ffe_config_ = value;
    else if (addr == 0x4501U) {
        ffe_irq_pending_ = false;
        ffe_irq_enabled_ = false;
    } else if (addr == 0x4502U) {
        ffe_irq_pending_ = false;
        ffe_irq_counter_ = (ffe_irq_counter_ & 0xFF00U) | value;
    } else if (addr == 0x4503U) {
        ffe_irq_pending_ = false;
        ffe_irq_counter_ = (ffe_irq_counter_ & 0x00FFU) | static_cast<std::uint16_t>(value << 8U);
        ffe_irq_enabled_ = true;
    } else if (addr >= 0x4504U && addr <= 0x4507U) {
        const auto slot = static_cast<unsigned>(addr - 0x4504U);
        ffe_prg_4m_banks_[slot] = value & 0x3FU;
        if (!ffe_extended_prg_mode() || (ffe_mode_2m_address_ & 2U) != 0U)
            ffe_prg_2m_banks_[slot] = value & 0x3FU;
    } else if (addr >= 0x4510U && addr <= 0x451BU)
        ffe_chr_banks_[addr - 0x4510U] = value;
}

Result<void> Mapper012::configure_board_jumpers(Byte value) {
    if (ffe_4m_)
        return std::unexpected("FFE 4M exposes no board jumpers");
    if (value > 1U)
        return std::unexpected("SL-5020B language input accepts values 0 and 1");
    sl_language_bit_ = value;
    return {};
}

bool Mapper012::ffe_pass_through() const {
    return (ffe_config_ & 0x80U) != 0U;
}
bool Mapper012::ffe_play_mode() const {
    return (ffe_config_ & 0x40U) != 0U;
}
bool Mapper012::ffe_prg_protected() const {
    return (ffe_mode_1m_address_ & 2U) != 0U;
}
bool Mapper012::ffe_chr_protected() const {
    return (ffe_latch_mode_ & 0xE0U) >= 0x80U;
}
bool Mapper012::ffe_extended_prg_mode() const {
    return (ffe_mode_2m_address_ & 1U) == 0U;
}
bool Mapper012::ffe_chr_1k_mode() const {
    return (ffe_config_ & 1U) != 0U;
}
bool Mapper012::ffe_mmc4_mode() const {
    return ffe_chr_1k_mode() && (ffe_config_ & 4U) == 0U;
}

std::optional<std::size_t> Mapper012::ffe_prg_offset(Address addr) const {
    if (ffe_pass_through())
        return std::nullopt;
    if (!ffe_play_mode()) {
        if (addr >= 0x6000U && addr < 0x8000U)
            return static_cast<std::size_t>(ffe_prg_4m_banks_[3U]) * 8192U + (addr & 0x1FFFU);
        if (addr >= 0x8000U && addr < 0xC000U)
            return static_cast<std::size_t>(ffe_prg_4m_banks_[(addr >> 13U) & 1U]) * 8192U +
                   (addr & 0x1FFFU);
        return std::nullopt;
    }
    if (ffe_extended_prg_mode()) {
        const auto slot = static_cast<unsigned>((addr >> 13U) & 3U);
        const auto& banks =
            (ffe_mode_2m_address_ & 2U) != 0U ? ffe_prg_2m_banks_ : ffe_prg_4m_banks_;
        return static_cast<std::size_t>(banks[slot] & 0x3FU) * 8192U + (addr & 0x1FFFU);
    }

    const auto mode = static_cast<unsigned>((ffe_latch_mode_ >> 5U) & 7U);
    const bool upper = addr >= 0xC000U;
    switch (mode) {
    case 0U:
        return static_cast<std::size_t>(upper ? 7U : ffe_latch_ & 7U) * 16384U + (addr & 0x3FFFU);
    case 1U:
        return static_cast<std::size_t>(upper ? 7U : (ffe_latch_ >> 2U) & 15U) * 16384U +
               (addr & 0x3FFFU);
    case 2U:
        return static_cast<std::size_t>(upper ? 15U : ffe_latch_ & 15U) * 16384U + (addr & 0x3FFFU);
    case 3U:
        return static_cast<std::size_t>(upper ? ffe_latch_ & 15U : 15U) * 16384U + (addr & 0x3FFFU);
    case 4U:
        return static_cast<std::size_t>((ffe_latch_ >> 4U) & 3U) * 32768U + (addr & 0x7FFFU);
    default:
        return 3U * 32768U + (addr & 0x7FFFU);
    }
}

std::size_t Mapper012::ffe_chr_offset(Address addr) const {
    addr &= 0x1FFFU;
    if (ffe_chr_1k_mode()) {
        if (ffe_mmc4_mode()) {
            const auto half = static_cast<unsigned>(addr >> 12U);
            const auto pair = static_cast<unsigned>((addr >> 10U) & 2U);
            const auto reg = half * 4U + pair + ffe_mmc4_latches_[half];
            return static_cast<std::size_t>(ffe_chr_banks_[reg] & 0xFCU) * 1024U + (addr & 0x0FFFU);
        }
        return static_cast<std::size_t>(ffe_chr_banks_[addr >> 10U]) * 1024U + (addr & 0x03FFU);
    }

    unsigned bank = 0U;
    if (ffe_extended_prg_mode())
        bank = ffe_latch_ & 3U;
    else {
        switch ((ffe_latch_mode_ >> 5U) & 7U) {
        case 1U:
        case 3U:
        case 4U:
        case 5U:
            bank = ffe_latch_ & 3U;
            break;
        case 6U:
            bank = ffe_latch_ & 1U;
            break;
        default:
            break;
        }
    }
    return static_cast<std::size_t>(bank) * 8192U + addr;
}

void Mapper012::observe_cpu_write(Address addr, Byte value) {
    if (!ffe_4m_ || ffe_pass_through())
        return;
    if (addr >= 0x2000U && addr <= 0x3FFFU)
        ffe_scratch_ram_[0x800U + (addr & 7U)] = value;
    else if (addr >= 0x4000U && addr <= 0x4015U)
        ffe_scratch_ram_[addr - 0x4000U] = value;
    else if (addr >= 0x4500U && addr <= 0x451FU)
        ffe_scratch_ram_[0x500U + (addr & 0x1FU)] = value;
}

bool Mapper012::observes_ppu_a12() const {
    return ffe_4m_;
}
void Mapper012::observe_ppu_a12(bool high) {
    if (!ffe_4m_)
        return;
    if (high && !ffe_ppu_a12_ && !ffe_pass_through() && (ffe_config_ & 8U) != 0U)
        clock_ffe_irq();
    ffe_ppu_a12_ = high;
}

bool Mapper012::observes_ppu_reads() const {
    return ffe_4m_;
}
void Mapper012::observe_ppu_read(Address addr) {
    if (!ffe_4m_ || ffe_pass_through() || !ffe_mmc4_mode())
        return;
    addr &= 0x1FFFU;
    const auto half = static_cast<unsigned>(addr >> 12U);
    if ((addr & 0x0FF8U) == 0x0FD8U)
        ffe_mmc4_latches_[half] = 0U;
    else if ((addr & 0x0FF8U) == 0x0FE8U)
        ffe_mmc4_latches_[half] = 1U;
}

void Mapper012::clock_ffe_irq() {
    if (!ffe_irq_enabled_)
        return;
    ++ffe_irq_counter_;
    if (ffe_irq_counter_ == 0U)
        ffe_irq_pending_ = true;
}
void Mapper012::clock_cpu() {
    if (ffe_4m_ && !ffe_pass_through() && (ffe_config_ & 8U) == 0U)
        clock_ffe_irq();
}
void Mapper012::clock_irq_counter() {
    if (ffe_4m_)
        return;
    const bool forced = sl_irq_reload_flag_;
    const auto old_counter = sl_irq_counter_;
    if (sl_irq_counter_ == 0U || forced) {
        sl_irq_counter_ = sl_irq_reload_;
        sl_irq_reload_flag_ = false;
    } else
        --sl_irq_counter_;
    sl_irq_reload_blocked_ = forced;
    if (sl_irq_counter_ == 0U && sl_irq_enabled_ && (old_counter > 0U || forced))
        sl_irq_pending_ = true;
}
bool Mapper012::irq_pending() const {
    return ffe_4m_ ? ffe_irq_pending_ : sl_irq_pending_;
}
void Mapper012::acknowledge_irq() {
    if (ffe_4m_)
        ffe_irq_pending_ = false;
    else
        sl_irq_pending_ = false;
}

void Mapper012::initialize_ffe_registers() {
    ffe_prg_2m_banks_.fill(0U);
    ffe_prg_4m_banks_.fill(0U);
    ffe_chr_banks_.fill(0U);
    ffe_mmc4_latches_.fill(0U);
    ffe_latch_ = 0U;
    ffe_latch_mode_ = initial_mirror_ == MirrorMode::Vertical ? 0x20U : 0x30U;
    ffe_mode_1m_address_ = 3U;
    ffe_mode_2m_address_ = 0U;
    ffe_config_ = 0x42U;
    ffe_irq_counter_ = 0U;
    ffe_irq_enabled_ = false;
    ffe_irq_pending_ = false;
    ffe_ppu_a12_ = false;
    const auto initial_bank = static_cast<Byte>(prg_source_size_ / 8192U - 4U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        ffe_prg_4m_banks_[slot] = initial_bank + slot;
        ffe_prg_2m_banks_[slot] = initial_bank + slot;
        ffe_scratch_ram_[0x504U + slot] = initial_bank + slot;
    }
    ffe_scratch_ram_[0x500U] = ffe_config_;
    std::fill_n(ffe_scratch_ram_.begin() + 0x510U, 12U, 0U);
}

void Mapper012::reset() {
    if (ffe_4m_) {
        initialize_ffe_registers();
        return;
    }
    sl_bank_select_ = 0U;
    sl_prg_mode_ = false;
    sl_chr_inversion_ = false;
    sl_bank_registers_.fill(0U);
    sl_chr_outer_ = 0U;
    sl_mirror_mode_ = initial_mirror_;
    sl_irq_counter_ = 0U;
    sl_irq_reload_ = 0U;
    sl_irq_reload_flag_ = false;
    sl_irq_reload_blocked_ = false;
    sl_irq_enabled_ = false;
    sl_irq_pending_ = false;
    sl_ram_enabled_ = true;
    sl_ram_write_protected_ = false;
}

void Mapper012::power_on() {
    if (ffe_4m_) {
        ffe_chr_ram_.fill(0U);
        ffe_wram_.fill(0U);
        ffe_scratch_ram_.fill(0U);
        initialize_ffe_registers();
        return;
    }
    reset();
}

void Mapper012::save_state(StateWriter& writer) const {
    if (ffe_4m_) {
        writer.write(kFfeStateTag);
        writer.write(static_cast<std::uint32_t>(prg_source_size_));
        writer.write(static_cast<std::uint32_t>(chr_source_size_));
        writer.write(static_cast<Byte>(initial_mirror_));
        writer.write(ffe_latch_mode_);
        writer.write(ffe_latch_);
        writer.write(ffe_mode_1m_address_);
        writer.write(ffe_mode_2m_address_);
        writer.write(ffe_config_);
        writer.write(ffe_irq_counter_);
        writer.write(static_cast<Byte>(ffe_irq_enabled_));
        writer.write(static_cast<Byte>(ffe_irq_pending_));
        writer.write(static_cast<Byte>(ffe_ppu_a12_));
        writer.write_array(ffe_prg_2m_banks_);
        writer.write_array(ffe_prg_4m_banks_);
        writer.write_array(ffe_chr_banks_);
        writer.write_array(ffe_mmc4_latches_);
        writer.write_bytes(ffe_prg_ram_);
        writer.write_bytes(ffe_wram_);
        writer.write_bytes(ffe_chr_ram_);
        writer.write_bytes(ffe_scratch_ram_);
        return;
    }

    writer.write(kSlStateTag);
    writer.write(static_cast<std::uint32_t>(prg_source_size_));
    writer.write(static_cast<std::uint32_t>(chr_source_size_));
    writer.write(static_cast<Byte>(initial_mirror_));
    writer.write(sl_language_bit_);
    writer.write(sl_bank_select_);
    writer.write(static_cast<Byte>(sl_prg_mode_));
    writer.write(static_cast<Byte>(sl_chr_inversion_));
    writer.write_array(sl_bank_registers_);
    writer.write(sl_chr_outer_);
    writer.write(static_cast<Byte>(sl_mirror_mode_));
    writer.write(sl_irq_counter_);
    writer.write(sl_irq_reload_);
    writer.write(static_cast<Byte>(sl_irq_reload_flag_));
    writer.write(static_cast<Byte>(sl_irq_reload_blocked_));
    writer.write(static_cast<Byte>(sl_irq_enabled_));
    writer.write(static_cast<Byte>(sl_irq_pending_));
    writer.write(static_cast<Byte>(sl_ram_enabled_));
    writer.write(static_cast<Byte>(sl_ram_write_protected_));
}

void Mapper012::load_state(StateReader& reader) {
    if (ffe_4m_)
        load_ffe_state(reader);
    else
        load_sl_state(reader);
}

void Mapper012::load_sl_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    const auto language = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    const auto prg_mode = reader.read<Byte>();
    const auto chr_inversion = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto outer = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    const auto counter = reader.read<Byte>();
    const auto reload = reader.read<Byte>();
    const auto reload_flag = reader.read<Byte>();
    const auto reload_blocked = reader.read<Byte>();
    const auto irq_enabled = reader.read<Byte>();
    const auto irq_pending = reader.read<Byte>();
    const auto ram_enabled = reader.read<Byte>();
    const auto ram_protected = reader.read<Byte>();
    const auto mirror_mode = static_cast<MirrorMode>(mirror);
    const bool valid_flags = prg_mode <= 1U && chr_inversion <= 1U && reload_flag <= 1U &&
                             reload_blocked <= 1U && irq_enabled <= 1U && irq_pending <= 1U &&
                             ram_enabled <= 1U && ram_protected <= 1U;
    if (!reader.ok() || tag != kSlStateTag || prg_size != prg_source_size_ ||
        chr_size != chr_source_size_ || initial_mirror != static_cast<Byte>(initial_mirror_) ||
        language > 1U || bank_select > 7U || !valid_flags || banks[6U] > 0x3FU ||
        banks[7U] > 0x3FU || (outer & 0xEEU) != 0U || !ordinary_mirroring(mirror_mode)) {
        reader.fail();
        return;
    }

    sl_language_bit_ = language;
    sl_bank_select_ = bank_select;
    sl_prg_mode_ = prg_mode != 0U;
    sl_chr_inversion_ = chr_inversion != 0U;
    sl_bank_registers_ = banks;
    sl_chr_outer_ = outer;
    sl_mirror_mode_ = mirror_mode;
    sl_irq_counter_ = counter;
    sl_irq_reload_ = reload;
    sl_irq_reload_flag_ = reload_flag != 0U;
    sl_irq_reload_blocked_ = reload_blocked != 0U;
    sl_irq_enabled_ = irq_enabled != 0U;
    sl_irq_pending_ = irq_pending != 0U;
    sl_ram_enabled_ = ram_enabled != 0U;
    sl_ram_write_protected_ = ram_protected != 0U;
}

void Mapper012::load_ffe_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    const auto latch_mode = reader.read<Byte>();
    const auto latch = reader.read<Byte>();
    const auto mode_1m = reader.read<Byte>();
    const auto mode_2m = reader.read<Byte>();
    const auto config = reader.read<Byte>();
    const auto counter = reader.read<std::uint16_t>();
    const auto enabled = reader.read<Byte>();
    const auto pending = reader.read<Byte>();
    const auto a12 = reader.read<Byte>();
    std::array<Byte, 4> banks_2m{};
    std::array<Byte, 4> banks_4m{};
    std::array<Byte, 12> chr_banks{};
    std::array<Byte, 2> mmc4_latches{};
    reader.read_array(banks_2m);
    reader.read_array(banks_4m);
    reader.read_array(chr_banks);
    reader.read_array(mmc4_latches);
    std::vector<Byte> prg;
    std::vector<Byte> wram;
    std::vector<Byte> chr;
    std::vector<Byte> scratch;
    reader.read_bytes(prg, kFfePrgRamSize);
    reader.read_bytes(wram, kFfeWorkRamSize);
    reader.read_bytes(chr, kFfeWorkRamSize);
    reader.read_bytes(scratch, kFfeScratchRamSize);
    const auto valid_banks = [](const auto& banks) {
        return std::ranges::all_of(banks, [](Byte bank) { return bank <= 0x3FU; });
    };
    if (!reader.ok() || tag != kFfeStateTag || prg_size != prg_source_size_ ||
        chr_size != chr_source_size_ || initial_mirror != static_cast<Byte>(initial_mirror_) ||
        (latch_mode & 0x0FU) != 0U || latch > 0x3FU || mode_1m > 3U || mode_2m > 3U ||
        enabled > 1U || pending > 1U || a12 > 1U || !valid_banks(banks_2m) ||
        !valid_banks(banks_4m) || mmc4_latches[0U] > 1U || mmc4_latches[1U] > 1U ||
        prg.size() != kFfePrgRamSize || wram.size() != kFfeWorkRamSize ||
        chr.size() != kFfeWorkRamSize || scratch.size() != kFfeScratchRamSize) {
        reader.fail();
        return;
    }

    ffe_latch_mode_ = latch_mode;
    ffe_latch_ = latch;
    ffe_mode_1m_address_ = mode_1m;
    ffe_mode_2m_address_ = mode_2m;
    ffe_config_ = config;
    ffe_irq_counter_ = counter;
    ffe_irq_enabled_ = enabled != 0U;
    ffe_irq_pending_ = pending != 0U;
    ffe_ppu_a12_ = a12 != 0U;
    ffe_prg_2m_banks_ = banks_2m;
    ffe_prg_4m_banks_ = banks_4m;
    ffe_chr_banks_ = chr_banks;
    ffe_mmc4_latches_ = mmc4_latches;
    ffe_prg_ram_ = std::move(prg);
    std::ranges::copy(wram, ffe_wram_.begin());
    std::ranges::copy(chr, ffe_chr_ram_.begin());
    std::ranges::copy(scratch, ffe_scratch_ram_.begin());
}

} // namespace mapperbus::core
