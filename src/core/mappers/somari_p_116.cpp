#include "core/mappers/somari_p_116.hpp"

#include <algorithm>
#include <bit>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr std::size_t k8K = 0x2000U;
constexpr std::size_t k16K = 0x4000U;
constexpr std::size_t k32K = 0x8000U;
constexpr std::size_t k256K = 0x40000U;
} // namespace

SomariP116::SomariP116(const INesHeader& header,
                       std::vector<Byte> prg_rom,
                       std::vector<Byte> chr_rom)
    : submapper_(header.is_nes2 ? header.submapper : 0U), initial_mirror_(header.mirror_mode),
      prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    reset();
}

Result<void> SomariP116::validate_geometry(const INesHeader& header,
                                           std::size_t prg_size,
                                           std::size_t chr_size) {
    if (header.mapper_number != 116U)
        return std::unexpected("SOMARI-P geometry validation requires mapper 116");
    if (header.is_nes2 && header.submapper == 3U)
        return std::unexpected(
            "Mapper 116 submapper 3 lacks a reset-bank and mixed-layout mapping specification");
    if (header.is_nes2 && header.submapper > 2U)
        return std::unexpected("Mapper 116 supports submappers 0 through 2");
    if (prg_size < k32K || prg_size > 2U * k256K || !std::has_single_bit(prg_size) ||
        chr_size < 0x2000U || chr_size > 2U * k256K || !std::has_single_bit(chr_size))
        return std::unexpected("Mapper 116 requires power-of-two PRG/CHR ROMs up to 512 KiB");
    if (header.has_battery || header.chr_ram_size != 0U || header.chr_nvram_size != 0U ||
        (header.is_nes2 && (header.prg_ram_size != 0U || header.prg_nvram_size != 0U)))
        return std::unexpected("Mapper 116 supports ROM-only boards without PRG RAM");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 116 requires horizontal or vertical startup mirroring");
    return {};
}

bool SomariP116::supervisor_address(Address addr) const {
    return addr >= 0x4020U && addr < 0x8000U && (addr & 0xE100U) == 0x4100U;
}

bool SomariP116::vrc2_active() const {
    return (supervisor_ & 3U) == 0U;
}

bool SomariP116::mmc3_active() const {
    return (supervisor_ & 3U) == 1U;
}

bool SomariP116::mmc1_active() const {
    return (supervisor_ & 3U) >= 2U;
}

void SomariP116::reset() {
    supervisor_ = 1U;
    vrc2_ = {};
    vrc2_.prg_banks = {0U, 1U};
    vrc2_.chr_banks.fill(0xFFU);
    vrc2_.mirror = initial_mirror_;
    mmc3_ = {};
    mmc3_.mirror = initial_mirror_;
    mmc3_.ram_enabled = true;
    mmc1_ = {};
}

void SomariP116::reset_mmc1_serial() {
    mmc1_.shift = 0x10U;
    mmc1_.write_count = 0U;
    mmc1_.control |= 0x0CU;
}

void SomariP116::write_supervisor(Byte value) {
    const bool was_mmc1 = mmc1_active();
    supervisor_ = value & 7U;
    if (submapper_ != 1U && !was_mmc1 && mmc1_active())
        reset_mmc1_serial();
}

Byte SomariP116::read_prg(Address addr) {
    if (addr < 0x8000U) {
        if (vrc2_active() && addr >= 0x6000U && addr < 0x7000U)
            return vrc2_.microwire;
        return 0U;
    }
    const auto offset = vrc2_active()   ? vrc2_prg_offset(addr)
                        : mmc3_active() ? mmc3_prg_offset(addr)
                                        : mmc1_prg_offset(addr);
    return prg_rom_[offset % prg_rom_.size()];
}

std::size_t SomariP116::vrc2_prg_offset(Address addr) const {
    const auto count = prg_rom_.size() / k8K;
    const auto slot = (addr - 0x8000U) / k8K;
    const auto bank = slot == 0U   ? vrc2_.prg_banks[0U]
                      : slot == 1U ? vrc2_.prg_banks[1U]
                      : slot == 2U ? count - 2U
                                   : count - 1U;
    return bank * k8K + (addr & 0x1FFFU);
}

