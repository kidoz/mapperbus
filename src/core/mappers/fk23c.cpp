#include "core/mappers/fk23c.hpp"

#include <algorithm>
#include <bit>
#include <charconv>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMaxRomSize = 0x1000000U;
constexpr std::size_t kMaxChrRamSize = 0x200000U;
constexpr uint16_t kStateTag = 0xB000U;

bool power_of_two(std::size_t size) {
    return size != 0U && std::has_single_bit(size);
}

bool valid_chr_ram_size(std::size_t size) {
    return size >= 0x2000U && size <= kMaxChrRamSize && power_of_two(size);
}

} // namespace

struct Fk23c::RuntimeState {
    Byte mode = 0U;
    Byte prg_base = 0U;
    Byte chr_base = 0U;
    Byte prg_base_msb = 0U;
    Byte chr_base_msb = 0U;
    Byte extended = 0U;
    Byte unrom_latch = 0U;
    Byte cnrom_latch = 0U;
    Byte bank_select = 0U;
    std::array<Byte, 12> banks{};
    Byte mirror_register = 0U;
    Byte ram_register = 0U;
    Byte irq_counter = 0U;
    Byte irq_reload = 0U;
    Byte irq_flags = 0U;
    std::vector<Byte> chr_ram;
    std::vector<Byte> volatile_wram;
    std::vector<Byte> nvram;
};

// NESdev mapper 176 r24008, its MMC3/RAMBO-1 references, and the original
// FK23C hardware write-up (r5310's explicit inner-mask/outer-base formulas).
Fk23c::Fk23c(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : submapper_(header.is_nes2 ? header.submapper : 0U), initial_mirror_(header.mirror_mode),
      prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    if (header.is_nes2) {
        chr_ram_.resize(header.chr_ram_size);
        if (submapper_ == 2U) {
            volatile_wram_.resize(header.prg_ram_size);
            nvram_.resize(header.prg_nvram_size);
        }
    } else if (chr_rom_.empty()) {
        chr_ram_.resize(0x2000U);
    }
    reset();
}

Result<void> Fk23c::validate_geometry(const INesHeader& h,
                                      std::size_t prg_size,
                                      std::size_t chr_size) {
    if (h.mapper_number != 176U)
        return std::unexpected("Fk23c requires mapper 176");
    if (h.is_nes2 && h.submapper > 5U)
        return std::unexpected("Mapper 176 supports NES 2.0 submappers 0 through 5");
    if (prg_size < 0x8000U || prg_size > kMaxRomSize || !power_of_two(prg_size) ||
        (chr_size != 0U &&
         (chr_size < 0x2000U || chr_size > kMaxRomSize || !power_of_two(chr_size))))
        return std::unexpected("Mapper 176 requires power-of-two 32 KiB-16 MiB PRG ROM and "
                               "optional 8 KiB-16 MiB CHR ROM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 176 supports ordinary horizontal/vertical CIRAM");
    if (!h.is_nes2) {
        if (h.has_battery)
            return std::unexpected("Legacy mapper 176 has no documented battery RAM population");
        return {};
    }

    const Byte submapper = h.submapper;
    if (h.chr_nvram_size != 0U)
        return std::unexpected("Mapper 176 does not provide nonvolatile CHR RAM");
    if (submapper >= 3U && (chr_size == 0U || h.chr_ram_size != 0U))
        return std::unexpected("Mapper 176 submappers 3-5 require CHR ROM and no CHR RAM");
    if (chr_size == 0U) {
        if (!valid_chr_ram_size(h.chr_ram_size))
            return std::unexpected(
                "CHR-ROM-less mapper 176 requires 8 KiB-2 MiB power-of-two CHR RAM");
    } else if (submapper <= 2U) {
        if (h.chr_ram_size != 0U && h.chr_ram_size != 0x2000U)
            return std::unexpected("Mixed mapper 176 boards require exactly 8 KiB CHR RAM");
    } else if (h.chr_ram_size != 0U) {
        return std::unexpected("Mapper 176 submappers 3-5 do not provide CHR RAM");
    }

    if (submapper != 2U) {
        if (h.has_battery || h.prg_ram_size != 0U || h.prg_nvram_size != 0U)
            return std::unexpected("Only mapper 176 submapper 2 provides banked WRAM");
        return {};
    }
    if (h.prg_ram_size == 0U && h.prg_nvram_size == 0U && !h.has_battery)
        return {};
    if (h.prg_ram_size != 0x4000U || h.prg_nvram_size != 0x4000U || !h.has_battery)
        return std::unexpected("Mapper 176 submapper 2 WRAM is 16 KiB volatile plus 16 KiB NVRAM");
    return {};
}

