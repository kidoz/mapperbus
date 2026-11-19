#include "core/mappers/mapper083.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {

// Original implementation from NESdev mapper 83 r24580. The article does not
// establish electrical reset values; reset uses deterministic zero registers.
ConyYoko::ConyYoko(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : profile_(profile_for(header)), prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      nvram_(has_nvram() ? 32768U : 0U) {
    power_on();
}

ConyYoko::Profile ConyYoko::profile_for(const INesHeader& header) {
    if (header.mapper_number == 264U)
        return Profile::Yoko;
    return static_cast<Profile>(header.is_nes2 ? header.submapper : 0U);
}

Result<void> ConyYoko::validate_geometry(const INesHeader& h,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (h.mapper_number != 83U && h.mapper_number != 264U)
        return std::unexpected("Cony/Yoko supports mappers 83 and 264");
    if ((h.mapper_number == 264U && (!h.is_nes2 || h.submapper != 0U)) ||
        (h.mapper_number == 83U && h.is_nes2 && h.submapper > 3U) ||
        (h.mapper_number == 83U && !h.is_nes2 && h.submapper != 0U))
        return std::unexpected("Unsupported Cony/Yoko mapper or submapper");

    const auto profile = profile_for(h);
    const bool exact_multicart =
        profile == Profile::NvramMulticart || profile == Profile::SmallMulticart;
    if (exact_multicart) {
        const std::size_t expected = profile == Profile::NvramMulticart ? 262144U : 131072U;
        if (prg_size != expected || chr_size != expected)
            return std::unexpected("Cony/Yoko multicart requires its documented PRG/CHR geometry");
    } else {
        const std::size_t max_chr = profile == Profile::Standard ? 262144U : 524288U;
        if (prg_size < 16384U || prg_size > 4194304U || !std::has_single_bit(prg_size) ||
            chr_size < 8192U || chr_size > max_chr || !std::has_single_bit(chr_size))
            return std::unexpected("Unsupported Cony/Yoko PRG/CHR ROM geometry");
    }

    const bool nvram = profile == Profile::NvramMulticart;
    if (h.has_battery != nvram || h.chr_ram_size != 0U || h.chr_nvram_size != 0U ||
        (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != (nvram ? 32768U : 0U))))
        return std::unexpected(nvram ? "Mapper 83.2 requires 32 KiB PRG NVRAM and no other RAM"
                                     : "Cony/Yoko board requires CHR ROM and no PRG RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Cony/Yoko supports ordinary H/V startup mirroring");
    return {};
}

Address ConyYoko::asic_address(Address addr) const {
    if (profile_ != Profile::Yoko)
        return addr;
    return static_cast<Address>((addr & 0xF000U) | ((addr & 0x0C00U) >> 2U) | (addr & 0x00FFU));
}

bool ConyYoko::has_nvram() const {
    return profile_ == Profile::NvramMulticart;
}

bool ConyYoko::has_large_chr() const {
    return profile_ == Profile::LargeChr || profile_ == Profile::Yoko;
}

bool ConyYoko::has_128k_inner_prg() const {
    return profile_ == Profile::SmallMulticart || profile_ == Profile::Yoko;
}

bool ConyYoko::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && (has_nvram() || (mode_ & 0x20U) != 0U));
}

std::size_t ConyYoko::prg_offset(Address addr) const {
    const std::size_t inner_mask = has_128k_inner_prg() ? 0x1FFFFU : 0x3FFFFU;
    const Byte base_mask = has_nvram() || profile_ == Profile::SmallMulticart ? 0x3FU : 0xFFU;
    const std::size_t base = static_cast<std::size_t>(prg_base_ & base_mask) << 14U;
    const auto banking_mode = (static_cast<unsigned>(mode_) >> 4U) & 3U;

    if (addr >= 0x8000U && banking_mode == 0U) {
        const std::size_t fixed = addr >= 0xC000U ? (inner_mask & ~0x3FFFU) : 0U;
        return ((base | fixed) + (addr & 0x3FFFU)) % prg_rom_.size();
    }
    if (addr >= 0x8000U && banking_mode == 1U)
        return (base + (addr & 0x3FFFU)) % prg_rom_.size();

    const unsigned slot = addr >= 0x8000U ? (addr - 0x8000U) >> 13U : 3U;
    const Byte bank_mask = has_128k_inner_prg() ? 0x0FU : 0x1FU;
    const std::size_t inner = addr >= 0x8000U && slot == 3U
                                  ? (inner_mask & ~0x1FFFU)
                                  : static_cast<std::size_t>(prg_banks_[slot] & bank_mask) << 13U;
    return ((base & ~inner_mask) | inner | (addr & 0x1FFFU)) % prg_rom_.size();
}