std::size_t SomariP116::mmc3_prg_offset(Address addr) const {
    const auto count = prg_rom_.size() / k8K;
    const auto slot = (addr - 0x8000U) / k8K;
    const bool swapped = (mmc3_.bank_select & 0x40U) != 0U;
    const auto bank = slot == 1U                ? mmc3_.banks[7U]
                      : slot == 3U              ? count - 1U
                      : (slot == 0U) != swapped ? mmc3_.banks[6U]
                                                : count - 2U;
    return bank * k8K + (addr & 0x1FFFU);
}

std::size_t SomariP116::mmc1_prg_offset(Address addr) const {
    const auto mode = (static_cast<unsigned>(mmc1_.control) >> 2U) & 3U;
    const auto bank = mmc1_.prg_bank & 0x0FU;
    const auto unit = submapper_ == 2U ? k8K : k16K;
    std::size_t offset = 0U;
    if (mode < 2U) {
        const auto alignment = submapper_ == 2U ? 0x0CU : 0x0EU;
        offset = (bank & alignment) * unit + (addr - 0x8000U);
    } else if (mode == 2U) {
        offset = addr < 0xC000U ? addr - 0x8000U : bank * unit + (addr - 0xC000U);
    } else {
        offset = addr < 0xC000U ? bank * unit + (addr - 0x8000U)
                                : prg_rom_.size() - k16K + (addr - 0xC000U);
    }
    return offset;
}

void SomariP116::write_prg(Address addr, Byte value) {
    write_prg_cpu(addr, value, false);
}

void SomariP116::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (supervisor_address(addr))
        write_supervisor(value);
    if (addr < 0x8000U) {
        if (vrc2_active() && addr >= 0x6000U && addr < 0x7000U)
            vrc2_.microwire = value & 1U;
        return;
    }
    if (vrc2_active())
        write_vrc2(addr, value);
    else if (mmc3_active())
        write_mmc3(addr, value);
    else
        write_mmc1(addr, value, consecutive);
}

void SomariP116::write_vrc2(Address addr, Byte value) {
    const auto group = static_cast<unsigned>(addr >> 12U);
    const auto port = addr & 3U;
    if (group == 8U || group == 10U) {
        vrc2_.prg_banks[group == 8U ? 0U : 1U] = value & 0x1FU;
    } else if (group == 9U) {
        vrc2_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
    } else if (group >= 11U && group <= 14U) {
        auto& bank = vrc2_.chr_banks[(group - 11U) * 2U + (port >> 1U)];
        if ((port & 1U) == 0U)
            bank = (bank & 0xF0U) | (value & 0x0FU);
        else
            bank = static_cast<Byte>((bank & 0x0FU) | (static_cast<unsigned>(value) << 4U));
    }
}