void Fk23c::power_on() {
    reset();
    std::ranges::fill(chr_ram_, 0U);
    std::ranges::fill(volatile_wram_, 0U);
}

void Fk23c::reset() {
    mode_ = 0U;
    prg_base_ = 0U;
    chr_base_ = 0U;
    prg_base_msb_ = 0U;
    chr_base_msb_ = 0U;
    extended_ = 0U;
    unrom_latch_ = 0U;
    cnrom_latch_ = 0U;
    bank_select_ = 0U;
    banks_ = {0U, 2U, 4U, 5U, 6U, 7U, 0U, 1U, 0xFEU, 0xFFU, 0xFFU, 0xFFU};
    mirror_register_ = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
    ram_register_ = 0U;
    irq_counter_ = 0U;
    irq_reload_ = 0U;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

bool Fk23c::extended_mode() const {
    return (submapper_ == 1U || submapper_ == 2U) && (extended_ & 2U) != 0U;
}

bool Fk23c::ram_configuration() const {
    return submapper_ == 2U && (ram_register_ & 0x20U) != 0U;
}

bool Fk23c::ram_enabled() const {
    return submapper_ == 2U && (ram_register_ & 0x80U) != 0U && !volatile_wram_.empty();
}

bool Fk23c::outer_registers_enabled() const {
    return submapper_ != 2U || !ram_configuration() || (ram_register_ & 0x40U) != 0U;
}

bool Fk23c::expansion_ram_enabled() const {
    return ram_configuration() && !outer_registers_enabled() && ram_enabled();
}

bool Fk23c::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && ram_enabled());
}

bool Fk23c::maps_expansion(Address addr) const {
    return (submapper_ == 5U && addr == 0x4800U) ||
           (addr >= 0x5000U && addr < 0x6000U && expansion_ram_enabled());
}

Byte Fk23c::expansion_read_mask(Address addr) const {
    return submapper_ == 5U && addr == 0x4800U ? 0U : 0xFFU;
}

std::size_t Fk23c::wram_offset(unsigned bank, Address addr) const {
    const std::size_t offset = addr & 0x1FFFU;
    return (bank & 2U) != 0U ? 0x2000U + offset : offset;
}

Byte Fk23c::read_wram(Address addr) const {
    const unsigned bank = ram_configuration() ? ram_register_ & 3U : 0U;
    const auto& storage = (bank & 1U) != 0U ? nvram_ : volatile_wram_;
    const auto offset = wram_offset(bank, addr);
    return offset < storage.size() ? storage[offset] : 0U;
}

void Fk23c::write_wram(Address addr, Byte value) {
    const unsigned bank = ram_configuration() ? ram_register_ & 3U : 0U;
    auto& storage = (bank & 1U) != 0U ? nvram_ : volatile_wram_;
    const auto offset = wram_offset(bank, addr);
    if (offset < storage.size())
        storage[offset] = value;
}

Byte Fk23c::read_expansion(Address addr) {
    if (!expansion_ram_enabled())
        return 0U;
    const auto offset = wram_offset(2U, static_cast<Address>(0x7000U + (addr & 0x0FFFU)));
    return offset < volatile_wram_.size() ? volatile_wram_[offset] : 0U;
}

void Fk23c::write_expansion(Address addr, Byte value) {
    if (submapper_ == 5U && addr == 0x4800U) {
        prg_base_msb_ = value & 0x3FU;
        return;
    }
    if (addr < 0x5000U || addr >= 0x6000U)
        return;
    if (expansion_ram_enabled()) {
        const auto offset = wram_offset(2U, static_cast<Address>(0x7000U + (addr & 0x0FFFU)));
        if (offset < volatile_wram_.size())
            volatile_wram_[offset] = value;
        return;
    }
    if (outer_registers_enabled())
        if (const auto reg = outer_register(addr))
            write_outer_register(*reg, value);
}

