#include "core/mappers/mapper474.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kChrSize = 0x2000UZ;
constexpr std::size_t kSaveRamSize = 0x2000UZ;
constexpr Address kSaveRamStart = 0x4020U;
constexpr Address kPrgWindowStart = 0x6000U;
constexpr std::uint16_t kStateTag = 0x4740U;

} // namespace

// Original implementation from NESdev Mapper 474 r23360 and its linked board
// description. Submapper 3 uses the persistent-storage hook for battery-less
// FRAM as well as the ordinary battery hook for SRAM; PRG flash is not writable.
Mapper474::Mapper474(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), submapper_(header.submapper),
      initial_mirror_(header.mirror_mode), battery_backed_(header.has_battery) {}

Result<void> Mapper474::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 474U) {
        return std::unexpected("Mapper 474 requires mapper ID 474");
    }
    if (!header.is_nes2 || header.submapper > 3U) {
        return std::unexpected("Mapper 474 requires NES 2.0 submapper 0-3");
    }
    constexpr std::array<std::size_t, 4> prg_sizes = {0xBFE0UZ, 0xB800UZ, 0xA000UZ, 0xA000UZ};
    if (prg_size != prg_sizes[header.submapper] || chr_size != kChrSize) {
        return std::unexpected(
            "Mapper 474 requires the submapper's exact PRG size and 8 KiB CHR ROM");
    }
    if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 474 requires CHR ROM without CHR RAM or NVRAM");
    }
    if (header.submapper == 3U) {
        if (header.prg_ram_size != 0U || header.prg_nvram_size != kSaveRamSize) {
            return std::unexpected("Mapper 474 submapper 3 requires 8 KiB PRG NVRAM");
        }
    } else if (header.prg_ram_size != 0U || header.prg_nvram_size != 0U || header.has_battery) {
        return std::unexpected("Mapper 474 submappers 0-2 have no cartridge RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 474 requires hard-wired H/V CIRAM metadata");
    }
    if (header.has_trainer) {
        return std::unexpected("Mapper 474 does not provide trainer RAM");
    }
    return {};
}

Address Mapper474::prg_start() const {
    switch (submapper_) {
    case 0U:
        return 0x4020U;
    case 1U:
        return 0x4800U;
    default:
        return kPrgWindowStart;
    }
}

bool Mapper474::has_save_ram() const {
    return submapper_ == 3U;
}

Byte Mapper474::read_prg(Address addr) {
    if (addr < kPrgWindowStart) {
        return 0U;
    }
    const auto offset = static_cast<std::size_t>(addr - prg_start());
    return offset < prg_rom_.size() ? prg_rom_[offset] : 0U;
}

void Mapper474::write_prg(Address /*addr*/, Byte /*value*/) {}

bool Mapper474::maps_prg(Address addr) const {
    return addr >= kPrgWindowStart;
}

Byte Mapper474::prg_read_mask(Address addr) const {
    return maps_prg(addr) ? 0xFFU : 0U;
}

Byte Mapper474::read_chr(Address addr) {
    return addr < chr_rom_.size() ? chr_rom_[addr] : 0U;
}

void Mapper474::write_chr(Address /*addr*/, Byte /*value*/) {}

MirrorMode Mapper474::mirror_mode() const {
    return initial_mirror_;
}

bool Mapper474::maps_expansion(Address addr) const {
    return addr >= kSaveRamStart && addr < kPrgWindowStart &&
           (has_save_ram() || addr >= prg_start());
}

Byte Mapper474::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0xFFU : 0U;
}

Byte Mapper474::read_expansion(Address addr) {
    if (!maps_expansion(addr)) {
        return 0U;
    }
    if (has_save_ram()) {
        return save_ram_[addr - kSaveRamStart];
    }
    const auto offset = static_cast<std::size_t>(addr - prg_start());
    return offset < prg_rom_.size() ? prg_rom_[offset] : 0U;
}

void Mapper474::write_expansion(Address addr, Byte value) {
    if (has_save_ram() && maps_expansion(addr)) {
        save_ram_[addr - kSaveRamStart] = value;
    }
}

void Mapper474::reset() {}

bool Mapper474::has_flash_storage() const {
    return has_save_ram() && !battery_backed_;
}

std::span<const Byte> Mapper474::battery_ram() const {
    return has_save_ram() ? std::span<const Byte>(save_ram_) : std::span<const Byte>{};
}

void Mapper474::set_battery_ram(std::span<const Byte> data) {
    if (has_save_ram() && data.size() == kSaveRamSize) {
        std::ranges::copy(data, save_ram_.begin());
    }
}

std::uint16_t Mapper474::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | submapper_ |
                                      (initial_mirror_ == MirrorMode::Vertical ? 0x10U : 0U) |
                                      (has_save_ram() && battery_backed_ ? 0x20U : 0U));
}

void Mapper474::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    if (has_save_ram()) {
        writer.write_array(save_ram_);
    }
}

void Mapper474::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    std::array<Byte, kSaveRamSize> save_ram{};
    if (has_save_ram()) {
        reader.read_array(save_ram);
    }

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size()) {
        reader.fail();
        return;
    }
    if (has_save_ram()) {
        save_ram_ = save_ram;
    }
}

} // namespace mapperbus::core
