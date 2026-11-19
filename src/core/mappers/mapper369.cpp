#include "core/mappers/mapper369.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kPrgNvramSize = 0x2000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr Address kOuterMask = 0xC100U;
constexpr Address kOuterValue = 0x4100U;
constexpr std::uint16_t kNtdecIrqMask = 0x1FFFU;
constexpr std::uint16_t kNtdecIrqPending = 0x1000U;
constexpr std::uint16_t kStateTag = 0x3690U;
constexpr Byte kIrqReload = 1U;
constexpr Byte kIrqEnabled = 2U;
constexpr Byte kIrqPending = 4U;

} // namespace

// NESdev mapper 369 oldid 7975 supplies the five outer values and their offsets.
// Other values are ignored because the article explicitly leaves their bits unknown.
Mapper369::Mapper369(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper369::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 369U) {
        return std::unexpected("Mapper 369 requires mapper ID 369");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 369 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgSize || chr_size != kChrSize) {
        return std::unexpected("Mapper 369 requires exactly 512 KiB PRG and 512 KiB CHR ROM");
    }
    if (!header.has_battery || header.prg_ram_size != 0U ||
        header.prg_nvram_size != kPrgNvramSize || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U) {
        return std::unexpected(
            "Mapper 369 requires a battery and exactly 8 KiB PRG NVRAM without other RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 369 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

std::optional<Mapper369::Mode> Mapper369::mode_for_outer(Byte value) {
    switch (value) {
    case 0x00U:
        return Mode::Nrom128;
    case 0x01U:
        return Mode::Nrom256;
    case 0x13U:
        return Mode::Ntdec;
    case 0x37U:
        return Mode::Mmc3Tlrom;
    case 0xFFU:
        return Mode::Mmc3Tkrom;
    default:
        return std::nullopt;
    }
}

bool Mapper369::is_outer_register(Address addr) {
    return (addr & kOuterMask) == kOuterValue;
}

bool Mapper369::is_mmc3() const {
    return mode_ == Mode::Mmc3Tlrom || mode_ == Mode::Mmc3Tkrom;
}

std::size_t Mapper369::prg_base() const {
    switch (mode_) {
    case Mode::Nrom128:
        return 0x00000UZ;
    case Mode::Nrom256:
        return 0x02000UZ;
    case Mode::Ntdec:
        return 0x10000UZ;
    case Mode::Mmc3Tlrom:
        return 0x20000UZ;
    case Mode::Mmc3Tkrom:
        return 0x40000UZ;
    }
    std::unreachable();
}

std::size_t Mapper369::chr_base() const {
    switch (mode_) {
    case Mode::Nrom128:
        return 0x00000UZ;
    case Mode::Nrom256:
        return 0x02000UZ;
    case Mode::Ntdec:
        return 0x06000UZ;
    case Mode::Mmc3Tlrom:
        return 0x20000UZ;
    case Mode::Mmc3Tkrom:
        return 0x40000UZ;
    }
    std::unreachable();
}

Byte Mapper369::read_prg(Address addr) {
    if (addr < 0x6000U) {
        return 0U;
    }

    if (mode_ == Mode::Nrom128 || mode_ == Mode::Nrom256) {
        if (addr < 0x8000U) {
            return 0U;
        }
        const auto offset = mode_ == Mode::Nrom128 ? static_cast<std::size_t>(addr & 0x3FFFU)
                                                   : static_cast<std::size_t>(addr - 0x8000U);
        return prg_rom_[prg_base() + offset];
    }

    if (mode_ == Mode::Ntdec) {
        unsigned bank = 7U;
        if (addr < 0x8000U) {
            bank = 6U;
        } else if (addr < 0xA000U) {
            bank = 4U;
        } else if (addr < 0xC000U) {
            bank = 5U;
        } else if (addr < 0xE000U) {
            bank = registers_.ntdec.prg_bank;
        }
        const auto offset =
            prg_base() + static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
        return prg_rom_[offset];
    }

    if (addr < 0x8000U) {
        return registers_.mmc3.ram_enabled ? prg_nvram_[addr - 0x6000U] : 0U;
    }
    const auto offset = prg_base() + static_cast<std::size_t>(mmc3_prg_bank(addr)) * kPrgBankSize +
                        (addr & 0x1FFFU);
    return prg_rom_[offset];
}

unsigned Mapper369::mmc3_prg_bank(Address addr) const {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned bank_count = mode_ == Mode::Mmc3Tlrom ? 16U : 32U;
    const unsigned mask = bank_count - 1U;
    const bool swapped = (registers_.mmc3.bank_select & 0x40U) != 0U;
    switch (slot) {
    case 0U:
        return swapped ? bank_count - 2U : registers_.mmc3.banks[6U] & mask;
    case 1U:
        return registers_.mmc3.banks[7U] & mask;
    case 2U:
        return swapped ? registers_.mmc3.banks[6U] & mask : bank_count - 2U;
    default:
        return bank_count - 1U;
    }
}

void Mapper369::write_prg(Address addr, Byte value) {
    write_outer(addr, value);
    if (addr < 0x6000U) {
        return;
    }
    if (addr < 0x8000U) {
        if (is_mmc3() && registers_.mmc3.ram_enabled && !registers_.mmc3.ram_write_protected) {
            prg_nvram_[addr - 0x6000U] = value;
        }
        return;
    }
    if (mode_ == Mode::Ntdec) {
        write_ntdec(addr, value);
    } else if (is_mmc3()) {
        write_mmc3(addr, value);
    }
}

void Mapper369::write_outer(Address addr, Byte value) {
    if (!is_outer_register(addr)) {
        return;
    }
    if (const auto mode = mode_for_outer(value)) {
        registers_.outer = value;
        mode_ = *mode;
    }
}

void Mapper369::write_ntdec(Address addr, Byte value) {
    switch (addr & 0xE000U) {
    case 0x8000U:
        registers_.ntdec.irq_enabled = false;
        registers_.ntdec.irq_counter = 0U;
        break;
    case 0xA000U:
        registers_.ntdec.irq_enabled = true;
        break;
    case 0xE000U:
        registers_.ntdec.prg_bank = value & 7U;
        break;
    default:
        break;
    }
}

void Mapper369::write_mmc3(Address addr, Byte value) {
    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.mmc3.bank_select = value & 0xC7U;
        break;
    case 0x8001U: {
        const unsigned index = registers_.mmc3.bank_select & 7U;
        registers_.mmc3.banks[index] = index >= 6U ? value & 0x1FU : value;
        break;
    }
    case 0xA000U:
        registers_.mmc3.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        break;
    case 0xA001U:
        registers_.mmc3.ram_enabled = (value & 0x80U) != 0U;
        registers_.mmc3.ram_write_protected = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        registers_.mmc3.irq_latch = value;
        break;
    case 0xC001U:
        registers_.mmc3.irq_counter = 0U;
        registers_.mmc3.irq_reload = true;
        break;
    case 0xE000U:
        registers_.mmc3.irq_enabled = false;
        registers_.mmc3.irq_pending = false;
        break;
    case 0xE001U:
        registers_.mmc3.irq_enabled = true;
        break;
    default:
        break;
    }
}

bool Mapper369::maps_prg(Address addr) const {
    if (addr >= 0x8000U) {
        return true;
    }
    if (addr < 0x6000U) {
        return false;
    }
    if (mode_ == Mode::Ntdec) {
        return true;
    }
    return is_mmc3() && registers_.mmc3.ram_enabled;
}

Byte Mapper369::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper369::read_chr(Address addr) {
    if (!is_mmc3()) {
        return chr_rom_[chr_base() + (addr & 0x1FFFU)];
    }
    const auto offset = chr_base() + static_cast<std::size_t>(mmc3_chr_bank(addr)) * kChrBankSize +
                        (addr & 0x03FFU);
    return chr_rom_[offset];
}

unsigned Mapper369::mmc3_chr_bank(Address addr) const {
    const auto pattern_addr = static_cast<Address>(addr & 0x1FFFU);
    const unsigned slot =
        ((pattern_addr >> 10U) & 7U) ^ ((registers_.mmc3.bank_select & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (registers_.mmc3.banks[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return registers_.mmc3.banks[slot - 2U];
}

void Mapper369::write_chr(Address, Byte) {}

MirrorMode Mapper369::mirror_mode() const {
    return is_mmc3() ? registers_.mmc3.mirror : initial_mirror_;
}

bool Mapper369::maps_expansion(Address addr) const {
    return addr >= 0x4020U && addr < 0x6000U && is_outer_register(addr);
}

Byte Mapper369::expansion_read_mask(Address) const {
    return 0U;
}

Byte Mapper369::read_expansion(Address) {
    return 0U;
}

void Mapper369::write_expansion(Address addr, Byte value) {
    write_outer(addr, value);
}

void Mapper369::power_on() {
    reset();
}

void Mapper369::reset() {
    registers_ = Registers{};
    registers_.mmc3.mirror = initial_mirror_;
    mode_ = Mode::Nrom128;
}

bool Mapper369::irq_pending() const {
    if (mode_ == Mode::Ntdec) {
        return registers_.ntdec.irq_enabled &&
               (registers_.ntdec.irq_counter & kNtdecIrqPending) != 0U;
    }
    return is_mmc3() && registers_.mmc3.irq_pending;
}

void Mapper369::acknowledge_irq() {
    if (is_mmc3()) {
        registers_.mmc3.irq_pending = false;
    }
}

void Mapper369::clock_irq_counter() {
    if (!is_mmc3()) {
        return;
    }
    if (registers_.mmc3.irq_counter == 0U || registers_.mmc3.irq_reload) {
        registers_.mmc3.irq_counter = registers_.mmc3.irq_latch;
        registers_.mmc3.irq_reload = false;
    } else {
        --registers_.mmc3.irq_counter;
    }
    if (registers_.mmc3.irq_counter == 0U && registers_.mmc3.irq_enabled) {
        registers_.mmc3.irq_pending = true;
    }
}

void Mapper369::clock_cpu() {
    if (mode_ == Mode::Ntdec && registers_.ntdec.irq_enabled) {
        registers_.ntdec.irq_counter = (registers_.ntdec.irq_counter + 1U) & kNtdecIrqMask;
    }
}

std::span<const Byte> Mapper369::battery_ram() const {
    return prg_nvram_;
}

void Mapper369::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == prg_nvram_.size()) {
        std::copy(data.begin(), data.end(), prg_nvram_.begin());
    }
}

std::uint16_t Mapper369::memory_state_tag() const {
    return static_cast<std::uint16_t>(2U | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper369::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(memory_state_tag());
    writer.write(registers_.outer);
    writer.write(registers_.ntdec.prg_bank);
    writer.write(registers_.ntdec.irq_counter);
    writer.write<Byte>(registers_.ntdec.irq_enabled ? 1U : 0U);
    writer.write(registers_.mmc3.bank_select);
    writer.write_array(registers_.mmc3.banks);
    writer.write<Byte>(registers_.mmc3.mirror == MirrorMode::Vertical ? 1U : 0U);
    writer.write(registers_.mmc3.irq_counter);
    writer.write(registers_.mmc3.irq_latch);
    writer.write<Byte>((registers_.mmc3.irq_reload ? kIrqReload : 0U) |
                       (registers_.mmc3.irq_enabled ? kIrqEnabled : 0U) |
                       (registers_.mmc3.irq_pending ? kIrqPending : 0U));
    writer.write<Byte>((registers_.mmc3.ram_enabled ? 0x80U : 0U) |
                       (registers_.mmc3.ram_write_protected ? 0x40U : 0U));
    writer.write_array(prg_nvram_);
}

void Mapper369::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto memory_tag = reader.read<std::uint16_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.ntdec.prg_bank);
    reader.read(next.ntdec.irq_counter);
    const auto ntdec_irq_enabled = reader.read<Byte>();
    reader.read(next.mmc3.bank_select);
    reader.read_array(next.mmc3.banks);
    const auto mirror = reader.read<Byte>();
    reader.read(next.mmc3.irq_counter);
    reader.read(next.mmc3.irq_latch);
    const auto irq_flags = reader.read<Byte>();
    const auto ram_flags = reader.read<Byte>();
    std::array<Byte, 0x2000> next_nvram{};
    reader.read_array(next_nvram);

    const auto next_mode = mode_for_outer(next.outer);
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || memory_tag != memory_state_tag() || !next_mode ||
        next.ntdec.prg_bank > 7U || next.ntdec.irq_counter > kNtdecIrqMask ||
        ntdec_irq_enabled > 1U || (ntdec_irq_enabled == 0U && next.ntdec.irq_counter != 0U) ||
        (next.mmc3.bank_select & 0x38U) != 0U || next.mmc3.banks[6U] > 0x1FU ||
        next.mmc3.banks[7U] > 0x1FU || mirror > 1U || irq_flags > 7U ||
        ((irq_flags & kIrqPending) != 0U && (irq_flags & kIrqEnabled) == 0U) ||
        (ram_flags & 0x3FU) != 0U) {
        reader.fail();
        return;
    }

    next.ntdec.irq_enabled = ntdec_irq_enabled != 0U;
    next.mmc3.mirror = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    next.mmc3.irq_reload = (irq_flags & kIrqReload) != 0U;
    next.mmc3.irq_enabled = (irq_flags & kIrqEnabled) != 0U;
    next.mmc3.irq_pending = (irq_flags & kIrqPending) != 0U;
    next.mmc3.ram_enabled = (ram_flags & 0x80U) != 0U;
    next.mmc3.ram_write_protected = (ram_flags & 0x40U) != 0U;
    registers_ = next;
    prg_nvram_ = next_nvram;
    mode_ = *next_mode;
}

} // namespace mapperbus::core
