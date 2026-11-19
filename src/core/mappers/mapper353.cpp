#include "core/mappers/mapper353.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x040000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kMinChrSize = 0x020000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kOuterPrgSize = 0x040000UZ;
constexpr std::size_t kOuterChrSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kCartridgeRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3530U;

constexpr bool is_outer_register(Address addr) {
    return (addr & 0x8080U) == 0x8080U;
}

} // namespace

// NESdev mapper 353 oldid 7920 leaves the complete outer mask open. Decode the
// documented A15/A14-A13/A7 pins and ignore data. Nothing inhibits the MMC3 on
// those writes, so both devices receive them. ROM bounds describe address-pin
// envelopes; smaller fitted chips wrap after outer and inner decoding.
Mapper353::Mapper353(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper353::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 353U) {
        return std::unexpected("Mapper 353 requires mapper ID 353");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 353 requires NES 2.0 submapper 0");
    }
    if (!std::has_single_bit(prg_size) || prg_size < kMinPrgSize || prg_size > kMaxPrgSize) {
        return std::unexpected("Mapper 353 requires power-of-two 256-1024 KiB PRG ROM");
    }
    if (!std::has_single_bit(chr_size) || chr_size < kMinChrSize || chr_size > kMaxChrSize) {
        return std::unexpected("Mapper 353 requires power-of-two 128-512 KiB CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kCartridgeRamSize ||
        header.prg_nvram_size != 0U || header.chr_ram_size != kCartridgeRamSize ||
        header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 353 requires 8 KiB volatile PRG RAM and CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 353 requires ordinary H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper353::chr_bank(Address addr) const {
    auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    if ((registers_.select & 0x80U) != 0U) {
        slot ^= 4U;
    }
    if (slot < 4U) {
        return static_cast<Byte>((registers_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers_.banks[slot - 2U];
}

bool Mapper353::current_chr_a17() const {
    return (chr_bank(static_cast<Address>(registers_.ppu_chr_slot * kChrBankSize)) & 0x80U) != 0U;
}

Byte Mapper353::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((registers_.ram_flags & 0x80U) == 0U) {
            return 0U;
        }
        return prg_ram_[addr & 0x1FFFU];
    }
    if (addr < 0x8000U) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const auto outer = registers_.outer & 3U;
    const auto bank_count = outer == 2U ? 16U : 32U;
    const auto r6 = static_cast<unsigned>(registers_.banks[6] & 0x3FU) % bank_count;
    const auto r7 = static_cast<unsigned>(registers_.banks[7] & 0x3FU) % bank_count;
    unsigned bank = bank_count - 1U;
    if ((registers_.select & 0x40U) == 0U) {
        if (slot == 0U) {
            bank = r6;
        } else if (slot == 1U) {
            bank = r7;
        } else if (slot == 2U) {
            bank = bank_count - 2U;
        }
    } else {
        if (slot == 0U) {
            bank = bank_count - 2U;
        } else if (slot == 1U) {
            bank = r7;
        } else if (slot == 2U) {
            bank = r6;
        }
    }

    std::size_t offset =
        static_cast<std::size_t>(outer) * kOuterPrgSize + bank * kPrgBankSize + (addr & 0x1FFFU);
    if (outer == 2U && current_chr_a17()) {
        offset |= 0x20000UZ;
    } else if (outer == 3U && !current_chr_a17()) {
        offset = (offset & ~0x20000UZ) | (static_cast<std::size_t>(addr & 0x4000U) << 3U);
    }
    return prg_rom_[offset % prg_rom_.size()];
}

void Mapper353::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        if ((registers_.ram_flags & 0xC0U) == 0x80U) {
            prg_ram_[addr & 0x1FFFU] = value;
        }
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    if (is_outer_register(addr)) {
        registers_.outer = static_cast<Byte>((addr >> 13U) & 3U);
    }

    switch (addr & 0xE001U) {
    case 0x8000U:
        registers_.select = value & 0xC7U;
        break;
    case 0x8001U: {
        const auto index = registers_.select & 7U;
        registers_.banks[index] = index >= 6U ? value & 0x3FU : value;
        break;
    }
    case 0xA000U:
        registers_.mirror = value & 1U;
        break;
    case 0xA001U:
        registers_.ram_flags = value & 0xC0U;
        break;
    case 0xC000U:
        registers_.irq_reload = value;
        break;
    case 0xC001U:
        registers_.irq_counter = 0U;
        registers_.irq_flags |= 1U;
        break;
    case 0xE000U:
        registers_.irq_flags &= 1U;
        break;
    case 0xE001U:
        registers_.irq_flags |= 2U;
        break;
    default:
        break;
    }
}

bool Mapper353::maps_prg(Address addr) const {
    return addr >= 0x8000U ||
           (addr >= 0x6000U && addr < 0x8000U && (registers_.ram_flags & 0x80U) != 0U);
}

Byte Mapper353::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper353::read_chr(Address addr) {
    const auto bank = chr_bank(addr);
    if (registers_.outer == 2U && (bank & 0x80U) != 0U) {
        return chr_ram_[addr & 0x1FFFU];
    }
    const auto offset = static_cast<std::size_t>(registers_.outer) * kOuterChrSize +
                        static_cast<std::size_t>(bank & 0x7FU) * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mapper353::write_chr(Address addr, Byte value) {
    if (registers_.outer == 2U && (chr_bank(addr) & 0x80U) != 0U) {
        chr_ram_[addr & 0x1FFFU] = value;
    }
}

MirrorMode Mapper353::mirror_mode() const {
    if (registers_.outer != 0U) {
        return registers_.mirror != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
    const auto a = chr_bank(0x0000U) & 0x80U;
    const auto b = chr_bank(0x0400U) & 0x80U;
    const auto c = chr_bank(0x0800U) & 0x80U;
    const auto d = chr_bank(0x0C00U) & 0x80U;
    if (a == b && b == c && c == d) {
        return a != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    }
    if (a == b && c == d) {
        return MirrorMode::Horizontal;
    }
    return MirrorMode::Vertical;
}

std::optional<Address> Mapper353::nametable_address(Address addr) const {
    if (registers_.outer != 0U) {
        return std::nullopt;
    }
    return static_cast<Address>(((chr_bank(addr & 0x0FFFU) & 0x80U) << 3U) | (addr & 0x03FFU));
}

bool Mapper353::observes_ppu_address() const {
    return true;
}

void Mapper353::observe_ppu_address(Address addr) {
    registers_.ppu_chr_slot = static_cast<Byte>((addr >> 10U) & 7U);
}

bool Mapper353::maps_expansion(Address addr) const {
    return addr == 0x4023U || (addr >= 0x4040U && addr <= 0x4097U);
}

Byte Mapper353::expansion_read_mask(Address addr) const {
    return (addr >= 0x4040U && addr <= 0x407FU) || (addr >= 0x4090U && addr <= 0x4097U) ? 0xFFU
                                                                                        : 0U;
}

Byte Mapper353::read_expansion(Address addr) {
    return expansion_read_mask(addr) != 0U ? audio_.read(addr) : 0U;
}

void Mapper353::write_expansion(Address addr, Byte value) {
    if (addr == 0x4023U) {
        audio_.write(addr, value & 0x02U);
    } else if (addr >= 0x4040U && addr <= 0x408FU) {
        audio_.write(addr, value);
    }
}

bool Mapper353::has_expansion_audio() const {
    return true;
}

void Mapper353::clock_audio() {
    audio_.clock_audio();
}

float Mapper353::audio_output() const {
    return audio_.audio_output();
}

void Mapper353::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper353::reset() {
    registers_ = Registers{};
    registers_.mirror = initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U;
    audio_.reset();
}

bool Mapper353::irq_pending() const {
    return (registers_.irq_flags & 4U) != 0U;
}

void Mapper353::acknowledge_irq() {
    registers_.irq_flags &= 3U;
}

void Mapper353::clock_irq_counter() {
    if (registers_.irq_counter == 0U || (registers_.irq_flags & 1U) != 0U) {
        registers_.irq_counter = registers_.irq_reload;
        registers_.irq_flags &= 6U;
    } else {
        --registers_.irq_counter;
    }
    if (registers_.irq_counter == 0U && (registers_.irq_flags & 2U) != 0U) {
        registers_.irq_flags |= 4U;
    }
}

std::uint16_t Mapper353::state_tag() const {
    return kStateTag | (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U);
}

void Mapper353::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(registers_.outer);
    writer.write(registers_.select);
    writer.write_array(registers_.banks);
    writer.write(registers_.mirror);
    writer.write(registers_.irq_counter);
    writer.write(registers_.irq_reload);
    writer.write(registers_.irq_flags);
    writer.write(registers_.ram_flags);
    writer.write(registers_.ppu_chr_slot);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
    audio_.save_state(writer);
}

void Mapper353::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    Registers next;
    reader.read(next.outer);
    reader.read(next.select);
    reader.read_array(next.banks);
    reader.read(next.mirror);
    reader.read(next.irq_counter);
    reader.read(next.irq_reload);
    reader.read(next.irq_flags);
    reader.read(next.ram_flags);
    reader.read(next.ppu_chr_slot);
    std::array<Byte, kCartridgeRamSize> prg_ram{};
    std::array<Byte, kCartridgeRamSize> chr_ram{};
    reader.read_array(prg_ram);
    reader.read_array(chr_ram);
    Fds audio = audio_;
    audio.load_state(reader);

    const bool valid_registers = next.outer <= 3U && (next.select & 0x38U) == 0U &&
                                 next.banks[6] <= 0x3FU && next.banks[7] <= 0x3FU &&
                                 next.mirror <= 1U && next.irq_flags <= 7U &&
                                 ((next.irq_flags & 4U) == 0U || (next.irq_flags & 2U) != 0U) &&
                                 (next.ram_flags & 0x3FU) == 0U && next.ppu_chr_slot <= 7U;
    const bool sound_only = !audio.is_loaded() && audio.side_count() == 0 &&
                            audio.current_side() == -1 && audio.disk_image().empty() &&
                            !audio.disk_active() && !audio.irq_pending();
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !valid_registers || !sound_only) {
        reader.fail();
        return;
    }

    registers_ = next;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
    audio_ = std::move(audio);
}

} // namespace mapperbus::core
