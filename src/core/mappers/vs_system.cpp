#include "core/mappers/vs_system.hpp"

#include <algorithm>
#include <utility>

#include "core/cartridge/rom_crc32.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kBankSize = 8192U;
constexpr std::size_t kBankedPrgSize = 40960U;
} // namespace

VsSystem::VsSystem(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode),
      battery_(header.has_battery) {
    if (header.console_type == 1U)
        cabinet_.emplace(header.vs_hardware & 15U, header.expansion_device == 5U);
    if (header.mapper_number == 75U || header.mapper_number == 151U) {
        auto pins = header;
        pins.mapper_number = 75U;
        vrc_ = std::make_unique<KonamiVrc>(pins, std::move(prg_rom_), std::move(chr_rom_));
    }
    power_on();
}

Result<std::unique_ptr<Mapper>> VsSystem::create_vrc1(const INesHeader& header,
                                                      std::vector<Byte> prg,
                                                      std::vector<Byte> chr,
                                                      std::span<const Byte> misc) {
    if (header.mapper_number == 75U && header.console_type != 1U)
        return std::make_unique<KonamiVrc>(header, std::move(prg), std::move(chr));

    auto pins = header;
    if (!pins.is_nes2) {
        // Legacy headers omit the RGB PPU model and joystick harness.
        // Match both ROM sockets against the NES 2.0 database entries.
        if (prg.size() != 65536U || chr.size() != 65536U || pins.has_battery || pins.has_trainer)
            return std::unexpected("Unrecognized legacy Vs. VRC1 cartridge");
        const auto prg_crc = crc32(prg);
        const auto chr_crc = crc32(chr);
        if (prg_crc == 0xD99A2087U && chr_crc == 0x23CF2FC3U) {
            pins.vs_hardware = 2U; // Vs. Gradius, RP2C04-0001
            pins.expansion_device = 5U;
        } else if (prg_crc == 0x1E438D52U && chr_crc == 0x7221C249U) {
            pins.vs_hardware = 4U; // Vs. The Goonies, RP2C04-0003
            pins.expansion_device = 4U;
        } else {
            return std::unexpected(
                "Unknown legacy Vs. VRC1 ROM pair; supply a NES 2.0 arcade header");
        }
        pins.is_nes2 = true;
        pins.console_type = 1U;
        pins.region = Region::NTSC;
        pins.mirror_mode = MirrorMode::FourScreen;
        pins.prg_ram_size = 2048U;
    }
    if (pins.has_battery || !misc.empty() || pins.misc_rom_count)
        return std::unexpected("Vs. VRC1 requires ROM sockets and volatile cabinet RAM");
    if (auto valid = validate_geometry(pins, prg.size(), chr.size()); !valid)
        return std::unexpected(valid.error());
    return std::make_unique<VsSystem>(pins, std::move(prg), std::move(chr));
}

Result<void> VsSystem::validate_geometry(const INesHeader& header,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (header.console_type != 1U)
        return std::unexpected("Vs. cartridges require the Vs. System console flag");
    if (!header.is_nes2 || header.has_trainer || header.region != Region::NTSC ||
        header.mirror_mode != MirrorMode::FourScreen || header.submapper != 0U)
        return std::unexpected("Vs. cartridges require a four-screen NTSC NES 2.0 header");
    const Byte ppu = header.vs_hardware & 15U;
    if ((header.vs_hardware >> 4U) != 0U ||
        !(ppu == 0U || (ppu >= 2U && ppu <= 5U) || (ppu >= 9U && ppu <= 11U)))
        return std::unexpected("Unsupported Vs. cabinet or PPU revision");
    if (header.expansion_device != 0U && header.expansion_device != 4U &&
        header.expansion_device != 5U)
        return std::unexpected("Unsupported Vs. joystick harness");
    if (header.mapper_number == 75U || header.mapper_number == 151U) {
        if (prg_size != 65536U || chr_size != 65536U)
            return std::unexpected("Vs. VRC1 requires 64 KiB each of PRG and CHR ROM");
    } else if ((prg_size != 24576U && prg_size != 32768U && prg_size != 40960U) ||
               (chr_size != kBankSize && chr_size != 2U * kBankSize))
        return std::unexpected("Mapper 099 requires 24/32/40 KiB PRG and 8/16 KiB CHR ROM");
    if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Vs. ROM boards do not provide CHR RAM");
    if (header.prg_ram_size != (header.has_battery ? 0U : 2048U) ||
        header.prg_nvram_size != (header.has_battery ? 2048U : 0U))
        return std::unexpected(
            "Vs. cabinet requires 2 KiB shared RAM matching battery persistence");
    return {};
}

