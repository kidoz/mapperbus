#include "core/mappers/mapper523.hpp"

#include <bit>
#include <charconv>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxRomSize = 0x1000000UZ;
constexpr std::size_t kWramBankSize = 0x4000UZ;
constexpr std::size_t kMaxFs005StateSize = 60UZ + 2UZ * kWramBankSize;
constexpr std::uint16_t kStateTag = 0x5230U;

bool power_of_two(std::size_t size) {
    return size != 0U && std::has_single_bit(size);
}

INesHeader fs005_header(const INesHeader& header) {
    auto result = header;
    result.mapper_number = 176U;
    result.is_nes2 = true;
    result.submapper = 2U;
    result.chr_rom_size = 0U;
    result.chr_ram_size = 0U;
    result.chr_nvram_size = 0U;
    return result;
}

} // namespace

std::optional<Mapper523::ShadowState> Mapper523::parse_shadow(std::span<const Byte> bytes) {
    StateReader reader(bytes);
    (void)reader.read<std::uint16_t>();
    (void)reader.read<std::uint32_t>();
    (void)reader.read<std::uint32_t>();
    Mapper523::ShadowState state;
    state.mode = reader.read<Byte>();
    (void)reader.read<Byte>();
    state.chr_base = reader.read<Byte>();
    (void)reader.read<Byte>();
    (void)reader.read<Byte>();
    state.extended = reader.read<Byte>();
    (void)reader.read<Byte>();
    (void)reader.read<Byte>();
    state.bank_select = reader.read<Byte>();
    reader.read_array(state.banks);
    (void)reader.read<Byte>();
    state.ram_register = reader.read<Byte>();
    (void)reader.read<Byte>();
    (void)reader.read<Byte>();
    (void)reader.read<Byte>();
    std::vector<Byte> ram;
    reader.read_bytes(ram, 0U);
    reader.read_bytes(ram, kWramBankSize);
    reader.read_bytes(ram, kWramBankSize);
    if (!reader.ok() || reader.remaining() != 0U) {
        return std::nullopt;
    }
    return state;
}

// Original implementation from NESdev NES 2.0 Mapper 523 oldid 23888.
// The FS005 core retains its documented PRG/WRAM/IRQ behavior. CHR-ROM A10
// comes directly from PPU A10; clone outputs A10-A17 drive CHR A11-A18.
Mapper523::Mapper523(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : initial_mirror_(header.mirror_mode),
      fs005_(fs005_header(header), std::move(prg_rom), std::vector<Byte>{}),
      chr_rom_(std::move(chr_rom)) {
    reset_shadow();
}

Result<void> Mapper523::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 523U) {
        return std::unexpected("Mapper 523 requires mapper ID 523");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 523 requires NES 2.0 submapper 0");
    }
    if (prg_size < kMinPrgSize || prg_size > kMaxRomSize || !power_of_two(prg_size) ||
        chr_size < kMinChrSize || chr_size > kMaxRomSize || !power_of_two(chr_size)) {
        return std::unexpected(
            "Mapper 523 requires power-of-two 32 KiB-16 MiB PRG and 8 KiB-16 MiB CHR ROM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 523 requires hard-wired horizontal or vertical CIRAM");
    }
    if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 523 requires CHR ROM without CHR RAM or NVRAM");
    }
    const bool has_wram =
        header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery;
    if (has_wram && (header.prg_ram_size != kWramBankSize ||
                     header.prg_nvram_size != kWramBankSize || !header.has_battery)) {
        return std::unexpected(
            "Mapper 523 WRAM is 16 KiB volatile plus 16 KiB battery-backed NVRAM");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 523 does not provide trainer RAM");
    }
    return {};
}

Byte Mapper523::read_prg(Address addr) {
    return fs005_.read_prg(addr);
}

void Mapper523::write_prg(Address addr, Byte value) {
    fs005_.write_prg(addr, value);
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xE003U) {
    case 0x8000U:
        shadow_.bank_select = value & 0xCFU;
        break;
    case 0x8001U: {
        const unsigned index = shadow_.bank_select & (extended_mode() ? 0x0FU : 0x07U);
        if (extended_mode()) {
            shadow_.banks[index] = value;
        } else {
            shadow_.banks[index] = index >= 6U ? value & 0x3FU : value;
        }
        break;
    }
    case 0xA001U:
        shadow_.ram_register = value;
        break;
    default:
        break;
    }
}

bool Mapper523::maps_prg(Address addr) const {
    return fs005_.maps_prg(addr);
}

std::uint32_t Mapper523::chr_address(Address addr) const {
    const std::uint32_t base = shadow_.chr_base;
    if ((shadow_.mode & 0x40U) != 0U) {
        return (base << 14U) | ((addr & 0x1800U) << 1U) | (addr & 0x07FFU);
    }

    const unsigned physical_slot = (addr >> 10U) & 6U;
    const unsigned slot = physical_slot ^ ((shadow_.bank_select & 0x80U) != 0U ? 4U : 0U);
    unsigned inner = 0U;
    if (extended_mode()) {
        constexpr std::array<Byte, 8> kSelectors = {0U, 10U, 1U, 11U, 2U, 3U, 4U, 5U};
        inner = shadow_.banks[kSelectors[slot]];
    } else if (slot < 4U) {
        inner = (shadow_.banks[slot >> 1U] & 0xFEU) | (slot & 1U);
    } else {
        inner = shadow_.banks[slot - 2U];
    }
    const std::uint32_t mask = (shadow_.mode & 0x10U) != 0U ? 0x7FU : 0xFFU;
    const std::uint32_t bank = (inner & mask) | ((base << 3U) & ~mask);
    return (bank << 11U) | (addr & 0x07FFU);
}

