#include "core/mappers/multicart233.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {

// Original code from NESdev r5799, Everynes mapper 226/233 descriptions and
// board thread 19679 (2020 identification / 2025 A19 observations).
// These sources disagree: keep Disch wiring and the tentative Everynes $FFFD
// model explicit. docs/reference/mapper-implementation.md records assumptions.
Multicart233::Multicart233(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)) {}

Result<void> Multicart233::validate_geometry(const INesHeader& h,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (h.is_nes2 && h.submapper != 0U)
        return std::unexpected("Mapper 233 supports submapper 0");
    if (prg_size < 0x4000U || prg_size > 0x100000U || prg_size % 0x4000U != 0U || chr_size != 0U)
        return std::unexpected("Mapper 233 requires 16 KiB-1 MiB PRG ROM and no CHR ROM");
    if (h.has_battery || (h.is_nes2 && (h.prg_ram_size != 0U || h.prg_nvram_size != 0U ||
                                        h.chr_ram_size != 8192U || h.chr_nvram_size != 0U)))
        return std::unexpected("Mapper 233 requires 8 KiB volatile CHR RAM and no CPU RAM");
    if (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 233 supports ordinary H/V CIRAM metadata");
    return {};
}

Byte Multicart233::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    unsigned page = control_ & 31U;
    if ((control_ & 0x20U) == 0U)
        page = (page & 30U) | ((addr >> 14U) & 1U);
    if (profile_ != Profile::Disch233)
        page |= (control_ >> 2U) & 32U;
    return prg_rom_[(page * 0x4000U + (addr & 0x3FFFU)) % prg_rom_.size()];
}

void Multicart233::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    if (profile_ == Profile::Disch233 || (addr & 1U) == 0U)
        control_ = value;
    else
        auxiliary_ = value & 2U; // Fitted boards stop at A19; A20 is not populated.
}

void Multicart233::observe_cpu_read(Address addr, Byte /*value*/) {
    // After the old mapping has supplied the byte. Direct ROM inspection has
    // no side effects. $FF is Everynes's conjectured value, not a verified latch.
    if (profile_ == Profile::EverynesVector && addr == 0xFFFDU)
        control_ = 0xFFU;
}

Byte Multicart233::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Multicart233::write_chr(Address addr, Byte value) {
    if (profile_ == Profile::Disch233 || auxiliary_ == 0U)
        chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Multicart233::mirror_mode() const {
    if (profile_ != Profile::Disch233)
        return (control_ & 0x40U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    constexpr std::array<MirrorMode, 4> kModes{MirrorMode::SingleLower,
                                               MirrorMode::Vertical,
                                               MirrorMode::Horizontal,
                                               MirrorMode::SingleUpper};
    return kModes[control_ >> 6U];
}

std::optional<Address> Multicart233::nametable_address(Address addr) const {
    if (profile_ != Profile::Disch233 || control_ >= 0x40U || addr < 0x2000U || addr >= 0x3F00U)
        return std::nullopt;
    // Disch's mode 00 is AAAB, which MirrorMode alone cannot represent.
    const unsigned table = ((addr >> 10U) & 3U) == 3U ? 1U : 0U;
    return static_cast<Address>(table * 0x400U + (addr & 0x3FFU));
}

Result<void> Multicart233::configure_board_profile(std::string_view name) {
    Profile profile = Profile::Reset42;
    if (name == "reset-42")
        profile = Profile::Reset42;
    else if (name == "disch-233") {
        if (prg_rom_.size() > 0x80000U)
            return std::unexpected("Mapper 233 disch-233 profile supports at most 512 KiB PRG");
        profile = Profile::Disch233;
    } else if (name == "everynes-vector")
        profile = Profile::EverynesVector;
    else
        return std::unexpected("Mapper 233 profiles are reset-42, disch-233 and everynes-vector");
    profile_ = profile;
    power_on(); // Configuration is selected before installing the cartridge.
    return {};
}

void Multicart233::power_on() {
    control_ = 0U;
    auxiliary_ = 0U;
    chr_ram_.fill(0U);
}

void Multicart233::reset() {
    // Reset42 toggles the currently selected A19 and clears inner/mirror/gate
    // bits. Other profiles clear latches; Everynes then reacts to vector reads.
    control_ = profile_ == Profile::Reset42 ? (control_ ^ 0x80U) & 0x80U : 0U;
    auxiliary_ = 0U;
}

uint16_t Multicart233::state_tag() const {
    return static_cast<uint16_t>(0xE900U | static_cast<Byte>(profile_));
}

void Multicart233::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(control_);
    writer.write(auxiliary_);
    writer.write_array(chr_ram_);
}

void Multicart233::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto control = reader.read<Byte>();
    const auto auxiliary = reader.read<Byte>();
    std::array<Byte, 8192> chr{};
    reader.read_array(chr);
    if (!reader.ok() || tag != state_tag() || (auxiliary & ~2U) != 0U ||
        (profile_ == Profile::Disch233 && auxiliary != 0U)) {
        reader.fail(); // Rejection must not reset the game or its RAM.
        return;
    }
    control_ = control;
    auxiliary_ = auxiliary;
    chr_ram_ = chr;
}

} // namespace mapperbus::core
