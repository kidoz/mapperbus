#include "core/mappers/mapper297.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgSize = 256U * 1024U;
constexpr std::size_t kChrSize = 256U * 1024U;
constexpr std::size_t kOuterSize = 128U * 1024U;
constexpr std::size_t kPrgBankSize = 16U * 1024U;
constexpr std::size_t kChrBankSize = 4U * 1024U;
constexpr std::uint32_t kStateTag = 0xC1297001U;

} // namespace

// Original implementation from NESdev mapper 297 r7767, its linked MMC1 and
// Mapper 70 descriptions, and the PCB trace in NESdev forum topic 17067.
Mapper297::Mapper297(const INesHeader&, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)) {
    power_on();
}

Result<void> Mapper297::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || header.mapper_number != 297U || header.submapper != 0U)
        return std::unexpected("Mapper 297 requires NES 2.0 mapper 297 submapper 0");
    if (prg_size != kPrgSize || chr_size != kChrSize)
        return std::unexpected("Mapper 297 requires 256 KiB PRG ROM and 256 KiB CHR ROM");
    if (header.has_trainer || header.has_battery || header.prg_ram_size != 0U ||
        header.prg_nvram_size != 0U || header.chr_ram_size != 0U || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 297 supports only the ROM-only PCB population");
    if (header.mirror_mode != MirrorMode::Vertical && header.mirror_mode != MirrorMode::Horizontal)
        return std::unexpected("Mapper 297 requires fixed vertical nametable wiring");
    return {};
}

bool Mapper297::mmc1_mode() const {
    return (mode_ & 1U) != 0U;
}

std::size_t Mapper297::mapper70_prg_address(Address addr) const {
    const std::size_t outer = static_cast<std::size_t>(mode_ & 1U) * kOuterSize;
    const std::size_t block = static_cast<std::size_t>((mode_ >> 1U) & 1U) * 65536U;
    const unsigned bank = addr < 0xC000U ? (mapper70_latch_ >> 4U) & 3U : 3U;
    return outer + block + bank * kPrgBankSize + (addr & 0x3FFFU);
}

std::size_t Mapper297::mmc1_prg_address(Address addr) const {
    const unsigned selected = prg_bank_ & 7U;
    const unsigned mode = (control_ >> 2U) & 3U;
    unsigned bank = 0U;
    if (mode <= 1U) {
        bank = (selected & 6U) | (addr >= 0xC000U ? 1U : 0U);
    } else if (mode == 2U) {
        bank = addr < 0xC000U ? 0U : selected;
    } else {
        bank = addr < 0xC000U ? selected : 7U;
    }
    return kOuterSize + bank * kPrgBankSize + (addr & 0x3FFFU);
}

Byte Mapper297::read_prg(Address addr) {
    if (addr < 0x8000U)
        return 0U;
    const auto offset = mmc1_mode() ? mmc1_prg_address(addr) : mapper70_prg_address(addr);
    return prg_rom_[offset];
}

void Mapper297::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U)
        return;
    if (mmc1_mode()) {
        write_mmc1(addr, value);
    } else {
        mapper70_latch_ = static_cast<Byte>(value & read_prg(addr));
    }
}

void Mapper297::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    if (addr < 0x8000U || !mmc1_mode() || !consecutive || (value & 0x80U) != 0U)
        write_prg(addr, value);
}

bool Mapper297::maps_prg(Address addr) const {
    return addr >= 0x8000U;
}

void Mapper297::write_expansion(Address addr, Byte value) {
    if ((addr & 0xE100U) == 0x4100U)
        mode_ = value & 3U;
}

std::size_t Mapper297::chr_address(Address addr) const {
    if (!mmc1_mode())
        return (mapper70_latch_ & 0x0FU) * 8192U + (addr & 0x1FFFU);

    const bool upper = (addr & 0x1000U) != 0U;
    unsigned bank = 0U;
    if ((control_ & 0x10U) != 0U) {
        bank = (upper ? chr_bank1_ : chr_bank0_) & 0x1FU;
    } else {
        bank = chr_bank0_ & 0x1EU;
        if (upper)
            ++bank;
    }
    return kOuterSize + bank * kChrBankSize + (addr & 0x0FFFU);
}

Byte Mapper297::read_chr(Address addr) {
    return chr_rom_[chr_address(addr)];
}

void Mapper297::write_chr(Address, Byte) {}

MirrorMode Mapper297::mirror_mode() const {
    return MirrorMode::Vertical;
}

void Mapper297::reset_serial_port() {
    serial_value_ = 0U;
    serial_count_ = 0U;
    control_ |= 0x0CU;
}

void Mapper297::write_mmc1(Address addr, Byte value) {
    if ((value & 0x80U) != 0U) {
        reset_serial_port();
        return;
    }
    serial_value_ |= static_cast<Byte>((value & 1U) << serial_count_);
    ++serial_count_;
    if (serial_count_ == 5U) {
        commit_mmc1_register(addr, serial_value_);
        serial_value_ = 0U;
        serial_count_ = 0U;
    }
}

void Mapper297::commit_mmc1_register(Address addr, Byte value) {
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

void Mapper297::power_on() {
    reset();
}

void Mapper297::reset() {
    // The PCB references do not establish reset circuitry. This deterministic
    // policy selects Mapper 70 bank zero and the MMC1 fixed-last PRG mode.
    mode_ = 0U;
    mapper70_latch_ = 0U;
    serial_value_ = 0U;
    serial_count_ = 0U;
    control_ = 0x0CU;
    chr_bank0_ = 0U;
    chr_bank1_ = 0U;
    prg_bank_ = 0U;
}

void Mapper297::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(mode_);
    writer.write(mapper70_latch_);
    writer.write(serial_value_);
    writer.write(serial_count_);
    writer.write(control_);
    writer.write(chr_bank0_);
    writer.write(chr_bank1_);
    writer.write(prg_bank_);
}

void Mapper297::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint32_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto mode = reader.read<Byte>();
    const auto latch = reader.read<Byte>();
    const auto serial = reader.read<Byte>();
    const auto count = reader.read<std::uint8_t>();
    const auto control = reader.read<Byte>();
    const auto chr0 = reader.read<Byte>();
    const auto chr1 = reader.read<Byte>();
    const auto prg = reader.read<Byte>();

    const bool valid_serial = count <= 4U && serial < (1U << count);
    if (!reader.ok() || tag != kStateTag || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || mode > 3U || !valid_serial || control > 0x1FU ||
        chr0 > 0x1FU || chr1 > 0x1FU || prg > 0x1FU) {
        reader.fail();
        return;
    }

    mode_ = mode;
    mapper70_latch_ = latch;
    serial_value_ = serial;
    serial_count_ = count;
    control_ = control;
    chr_bank0_ = chr0;
    chr_bank1_ = chr1;
    prg_bank_ = prg;
}

} // namespace mapperbus::core
