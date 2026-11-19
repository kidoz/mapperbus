#include "core/mappers/mapper306.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomSize = 128UZ * 1024UZ;
constexpr std::size_t kChrRamSize = 8192U;
constexpr std::size_t kFixedPrgOffset = 3UZ * 32768UZ;
constexpr Address kRegisterMask = 0xD903U;
constexpr uint16_t kStateTag = 0xC306U;

} // namespace

Mapper306::Mapper306(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : ks7016b_(header.mapper_number == 549U), prg_rom_(std::move(prg_rom)) {
    power_on();
}

Result<void> Mapper306::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (!header.is_nes2 || (header.mapper_number != 306U && header.mapper_number != 549U) ||
        header.submapper != 0U)
        return std::unexpected("Mapper 306 requires NES 2.0 mapper 306 submapper 0");
    if (header.mapper_number == 549U && header.has_trainer)
        return std::unexpected("KS-7016B does not have trainer RAM");
    if (prg_size != kPrgRomSize || chr_size != 0U)
        return std::unexpected("Mapper 306 requires 128 KiB PRG ROM and no CHR ROM");
    if (header.has_battery || header.prg_ram_size != 0U || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kChrRamSize || header.chr_nvram_size != 0U)
        return std::unexpected("Mapper 306 requires only 8 KiB volatile CHR RAM");
    if (header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 306 requires vertical mirroring");
    return {};
}

Byte Mapper306::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        const auto offset = static_cast<std::size_t>(prg_bank_) * 8192U + (addr & 0x1FFFU);
        return prg_rom_[offset];
    }
    if (addr >= 0x8000U)
        return prg_rom_[(ks7016b_ ? 65536U : kFixedPrgOffset) + (addr & 0x7FFFU)];
    return 0U;
}

void Mapper306::write_prg(Address addr, Byte) {
    if (ks7016b_) {
        // KS-7016B PCB trace: on ROM writes CPU A5 sets both PRG A16 and A15.
        if (addr >= 0x8000U) {
            prg_bank_ = (addr >> 2U) & 15U;
            if (prg_bank_ & 8U)
                prg_bank_ |= 4U;
        }
        return;
    }
    if ((addr & kRegisterMask) != kRegisterMask)
        return;

    if ((addr & 0x0040U) == 0U) {
        prg_bank_ = static_cast<Byte>(0x08U | ((addr >> 2U) & 0x03U));
    } else {
        prg_bank_ = static_cast<Byte>((addr >> 2U) & 0x0FU);
        if (prg_bank_ >= 0x0CU)
            prg_bank_ = 0x0BU;
    }
}

bool Mapper306::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper306::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper306::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper306::mirror_mode() const {
    return MirrorMode::Vertical;
}

void Mapper306::reset() {
    // The source does not establish reset circuitry; zero is a deterministic policy.
    prg_bank_ = 0U;
}

void Mapper306::power_on() {
    reset();
    chr_ram_.fill(0U);
}

void Mapper306::save_state(StateWriter& writer) const {
    writer.write<Word>(ks7016b_ ? 549U : kStateTag);
    writer.write(prg_bank_);
    writer.write_array(chr_ram_);
}

void Mapper306::load_state(StateReader& reader) {
    const auto tag = reader.read<uint16_t>();
    const auto prg_bank = reader.read<Byte>();
    std::array<Byte, kChrRamSize> chr_ram{};
    reader.read_array(chr_ram);
    if (!reader.ok() || tag != (ks7016b_ ? 549U : kStateTag) ||
        (ks7016b_ ? (prg_bank > 15U || (prg_bank & 12U) == 8U) : prg_bank > 11U)) {
        reader.fail();
        return;
    }
    prg_bank_ = prg_bank;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