std::optional<unsigned> Fk23c::outer_register(Address addr) const {
    if (addr < 0x5000U || addr >= 0x6000U)
        return std::nullopt;
    const Address selected = static_cast<Address>(0x10U << solder_pad_);
    const Address mask = static_cast<Address>(0x5003U | selected | (submapper_ == 3U ? 4U : 0U));
    const unsigned index = addr & (submapper_ == 3U ? 7U : 3U);
    if ((addr & mask) != static_cast<Address>(0x5000U | selected | index))
        return std::nullopt;
    return index;
}

void Fk23c::write_outer_register(unsigned index, Byte value) {
    if (index == 0U) {
        mode_ = value;
    } else if (index == 1U) {
        prg_base_ = value & (submapper_ == 5U ? 0x1FU : 0x7FU);
    } else if (index == 2U) {
        chr_base_ = value;
    } else if (index == 3U && (submapper_ == 1U || submapper_ == 2U)) {
        extended_ = value & 2U;
    } else if (submapper_ == 3U && index == 5U) {
        prg_base_msb_ = value & 0x0FU;
    } else if (submapper_ == 3U && index == 6U) {
        chr_base_msb_ = value & 0x0FU;
    }
}

uint32_t Fk23c::prg_base() const {
    uint32_t base = prg_base_;
    if (submapper_ == 2U) {
        base |= static_cast<uint32_t>(mode_ & 8U) << 4U;
        base |= static_cast<uint32_t>(mode_ & 0x80U) << 1U;
        base |= static_cast<uint32_t>(chr_base_ & 0x80U) << 2U;
        base |= static_cast<uint32_t>(chr_base_ & 0x40U) << 4U;
        base |= static_cast<uint32_t>(chr_base_ & 0x20U) << 6U;
    } else if (submapper_ == 3U) {
        base |= static_cast<uint32_t>(prg_base_msb_) << 7U;
    } else if (submapper_ == 4U) {
        base |= static_cast<uint32_t>(chr_base_ & 0x80U);
    } else if (submapper_ == 5U) {
        base |= static_cast<uint32_t>(prg_base_msb_) << 5U;
    }
    return base;
}

Byte Fk23c::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return ram_enabled() ? read_wram(addr) : 0U;
    if (addr < 0x8000U || prg_rom_.empty())
        return 0U;
    return prg_rom_[prg_address(addr) % prg_rom_.size()];
}

uint32_t Fk23c::prg_address(Address addr) const {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    const unsigned prg_mode = (mode_ & 7U);
    const uint32_t base = prg_base();
    if (!extended_mode() && prg_mode == 3U)
        return (base << 14U) | (addr & 0x3FFFU);
    if (!extended_mode() && prg_mode == 4U)
        return ((base & ~1U) << 14U) | (addr & 0x7FFFU);
    if (!extended_mode() && prg_mode == 5U) {
        const uint32_t inner = addr >= 0xC000U ? 7U : unrom_latch_ & 7U;
        return (((base & ~7U) | inner) << 14U) | (addr & 0x3FFFU);
    }

    const bool eight_bit = extended_mode() || submapper_ == 1U || submapper_ == 3U;
    const uint32_t mask = extended_mode() || prg_mode == 0U || prg_mode > 5U
                              ? (eight_bit ? 0xFFU : 0x3FU)
                          : prg_mode == 1U ? 0x1FU
                                           : 0x0FU;
    unsigned bank = 0U;
    if (extended_mode()) {
        const bool swapped = (bank_select_ & 0x40U) != 0U;
        if (slot == 1U)
            bank = banks_[7U];
        else if (slot == 3U)
            bank = banks_[9U];
        else if ((slot == 0U) != swapped)
            bank = banks_[6U];
        else
            bank = banks_[8U];
    } else {
        const bool swapped = (bank_select_ & 0x40U) != 0U;
        if (slot == 1U)
            bank = banks_[7U];
        else if (slot == 3U)
            bank = mask;
        else if ((slot == 0U) != swapped)
            bank = banks_[6U];
        else
            bank = mask - 1U;
    }
    const uint32_t final_bank = (bank & mask) | ((base << 1U) & ~mask);
    return (final_bank << 13U) | (addr & 0x1FFFU);
}

