#include "core/mappers/mmc1a.hpp"

#include <algorithm>
#include <bit>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kBank16K = 16384U;
constexpr std::size_t kBank8K = 8192U;
constexpr std::uint32_t kStateTag = 0x9B0001U;

bool valid_shift_register(Byte shift, uint8_t count) {
    if (count > 4U || shift > 0x1FU)
        return false;
    const auto sentinel = static_cast<Byte>(0x10U >> count);
    return (shift & sentinel) != 0U && (shift & (sentinel - 1U)) == 0U;
}

} // namespace

Mmc1a::Mmc1a(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    if (chr_rom_.empty())
        chr_ram_.resize(kBank8K);
    if (header.is_nes2) {
        prg_ram_.resize(header.prg_ram_size + header.prg_nvram_size);
        prg_nvram_size_ = header.prg_nvram_size;
    } else {
        // The pinned MMC1 article recommends 32 KiB when iNES cannot declare it.
        prg_ram_.resize(32768U);
        prg_nvram_size_ = header.has_battery ? prg_ram_.size() : 0U;
    }
    prg_nvram_offset_ = prg_ram_.size() - prg_nvram_size_;
    const auto chr_size = chr_rom_.empty() ? chr_ram_.size() : chr_rom_.size();
    snrom_ = prg_ram_.size() == kBank8K && chr_size == kBank8K && prg_rom_.size() <= 262144U;
    power_on();
}

Result<void> Mmc1a::validate_geometry(const INesHeader& header,
                                      std::size_t prg_size,
                                      std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U && header.submapper != 5U)
        return std::unexpected("Mapper 155 supports SxROM submappers 0 and 5, not 2ME or KS-7058");
    if (prg_size < 32768U || prg_size > 524288U || !std::has_single_bit(prg_size) ||
        (chr_size != 0U &&
         (chr_size < 8192U || chr_size > 131072U || !std::has_single_bit(chr_size))))
        return std::unexpected("Unsupported Mapper 155 PRG/CHR ROM geometry");
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 155 requires H/V startup nametable metadata");
    if (header.chr_nvram_size != 0U ||
        (chr_size == 0U ? header.chr_ram_size != kBank8K : header.chr_ram_size != 0U))
        return std::unexpected("Mapper 155 supports only 8 KiB volatile CHR RAM");

    std::size_t ram_size = 32768U;
    std::size_t nvram_size = header.has_battery ? ram_size : 0U;
    if (header.is_nes2) {
        ram_size = header.prg_ram_size + header.prg_nvram_size;
        nvram_size = header.prg_nvram_size;
        if (header.has_battery != (nvram_size != 0U))
            return std::unexpected("Mapper 155 battery and PRG-NVRAM declarations disagree");
        const bool valid_ram =
            ram_size == 0U ||
            (ram_size == kBank8K && header.prg_ram_size + header.prg_nvram_size == kBank8K &&
             (header.prg_ram_size == 0U || header.prg_nvram_size == 0U)) ||
            (ram_size == 16384U && ((nvram_size == 0U && header.prg_ram_size == 16384U) ||
                                    (nvram_size == kBank8K && header.prg_ram_size == kBank8K))) ||
            (ram_size == 32768U && ((nvram_size == 0U && header.prg_ram_size == 32768U) ||
                                    (nvram_size == 32768U && header.prg_ram_size == 0U)));
        if (!valid_ram)
            return std::unexpected("Unsupported Mapper 155 PRG-RAM/NVRAM population");
    } else if (header.chr_ram_size != (chr_size == 0U ? kBank8K : 0U)) {
        return std::unexpected("Mapper 155 legacy CHR-RAM metadata disagrees");
    }

    if (ram_size != 0U && ram_size != kBank8K && ram_size != 16384U && ram_size != 32768U)
        return std::unexpected("Unsupported Mapper 155 PRG-RAM size");
    const auto chr_capacity = chr_size == 0U ? kBank8K : chr_size;
    if (prg_size <= 65536U && chr_size < 16384U)
        return std::unexpected("Unsupported Mapper 155 small-PRG CHR population");
    if (prg_size == 524288U && chr_capacity != kBank8K)
        return std::unexpected("Mapper 155 512 KiB PRG requires 8 KiB CHR");
    if (!header.is_nes2)
        return {};
    if (header.submapper == 5U && (prg_size != 32768U || ram_size != 0U || chr_size < 16384U))
        return std::unexpected("Mapper 155 submapper 5 requires unbanked 32 KiB PRG and no WRAM");
    if (prg_size <= 65536U && (chr_size < 16384U || ram_size > kBank8K))
        return std::unexpected("Unsupported Mapper 155 small-PRG board population");
    if (prg_size == 65536U && ram_size != 0U)
        return std::unexpected("Mapper 155 64 KiB PRG boards have no PRG RAM");
    if (prg_size < 131072U && ram_size > kBank8K)
        return std::unexpected("Mapper 155 banked WRAM requires at least 128 KiB PRG");
    if (prg_size == 524288U &&
        (chr_capacity != kBank8K || (ram_size != kBank8K && ram_size != 32768U)))
        return std::unexpected("Mapper 155 512 KiB PRG requires SUROM/SXROM memory");
    if (ram_size == 16384U && prg_size >= 131072U && chr_capacity != kBank8K &&
        (chr_capacity < 16384U || chr_capacity > 65536U))
        return std::unexpected("Mapper 155 16 KiB WRAM requires SOROM or SZROM geometry");
    if (ram_size == 32768U && chr_capacity != kBank8K)
        return std::unexpected("Mapper 155 32 KiB WRAM requires SXROM's 8 KiB CHR");
    return {};
}

