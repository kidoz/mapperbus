#include "core/mappers/super_game_mega_type_iii.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomSize = 0x390000U;
constexpr std::size_t kChrRomSize = 0x200000U;
constexpr std::size_t kControlRomBase = 0x380000U;
constexpr std::size_t kPrgOuterSize = 0x40000U;
constexpr std::size_t kPrgA17Size = 0x20000U;
constexpr std::size_t kChrOuterSize = 0x40000U;
constexpr std::size_t kChrA17Size = 0x20000U;
constexpr uint16_t kStateTag = 0x7C00U;

bool is_dip_address(Address addr) {
    return (addr & 0xFF0FU) == 0x4F0FU;
}

bool is_audio_address(Address addr) {
    return (addr & 0xFF0FU) == 0x5000U;
}

bool is_control_address(Address addr) {
    return (addr & 0xFF0FU) == 0x5001U;
}

bool is_mapping_address(Address addr) {
    return (addr & 0xFF0FU) == 0x5002U;
}

} // namespace

SuperGameMegaTypeIII::SuperGameMegaTypeIII(const INesHeader&,
                                           std::vector<Byte> prg_rom,
                                           std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> SuperGameMegaTypeIII::validate_geometry(const INesHeader& header,
                                                     std::size_t prg_size,
                                                     std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 124U || header.submapper != 0U)
        return std::unexpected("Mapper 124 requires NES 2.0 submapper 0");
    if (prg_size != kPrgRomSize || chr_size != kChrRomSize)
        return std::unexpected(
            "Mapper 124 requires 3.5625 MiB PRG including control ROM and 2 MiB CHR ROM");
    if (header.has_battery || header.prg_ram_size != 8192U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 8192U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 124 requires 8 KiB volatile PRG RAM and 8 KiB CHR RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 124 requires ordinary H/V header mirroring");
    return {};
}

unsigned SuperGameMegaTypeIII::mapper_select() const {
    return (registers_.mapping >> 4U) & 3U;
}

unsigned SuperGameMegaTypeIII::mmc3_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / 8192U;
    const bool switch_first = !registers_.mmc3_prg_mode;
    if (slot == 1U)
        return registers_.mmc3_banks[7U] & 0x3FU;
    if (slot == 3U)
        return 15U;
    if ((slot == 0U) == switch_first)
        return registers_.mmc3_banks[6U] & 0x3FU;
    return 14U;
}

unsigned SuperGameMegaTypeIII::mmc1_prg_bank(Address addr) const {
    const unsigned mode = (registers_.mmc1_control >> 2U) & 3U;
    const unsigned bank = registers_.mmc1_prg & 0x0FU;
    const unsigned pair = (addr >= 0xC000U) ? 1U : 0U;
    if (mode < 2U)
        return ((bank & 0x0EU) | pair) * 2U + ((addr >> 13U) & 1U);
    if (mode == 2U)
        return pair == 0U ? ((addr >> 13U) & 1U) : bank * 2U + ((addr >> 13U) & 1U);
    return pair == 0U ? bank * 2U + ((addr >> 13U) & 1U) : 14U + ((addr >> 13U) & 1U);
}

unsigned SuperGameMegaTypeIII::prg_a17(Address addr) const {
    if ((registers_.control & 0x20U) == 0U)
        return (mmc3_prg_bank(addr) >> 4U) & 1U;
    if (mapper_select() == 1U)
        return (registers_.amrom_bank >> 2U) & 1U;
    return registers_.control & 1U;
}

Byte SuperGameMegaTypeIII::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((registers_.control & 0x20U) != 0U)
            return prg_rom_[kControlRomBase + addr];
        return registers_.wram_enabled ? prg_ram_[addr - 0x6000U] : 0U;
    }
    if (addr < 0x8000U)
        return 0U;
    if ((registers_.control & 0x80U) == 0U)
        return prg_rom_[kControlRomBase + addr];

    unsigned inner = 0U;
    switch (mapper_select()) {
    case 0U:
        inner =
            ((addr & 0x4000U) != 0U ? 7U : registers_.unrom_bank & 7U) * 2U + ((addr >> 13U) & 1U);
        break;
    case 1U:
        inner = (registers_.amrom_bank & 3U) * 4U + (addr - 0x8000U) / 8192U;
        break;
    case 2U:
        inner = mmc1_prg_bank(addr);
        break;
    default:
        inner = mmc3_prg_bank(addr) & 0x0FU;
        break;
    }
    const std::size_t offset = ((registers_.control >> 1U) & 0x0FU) * kPrgOuterSize +
                               prg_a17(addr) * kPrgA17Size + inner * 8192U + (addr & 8191U);
    return offset < kControlRomBase ? prg_rom_[offset] : 0U;
}