void Fk23c::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if (ram_enabled() && (ram_configuration() || (ram_register_ & 0x40U) == 0U))
            write_wram(addr, value);
        return;
    }
    if (addr < 0x8000U)
        return;

    if (!extended_mode() && (mode_ & 7U) == 5U)
        unrom_latch_ = value & 7U;
    if (submapper_ == 1U && (mode_ & 0x40U) != 0U && (mode_ & 0x20U) == 0U)
        cnrom_latch_ = value & 3U;

    switch (addr & 0xE003U) {
    case 0x8000U:
        bank_select_ = value & 0xCFU;
        break;
    case 0x8001U: {
        const unsigned index = bank_select_ & (extended_mode() ? 0x0FU : 0x07U);
        if (extended_mode()) {
            if (index < banks_.size())
                banks_[index] = value;
        } else {
            banks_[index] =
                index >= 6U && submapper_ != 1U && submapper_ != 3U ? value & 0x3FU : value;
        }
        break;
    }
    case 0xA000U:
        mirror_register_ = value & (submapper_ == 2U ? 3U : 1U);
        break;
    case 0xA001U:
        ram_register_ = value;
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

uint32_t Fk23c::chr_address(Address addr) const {
    uint32_t base = chr_base_;
    if (submapper_ == 3U)
        base |= static_cast<uint32_t>(chr_base_msb_) << 8U;
    if ((mode_ & 0x40U) != 0U) {
        if (submapper_ == 1U && (mode_ & 0x20U) == 0U) {
            const uint32_t mask = (mode_ & 0x10U) != 0U ? 1U : 3U;
            return (((base & ~mask) | (cnrom_latch_ & mask)) << 13U) | (addr & 0x1FFFU);
        }
        return (base << 13U) | (addr & 0x1FFFU);
    }

    const unsigned physical_slot = (addr >> 10U) & 7U;
    const unsigned slot = physical_slot ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    unsigned inner = 0U;
    if (extended_mode()) {
        constexpr std::array<Byte, 8> selectors = {0U, 10U, 1U, 11U, 2U, 3U, 4U, 5U};
        inner = banks_[selectors[slot]];
    } else if (slot < 4U) {
        inner = (banks_[slot >> 1U] & 0xFEU) | (slot & 1U);
    } else {
        inner = banks_[slot - 2U];
    }
    const uint32_t mask = (mode_ & 0x10U) != 0U ? 0x7FU : 0xFFU;
    const uint32_t bank = (inner & mask) | ((base << 3U) & ~mask);
    return (bank << 10U) | (addr & 0x03FFU);
}

bool Fk23c::chr_uses_ram(uint32_t offset) const {
    if (chr_ram_.empty())
        return false;
    if (chr_rom_.empty())
        return true;
    if (submapper_ <= 1U)
        return (mode_ & 0x20U) != 0U;
    return submapper_ == 2U && ram_configuration() && (ram_register_ & 4U) != 0U &&
           offset < 0x2000U;
}

Byte Fk23c::read_chr(Address addr) {
    const uint32_t offset = chr_address(addr);
    if (chr_uses_ram(offset))
        return chr_ram_[offset % chr_ram_.size()];
    return chr_rom_.empty() ? 0U : chr_rom_[offset % chr_rom_.size()];
}

void Fk23c::write_chr(Address addr, Byte value) {
    const uint32_t offset = chr_address(addr);
    if (chr_uses_ram(offset))
        chr_ram_[offset % chr_ram_.size()] = value;
}

MirrorMode Fk23c::mirror_mode() const {
    if (submapper_ == 2U && ram_configuration()) {
        if (mirror_register_ == 2U)
            return MirrorMode::SingleLower;
        if (mirror_register_ == 3U)
            return MirrorMode::SingleUpper;
    }
    return (mirror_register_ & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

bool Fk23c::irq_pending() const {
    return irq_pending_;
}

void Fk23c::acknowledge_irq() {
    irq_pending_ = false;
}

void Fk23c::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

Result<void> Fk23c::configure_board_jumpers(Byte value) {
    if (value > 7U)
        return std::unexpected("Mapper 176 solder pad setting must be 0 through 7");
    solder_pad_ = value;
    return {};
}

Result<void> Fk23c::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("menu-"))
        return std::unexpected("Mapper 176 profiles are menu-0 through menu-7");
    profile.remove_prefix(5U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 7U)
        return std::unexpected("Mapper 176 profiles are menu-0 through menu-7");
    return configure_board_jumpers(static_cast<Byte>(value));
}

std::span<const Byte> Fk23c::battery_ram() const {
    return nvram_;
}

void Fk23c::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == nvram_.size())
        std::ranges::copy(data, nvram_.begin());
}