Byte Mmc1a::active_chr_register() const {
    return (control_ & 0x10U) != 0U && ppu_a12_ ? chr_bank1_ : chr_bank0_;
}

bool Mmc1a::snrom_ram_disabled() const {
    return snrom_ && (active_chr_register() & 0x10U) != 0U;
}

bool Mmc1a::ram_mapped() const {
    return !prg_ram_.empty() && !snrom_ram_disabled();
}

std::size_t Mmc1a::ram_bank() const {
    const auto chr = active_chr_register();
    if (prg_ram_.size() == 16384U) {
        const auto chr_size = chr_rom_.empty() ? chr_ram_.size() : chr_rom_.size();
        return chr_size == kBank8K ? (chr >> 3U) & 1U : (chr >> 4U) & 1U;
    }
    if (prg_ram_.size() == 32768U)
        return (chr >> 2U) & 3U;
    return 0U;
}

std::size_t Mmc1a::prg_address(Address addr) const {
    const auto mode = (control_ >> 2U) & 3U;
    const auto selected = prg_bank_ & 0x0FU;
    unsigned bank = 0;
    bool fixed = false;
    if (mode <= 1U) {
        bank = (selected & 0x0EU) | (addr >= 0xC000U ? 1U : 0U);
    } else if (mode == 2U) {
        fixed = addr < 0xC000U;
        bank = fixed ? 0U : selected;
    } else {
        fixed = addr >= 0xC000U;
        bank = fixed ? 0x0FU : selected;
    }
    if (fixed && (prg_bank_ & 0x10U) != 0U)
        bank = (bank & 7U) | ((prg_bank_ & 8U) != 0U ? 8U : 0U);

    std::size_t offset = bank * kBank16K + (addr & 0x3FFFU);
    if (prg_rom_.size() == 524288U && (active_chr_register() & 0x10U) != 0U)
        offset += 262144U;
    return offset & (prg_rom_.size() - 1U);
}

std::size_t Mmc1a::chr_address(Address addr) const {
    const bool upper = (addr & 0x1000U) != 0U;
    unsigned bank = 0;
    if ((control_ & 0x10U) != 0U) {
        bank = upper ? chr_bank1_ : chr_bank0_;
    } else {
        bank = chr_bank0_ & 0x1EU;
        if (upper)
            ++bank;
    }
    const auto size = chr_rom_.empty() ? chr_ram_.size() : chr_rom_.size();
    return (bank * 4096U + (addr & 0x0FFFU)) & (size - 1U);
}

Byte Mmc1a::read_prg(Address addr) {
    if (addr >= 0x8000U)
        return prg_rom_[prg_address(addr)];
    if (addr >= 0x6000U && ram_mapped())
        return prg_ram_[ram_bank() * kBank8K + (addr & 0x1FFFU)];
    return 0U;
}

void Mmc1a::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (addr >= 0x6000U && ram_mapped())
            prg_ram_[ram_bank() * kBank8K + (addr & 0x1FFFU)] = value;
        return;
    }
    if ((value & 0x80U) != 0U) {
        reset_serial_port();
        return;
    }
    shift_register_ = static_cast<Byte>((shift_register_ >> 1U) | ((value & 1U) << 4U));
    ++write_count_;
    if (write_count_ == 5U) {
        write_register(addr, shift_register_);
        shift_register_ = 0x10U;
        write_count_ = 0U;
    }
}

void Mmc1a::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || !consecutive || (value & 0x80U) != 0U)
        write_prg(addr, value);
}