Byte Mapper523::read_chr(Address addr) {
    return chr_rom_.empty() ? 0U : chr_rom_[chr_address(addr) % chr_rom_.size()];
}

void Mapper523::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper523::mirror_mode() const {
    return initial_mirror_;
}

bool Mapper523::maps_expansion(Address addr) const {
    return fs005_.maps_expansion(addr);
}

Byte Mapper523::expansion_read_mask(Address addr) const {
    return fs005_.expansion_read_mask(addr);
}

Byte Mapper523::read_expansion(Address addr) {
    return fs005_.read_expansion(addr);
}

bool Mapper523::extended_mode() const {
    return (shadow_.extended & 2U) != 0U;
}

bool Mapper523::ram_configuration() const {
    return (shadow_.ram_register & 0x20U) != 0U;
}

bool Mapper523::outer_registers_enabled() const {
    return !ram_configuration() || (shadow_.ram_register & 0x40U) != 0U;
}

std::optional<unsigned> Mapper523::outer_register(Address addr) const {
    if (addr < 0x5000U || addr >= 0x6000U) {
        return std::nullopt;
    }
    const Address selected = static_cast<Address>(0x10U << solder_pad_);
    const Address mask = static_cast<Address>(0x5003U | selected);
    const unsigned index = addr & 3U;
    if ((addr & mask) != static_cast<Address>(0x5000U | selected | index)) {
        return std::nullopt;
    }
    return index;
}

void Mapper523::write_outer_register(unsigned index, Byte value) {
    if (index == 0U) {
        shadow_.mode = value;
    } else if (index == 2U) {
        shadow_.chr_base = value;
    } else if (index == 3U) {
        shadow_.extended = value & 2U;
    }
}

void Mapper523::write_expansion(Address addr, Byte value) {
    fs005_.write_expansion(addr, value);
    if (outer_registers_enabled()) {
        if (const auto reg = outer_register(addr)) {
            write_outer_register(*reg, value);
        }
    }
}

Result<void> Mapper523::configure_board_jumpers(Byte value) {
    auto result = fs005_.configure_board_jumpers(value);
    if (result) {
        solder_pad_ = value;
    }
    return result;
}

Result<void> Mapper523::configure_board_profile(std::string_view profile) {
    if (!profile.starts_with("menu-")) {
        return std::unexpected("Mapper 523 profiles are menu-0 through menu-7");
    }
    profile.remove_prefix(5U);
    unsigned value = 0U;
    const auto [end, error] =
        std::from_chars(profile.data(), profile.data() + profile.size(), value);
    if (error != std::errc{} || end != profile.data() + profile.size() || value > 7U) {
        return std::unexpected("Mapper 523 profiles are menu-0 through menu-7");
    }
    return configure_board_jumpers(static_cast<Byte>(value));
}

void Mapper523::power_on() {
    fs005_.power_on();
    reset_shadow();
}

void Mapper523::reset_shadow() {
    shadow_ = ShadowState{};
}

void Mapper523::reset() {
    fs005_.reset();
    reset_shadow();
}

bool Mapper523::irq_pending() const {
    return fs005_.irq_pending();
}

void Mapper523::acknowledge_irq() {
    fs005_.acknowledge_irq();
}

void Mapper523::clock_irq_counter() {
    fs005_.clock_irq_counter();
}

std::span<const Byte> Mapper523::battery_ram() const {
    return fs005_.battery_ram();
}

void Mapper523::set_battery_ram(std::span<const Byte> data) {
    fs005_.set_battery_ram(data);
}

void Mapper523::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(solder_pad_);
    writer.write<Byte>(initial_mirror_ == MirrorMode::Horizontal ? 1U : 0U);
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    StateWriter core_writer;
    fs005_.save_state(core_writer);
    writer.write_bytes(core_writer.data());
}

void Mapper523::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto solder_pad = reader.read<Byte>();
    const auto mirror = reader.read<Byte>();
    const auto chr_size = reader.read<std::uint32_t>();
    std::vector<Byte> core_state;
    reader.read_bytes(core_state, kMaxFs005StateSize);
    if (!reader.ok() || reader.remaining() != 0U || tag != kStateTag || solder_pad != solder_pad_ ||
        mirror > 1U || chr_size != chr_rom_.size() ||
        (mirror == 1U) != (initial_mirror_ == MirrorMode::Horizontal)) {
        reader.fail();
        return;
    }

    const auto shadow = parse_shadow(core_state);
    if (!shadow) {
        reader.fail();
        return;
    }
    StateReader core_reader(core_state);
    fs005_.load_state(core_reader);
    if (!core_reader.ok() || core_reader.remaining() != 0U) {
        reader.fail();
        return;
    }
    shadow_ = *shadow;
}

} // namespace mapperbus::core