bool SuperGameMegaTypeIII::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return true;
    return addr >= 0x6000U && addr < 0x8000U &&
           ((registers_.control & 0x20U) != 0U || registers_.wram_enabled);
}

void SuperGameMegaTypeIII::write_serial_mmc1(Address addr, Byte value) {
    if ((value & 0x80U) != 0U) {
        registers_.mmc1_shift = 0x10U;
        registers_.mmc1_control |= 0x0CU;
        return;
    }
    const bool complete = (registers_.mmc1_shift & 1U) != 0U;
    registers_.mmc1_shift = static_cast<Byte>((registers_.mmc1_shift >> 1U) | ((value & 1U) << 4U));
    if (!complete)
        return;
    commit_mmc1(addr, registers_.mmc1_shift & 0x1FU);
    registers_.mmc1_shift = 0x10U;
}

void SuperGameMegaTypeIII::commit_mmc1(Address addr, Byte value) {
    if (addr < 0xA000U)
        registers_.mmc1_control = value;
    else if (addr < 0xC000U)
        registers_.mmc1_chr0 = value;
    else if (addr < 0xE000U)
        registers_.mmc1_chr1 = value;
    else
        registers_.mmc1_prg = value;
}

void SuperGameMegaTypeIII::write_mmc3(Address addr, Byte value) {
    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            registers_.mmc3_select = value & 7U;
            registers_.mmc3_prg_mode = (value & 0x40U) != 0U;
            registers_.mmc3_chr_inversion = (value & 0x80U) != 0U;
        } else {
            auto& bank = registers_.mmc3_banks[registers_.mmc3_select];
            bank = registers_.mmc3_select >= 6U ? value & 0x3FU : value;
        }
    } else if (addr < 0xC000U) {
        if (even)
            registers_.mmc3_mirror =
                (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        else {
            registers_.wram_enabled = (value & 0x80U) != 0U;
            registers_.wram_write_protected = (value & 0x40U) != 0U;
        }
    } else if (addr < 0xE000U) {
        if (even)
            registers_.mmc3_irq_reload = value;
        else {
            registers_.mmc3_irq_counter = 0U;
            registers_.mmc3_irq_reload_flag = true;
        }
    } else if (even) {
        registers_.mmc3_irq_enabled = false;
        registers_.mmc3_irq_pending = false;
    } else {
        registers_.mmc3_irq_enabled = true;
    }
}

void SuperGameMegaTypeIII::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((registers_.control & 0x20U) == 0U && registers_.wram_enabled &&
            !registers_.wram_write_protected)
            prg_ram_[addr - 0x6000U] = value;
        return;
    }
    if (addr < 0x8000U)
        return;
    registers_.unrom_bank = value & 7U;
    registers_.amrom_bank = static_cast<Byte>(((value & 0x10U) >> 1U) | (value & 7U));
    write_serial_mmc1(addr, value);
    write_mmc3(addr, value);
}

void SuperGameMegaTypeIII::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || !consecutive || (value & 0x80U) != 0U) {
        write_prg(addr, value);
        return;
    }
    registers_.unrom_bank = value & 7U;
    registers_.amrom_bank = static_cast<Byte>(((value & 0x10U) >> 1U) | (value & 7U));
    write_mmc3(addr, value);
}

unsigned SuperGameMegaTypeIII::mmc3_chr_bank(Address addr) const {
    const unsigned slot =
        (((addr & 0x1FFFU) >> 10U) & 7U) ^ (registers_.mmc3_chr_inversion ? 4U : 0U);
    if (slot < 4U)
        return (registers_.mmc3_banks[slot / 2U] & 0xFEU) | (slot & 1U);
    return registers_.mmc3_banks[slot - 2U];
}

unsigned SuperGameMegaTypeIII::mmc1_chr_bank(Address addr) const {
    if ((registers_.mmc1_control & 0x10U) == 0U)
        return (registers_.mmc1_chr0 & 0x1EU) | ((addr >> 12U) & 1U);
    return (addr & 0x1000U) == 0U ? registers_.mmc1_chr0 : registers_.mmc1_chr1;
}