bool Mmc1a::maps_prg(Address addr) const {
    return addr >= 0x8000U || (addr >= 0x6000U && addr < 0x8000U && ram_mapped());
}

Byte Mmc1a::read_chr(Address addr) {
    ppu_a12_ = (addr & 0x1000U) != 0U;
    const auto offset = chr_address(addr);
    return chr_rom_.empty() ? chr_ram_[offset] : chr_rom_[offset];
}

void Mmc1a::write_chr(Address addr, Byte value) {
    ppu_a12_ = (addr & 0x1000U) != 0U;
    if (!chr_rom_.empty())
        return;
    chr_ram_[chr_address(addr)] = value;
}

MirrorMode Mmc1a::mirror_mode() const {
    switch (control_ & 3U) {
    case 0U:
        return MirrorMode::SingleLower;
    case 1U:
        return MirrorMode::SingleUpper;
    case 2U:
        return MirrorMode::Vertical;
    default:
        return MirrorMode::Horizontal;
    }
}

bool Mmc1a::observes_ppu_a12() const {
    return true;
}

void Mmc1a::observe_ppu_a12(bool high) {
    ppu_a12_ = high;
}

void Mmc1a::reset_serial_port() {
    shift_register_ = 0x10U;
    write_count_ = 0U;
    control_ |= 0x0CU;
}

void Mmc1a::reset() {
    reset_serial_port();
}

void Mmc1a::power_on() {
    shift_register_ = 0x10U;
    write_count_ = 0U;
    control_ = 0x0CU;
    chr_bank0_ = 0U;
    chr_bank1_ = 0U;
    prg_bank_ = 0U;
    ppu_a12_ = false;
    std::ranges::fill(chr_ram_, 0U);
    std::fill_n(prg_ram_.begin(), prg_nvram_offset_, 0U);
}

void Mmc1a::write_register(Address addr, Byte value) {
    value &= 0x1FU;
    if (addr < 0xA000U)
        control_ = value;
    else if (addr < 0xC000U)
        chr_bank0_ = value;
    else if (addr < 0xE000U)
        chr_bank1_ = value;
    else
        prg_bank_ = value;
}

std::span<const Byte> Mmc1a::battery_ram() const {
    if (prg_nvram_size_ == 0U)
        return {};
    return std::span<const Byte>(prg_ram_).subspan(prg_nvram_offset_, prg_nvram_size_);
}

void Mmc1a::set_battery_ram(std::span<const Byte> data) {
    if (prg_nvram_size_ == 0U)
        return;
    const auto count = std::min(data.size(), prg_nvram_size_);
    std::copy_n(
        data.begin(), count, prg_ram_.begin() + static_cast<std::ptrdiff_t>(prg_nvram_offset_));
}

void Mmc1a::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(static_cast<std::uint32_t>(prg_ram_.size()));
    writer.write(static_cast<std::uint32_t>(prg_nvram_size_));
    writer.write(shift_register_);
    writer.write(write_count_);
    writer.write(control_);
    writer.write(chr_bank0_);
    writer.write(chr_bank1_);
    writer.write(prg_bank_);
    writer.write(static_cast<Byte>(ppu_a12_));
    writer.write_bytes(prg_ram_);
    writer.write_bytes(chr_ram_);
}

void Mmc1a::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint32_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto ram_size = reader.read<std::uint32_t>();
    const auto nvram_size = reader.read<std::uint32_t>();
    const auto shift = reader.read<Byte>();
    const auto count = reader.read<uint8_t>();
    const auto control = reader.read<Byte>();
    const auto chr0 = reader.read<Byte>();
    const auto chr1 = reader.read<Byte>();
    const auto prg = reader.read<Byte>();
    const auto a12 = reader.read<Byte>();
    std::vector<Byte> ram;
    std::vector<Byte> chr;
    reader.read_bytes(ram, 32768U);
    reader.read_bytes(chr, kBank8K);
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || ram_size != prg_ram_.size() ||
        nvram_size != prg_nvram_size_ || ram.size() != prg_ram_.size() ||
        chr.size() != chr_ram_.size() || !valid_shift_register(shift, count) || control > 0x1FU ||
        chr0 > 0x1FU || chr1 > 0x1FU || prg > 0x1FU || a12 > 1U) {
        reader.fail();
        return;
    }
    shift_register_ = shift;
    write_count_ = count;
    control_ = control;
    chr_bank0_ = chr0;
    chr_bank1_ = chr1;
    prg_bank_ = prg;
    ppu_a12_ = a12 != 0U;
    prg_ram_ = std::move(ram);
    chr_ram_ = std::move(chr);
}

} // namespace mapperbus::core