uint16_t Fk23c::state_tag() const {
    return static_cast<uint16_t>(kStateTag | submapper_ | (solder_pad_ << 4U) |
                                 (nvram_.empty() ? 0U : 0x80U) |
                                 (initial_mirror_ == MirrorMode::Horizontal ? 0x100U : 0U));
}

void Fk23c::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_rom_.size()));
    writer.write(mode_);
    writer.write(prg_base_);
    writer.write(chr_base_);
    writer.write(prg_base_msb_);
    writer.write(chr_base_msb_);
    writer.write(extended_);
    writer.write(unrom_latch_);
    writer.write(cnrom_latch_);
    writer.write(bank_select_);
    writer.write_array(banks_);
    writer.write(mirror_register_);
    writer.write(ram_register_);
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    const Byte flags = static_cast<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                                         (irq_pending_ ? 4U : 0U));
    writer.write(flags);
    writer.write_bytes(chr_ram_);
    writer.write_bytes(volatile_wram_);
    writer.write_bytes(nvram_);
}

void Fk23c::apply_state(RuntimeState state) {
    mode_ = state.mode;
    prg_base_ = state.prg_base;
    chr_base_ = state.chr_base;
    prg_base_msb_ = state.prg_base_msb;
    chr_base_msb_ = state.chr_base_msb;
    extended_ = state.extended;
    unrom_latch_ = state.unrom_latch;
    cnrom_latch_ = state.cnrom_latch;
    bank_select_ = state.bank_select;
    banks_ = state.banks;
    mirror_register_ = state.mirror_register;
    ram_register_ = state.ram_register;
    irq_counter_ = state.irq_counter;
    irq_reload_ = state.irq_reload;
    irq_reload_flag_ = (state.irq_flags & 1U) != 0U;
    irq_enabled_ = (state.irq_flags & 2U) != 0U;
    irq_pending_ = (state.irq_flags & 4U) != 0U;
    chr_ram_ = std::move(state.chr_ram);
    volatile_wram_ = std::move(state.volatile_wram);
    nvram_ = std::move(state.nvram);
}

void Fk23c::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    RuntimeState state;
    state.mode = reader.read<Byte>();
    state.prg_base = reader.read<Byte>();
    state.chr_base = reader.read<Byte>();
    state.prg_base_msb = reader.read<Byte>();
    state.chr_base_msb = reader.read<Byte>();
    state.extended = reader.read<Byte>();
    state.unrom_latch = reader.read<Byte>();
    state.cnrom_latch = reader.read<Byte>();
    state.bank_select = reader.read<Byte>();
    reader.read_array(state.banks);
    state.mirror_register = reader.read<Byte>();
    state.ram_register = reader.read<Byte>();
    state.irq_counter = reader.read<Byte>();
    state.irq_reload = reader.read<Byte>();
    state.irq_flags = reader.read<Byte>();
    reader.read_bytes(state.chr_ram, kMaxChrRamSize);
    reader.read_bytes(state.volatile_wram, 0x4000U);
    reader.read_bytes(state.nvram, 0x4000U);

    const bool extended_profile = submapper_ == 1U || submapper_ == 2U;
    const Byte max_prg_msb = submapper_ == 3U ? 0x0FU : submapper_ == 5U ? 0x3FU : 0U;
    const Byte max_chr_msb = submapper_ == 3U ? 0x0FU : 0U;
    const bool valid =
        reader.ok() && reader.remaining() == 0U && tag == state_tag() &&
        prg_size == prg_rom_.size() && chr_size == chr_rom_.size() &&
        state.prg_base <= (submapper_ == 5U ? 0x1FU : 0x7FU) && state.prg_base_msb <= max_prg_msb &&
        state.chr_base_msb <= max_chr_msb && state.extended <= (extended_profile ? 2U : 0U) &&
        state.unrom_latch <= 7U && state.cnrom_latch <= 3U && (state.bank_select & 0x30U) == 0U &&
        (extended_profile || (state.bank_select & 0x0FU) <= 7U) &&
        state.mirror_register <= (submapper_ == 2U ? 3U : 1U) && state.irq_flags <= 7U &&
        ((state.irq_flags & 4U) == 0U || (state.irq_flags & 2U) != 0U) &&
        state.chr_ram.size() == chr_ram_.size() &&
        state.volatile_wram.size() == volatile_wram_.size() && state.nvram.size() == nvram_.size();
    if (!valid) {
        reader.fail();
        return;
    }
    apply_state(std::move(state));
}

} // namespace mapperbus::core
