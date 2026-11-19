#include "core/mappers/mapper595.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
Mapper595::Mapper595(const INesHeader&, std::vector<Byte> prg, std::vector<Byte>)
    : flash_(std::move(prg)) {}
Result<void> Mapper595::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 595 || !h.is_nes2 || h.submapper || prg != Sst39sf040::kSize || chr ||
        h.prg_ram_size != 8192 || h.prg_nvram_size || h.chr_ram_size != 16384 || h.chr_nvram_size ||
        h.has_trainer || h.mirror_mode != MirrorMode::FourScreen)
        return std::unexpected("NES-4MROM-512 requires 512 KiB flash, 8 KiB work RAM and 16 KiB "
                               "four-screen video RAM");
    return {};
}
bool Mapper595::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}
std::uint32_t Mapper595::flash_address(Address addr) const {
    return (addr < 0xC000U ? bank_ : 31U) * 16384U + (addr & 0x3FFFU);
}
Byte Mapper595::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return flash_.read(flash_address(addr));
    return addr >= 0x6000U ? wram_[addr & 0x1FFFU] : 0U;
}
void Mapper595::write_prg(Address addr, Byte value) {
    if (addr >= 0xC000U)
        bank_ = static_cast<Byte>((bank_ >> 1U) | ((value & 1U) << 4U));
    else if (addr >= 0x8000U)
        flash_.write(flash_address(addr), value);
    else if (addr >= 0x6000U)
        wram_[addr & 0x1FFFU] = value;
}
Byte Mapper595::read_chr(Address addr) {
    return video_ram_[addr & 0x1FFFU];
}
void Mapper595::write_chr(Address addr, Byte value) {
    video_ram_[addr & 0x1FFFU] = value;
}
bool Mapper595::unmirrored_nametables() const {
    return true;
}
std::optional<Byte> Mapper595::read_nametable(Address addr) const {
    return video_ram_[addr & 0x3FFFU];
}
bool Mapper595::write_nametable(Address addr, Byte value) {
    video_ram_[addr & 0x3FFFU] = value;
    return true;
}
MirrorMode Mapper595::mirror_mode() const {
    return MirrorMode::FourScreen;
}
void Mapper595::reset() {} // The serial register has no reset input.
void Mapper595::clock_cpu() {
    flash_.clock_cpu();
}
bool Mapper595::has_flash_storage() const {
    return true;
}
std::span<const Byte> Mapper595::battery_ram() const {
    return flash_.nvram();
}
void Mapper595::set_battery_ram(std::span<const Byte> data) {
    flash_.restore_nvram(data);
}
void Mapper595::save_state(StateWriter& w) const {
    w.write<Word>(595);
    w.write(bank_);
    w.write_array(wram_);
    w.write_array(video_ram_);
    flash_.save_state(w);
}
void Mapper595::load_state(StateReader& r) {
    auto tag = r.read<Word>();
    auto bank = r.read<Byte>();
    decltype(wram_) wram{};
    decltype(video_ram_) video_ram{};
    r.read_array(wram);
    r.read_array(video_ram);
    if (!r.ok() || tag != 595 || bank > 31) {
        r.fail();
        return;
    }
    auto flash = flash_;
    flash.load_state(r);
    if (r.ok()) {
        flash_ = std::move(flash);
        wram_ = wram;
        video_ram_ = video_ram;
        bank_ = bank;
    }
}
} // namespace mapperbus::core