void SomariP116::write_mmc3(Address addr, Byte value) {
    const auto group = addr >> 12U;
    const bool even = (addr & 1U) == 0U;
    if (group == 8U || group == 9U) {
        if (even) {
            mmc3_.bank_select = value & 0xC7U;
        } else {
            auto& bank = mmc3_.banks[mmc3_.bank_select & 7U];
            bank = (mmc3_.bank_select & 7U) >= 6U ? value & 0x3FU : value;
        }
    } else if (group == 10U || group == 11U) {
        if (even)
            mmc3_.mirror = (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        else {
            mmc3_.ram_enabled = (value & 0x80U) != 0U;
            mmc3_.ram_write_protected = (value & 0x40U) != 0U;
        }
    } else if (group == 12U || group == 13U) {
        if (even)
            mmc3_.irq_reload = value;
        else {
            mmc3_.irq_counter = 0U;
            mmc3_.irq_reload_flag = true;
        }
    } else if (even) {
        mmc3_.irq_enabled = false;
        mmc3_.irq_pending = false;
    } else {
        mmc3_.irq_enabled = true;
    }
}

void SomariP116::write_mmc1(Address addr, Byte value, bool consecutive) {
    if (consecutive && (value & 0x80U) == 0U)
        return;
    if ((value & 0x80U) != 0U) {
        reset_mmc1_serial();
        return;
    }
    mmc1_.shift >>= 1U;
    mmc1_.shift |= (value & 1U) << 4U;
    ++mmc1_.write_count;
    if (mmc1_.write_count == 5U) {
        commit_mmc1(addr, mmc1_.shift);
        mmc1_.shift = 0x10U;
        mmc1_.write_count = 0U;
    }
}

void SomariP116::commit_mmc1(Address addr, Byte value) {
    switch ((static_cast<unsigned>(addr) >> 13U) & 3U) {
    case 0U:
        mmc1_.control = value & 0x1FU;
        break;
    case 1U:
        mmc1_.chr_bank0 = value & 0x1FU;
        break;
    case 2U:
        mmc1_.chr_bank1 = value & 0x1FU;
        break;
    default:
        mmc1_.prg_bank = value & 0x1FU;
        break;
    }
}

bool SomariP116::maps_prg(Address addr) const {
    if (addr >= 0x8000U || (addr >= 0x6000U && supervisor_address(addr)))
        return true;
    return vrc2_active() && addr >= 0x6000U && addr < 0x7000U;
}

Byte SomariP116::prg_read_mask(Address addr) const {
    if (addr >= 0x8000U)
        return 0xFFU;
    if (vrc2_active() && addr >= 0x6000U && addr < 0x7000U)
        return 1U;
    return 0U;
}

Byte SomariP116::read_chr(Address addr) {
    addr &= 0x1FFFU;
    const auto inner = vrc2_active()   ? vrc2_chr_offset(addr)
                       : mmc3_active() ? mmc3_chr_offset(addr)
                                       : mmc1_chr_offset(addr);
    const auto outer = (supervisor_ & 4U) != 0U ? k256K : 0U;
    return chr_rom_[(outer + inner) % chr_rom_.size()];
}

std::size_t SomariP116::vrc2_chr_offset(Address addr) const {
    return vrc2_.chr_banks[addr >> 10U] * 0x400U + (addr & 0x3FFU);
}

std::size_t SomariP116::mmc3_chr_offset(Address addr) const {
    const auto slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((mmc3_.bank_select & 0x80U) != 0U ? 4U : 0U);
    const auto bank =
        slot < 4U ? (mmc3_.banks[slot / 2U] & 0xFEU) | (slot & 1U) : mmc3_.banks[slot - 2U];
    return bank * 0x400U + (addr & 0x3FFU);
}

std::size_t SomariP116::mmc1_chr_offset(Address addr) const {
    if ((mmc1_.control & 0x10U) == 0U)
        return (mmc1_.chr_bank0 & 0x1EU) * 0x1000U + addr;
    const auto bank = addr < 0x1000U ? mmc1_.chr_bank0 : mmc1_.chr_bank1;
    return bank * 0x1000U + (addr & 0x0FFFU);
}

void SomariP116::write_chr(Address, Byte) {}

MirrorMode SomariP116::mirror_mode() const {
    if (vrc2_active())
        return vrc2_.mirror;
    if (mmc3_active())
        return mmc3_.mirror;
    constexpr std::array kModes = {MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper,
                                   MirrorMode::Vertical,
                                   MirrorMode::Horizontal};
    return kModes[mmc1_.control & 3U];
}

bool SomariP116::irq_pending() const {
    return mmc3_active() && mmc3_.irq_pending;
}

void SomariP116::acknowledge_irq() {
    if (mmc3_active())
        mmc3_.irq_pending = false;
}

void SomariP116::clock_irq_counter() {
    if (!mmc3_active())
        return;
    if (mmc3_.irq_counter == 0U || mmc3_.irq_reload_flag) {
        mmc3_.irq_counter = mmc3_.irq_reload;
        mmc3_.irq_reload_flag = false;
    } else {
        --mmc3_.irq_counter;
    }
    if (mmc3_.irq_counter == 0U && mmc3_.irq_enabled)
        mmc3_.irq_pending = true;
}

bool SomariP116::maps_expansion(Address addr) const {
    return supervisor_address(addr);
}

Byte SomariP116::expansion_read_mask(Address) const {
    return 0U;
}

Byte SomariP116::read_expansion(Address) {
    return 0U;
}

void SomariP116::write_expansion(Address addr, Byte value) {
    if (supervisor_address(addr))
        write_supervisor(value);
}

void SomariP116::save_state(StateWriter& writer) const {
    writer.write<std::uint16_t>(0x7400U | submapper_);
    writer.write<std::uint32_t>(prg_rom_.size());
    writer.write<std::uint32_t>(chr_rom_.size());
    writer.write(supervisor_);
    writer.write_array(vrc2_.prg_banks);
    writer.write_array(vrc2_.chr_banks);
    writer.write<Byte>(static_cast<Byte>(vrc2_.mirror));
    writer.write(vrc2_.microwire);
    writer.write(mmc3_.bank_select);
    writer.write_array(mmc3_.banks);
    writer.write<Byte>(static_cast<Byte>(mmc3_.mirror));
    writer.write(mmc3_.irq_counter);
    writer.write(mmc3_.irq_reload);
    writer.write<Byte>((mmc3_.irq_reload_flag ? 1U : 0U) | (mmc3_.irq_enabled ? 2U : 0U) |
                       (mmc3_.irq_pending ? 4U : 0U));
    writer.write<Byte>((mmc3_.ram_enabled ? 0x80U : 0U) | (mmc3_.ram_write_protected ? 0x40U : 0U));
    writer.write(mmc1_.shift);
    writer.write(mmc1_.write_count);
    writer.write(mmc1_.control);
    writer.write(mmc1_.chr_bank0);
    writer.write(mmc1_.chr_bank1);
    writer.write(mmc1_.prg_bank);
}

void SomariP116::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto supervisor = reader.read<Byte>();
    Vrc2State vrc2{};
    reader.read_array(vrc2.prg_banks);
    reader.read_array(vrc2.chr_banks);
    vrc2.mirror = static_cast<MirrorMode>(reader.read<Byte>());
    reader.read(vrc2.microwire);
    Mmc3State mmc3{};
    reader.read(mmc3.bank_select);
    reader.read_array(mmc3.banks);
    mmc3.mirror = static_cast<MirrorMode>(reader.read<Byte>());
    reader.read(mmc3.irq_counter);
    reader.read(mmc3.irq_reload);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    Mmc1State mmc1{};
    reader.read(mmc1.shift);
    reader.read(mmc1.write_count);
    reader.read(mmc1.control);
    reader.read(mmc1.chr_bank0);
    reader.read(mmc1.chr_bank1);
    reader.read(mmc1.prg_bank);

    const auto valid_mirror = [](MirrorMode mirror) {
        return mirror == MirrorMode::Horizontal || mirror == MirrorMode::Vertical;
    };
    const auto sentinel = mmc1.write_count <= 4U ? 0x10U >> mmc1.write_count : 0U;
    const bool valid_shift = sentinel != 0U && mmc1.shift <= 0x1FU &&
                             (mmc1.shift & sentinel) != 0U &&
                             (mmc1.write_count != 0U || mmc1.shift == 0x10U);
    if (!reader.ok() || tag != (0x7400U | submapper_) || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || supervisor > 7U ||
        std::ranges::any_of(vrc2.prg_banks, [](Byte bank) { return bank > 0x1FU; }) ||
        !valid_mirror(vrc2.mirror) || vrc2.microwire > 1U || (mmc3.bank_select & 0x38U) != 0U ||
        mmc3.banks[6U] > 0x3FU || mmc3.banks[7U] > 0x3FU || !valid_mirror(mmc3.mirror) ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U || !valid_shift ||
        mmc1.control > 0x1FU || mmc1.chr_bank0 > 0x1FU || mmc1.chr_bank1 > 0x1FU ||
        mmc1.prg_bank > 0x1FU) {
        reader.fail();
        return;
    }
    supervisor_ = supervisor;
    vrc2_ = vrc2;
    mmc3_ = mmc3;
    mmc3_.irq_reload_flag = (irq & 1U) != 0U;
    mmc3_.irq_enabled = (irq & 2U) != 0U;
    mmc3_.irq_pending = (irq & 4U) != 0U;
    mmc3_.ram_enabled = (ram & 0x80U) != 0U;
    mmc3_.ram_write_protected = (ram & 0x40U) != 0U;
    mmc1_ = mmc1;
}

} // namespace mapperbus::core