Byte ConyYoko::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0U;
    if (addr < 0x8000U) {
        if (has_nvram()) {
            const auto bank =
                static_cast<std::size_t>((static_cast<unsigned>(prg_base_) >> 6U) & 3U);
            return nvram_[bank * 8192U + (addr & 0x1FFFU)];
        }
        return (mode_ & 0x20U) != 0U ? prg_rom_[prg_offset(addr)] : 0U;
    }
    return prg_rom_[prg_offset(addr)];
}

void ConyYoko::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && has_nvram()) {
            const auto bank =
                static_cast<std::size_t>((static_cast<unsigned>(prg_base_) >> 6U) & 3U);
            nvram_[bank * 8192U + (addr & 0x1FFFU)] = value;
        }
        return;
    }

    const auto reg = asic_address(addr);
    if ((reg & 0x8300U) == 0x8000U)
        prg_base_ = value;
    else if ((reg & 0x8300U) == 0x8100U)
        mode_ = value;
    else if ((reg & 0x8301U) == 0x8200U) {
        irq_counter_ = static_cast<uint16_t>((irq_counter_ & 0xFF00U) | value);
        irq_pending_ = false;
    } else if ((reg & 0x8301U) == 0x8201U) {
        irq_counter_ =
            static_cast<uint16_t>((irq_counter_ & 0x00FFU) | (static_cast<unsigned>(value) << 8U));
        irq_enabled_ = (mode_ & 0x80U) != 0U;
    } else if ((reg & 0x8318U) == 0x8318U)
        irq_source_ = value;
    else if ((reg & 0x8318U) == 0x8310U)
        chr_banks_[reg & 7U] = value;
    else if ((reg & 0x8310U) == 0x8300U)
        prg_banks_[reg & 3U] = value;
}

Byte ConyYoko::read_chr(Address addr) {
    const auto offset = static_cast<unsigned>(addr) & 0x1FFFU;
    const unsigned slot = offset >> 10U;
    std::size_t rom_offset = 0U;
    if (has_large_chr()) {
        constexpr std::array<unsigned, 4> kRegisters = {0U, 1U, 6U, 7U};
        const auto bank = static_cast<std::size_t>(chr_banks_[kRegisters[slot / 2U]]);
        rom_offset = (bank << 11U) | (offset & 0x7FFU);
    } else {
        const unsigned outer = has_nvram() ? (static_cast<unsigned>(prg_base_) >> 4U) & 3U
                               : profile_ == Profile::SmallMulticart
                                   ? (static_cast<unsigned>(prg_base_) >> 6U) & 3U
                                   : 0U;
        const auto bank = (static_cast<std::size_t>(outer) << 8U) | chr_banks_[slot];
        rom_offset = (bank << 10U) | (offset & 0x3FFU);
    }
    return chr_rom_[rom_offset % chr_rom_.size()];
}

void ConyYoko::write_chr(Address, Byte) {}

MirrorMode ConyYoko::mirror_mode() const {
    constexpr std::array<MirrorMode, 4> kModes = {MirrorMode::Vertical,
                                                  MirrorMode::Horizontal,
                                                  MirrorMode::SingleLower,
                                                  MirrorMode::SingleUpper};
    return kModes[mode_ & 3U];
}

bool ConyYoko::maps_expansion(Address addr) const {
    const auto reg = asic_address(addr);
    return (reg & 0xF100U) == 0x5000U || (reg & 0xF100U) == 0x5100U;
}

Byte ConyYoko::expansion_read_mask(Address addr) const {
    const auto reg = asic_address(addr);
    if ((reg & 0xF100U) == 0x5100U)
        return 0xFFU;
    return (reg & 0xF100U) == 0x5000U ? 0x03U : 0U;
}