std::optional<std::size_t> VsSystem::prg_offset(Address addr) const {
    if (addr < 0x8000U)
        return std::nullopt;
    std::size_t bank = (addr - 0x8000U) / kBankSize;
    if (cabinet_ && prg_rom_.size() == 3U * kBankSize) {
        if (bank == 0U)
            return std::nullopt;
        --bank;
    }
    if (bank == 0U && prg_rom_.size() == kBankedPrgSize && bank_select_ != 0U)
        bank = 4U;
    const std::size_t offset = bank * kBankSize + (addr & 0x1FFFU);
    return offset < prg_rom_.size() ? std::optional<std::size_t>{offset} : std::nullopt;
}

Byte VsSystem::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return shared_ram_[addr & 0x07FFU];
    if (vrc_ && addr >= 0x8000U)
        return vrc_->read_prg(addr);
    if (const auto offset = prg_offset(addr))
        return prg_rom_[*offset];
    return 0U;
}

void VsSystem::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U)
        shared_ram_[addr & 0x07FFU] = value;
    else if (vrc_ && addr >= 0x8000U)
        vrc_->write_prg(addr, value);
}

bool VsSystem::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte VsSystem::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && !vrc_ && !prg_offset(addr) ? 0U : 0xFFU;
}

void VsSystem::observe_cpu_write(Address addr, Byte value) {
    if (cabinet_)
        cabinet_->observe_counter(addr, value);
    if (!vrc_ && addr == 0x4016U)
        bank_select_ = (value >> 2U) & 1U;
}

Byte VsSystem::read_chr(Address addr) {
    if (vrc_)
        return vrc_->read_chr(addr);
    const std::size_t offset =
        static_cast<std::size_t>(bank_select_) * kBankSize + (addr & 0x1FFFU);
    if (offset < chr_rom_.size())
        return chr_rom_[offset];
    // Undriven external PPU reads retain the multiplexed low address byte,
    // matching the core's existing unmapped-nametable policy.
    return static_cast<Byte>(addr);
}

void VsSystem::write_chr(Address, Byte) {}

MirrorMode VsSystem::mirror_mode() const {
    return mirror_mode_;
}

void VsSystem::power_on() {
    reset();
    if (!battery_)
        shared_ram_.fill(0U);
}

void VsSystem::reset() {
    if (vrc_)
        vrc_->reset();
    if (cabinet_)
        cabinet_->reset();
    bank_select_ = 0U;
}

std::span<const Byte> VsSystem::battery_ram() const {
    return battery_ ? std::span<const Byte>{shared_ram_} : std::span<const Byte>{};
}

void VsSystem::set_battery_ram(std::span<const Byte> data) {
    if (battery_ && data.size() == shared_ram_.size())
        std::ranges::copy(data, shared_ram_.begin());
}

uint16_t VsSystem::state_tag() const {
    if (vrc_)
        return 0x7500U;
    const auto mirror = mirror_mode_ == MirrorMode::FourScreen ? 2U
                        : mirror_mode_ == MirrorMode::Vertical ? 1U
                                                               : 0U;
    return static_cast<uint16_t>(0x6300U | (prg_rom_.size() / kBankSize - 1U) |
                                 ((chr_rom_.size() / kBankSize - 1U) << 3U) | (mirror << 4U) |
                                 (battery_ ? 0x40U : 0U) | (cabinet_ ? 0x80U : 0U));
}

void VsSystem::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(bank_select_);
    writer.write_array(shared_ram_);
    if (cabinet_)
        cabinet_->save_state(writer);
    if (vrc_)
        vrc_->save_state(writer);
}

void VsSystem::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto bank = reader.read<Byte>();
    std::array<Byte, 2048> shared_ram{};
    reader.read_array(shared_ram);
    if (!reader.ok() || tag != state_tag() || bank > (vrc_ ? 0U : 1U)) {
        reader.fail();
        return;
    }
    auto cabinet = cabinet_;
    if (cabinet) {
        cabinet->load_state(reader);
        if (!reader.ok())
            return;
    }
    if (vrc_) {
        StateWriter previous;
        vrc_->save_state(previous);
        vrc_->load_state(reader);
        if (!reader.ok()) {
            StateReader restore(previous.data());
            vrc_->load_state(restore);
            return;
        }
    }
    cabinet_ = cabinet;
    bank_select_ = bank;
    shared_ram_ = shared_ram;
}

} // namespace mapperbus::core
