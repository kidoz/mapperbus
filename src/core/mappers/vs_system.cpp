#include "core/mappers/vs_system.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t kBankSize = 8192U;
constexpr std::size_t kBankedPrgSize = 40960U;
} // namespace

VsSystem::VsSystem(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_mode_(header.mirror_mode),
      battery_(header.has_battery) {
    power_on();
}

Result<void> VsSystem::validate_geometry(const INesHeader& header,
                                         std::size_t prg_size,
                                         std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 099 supports submapper 0");
    if (prg_size < kBankSize || prg_size > kBankedPrgSize || prg_size % kBankSize != 0U ||
        (chr_size != kBankSize && chr_size != 2U * kBankSize))
        return std::unexpected(
            "Mapper 099 requires 8-40 KiB PRG in 8 KiB steps and 8/16 KiB CHR ROM");
    if (header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 099 does not provide CHR RAM");
    if (header.is_nes2 && (header.prg_ram_size != (header.has_battery ? 0U : 2048U) ||
                           header.prg_nvram_size != (header.has_battery ? 2048U : 0U)))
        return std::unexpected("Mapper 099 requires 2 KiB shared RAM matching battery persistence");
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical && header.mirror_mode != MirrorMode::FourScreen)
        return std::unexpected("Mapper 099 requires fixed H/V or four-screen mirroring");
    return {};
}

std::optional<std::size_t> VsSystem::prg_offset(Address addr) const {
    if (addr < 0x8000U)
        return std::nullopt;
    std::size_t bank = (addr - 0x8000U) / kBankSize;
    if (bank == 0U && prg_rom_.size() == kBankedPrgSize && bank_select_ != 0U)
        bank = 4U;
    const std::size_t offset = bank * kBankSize + (addr & 0x1FFFU);
    return offset < prg_rom_.size() ? std::optional<std::size_t>{offset} : std::nullopt;
}

Byte VsSystem::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U)
        return shared_ram_[addr & 0x07FFU];
    if (const auto offset = prg_offset(addr))
        return prg_rom_[*offset];
    return 0U;
}

void VsSystem::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U)
        shared_ram_[addr & 0x07FFU] = value;
}

bool VsSystem::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte VsSystem::prg_read_mask(Address addr) const {
    return addr >= 0x8000U && !prg_offset(addr) ? 0U : 0xFFU;
}

void VsSystem::observe_cpu_write(Address addr, Byte value) {
    if (addr == 0x4016U)
        bank_select_ = (value >> 2U) & 1U;
}

Byte VsSystem::read_chr(Address addr) {
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
    bank_select_ = 0U;
    if (!battery_)
        shared_ram_.fill(0U);
}

void VsSystem::reset() {
    // Deterministic OUT2 reset policy; the shared RAM is not cleared.
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
    const auto mirror = mirror_mode_ == MirrorMode::FourScreen ? 2U
                        : mirror_mode_ == MirrorMode::Vertical ? 1U
                                                               : 0U;
    return static_cast<uint16_t>(0x6300U | (prg_rom_.size() / kBankSize - 1U) |
                                 ((chr_rom_.size() / kBankSize - 1U) << 3U) | (mirror << 4U) |
                                 (battery_ ? 0x40U : 0U));
}

void VsSystem::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(bank_select_);
    writer.write_array(shared_ram_);
}

void VsSystem::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto bank = reader.read<Byte>();
    std::array<Byte, 2048> shared_ram{};
    reader.read_array(shared_ram);
    if (!reader.ok() || tag != state_tag() || bank > 1U) {
        reader.fail();
        return;
    }
    bank_select_ = bank;
    shared_ram_ = shared_ram;
}

} // namespace mapperbus::core
