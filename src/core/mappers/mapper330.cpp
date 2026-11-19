#include "core/mappers/mapper330.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankBytes = 8192U;
constexpr std::size_t kChrBankBytes = 1024U;
constexpr std::size_t kPrgRomBytes = 256U * 1024U;
constexpr std::size_t kChrRomBytes = 256U * 1024U;
constexpr std::uint16_t kIrqEnable = 0x8000U;

} // namespace

// Original implementation from NESdev r20367 and hardware-analysis posts
// 220157/220212. The board has no documented reset input; zeroed cold-power
// latches are deterministic emulator policies, not measured power-up contents.
Mapper330::Mapper330(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      initial_mirror_(header.mirror_mode) {
    power_on();
}

Result<void> Mapper330::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.mapper_number != 330U) {
        return std::unexpected("Mapper 330 requires mapper ID 330");
    }
    if (!header.is_nes2 || header.submapper != 0U) {
        return std::unexpected("Mapper 330 requires NES 2.0 submapper 0");
    }
    if (prg_size != kPrgRomBytes || chr_size != kChrRomBytes) {
        return std::unexpected("Mapper 330 requires 256 KiB PRG ROM and 256 KiB CHR ROM");
    }
    if (!header.has_battery || header.prg_ram_size != 0U ||
        header.prg_nvram_size != kPrgBankBytes || header.chr_ram_size != 0U ||
        header.chr_nvram_size != 0U) {
        return std::unexpected("Mapper 330 requires exactly 8 KiB battery-backed PRG NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal &&
        header.mirror_mode != MirrorMode::Vertical) {
        return std::unexpected("Mapper 330 requires ordinary H/V nametable metadata");
    }
    return {};
}

bool Mapper330::maps_prg(Address addr) const {
    return addr >= 0x6000U;
}

Byte Mapper330::read_prg(Address addr) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return prg_nvram_[addr - 0x6000U];
    }
    if (addr < 0x8000U) {
        return 0U;
    }

    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankBytes);
    const std::size_t bank = slot < 3U ? prg_banks_[slot] : (kPrgRomBytes / kPrgBankBytes) - 1U;
    return prg_rom_[bank * kPrgBankBytes + (addr & (kPrgBankBytes - 1U))];
}

void Mapper330::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        prg_nvram_[addr - 0x6000U] = value;
        return;
    }
    if (addr < 0x8000U) {
        return;
    }

    switch (addr & 0xFC00U) {
    case 0x8000U:
    case 0x8800U:
    case 0x9000U:
    case 0x9800U:
    case 0xA000U:
    case 0xA800U:
    case 0xB000U:
    case 0xB800U:
        chr_banks_[(addr - 0x8000U) / 0x0800U] = value;
        break;
    case 0xC000U:
    case 0xC800U:
    case 0xD000U:
    case 0xD800U:
        nametable_banks_[(addr - 0xC000U) / 0x0800U] = value & 1U;
        break;
    case 0xE000U:
        prg_banks_[0] = value & 0x1FU;
        break;
    case 0xE800U:
        prg_banks_[1] = value & 0x1FU;
        break;
    case 0xF000U:
        prg_banks_[2] = value & 0x1FU;
        break;
    default:
        break;
    }

    switch (addr & 0xE400U) {
    case 0x8400U:
        irq_counter_ = (irq_counter_ & 0xFF00U) | value;
        irq_load_pending_ = true;
        break;
    case 0xA400U:
        irq_counter_ = (irq_counter_ & 0x00FFU) | (static_cast<std::uint16_t>(value) << 8U);
        irq_pending_ = false;
        irq_load_pending_ = true;
        break;
    default:
        break;
    }
}

Byte Mapper330::read_chr(Address addr) {
    const auto slot = static_cast<unsigned>((addr >> 10U) & 7U);
    return chr_rom_[static_cast<std::size_t>(chr_banks_[slot]) * kChrBankBytes +
                    (addr & (kChrBankBytes - 1U))];
}

void Mapper330::write_chr(Address, Byte) {}

MirrorMode Mapper330::mirror_mode() const {
    return initial_mirror_;
}

std::optional<Address> Mapper330::nametable_address(Address addr) const {
    if (addr < 0x2000U || addr >= 0x3F00U) {
        return std::nullopt;
    }
    const auto table = static_cast<unsigned>((addr >> 10U) & 3U);
    return static_cast<Address>(((nametable_banks_[table] & 1U) << 10U) | (addr & 0x03FFU));
}

void Mapper330::power_on() {
    prg_banks_.fill(0U);
    chr_banks_.fill(0U);
    nametable_banks_.fill(0U);
    irq_counter_ = 0U;
    irq_pending_ = false;
    irq_load_pending_ = false;
}

void Mapper330::reset() {}

bool Mapper330::irq_pending() const {
    return irq_pending_;
}

void Mapper330::clock_cpu_falling_edge() {
    if (irq_load_pending_) {
        irq_load_pending_ = false;
        return;
    }
    if ((irq_counter_ & kIrqEnable) == 0U) {
        return;
    }
    ++irq_counter_;
    if (irq_counter_ == 0U) {
        irq_pending_ = true;
    }
}

std::uint16_t Mapper330::state_tag() const {
    return static_cast<std::uint16_t>(0x3300U |
                                      (initial_mirror_ == MirrorMode::Vertical ? 1U : 0U));
}

void Mapper330::save_state(StateWriter& writer) const {
    writer.write(state_tag());
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write_array(nametable_banks_);
    writer.write(irq_counter_);
    writer.write<Byte>(irq_pending_ ? 1U : 0U);
    writer.write<Byte>(irq_load_pending_ ? 1U : 0U);
    writer.write_array(prg_nvram_);
}

void Mapper330::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint16_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    std::array<Byte, 3> prg_banks{};
    std::array<Byte, 8> chr_banks{};
    std::array<Byte, 4> nametable_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    reader.read_array(nametable_banks);
    const auto irq_counter = reader.read<std::uint16_t>();
    const auto irq_pending = reader.read<Byte>();
    const auto irq_load_pending = reader.read<Byte>();
    std::array<Byte, 8192> prg_nvram{};
    reader.read_array(prg_nvram);

    const bool valid_prg = std::ranges::all_of(prg_banks, [](Byte bank) { return bank <= 0x1FU; });
    const bool valid_nametables =
        std::ranges::all_of(nametable_banks, [](Byte bank) { return bank <= 1U; });
    if (!reader.ok() || tag != state_tag() || prg_size != prg_rom_.size() ||
        chr_size != chr_rom_.size() || !valid_prg || !valid_nametables || irq_pending > 1U ||
        irq_load_pending > 1U || (irq_pending != 0U && irq_counter != 0U)) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    nametable_banks_ = nametable_banks;
    irq_counter_ = irq_counter;
    irq_pending_ = irq_pending != 0U;
    irq_load_pending_ = irq_load_pending != 0U;
    prg_nvram_ = prg_nvram;
}

std::span<const Byte> Mapper330::battery_ram() const {
    return prg_nvram_;
}

void Mapper330::set_battery_ram(std::span<const Byte> data) {
    if (data.size() == prg_nvram_.size()) {
        std::ranges::copy(data, prg_nvram_.begin());
    }
}

} // namespace mapperbus::core
