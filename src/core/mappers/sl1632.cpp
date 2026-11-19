#include "core/mappers/sl1632.hpp"

#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr Byte kSupervisorMask = 0x71U;
constexpr uint16_t kStateTag = 0x0E00U;

Byte mirror_bits(MirrorMode mode) {
    return static_cast<Byte>(mode);
}

bool hv_mirror(MirrorMode mode) {
    return mode == MirrorMode::Horizontal || mode == MirrorMode::Vertical;
}
} // namespace

// NESdev mapper 014 r19503, mapper 116, MMC3 r24445, and VRC2_and_VRC4.
// The SL-1632 reset values are deterministic policy; only the related Huang-1
// power-on mode and VRC2 CHR initialization are documented by the family source.
Sl1632::Sl1632(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Sl1632::validate_geometry(const INesHeader& header,
                                       std::size_t prg_size,
                                       std::size_t chr_size) {
    if (header.mapper_number != 14U)
        return std::unexpected("SL-1632 geometry validation requires mapper 014");
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 014 supports submapper 0");
    if (prg_size < 0x8000U || prg_size > 0x80000U || !std::has_single_bit(prg_size) ||
        chr_size < 0x2000U || chr_size > 0x80000U || !std::has_single_bit(chr_size))
        return std::unexpected(
            "Mapper 014 requires power-of-two 32-512 KiB PRG and 8-512 KiB CHR ROM");
    if (header.has_battery || header.prg_nvram_size != 0U || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U || (header.is_nes2 && header.prg_ram_size != 0U))
        return std::unexpected("Mapper 014 supports ROM-only board memory");
    if (!hv_mirror(header.mirror_mode))
        return std::unexpected("Mapper 014 supports ordinary H/V CIRAM");
    return {};
}

bool Sl1632::mmc3_mode() const {
    return (supervisor_ & 1U) != 0U;
}

Byte Sl1632::read_prg(Address addr) {
    if (addr < 0x8000U)
        return !mmc3_mode() && addr >= 0x6000U && addr < 0x7000U
                   ? static_cast<Byte>(vrc2_.microwire ? 1U : 0U)
                   : 0U;

    const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
    const std::size_t count = prg_rom_.size() / 0x2000U;
    std::size_t bank = count - 1U;
    if (mmc3_mode()) {
        if (slot == 1U)
            bank = mmc3_.banks[7U];
        else if (slot != 3U) {
            const bool swapped = (mmc3_.bank_select & 0x40U) != 0U;
            bank = (slot == 0U) != swapped ? mmc3_.banks[6U] : count - 2U;
        }
    } else {
        if (slot < 2U)
            bank = vrc2_.prg_banks[slot];
        else if (slot == 2U)
            bank = count - 2U;
    }
    const auto offset = bank * 0x2000U + (addr & 0x1FFFU);
    return prg_rom_[offset % prg_rom_.size()];
}

bool Sl1632::maps_prg(Address addr) const {
    return addr >= 0x8000U || (!mmc3_mode() && addr >= 0x6000U && addr < 0x7000U);
}

Byte Sl1632::prg_read_mask(Address addr) const {
    return !mmc3_mode() && addr >= 0x6000U && addr < 0x7000U ? 1U : 0xFFU;
}

void Sl1632::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (!mmc3_mode() && addr >= 0x6000U && addr < 0x7000U)
            vrc2_.microwire = (value & 1U) != 0U;
        return;
    }

    if (addr == 0xA131U)
        supervisor_ = value & kSupervisorMask;
    if (mmc3_mode())
        write_mmc3(addr, value);
    else
        write_vrc2(addr, value);
}