Byte SuperGameMegaTypeIII::read_chr(Address addr) {
    if ((registers_.control & 0x40U) == 0U)
        return chr_ram_[addr & 0x1FFFU];
    const unsigned a17 = (registers_.mapping & 0x40U) != 0U ? registers_.mapping & 1U
                                                            : (mmc3_chr_bank(addr) >> 7U) & 1U;
    std::size_t offset = ((registers_.mapping >> 1U) & 7U) * kChrOuterSize + a17 * kChrA17Size;
    if ((registers_.mapping & 0x10U) == 0U)
        offset += mmc1_chr_bank(addr) * 4096U + (addr & 4095U);
    else
        offset += (mmc3_chr_bank(addr) & 0x7FU) * 1024U + (addr & 1023U);
    return chr_rom_[offset];
}

void SuperGameMegaTypeIII::write_chr(Address addr, Byte value) {
    if ((registers_.control & 0x40U) == 0U)
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode SuperGameMegaTypeIII::mirror_mode() const {
    switch (mapper_select()) {
    case 0U:
        return MirrorMode::Horizontal;
    case 1U:
        return (registers_.amrom_bank & 8U) != 0U ? MirrorMode::SingleUpper
                                                  : MirrorMode::SingleLower;
    case 2U:
        switch (registers_.mmc1_control & 3U) {
        case 0U:
            return MirrorMode::SingleLower;
        case 1U:
            return MirrorMode::SingleUpper;
        case 2U:
            return MirrorMode::Vertical;
        default:
            return MirrorMode::Horizontal;
        }
    default:
        return registers_.mmc3_mirror;
    }
}

bool SuperGameMegaTypeIII::maps_expansion(Address addr) const {
    return is_dip_address(addr) || (addr >= 0x5000U && addr <= 0x5FFFU);
}

Byte SuperGameMegaTypeIII::expansion_read_mask(Address) const {
    return 0xFFU;
}

Byte SuperGameMegaTypeIII::read_expansion(Address addr) {
    if (is_dip_address(addr)) {
        const unsigned open = static_cast<unsigned>(~dip_switches_);
        return static_cast<Byte>((coin_inserted_ ? 0U : 0x80U) | (((open >> 2U) & 1U) << 6U) |
                                 (((open >> 4U) & 1U) << 5U) | (((open >> 6U) & 1U) << 4U) |
                                 (((open >> 7U) & 1U) << 3U) | (((open >> 5U) & 1U) << 2U) |
                                 (((open >> 3U) & 1U) << 1U) | ((open >> 1U) & 1U));
    }
    return addr >= 0x5000U && addr <= 0x5FFFU ? prg_rom_[kControlRomBase + addr] : 0U;
}

void SuperGameMegaTypeIII::write_expansion(Address addr, Byte value) {
    if (is_audio_address(addr))
        registers_.audio_enabled = (value & 1U) != 0U;
    else if (is_control_address(addr))
        registers_.control = value;
    else if (is_mapping_address(addr))
        registers_.mapping = value & 0x7FU;
}

Result<void> SuperGameMegaTypeIII::configure_board_jumpers(Byte value) {
    dip_switches_ = value;
    return {};
}

void SuperGameMegaTypeIII::set_coin_inserted(bool inserted) {
    coin_inserted_ = inserted;
}

bool SuperGameMegaTypeIII::audio_enabled() const {
    return registers_.audio_enabled;
}

void SuperGameMegaTypeIII::reset() {
    registers_.control = 0U;
    registers_.mapping = 0U;
    registers_.mmc1_shift = 0x10U;
    registers_.mmc1_control = 0x0CU;
    registers_.mmc1_chr0 = 0U;
    registers_.mmc1_chr1 = 0U;
    registers_.mmc1_prg = 0U;
    registers_.mmc3_select = 0U;
    registers_.mmc3_prg_mode = false;
    registers_.mmc3_chr_inversion = false;
    registers_.mmc3_banks.fill(0U);
    registers_.mmc3_mirror = MirrorMode::Vertical;
    registers_.mmc3_irq_counter = 0U;
    registers_.mmc3_irq_reload = 0U;
    registers_.mmc3_irq_reload_flag = false;
    registers_.mmc3_irq_enabled = false;
    registers_.mmc3_irq_pending = false;
    registers_.wram_enabled = true;
    registers_.wram_write_protected = false;
    registers_.audio_enabled = false;
}

void SuperGameMegaTypeIII::power_on() {
    reset();
    registers_.unrom_bank = 0U;
    registers_.amrom_bank = 0U;
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

bool SuperGameMegaTypeIII::irq_pending() const {
    return mapper_select() == 3U && registers_.mmc3_irq_pending;
}

void SuperGameMegaTypeIII::acknowledge_irq() {
    registers_.mmc3_irq_pending = false;
}

void SuperGameMegaTypeIII::clock_irq_counter() {
    if (registers_.mmc3_irq_counter == 0U || registers_.mmc3_irq_reload_flag) {
        registers_.mmc3_irq_counter = registers_.mmc3_irq_reload;
        registers_.mmc3_irq_reload_flag = false;
    } else {
        --registers_.mmc3_irq_counter;
    }
    if (registers_.mmc3_irq_counter == 0U && registers_.mmc3_irq_enabled)
        registers_.mmc3_irq_pending = true;
}

void SuperGameMegaTypeIII::save_state(StateWriter& writer) const {
    writer.write<uint16_t>(kStateTag);
    writer.write(registers_.control);
    writer.write(registers_.mapping);
    writer.write(registers_.unrom_bank);
    writer.write(registers_.amrom_bank);
    writer.write(registers_.mmc1_shift);
    writer.write(registers_.mmc1_control);
    writer.write(registers_.mmc1_chr0);
    writer.write(registers_.mmc1_chr1);
    writer.write(registers_.mmc1_prg);
    writer.write(registers_.mmc3_select);
    writer.write<Byte>((registers_.mmc3_prg_mode ? 0x40U : 0U) |
                       (registers_.mmc3_chr_inversion ? 0x80U : 0U));
    writer.write_array(registers_.mmc3_banks);
    writer.write<Byte>(static_cast<Byte>(registers_.mmc3_mirror));
    writer.write(registers_.mmc3_irq_counter);
    writer.write(registers_.mmc3_irq_reload);
    writer.write<Byte>((registers_.mmc3_irq_reload_flag ? 1U : 0U) |
                       (registers_.mmc3_irq_enabled ? 2U : 0U) |
                       (registers_.mmc3_irq_pending ? 4U : 0U));
    writer.write<Byte>((registers_.wram_enabled ? 0x80U : 0U) |
                       (registers_.wram_write_protected ? 0x40U : 0U));
    writer.write<Byte>((registers_.audio_enabled ? 1U : 0U) | (coin_inserted_ ? 2U : 0U));
    writer.write(dip_switches_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void SuperGameMegaTypeIII::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    Registers staged;
    reader.read(staged.control);
    reader.read(staged.mapping);
    reader.read(staged.unrom_bank);
    reader.read(staged.amrom_bank);
    reader.read(staged.mmc1_shift);
    reader.read(staged.mmc1_control);
    reader.read(staged.mmc1_chr0);
    reader.read(staged.mmc1_chr1);
    reader.read(staged.mmc1_prg);
    reader.read(staged.mmc3_select);
    const auto mmc3_mode = reader.read<Byte>();
    reader.read_array(staged.mmc3_banks);
    const auto mirror = reader.read<Byte>();
    reader.read(staged.mmc3_irq_counter);
    reader.read(staged.mmc3_irq_reload);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    const auto flags = reader.read<Byte>();
    const auto dips = reader.read<Byte>();
    std::array<Byte, 8192> prg_ram{};
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);

    const bool valid =
        reader.ok() && tag == kStateTag && staged.mapping <= 0x7FU && staged.unrom_bank <= 7U &&
        staged.amrom_bank <= 0x0FU && staged.mmc1_shift > 0U && staged.mmc1_shift <= 0x1FU &&
        staged.mmc1_control <= 0x1FU && staged.mmc1_chr0 <= 0x1FU && staged.mmc1_chr1 <= 0x1FU &&
        staged.mmc1_prg <= 0x1FU && staged.mmc3_select <= 7U && (mmc3_mode & 0x3FU) == 0U &&
        staged.mmc3_banks[6U] <= 0x3FU && staged.mmc3_banks[7U] <= 0x3FU &&
        mirror <= static_cast<Byte>(MirrorMode::Vertical) && irq <= 7U &&
        ((irq & 4U) == 0U || (irq & 2U) != 0U) && (ram & 0x3FU) == 0U && (flags & 0xFCU) == 0U;
    if (!valid) {
        reader.fail();
        return;
    }
    staged.mmc3_prg_mode = (mmc3_mode & 0x40U) != 0U;
    staged.mmc3_chr_inversion = (mmc3_mode & 0x80U) != 0U;
    staged.mmc3_mirror = static_cast<MirrorMode>(mirror);
    staged.mmc3_irq_reload_flag = (irq & 1U) != 0U;
    staged.mmc3_irq_enabled = (irq & 2U) != 0U;
    staged.mmc3_irq_pending = (irq & 4U) != 0U;
    staged.wram_enabled = (ram & 0x80U) != 0U;
    staged.wram_write_protected = (ram & 0x40U) != 0U;
    staged.audio_enabled = (flags & 1U) != 0U;
    registers_ = staged;
    coin_inserted_ = (flags & 2U) != 0U;
    dip_switches_ = dips;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
