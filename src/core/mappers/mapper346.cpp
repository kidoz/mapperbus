#include "core/mappers/mapper346.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgRomSize = 0x10000UZ;
constexpr std::size_t kPrgBankSize = 0x8000UZ;
constexpr std::size_t kWorkRamSize = 0x2000UZ;
constexpr std::uint16_t kStateTag = 0x3460U;

} // namespace

// Board decoding follows NESdev mapper 346 oldid 7898. Its address masks are
// unknown, so only the two published addresses select banks. Soft reset restores
// the documented default bank; cold RAM clearing is a deterministic policy.
Mapper346::Mapper346(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte>)
    : prg_rom_(std::move(prg_rom)), mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper346::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 346U) {
        return std::unexpected("Mapper 346 requires mapper ID 346");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 346 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgRomSize || chr_size != 0U) {
        return std::unexpected("Mapper 346 requires 64 KiB PRG ROM and no CHR ROM");
    }
    if (header.has_battery || header.prg_ram_size != kWorkRamSize || header.prg_nvram_size != 0U ||
        header.chr_ram_size != kWorkRamSize || header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 346 requires exactly 8 KiB volatile PRG and CHR RAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 346 requires fixed H/V CIRAM metadata");
    }
    return {};
}

Byte Mapper346::read_prg(Address addr) {
    if (addr < 0x6000U) {
        return 0U;
    }
    if (addr < 0x8000U) {
        return prg_ram_[addr & 0x1FFFU];
    }
    const std::size_t offset =
        static_cast<std::size_t>(prg_bank_) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return prg_rom_[offset];
}

void Mapper346::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_ram_[addr & 0x1FFFU] = value;
    } else if (addr == 0xE0A0U) {
        prg_bank_ = 0U;
    } else if (addr == 0xEE36U) {
        prg_bank_ = 1U;
    }
}

bool Mapper346::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper346::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void Mapper346::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode Mapper346::mirror_mode() const {
    return mirror_;
}

void Mapper346::power_on() {
    reset();
    prg_ram_.fill(0U);
    chr_ram_.fill(0U);
}

void Mapper346::reset() {
    prg_bank_ = 1U;
}

std::uint16_t Mapper346::state_tag() const {
    return static_cast<std::uint16_t>(kStateTag | (mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper346::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(prg_bank_);
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper346::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto prg_bank = reader.read<Byte>();
    std::array<Byte, kWorkRamSize> prg_ram{};
    reader.read_array(prg_ram);
    std::array<Byte, kWorkRamSize> chr_ram{};
    reader.read_array(chr_ram);

    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() || prg_bank > 1U) {
        reader.fail();
        return;
    }

    prg_bank_ = prg_bank;
    prg_ram_ = prg_ram;
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