void Sl1632::write_mmc3(Address addr, Byte value) {
    switch (addr & 0xE001U) {
    case 0x8000U:
        mmc3_.bank_select = value & 0xC7U;
        break;
    case 0x8001U: {
        const auto index = mmc3_.bank_select & 7U;
        mmc3_.banks[index] = index >= 6U ? value & 0x3FU : value;
        break;
    }
    case 0xA000U:
        mmc3_.mirror = (value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        break;
    case 0xA001U:
        mmc3_.ram_enabled = (value & 0x80U) != 0U;
        mmc3_.ram_write_protected = (value & 0x40U) != 0U;
        break;
    case 0xC000U:
        mmc3_.irq_latch = value;
        break;
    case 0xC001U:
        mmc3_.irq_counter = 0U;
        mmc3_.irq_reload = true;
        break;
    case 0xE000U:
        mmc3_.irq_enabled = false;
        mmc3_.irq_pending = false;
        break;
    case 0xE001U:
        mmc3_.irq_enabled = true;
        break;
    default:
        break;
    }
}

void Sl1632::write_vrc2(Address addr, Byte value) {
    const auto group = static_cast<unsigned>(addr) >> 12U;
    const auto port = static_cast<unsigned>(addr) & 3U;

    if (group == 8U || group == 10U) {
        vrc2_.prg_banks[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        vrc2_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = vrc2_.chr_banks[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U)
            bank = (bank & 0xF0U) | (value & 0x0FU);
        else
            bank = (bank & 0x0FU) | ((value & 0x0FU) << 4U);
    }
}

Byte Sl1632::mmc3_chr_bank(Address addr) const {
    auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    if ((mmc3_.bank_select & 0x80U) != 0U)
        slot ^= 4U;
    if (slot < 4U)
        return static_cast<Byte>((mmc3_.banks[slot / 2U] & 0xFEU) | (slot & 1U));
    return mmc3_.banks[slot - 2U];
}

Byte Sl1632::read_chr(Address addr) {
    const auto physical = static_cast<Address>(addr & 0x1FFFU);
    const Byte bank =
        mmc3_mode() ? mmc3_chr_bank(physical) : vrc2_.chr_banks[(physical >> 10U) & 7U];
    const auto region = static_cast<unsigned>(physical >> 12U);
    const auto outer = static_cast<unsigned>((supervisor_ >> (4U + region)) & 1U);
    const auto offset =
        static_cast<std::size_t>(bank) * 0x400U + (outer << 18U) + (physical & 0x03FFU);
    return chr_rom_[offset % chr_rom_.size()];
}

void Sl1632::write_chr(Address, Byte) {}

MirrorMode Sl1632::mirror_mode() const {
    return mmc3_mode() ? mmc3_.mirror : vrc2_.mirror;
}

void Sl1632::power_on() {
    reset();
}

void Sl1632::reset() {
    supervisor_ = 1U;
    mmc3_ = Mmc3State{};
    mmc3_.mirror = initial_mirror_;
    vrc2_ = Vrc2State{};
    vrc2_.chr_banks.fill(0xFFU);
    vrc2_.mirror = initial_mirror_;
}

bool Sl1632::irq_pending() const {
    return mmc3_mode() && mmc3_.irq_pending;
}

void Sl1632::acknowledge_irq() {
    if (mmc3_mode())
        mmc3_.irq_pending = false;
}

void Sl1632::clock_irq_counter() {
    if (!mmc3_mode())
        return;
    if (mmc3_.irq_counter == 0U || mmc3_.irq_reload) {
        mmc3_.irq_counter = mmc3_.irq_latch;
        mmc3_.irq_reload = false;
    } else {
        --mmc3_.irq_counter;
    }
    if (mmc3_.irq_counter == 0U && mmc3_.irq_enabled)
        mmc3_.irq_pending = true;
}

void Sl1632::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<uint32_t>(prg_rom_.size()));
    writer.write(static_cast<uint32_t>(chr_rom_.size()));
    writer.write(mirror_bits(initial_mirror_));
    writer.write(supervisor_);
    writer.write(mmc3_.bank_select);
    writer.write_array(mmc3_.banks);
    writer.write(mirror_bits(mmc3_.mirror));
    writer.write(mmc3_.irq_counter);
    writer.write(mmc3_.irq_latch);
    writer.write<Byte>((mmc3_.irq_reload ? 1U : 0U) | (mmc3_.irq_enabled ? 2U : 0U) |
                       (mmc3_.irq_pending ? 4U : 0U));
    writer.write<Byte>((mmc3_.ram_enabled ? 0x80U : 0U) | (mmc3_.ram_write_protected ? 0x40U : 0U));
    writer.write_array(vrc2_.prg_banks);
    writer.write_array(vrc2_.chr_banks);
    writer.write(mirror_bits(vrc2_.mirror));
    writer.write<Byte>(vrc2_.microwire ? 1U : 0U);
}

void Sl1632::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_size = reader.read<uint32_t>();
    const auto chr_size = reader.read<uint32_t>();
    const auto initial_mirror = reader.read<Byte>();
    const auto supervisor = reader.read<Byte>();

    Mmc3State mmc3;
    reader.read(mmc3.bank_select);
    reader.read_array(mmc3.banks);
    const auto mmc3_mirror = reader.read<Byte>();
    reader.read(mmc3.irq_counter);
    reader.read(mmc3.irq_latch);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();

    Vrc2State vrc2;
    reader.read_array(vrc2.prg_banks);
    reader.read_array(vrc2.chr_banks);
    const auto vrc2_mirror = reader.read<Byte>();
    const auto microwire = reader.read<Byte>();

    const bool valid_irq = (irq & ~7U) == 0U && ((irq & 4U) == 0U || (irq & 2U) != 0U);
    const bool valid =
        reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
        chr_size == chr_rom_.size() && initial_mirror == mirror_bits(initial_mirror_) &&
        (supervisor & ~kSupervisorMask) == 0U && (mmc3.bank_select & ~0xC7U) == 0U &&
        mmc3.banks[6U] <= 0x3FU && mmc3.banks[7U] <= 0x3FU &&
        mmc3_mirror <= mirror_bits(MirrorMode::Vertical) && valid_irq && (ram & ~0xC0U) == 0U &&
        vrc2.prg_banks[0U] <= 0x1FU && vrc2.prg_banks[1U] <= 0x1FU &&
        vrc2_mirror <= mirror_bits(MirrorMode::Vertical) && microwire <= 1U;
    if (!valid) {
        reader.fail();
        return;
    }

    mmc3.mirror = static_cast<MirrorMode>(mmc3_mirror);
    mmc3.irq_reload = (irq & 1U) != 0U;
    mmc3.irq_enabled = (irq & 2U) != 0U;
    mmc3.irq_pending = (irq & 4U) != 0U;
    mmc3.ram_enabled = (ram & 0x80U) != 0U;
    mmc3.ram_write_protected = (ram & 0x40U) != 0U;
    vrc2.mirror = static_cast<MirrorMode>(vrc2_mirror);
    vrc2.microwire = microwire != 0U;
    supervisor_ = supervisor;
    mmc3_ = mmc3;
    vrc2_ = vrc2;
}

} // namespace mapperbus::core
