#include "core/mappers/mapper555.hpp"

#include "core/state/state.hpp"
namespace mapperbus::core {
Result<void> Mapper555::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (h.mapper_number != 555 || !h.is_nes2 || h.submapper || h.has_battery || h.has_trainer ||
        prg != 524288 || chr != 262144 || h.prg_ram_size != 16384 || h.prg_nvram_size ||
        h.chr_nvram_size || h.chr_ram_size != 8192 ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected(
            "Campus Challenge requires 512/256 KiB ROM, 16 KiB work RAM and 8 KiB CHR RAM");
    return {};
}
Byte Mapper555::read_prg(Address addr) {
    if (addr < 0x8000U)
        return Mmc3::read_prg(addr);
    const unsigned mask = 7U | ((game_ & 3U) << 3U);
    const unsigned bank = ((game_ & 4U) << 3U) | (prg_bank(addr) & mask);
    return physical_prg(bank * 8192U + (addr & 8191U));
}
bool Mapper555::maps_pattern_ram(Byte bank) const {
    return (game_ & 6U) == 2U && (bank & 0x40U);
}
Byte Mapper555::read_chr(Address addr) {
    auto bank = chr_bank(addr);
    if (maps_pattern_ram(bank))
        return pattern_ram_[(bank & 7U) * 1024U + (addr & 1023U)];
    return physical_chr(((bank & 127U) | ((game_ & 4U) << 5U)) * 1024U + (addr & 1023U));
}
void Mapper555::write_chr(Address addr, Byte value) {
    auto bank = chr_bank(addr);
    if (maps_pattern_ram(bank))
        pattern_ram_[(bank & 7U) * 1024U + (addr & 1023U)] = value;
}
bool Mapper555::maps_expansion(Address addr) const {
    return (addr & 0xF000U) == 0x5000U;
}
Byte Mapper555::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0xFFU : 0U;
}
Byte Mapper555::read_expansion(Address addr) {
    if (!maps_expansion(addr))
        return 0;
    return (addr & 0x800U) ? static_cast<Byte>(0x5CU | (elapsed_ == timeout() ? 0x80U : 0U))
                           : scratch_[addr & 2047U];
}
void Mapper555::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr))
        return;
    if (!(addr & 0x800U))
        scratch_[addr & 2047U] = value;
    else if (!(addr & 0x400U)) {
        game_ = value & 15U;
        if (!(game_ & 8U))
            elapsed_ = 0;
    }
}
std::uint32_t Mapper555::timeout() const {
    return (16U + time_switches_) << 25U;
}
Result<void> Mapper555::configure_board_jumpers(Byte value) {
    if (value > 15)
        return std::unexpected("Campus Challenge has four timer switches");
    time_switches_ = value;
    if (elapsed_ > timeout())
        elapsed_ = timeout();
    return {};
}
void Mapper555::clock_cpu() {
    if ((game_ & 8U) && elapsed_ < timeout())
        ++elapsed_;
}
void Mapper555::reset() {
    Mmc3::reset();
    game_ = 0;
    elapsed_ = 0;
}
void Mapper555::save_state(StateWriter& w) const {
    w.write<Word>(555);
    w.write(time_switches_);
    w.write(game_);
    w.write(elapsed_);
    w.write_array(scratch_);
    w.write_array(pattern_ram_);
    Mmc3::save_state(w);
}
void Mapper555::load_state(StateReader& r) {
    auto tag = r.read<Word>();
    auto switches = r.read<Byte>();
    auto game = r.read<Byte>();
    auto elapsed = r.read<std::uint32_t>();
    decltype(scratch_) scratch{};
    decltype(pattern_ram_) ram{};
    r.read_array(scratch);
    r.read_array(ram);
    if (!r.ok() || tag != 555 || switches != time_switches_ || game > 15 || elapsed > timeout() ||
        (elapsed && !(game & 8U))) {
        r.fail();
        return;
    }
    Mmc3::load_state(r);
    if (r.ok()) {
        game_ = game;
        elapsed_ = elapsed;
        scratch_ = scratch;
        pattern_ram_ = ram;
    }
}
} // namespace mapperbus::core