Byte ConyYoko::read_expansion(Address addr) {
    const auto reg = asic_address(addr);
    if ((reg & 0xF100U) == 0x5100U)
        return scratch_ram_[reg & 3U];
    return (reg & 0xF100U) == 0x5000U ? jumpers_ : 0U;
}

void ConyYoko::write_expansion(Address addr, Byte value) {
    const auto reg = asic_address(addr);
    if ((reg & 0xF100U) == 0x5100U)
        scratch_ram_[reg & 3U] = value;
}

Result<void> ConyYoko::configure_board_jumpers(Byte value) {
    if (value > 3U)
        return std::unexpected("Cony/Yoko solder pads require a value 0..3");
    jumpers_ = value;
    return {};
}

std::span<const Byte> ConyYoko::battery_ram() const {
    return nvram_;
}

void ConyYoko::set_battery_ram(std::span<const Byte> data) {
    std::ranges::copy(data.first(std::min(data.size(), nvram_.size())), nvram_.begin());
}

void ConyYoko::power_on() {
    reset();
    scratch_ram_.fill(0U);
}

void ConyYoko::reset() {
    prg_base_ = 0U;
    mode_ = 0U;
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    irq_counter_ = 0U;
    irq_source_ = 0U;
    irq_enabled_ = false;
    irq_pending_ = false;
    ppu_a12_ = false;
}

bool ConyYoko::irq_pending() const {
    return irq_pending_;
}

void ConyYoko::clock_irq() {
    if (!irq_enabled_ || irq_counter_ == 0U)
        return;
    if ((mode_ & 0x40U) != 0U)
        --irq_counter_;
    else
        ++irq_counter_;
    if (irq_counter_ == 0U) {
        irq_enabled_ = false;
        irq_pending_ = true;
    }
}

void ConyYoko::clock_cpu() {
    if (irq_source_ == 0U)
        clock_irq();
}

bool ConyYoko::observes_ppu_a12() const {
    return true;
}

void ConyYoko::observe_ppu_a12(bool high) {
    if (irq_source_ != 0U && high && !ppu_a12_)
        clock_irq();
    ppu_a12_ = high;
}

uint16_t ConyYoko::state_tag() const {
    return static_cast<uint16_t>(0x5300U | (static_cast<unsigned>(jumpers_) << 4U) |
                                 static_cast<Byte>(profile_));
}

void ConyYoko::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write<uint32_t>(static_cast<uint32_t>(prg_rom_.size()));
    writer.write<uint32_t>(static_cast<uint32_t>(chr_rom_.size()));
    writer.write(prg_base_);
    writer.write(mode_);
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(irq_counter_);
    writer.write(irq_source_);
    const Byte irq_flags = static_cast<Byte>((irq_enabled_ ? 1U : 0U) | (irq_pending_ ? 2U : 0U) |
                                             (ppu_a12_ ? 4U : 0U));
    writer.write(irq_flags);
    writer.write_array(scratch_ram_);
    for (Byte value : nvram_)
        writer.write(value);
}

void ConyYoko::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto prg_base = reader.read<Byte>();
    const auto mode = reader.read<Byte>();
    std::array<Byte, 4> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto irq_counter = reader.read<uint16_t>();
    const auto irq_source = reader.read<Byte>();
    const auto irq_flags = reader.read<Byte>();
    std::array<Byte, 4> scratch_ram{};
    reader.read_array(scratch_ram);
    std::vector<Byte> nvram(nvram_.size());
    for (auto& value : nvram)
        value = reader.read<Byte>();

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || irq_flags > 7U) {
        reader.fail();
        return;
    }

    prg_base_ = prg_base;
    mode_ = mode;
    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    irq_counter_ = irq_counter;
    irq_source_ = irq_source;
    irq_enabled_ = (irq_flags & 1U) != 0U;
    irq_pending_ = (irq_flags & 2U) != 0U;
    ppu_a12_ = (irq_flags & 4U) != 0U;
    scratch_ram_ = scratch_ram;
    nvram_ = std::move(nvram);
}

} // namespace mapperbus::core
