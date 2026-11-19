#include "core/mappers/game_doctor.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
Word word_at(std::span<const Byte> bytes, std::size_t offset) {
    return bytes[offset] | (static_cast<Word>(bytes[offset + 1]) << 8U);
}
// An extracted game starts after the Doctor BIOS loaded its RAM image.
// Emulate the BIOS's JSR to the trainer followed by a jump through the game's reset vector.
constexpr Address kTrainerStub = 0x4700;
} // namespace
Result<std::unique_ptr<Mapper>> GameDoctor::create(const INesHeader& h,
                                                   std::vector<Byte> prg,
                                                   std::vector<Byte> chr,
                                                   std::span<const Byte> misc) {
    const bool turbo = h.mapper_number == 562;
    if ((!turbo && h.mapper_number != 561) || !h.is_nes2 || h.submapper > 7 || h.has_trainer ||
        h.has_battery || h.prg_nvram_size || h.chr_nvram_size ||
        (h.prg_ram_size && h.prg_ram_size != 8192) || h.chr_ram_size > (turbo ? 262144U : 32768U) ||
        (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal) ||
        prg.size() < 32768 || prg.size() > 524288 || prg.size() % 8192 ||
        chr.size() > (turbo ? 262144U : 32768U) || chr.size() % 1024)
        return std::unexpected(
            "Game Doctor requires a supported RAM cartridge image and an initial banking mode 0-7");
    if (h.misc_rom_count > 1 || (h.misc_rom_count == 0 && !misc.empty()) ||
        (h.misc_rom_count == 1 && misc.size() < 4))
        return std::unexpected(
            "Game Doctor trainer requires a four-byte load address and entry point header");
    if (!misc.empty()) {
        Word load = word_at(misc, 0);
        if (load < 0x6000U || load >= 0x8000U || misc.size() - 4 > 0x8000U - load)
            return std::unexpected("Game Doctor trainer exceeds the $6000-$7FFF work RAM window");
    }
    return std::unique_ptr<Mapper>(new GameDoctor(h, std::move(prg), std::move(chr), misc));
}
GameDoctor::GameDoctor(const INesHeader& h,
                       std::vector<Byte> prg,
                       std::vector<Byte> chr,
                       std::span<const Byte> misc)
    : turbo_(h.mapper_number == 562),
      initial_mode_((h.submapper << 5U) | (h.mirror_mode == MirrorMode::Vertical ? 3U : 0x13U)),
      prg_image_(std::move(prg)), chr_image_(std::move(chr)), trainer_(misc.begin(), misc.end()),
      prg_(prg_image_.size() > 262144U ||
                   (turbo_ && std::max(chr_image_.size(), h.chr_ram_size) <= 32768U)
               ? 524288U
               : 262144U),
      chr_(std::max(chr_image_.size(), h.chr_ram_size) > 32768U ? 262144U : 32768U) {
    power_on();
}
Result<void> GameDoctor::configure_board_profile(std::string_view model) {
    // Short disk extracts do not uniquely identify the installed RAM cartridge.
    const bool small = model == "sgd-2m" || model == "tgd-6plus";
    const bool large_chr = model == "tgd-6plus" || model == "tgd-6m";
    const bool valid =
        turbo_ ? (large_chr || model == "tgd-4plus") : (model == "sgd-2m" || model == "sgd-4m");
    const unsigned prg = small ? 262144U : 524288U, chr = large_chr ? 262144U : 32768U;
    if (!valid || prg_image_.size() > prg || chr_image_.size() > chr)
        return std::unexpected("Game Doctor board profile is incompatible with the RAM image");
    prg_.resize(prg);
    chr_.resize(chr);
    power_on();
    return {};
}
Result<void> GameDoctor::configure_game_doctor_bios(std::span<const Byte> data) {
    if (!turbo_ || data.size() != 8192)
        return std::unexpected(
            "Mapper 562 requires an 8192-byte raw Turbo Game Doctor firmware image");
    std::ranges::copy(data.subspan(0x800, 0x800), bios_.begin());
    has_bios_ = true;
    return {};
}
void GameDoctor::power_on() {
    s_ = {};
    s_.mode = initial_mode_;
    s_.turbo_mode = turbo_ ? 3 : 0;
    wram_.fill(0);
    for (std::size_t i = 0; i < prg_.size(); ++i)
        prg_[i] = prg_image_[i % prg_image_.size()];
    std::ranges::fill(chr_, 0);
    if (!chr_image_.empty())
        for (std::size_t i = 0; i < chr_.size(); ++i)
            chr_[i] = chr_image_[i % chr_image_.size()];
    reset();
    if (!trainer_.empty()) {
        std::ranges::copy(std::span{trainer_}.subspan(4),
                          wram_.begin() + (word_at(trainer_, 0) - 0x6000));
        s_.boot = word_at(trainer_, 2) ? 1U : 0U;
    }
}
void GameDoctor::reset() {
    s_.counter = turbo_ ? 0x8000 : 0;
    s_.target = s_.disk_phase = 0;
    s_.timer_enable = s_.disk_enable = s_.pending = s_.boot = 0;
}
bool GameDoctor::wide_prg() const {
    return !(s_.outer & 1U) || (s_.turbo_mode & 0x80U);
}
unsigned GameDoctor::prg_bank(unsigned slot) const {
    if (wide_prg()) {
        if (turbo_ && !(s_.turbo_mode & 0x80U))
            return (s_.pages[slot] & 15U) | ((s_.outer >> 2U) & 16U);
        return s_.pages[slot];
    }
    const unsigned latch = s_.latch;
    switch (s_.mode >> 5U) {
    case 0:
        return slot < 2 ? (latch & 7U) * 2U + slot : 12U + slot;
    case 1:
        return slot < 2 ? ((latch >> 2U) & 15U) * 2U + slot : 12U + slot;
    case 2:
        return slot < 2 ? (latch & 15U) * 2U + slot : 28U + slot;
    case 3:
        return slot < 2 ? 30U + slot : (latch & 15U) * 2U + (slot & 1U);
    case 4:
        return ((latch >> 4U) & 3U) * 4U + slot;
    case 5:
        return 12U + slot;
    case 6:
        return slot == 0 ? latch & 15U : slot == 1 ? latch >> 4U : 12U + slot;
    default:
        return slot == 0 ? latch & 14U : slot == 1 ? (latch >> 4U) | 1U : 12U + slot;
    }
}
Byte GameDoctor::read_prg(Address addr) {
    if (addr < 0x6000)
        return 0;
    if (addr < 0x8000)
        return wram_[addr & 8191U];
    if (s_.boot == 1 && (addr & 0xFFFEU) == 0xFFFCU)
        return static_cast<Byte>(kTrainerStub >> ((addr & 1U) * 8U));
    const auto bank = prg_bank((addr >> 13U) & 3U);
    return prg_[(bank * 8192U + (addr & 8191U)) % prg_.size()];
}
void GameDoctor::write_prg(Address addr, Byte v) {
    if (addr < 0x6000)
        return;
    if (addr < 0x8000) {
        wram_[addr & 8191U] = v;
        return;
    }
    const auto slot = (addr >> 13U) & 3U;
    if (!(s_.mode & 2U)) {
        prg_[(prg_bank(slot) * 8192U + (addr & 8191U)) % prg_.size()] = v;
        return;
    }
    s_.latch = v;
    s_.pages[slot] = v >> 2U;
    const unsigned mode = s_.mode >> 5U;
    if (wide_prg() || mode == 1 || mode == 4 || mode == 5)
        s_.chr8 = v & 3U;
    else if (mode == 3)
        s_.chr8 = (v >> 4U) & 3U;
    else if (mode == 0 || mode == 2)
        s_.chr8 = 0;
}
unsigned GameDoctor::chr_offset(Address addr) const {
    const unsigned bank = (s_.turbo_mode & 64U) ? s_.tiles[(addr >> 10U) & 7U]
                                                : (s_.chr8 * 8U + ((addr >> 10U) & 7U));
    return (bank * 1024U + (addr & 1023U)) % chr_.size();
}
Byte GameDoctor::read_chr(Address addr) {
    return chr_[chr_offset(addr)];
}
void GameDoctor::write_chr(Address addr, Byte value) {
    if (turbo_ && (s_.mode >> 5U) >= 5 && !(s_.mode & 1U))
        s_.chr_lock = (s_.mode >> 4U) & 1U;
    if (s_.mode < 0x80 && !s_.chr_lock)
        chr_[chr_offset(addr)] = value;
}
MirrorMode GameDoctor::mirror_mode() const {
    if (s_.mode & 1U)
        return (s_.mode & 0x10U) ? MirrorMode::Horizontal : MirrorMode::Vertical;
    return (s_.mode & 0x10U) ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
}
void GameDoctor::observe_ppu_address(Address addr) {
    if (addr < 0x2000)
        s_.ppu_slot = (addr >> 10U) & 7U;
}
void GameDoctor::observe_cpu_read(Address addr, Byte) {
    if (s_.boot == 1 && addr == kTrainerStub)
        s_.boot = 2;
    if (s_.boot == 2 && addr == kTrainerStub + 5)
        s_.boot = 0;
}
bool GameDoctor::maps_expansion(Address addr) const {
    return (s_.boot && addr >= kTrainerStub && addr <= kTrainerStub + 5) ||
           (turbo_ && ((addr >= 0x4400 && addr <= 0x440D) || addr == 0x4411 || addr == 0x4415 ||
                       addr == 0x4420 || (has_bios_ && addr >= 0x4800 && addr < 0x5000)));
}
Byte GameDoctor::expansion_read_mask(Address addr) const {
    return maps_expansion(addr) ? 0xFF : 0;
}
Byte GameDoctor::read_expansion(Address addr) {
    if (!maps_expansion(addr))
        return 0;
    if (addr >= kTrainerStub && addr <= kTrainerStub + 5) {
        const std::array<Byte, 6> code{0x20, trainer_[2], trainer_[3], 0x6C, 0xFC, 0xFF};
        return code[addr - kTrainerStub];
    }
    if (addr >= 0x4800)
        return bios_[addr & 2047U];
    if (addr < 0x4408)
        return s_.tiles[addr & 7U];
    if (addr < 0x440C)
        return (s_.pages[addr & 3U] << 2U) | (s_.latch & 3U);
    if (addr == 0x440C)
        return s_.counter >> 8U;
    if (addr == 0x440D)
        return static_cast<Byte>(s_.counter);
    if (addr == 0x4411)
        return s_.turbo_mode;
    if (addr == 0x4415)
        return s_.mode;
    return s_.tiles[s_.ppu_slot];
}
void GameDoctor::write_expansion(Address addr, Byte v) {
    if (addr == 0x4024 || addr == 0x4025) {
        s_.pending &= ~2U;
        if (addr == 0x4025) {
            s_.disk_enable = v >> 7U;
            if (!s_.disk_enable || (v & 0x42U))
                s_.disk_phase = 0;
        }
    } else if ((addr & 0xFFFCU) == 0x42FC) {
        s_.mode = (v & 0xF0U) | (addr & 3U);
        if (s_.mode >= 0x80U)
            s_.chr_lock = 0;
        if (!wide_prg() && (s_.mode >> 5U == 0 || s_.mode >> 5U == 2))
            s_.chr8 = 0;
    } else if ((addr & 0xFFFEU) == 0x43FE) {
        s_.outer = (v & 0x40U) | (addr & 1U);
        s_.chr8 = v & 3U;
    } else if (!turbo_) {
        if (addr == 0x4100)
            s_.counter = (s_.counter & 0xFF00U) | v;
        if (addr == 0x4101) {
            s_.counter = (s_.counter & 255U) | (v << 8U);
            s_.timer_enable = v != 0;
            if (!s_.timer_enable)
                s_.pending &= ~1U;
        }
    } else if (addr >= 0x4400 && addr <= 0x4407)
        s_.tiles[addr & 7U] = v;
    else if (addr == 0x440C || addr == 0x440D) {
        s_.pending &= ~1U;
        if (addr == 0x440D)
            s_.target = (s_.target & 0xFF00U) | v;
        else {
            s_.target = (s_.target & 255U) | (v << 8U);
            s_.timer_enable = v >> 7U;
            if (!s_.timer_enable)
                s_.counter = 0x8000;
        }
    } else if (addr == 0x4411)
        s_.turbo_mode = v;
}
void GameDoctor::clock_cpu() {
    if (s_.disk_enable) {
        s_.disk_phase += 3;
        if (s_.disk_phase >= 448) {
            s_.disk_phase -= 448;
            s_.pending |= 2;
        }
    }
    if (!s_.timer_enable)
        return;
    if (turbo_) {
        if (s_.counter != 0xFFFF && ++s_.counter == s_.target)
            s_.pending |= 1U;
    } else if (++s_.counter == 0)
        s_.pending |= 1U;
}
void GameDoctor::save_state(StateWriter& w) const {
    w.write<Word>(turbo_ ? 562 : 561);
    w.write(initial_mode_);
    w.write(s_.mode);
    w.write(s_.outer);
    w.write(s_.turbo_mode);
    w.write(s_.latch);
    w.write(s_.chr8);
    w.write(s_.chr_lock);
    w.write(s_.ppu_slot);
    w.write(s_.boot);
    w.write(s_.pending);
    w.write(s_.timer_enable);
    w.write(s_.disk_enable);
    w.write(s_.counter);
    w.write(s_.target);
    w.write(s_.disk_phase);
    w.write_array(s_.pages);
    w.write_array(s_.tiles);
    w.write_array(wram_);
    w.write_bytes(prg_);
    w.write_bytes(chr_);
}
void GameDoctor::load_state(StateReader& r) {
    auto tag = r.read<Word>();
    auto initial = r.read<Byte>();
    Registers s;
    r.read(s.mode);
    r.read(s.outer);
    r.read(s.turbo_mode);
    r.read(s.latch);
    r.read(s.chr8);
    r.read(s.chr_lock);
    r.read(s.ppu_slot);
    r.read(s.boot);
    r.read(s.pending);
    r.read(s.timer_enable);
    r.read(s.disk_enable);
    r.read(s.counter);
    r.read(s.target);
    r.read(s.disk_phase);
    r.read_array(s.pages);
    r.read_array(s.tiles);
    decltype(wram_) wram{};
    std::vector<Byte> prg, chr;
    r.read_array(wram);
    r.read_bytes(prg, prg_.size());
    r.read_bytes(chr, chr_.size());
    if (!r.ok() || tag != (turbo_ ? 562 : 561) || initial != initial_mode_ || (s.mode & 12U) ||
        (s.outer & ~0x41U) || s.chr8 > 3 || s.chr_lock > 1 || s.ppu_slot > 7 || s.boot > 2 ||
        (s.boot && (trainer_.empty() || !word_at(trainer_, 2))) || s.pending > 3 ||
        s.timer_enable > 1 || s.disk_enable > 1 || s.disk_phase >= 448 ||
        (turbo_ && (s.counter < 0x8000U || s.timer_enable != (s.target >> 15U))) ||
        (!turbo_ && (s.turbo_mode || s.chr_lock || s.target)) ||
        std::ranges::any_of(s.pages, [](Byte v) { return v > 63; }) || prg.size() != prg_.size() ||
        chr.size() != chr_.size()) {
        r.fail();
        return;
    }
    s_ = s;
    wram_ = wram;
    prg_ = std::move(prg);
    chr_ = std::move(chr);
}
} // namespace mapperbus::core
