#include "core/mappers/jaleco_jf13.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Byte kBankMask = 0x73U;
constexpr Byte kAudioMask = 0x7FU;
constexpr Byte kAudioReset = 0x40U;
constexpr uint16_t kAudioResetCycles = 34U;
} // namespace

// Original implementation from NESdev mapper 86 r24513. The board's speech
// samples are in undocumented internal µPD7756C ROM, so only its control port
// is modeled; no replacement audio or sample data is invented.
JalecoJf13::JalecoJf13(const INesHeader& header,
                       std::vector<Byte> prg_rom,
                       std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), mirror_(header.mirror_mode) {
    reset();
}

Result<void> JalecoJf13::validate_geometry(const INesHeader& header,
                                           std::size_t prg_size,
                                           std::size_t chr_size) {
    if (header.mapper_number != 86U || (header.is_nes2 && header.submapper != 0U))
        return std::unexpected("Mapper 086 supports only mapper 86 submapper 0");
    if (prg_size != 131072U || chr_size != 65536U)
        return std::unexpected("Mapper 086 requires 128 KiB PRG and 64 KiB CHR ROM");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 086 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 086 requires fixed H/V mirroring");
    return {};
}

Byte JalecoJf13::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto bank = static_cast<std::size_t>((bank_ >> 4U) & 3U);
    return prg_rom_[bank * 32768U + (addr & 0x7FFFU)];
}

void JalecoJf13::write_prg(Address addr, Byte value) {
    switch (addr & 0xF000U) {
    case 0x6000U:
    case 0xE000U:
        bank_ = value & kBankMask;
        break;
    case 0x7000U:
    case 0xF000U:
        audio_control_ = value & kAudioMask;
        if ((audio_control_ & kAudioReset) != 0U)
            audio_reset_low_cycles_ = 0U;
        break;
    default:
        break;
    }
}

Byte JalecoJf13::read_chr(Address addr) {
    const auto bank = static_cast<std::size_t>(((bank_ >> 4U) & 4U) | (bank_ & 3U));
    return chr_rom_[bank * 8192U + (addr & 0x1FFFU)];
}

void JalecoJf13::write_chr(Address, Byte) {}

MirrorMode JalecoJf13::mirror_mode() const {
    return mirror_;
}

void JalecoJf13::reset() {
    // Deterministic defaults; the article does not establish electrical reset wiring.
    bank_ = 0U;
    audio_control_ = 0U;
    audio_reset_low_cycles_ = 0U;
}

void JalecoJf13::clock_cpu() {
    if ((audio_control_ & kAudioReset) != 0U) {
        audio_reset_low_cycles_ = 0U;
    } else {
        audio_reset_low_cycles_ =
            std::min<uint16_t>(audio_reset_low_cycles_ + 1U, kAudioResetCycles);
    }
}

uint16_t JalecoJf13::state_tag() const {
    return static_cast<uint16_t>(0x5600U | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void JalecoJf13::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(bank_);
    writer.write(audio_control_);
    writer.write(audio_reset_low_cycles_);
}

void JalecoJf13::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto bank = reader.read<Byte>();
    const auto audio_control = reader.read<Byte>();
    const auto audio_reset_low_cycles = reader.read<uint16_t>();
    const bool invalid_reset_phase =
        audio_reset_low_cycles > kAudioResetCycles ||
        ((audio_control & kAudioReset) != 0U && audio_reset_low_cycles != 0U);
    if (!reader.ok() || tag != state_tag() || (bank & ~kBankMask) != 0U ||
        (audio_control & ~kAudioMask) != 0U || invalid_reset_phase) {
        reader.fail();
        return;
    }
    bank_ = bank;
    audio_control_ = audio_control;
    audio_reset_low_cycles_ = audio_reset_low_cycles;
}

} // namespace mapperbus::core
