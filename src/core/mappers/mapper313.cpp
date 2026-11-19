#include "core/mappers/mapper313.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kKiB = 1024U;
constexpr std::size_t kPrgBankSize = 8U * kKiB;
constexpr std::size_t kChrBankSize = kKiB;
constexpr std::size_t kGameCount = 4U;
constexpr std::uint16_t kStateTag = 0x3130U;

struct GameGeometry {
    std::size_t prg_size;
    std::size_t chr_size;
    std::size_t total_prg_size;
};

constexpr std::array<GameGeometry, 5> kGeometries{{
    {128U * kKiB, 128U * kKiB, 512U * kKiB},
    {256U * kKiB, 128U * kKiB, 1024U * kKiB},
    {128U * kKiB, 256U * kKiB, 512U * kKiB},
    {256U * kKiB, 256U * kKiB, 1024U * kKiB},
    {128U * kKiB, 128U * kKiB, 640U * kKiB},
}};

} // namespace

// Original implementation from NESdev mapper 313 r7804 and MMC3 r24445.
// Power-on game zero and cleared MMC3 registers are deterministic policies;
// the mapper article specifies only outer-bank increment on M2 interruption.
Mapper313::Mapper313(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), submapper_(header.submapper),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper313::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 313U)
        return std::unexpected("Mapper313 requires mapper ID 313");
    if (!header.is_nes2 || header.submapper >= kGeometries.size())
        return std::unexpected("Mapper 313 requires NES 2.0 submapper 0-4");

    const auto& geometry = kGeometries[header.submapper];
    const auto chr_total = geometry.chr_size * kGameCount;
    if (prg_size != geometry.total_prg_size || chr_size != chr_total)
        return std::unexpected("Mapper 313 ROM sizes do not contain four complete games");
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 313 supports ROM-only board memory");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 313 supports ordinary H/V CIRAM metadata");
    return {};
}

std::size_t Mapper313::prg_game_size() const {
    if (submapper_ == 4U && game_ == 0U)
        return 256U * kKiB;
    return kGeometries[submapper_].prg_size;
}

std::size_t Mapper313::prg_game_base(Byte game) const {
    if (submapper_ != 4U)
        return static_cast<std::size_t>(game) * kGeometries[submapper_].prg_size;
    return game == 0U ? 0U : 256U * kKiB + static_cast<std::size_t>(game - 1U) * 128U * kKiB;
}

Byte Mapper313::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;

    const auto bank_count = prg_game_size() / kPrgBankSize;
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    std::size_t bank = 0U;
    if (slot == 1U)
        bank = banks_[7U] % bank_count;
    else if (slot == 3U)
        bank = bank_count - 1U;
    else if ((slot == 0U) == ((bank_select_ & 0x40U) == 0U))
        bank = banks_[6U] % bank_count;
    else
        bank = bank_count - 2U;

    const auto offset = prg_game_base(game_) + bank * kPrgBankSize + (addr & 0x1FFFU);
    return prg_rom_[offset];
}

void Mapper313::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;

    const bool even = (addr & 1U) == 0U;
    if (addr < 0xA000U) {
        if (even) {
            bank_select_ = value & 0xC7U;
        } else {
            const auto index = bank_select_ & 7U;
            banks_[index] = index >= 6U ? value & 0x3FU : value;
        }
    } else if (addr < 0xC000U) {
        if (even)
            mirror_ = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    } else if (addr < 0xE000U) {
        if (even) {
            irq_latch_ = value;
        } else {
            irq_counter_ |= 0x80U;
            if (!irq_reload_blocked_) {
                irq_reload_ = true;
                irq_reload_blocked_ = true;
            }
        }
    } else if (even) {
        irq_enabled_ = false;
        irq_pending_ = false;
    } else {
        irq_enabled_ = true;
    }
}

Byte Mapper313::chr_bank(Address addr) const {
    const auto slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((bank_select_ & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U)
        return static_cast<Byte>((banks_[slot / 2U] & 0xFEU) | (slot & 1U));
    return banks_[slot - 2U];
}

Byte Mapper313::read_chr(Address addr) {
    const auto offset = static_cast<std::size_t>(game_) * kGeometries[submapper_].chr_size +
                        static_cast<std::size_t>(chr_bank(addr)) * kChrBankSize + (addr & 0x03FFU);
    return chr_rom_[offset];
}

void Mapper313::write_chr(Address, Byte) {}

MirrorMode Mapper313::mirror_mode() const {
    return mirror_;
}

void Mapper313::power_on() {
    game_ = 0U;
    reset_mmc3();
}

void Mapper313::reset() {
    game_ = static_cast<Byte>((game_ + 1U) % kGameCount);
    reset_mmc3();
}

void Mapper313::reset_mmc3() {
    mirror_ = initial_mirror_;
    bank_select_ = 0U;
    banks_.fill(0U);
    irq_counter_ = 0U;
    irq_latch_ = 0U;
    irq_reload_ = false;
    irq_reload_blocked_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
}

void Mapper313::clock_irq_counter() {
    if (irq_counter_ == 0U || irq_reload_)
        irq_counter_ = irq_latch_;
    else
        --irq_counter_;

    irq_reload_ = false;
    irq_reload_blocked_ = false;
    if (irq_counter_ == 0U && irq_enabled_)
        irq_pending_ = true;
}

std::uint16_t Mapper313::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | submapper_);
}

void Mapper313::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(game_);
    writer.write(bank_select_);
    writer.write_array(banks_);
    writer.write<Byte>(mirror_ == MirrorMode::Vertical ? 1U : 0U);
    writer.write(irq_counter_);
    writer.write(irq_latch_);
    writer.write<Byte>((irq_reload_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U) | (irq_reload_blocked_ ? 8U : 0U));
}

void Mapper313::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto game = reader.read<Byte>();
    const auto bank_select = reader.read<Byte>();
    std::array<Byte, 8> banks{};
    reader.read_array(banks);
    const auto mirror = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq = reader.read<Byte>();

    const bool valid = reader.ok() && tag == state_tag() && prg_size == prg_rom_.size() &&
                       chr_size == chr_rom_.size() && game < kGameCount &&
                       (bank_select & 0x38U) == 0U && banks[6U] <= 0x3FU && banks[7U] <= 0x3FU &&
                       mirror <= 1U && irq <= 0x0FU && ((irq & 4U) == 0U || (irq & 2U) != 0U) &&
                       ((irq & 8U) == 0U || (irq & 1U) != 0U);
    if (!valid) {
        reader.fail();
        return;
    }

    game_ = game;
    bank_select_ = bank_select;
    banks_ = banks;
    mirror_ = mirror != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
    irq_counter_ = irq_counter;
    irq_latch_ = irq_latch;
    irq_reload_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    irq_reload_blocked_ = (irq & 8U) != 0U;
}

} // namespace mapperbus::core
