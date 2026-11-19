#include "core/mappers/mapper027.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {

constexpr std::size_t kPrgBankBytes = 8192;
constexpr std::size_t kChrBankBytes = 1024;
constexpr std::size_t kMaxPrgBytes = 256 * 1024;
constexpr std::size_t kMaxChrBytes = 512 * 1024;
constexpr std::uint32_t kStateTag = 0x1B4D4253U;
constexpr std::uint16_t kIrqPrescalerReset = 341;

} // namespace

// NESdev Mapper 027 r23209 identifies the VRC4f wiring. VRC2/VRC4 r24169
// supplies the bank and WRAM behavior; VRC IRQ r22720 supplies the CPU timer.
Mapper027::Mapper027(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)),
      prg_ram_(header.is_nes2 ? header.prg_ram_size + header.prg_nvram_size : 8192U),
      nvram_size_(header.is_nes2 ? header.prg_nvram_size
                                 : (header.has_battery ? prg_ram_.size() : 0U)),
      initial_mirror_(header.mirror_mode), mirror_(initial_mirror_) {
    reset();
}

Result<void> Mapper027::validate_geometry(const INesHeader& header,
                                          std::size_t prg_size,
                                          std::size_t chr_size) {
    if (header.is_nes2 && header.submapper != 0U)
        return std::unexpected("Mapper 027 supports submapper 0");
    if (prg_size < 4U * kPrgBankBytes || prg_size > kMaxPrgBytes || prg_size % kPrgBankBytes != 0U)
        return std::unexpected("Mapper 027 requires 32-256 KiB PRG ROM in 8 KiB units");
    if ((chr_size != 0U && chr_size < kChrBankBytes) || chr_size > kMaxChrBytes ||
        chr_size % kChrBankBytes != 0U)
        return std::unexpected("Mapper 027 supports at most 512 KiB CHR ROM in 1 KiB units");
    if (header.chr_nvram_size != 0U ||
        (chr_size == 0U && header.is_nes2 && header.chr_ram_size != 8192U) ||
        (chr_size != 0U && header.chr_ram_size != 0U))
        return std::unexpected("Mapper 027 requires either CHR ROM or 8 KiB volatile CHR RAM");
    if (header.is_nes2) {
        const std::size_t ram_size = header.prg_ram_size + header.prg_nvram_size;
        if ((ram_size != 0U && ram_size != 2048U && ram_size != 8192U) ||
            header.has_battery != (header.prg_nvram_size != 0U))
            return std::unexpected("Mapper 027 supports optional 2/8 KiB PRG RAM or NVRAM");
    }
    if (header.mirror_mode != MirrorMode::Horizontal && header.mirror_mode != MirrorMode::Vertical)
        return std::unexpected("Mapper 027 supports H/V startup mirroring");
    return {};
}

void Mapper027::reset() {
    prg_banks_ = {0U, 1U};
    chr_banks_.fill(0U);
    mirror_ = initial_mirror_;
    prg_control_ = 0U;
    irq_latch_ = 0U;
    irq_counter_ = 0U;
    irq_prescaler_ = kIrqPrescalerReset;
    irq_control_ = 0U;
    irq_pending_ = 0U;
}

void Mapper027::power_on() {
    reset();
    std::fill_n(prg_ram_.begin(), prg_ram_.size() - nvram_size_, 0U);
    chr_ram_.fill(0U);
}

bool Mapper027::maps_prg(Address addr) const {
    if (addr >= 0x8000U)
        return true;
    return addr >= 0x6000U && (prg_control_ & 1U) != 0U && !prg_ram_.empty() &&
           (prg_ram_.size() == 8192U || addr < 0x7000U);
}

Byte Mapper027::read_prg(Address addr) {
    if (addr < 0x8000U) {
        if (!maps_prg(addr))
            return 0U;
        return prg_ram_[(addr - 0x6000U) % prg_ram_.size()];
    }
    if (prg_rom_.empty())
        return 0U;

    const std::size_t bank_count = prg_rom_.size() / kPrgBankBytes;
    const auto slot = (addr - 0x8000U) / kPrgBankBytes;
    std::size_t bank = bank_count - 1U;
    if (slot == 1U) {
        bank = prg_banks_[1U];
    } else if (slot == 0U || slot == 2U) {
        const bool swapped = (prg_control_ & 2U) != 0U;
        bank = (slot == 0U) != swapped ? prg_banks_[0U] : bank_count - 2U;
    }
    return prg_rom_[(bank * kPrgBankBytes + (addr & 0x1FFFU)) % prg_rom_.size()];
}

void Mapper027::write_prg(Address addr, Byte value) {
    if (addr < 0x8000U) {
        if (maps_prg(addr))
            prg_ram_[(addr - 0x6000U) % prg_ram_.size()] = value;
        return;
    }

    const Address reg = addr & 0xF00FU;
    if (reg >= 0xB000U && reg <= 0xE003U) {
        const auto group = static_cast<unsigned>(reg >> 12U);
        const auto port = static_cast<unsigned>(reg & 3U);
        auto& bank = chr_banks_[(group - 0xBU) * 2U + port / 2U];
        if ((port & 1U) == 0U)
            bank = (bank & 0x1F0U) | (value & 0x0FU);
        else
            bank = (bank & 0x00FU) | ((value & 0x1FU) << 4U);
        return;
    }

    switch (reg) {
    case 0x8000U:
        prg_banks_[0U] = value & 0x1FU;
        break;
    case 0x9000U: {
        constexpr std::array kMirrorModes = {MirrorMode::Vertical,
                                             MirrorMode::Horizontal,
                                             MirrorMode::SingleLower,
                                             MirrorMode::SingleUpper};
        mirror_ = kMirrorModes[value & 3U];
        break;
    }
    case 0x9002U:
        prg_control_ = value & 3U;
        break;
    case 0xA000U:
        prg_banks_[1U] = value & 0x1FU;
        break;
    case 0xF000U:
        irq_latch_ = (irq_latch_ & 0xF0U) | (value & 0x0FU);
        break;
    case 0xF001U:
        irq_latch_ = (irq_latch_ & 0x0FU) | ((value & 0x0FU) << 4U);
        break;
    case 0xF002U:
        write_irq_control(value);
        break;
    case 0xF003U:
        acknowledge_irq_register();
        break;
    default:
        break;
    }
}

Byte Mapper027::read_chr(Address addr) {
    addr &= 0x1FFFU;
    const std::size_t offset = chr_banks_[addr / kChrBankBytes] * kChrBankBytes + (addr & 0x03FFU);
    return chr_rom_.empty() ? chr_ram_[offset % chr_ram_.size()]
                            : chr_rom_[offset % chr_rom_.size()];
}

void Mapper027::write_chr(Address addr, Byte value) {
    if (chr_rom_.empty()) {
        addr &= 0x1FFFU;
        const std::size_t offset =
            chr_banks_[addr / kChrBankBytes] * kChrBankBytes + (addr & 0x03FFU);
        chr_ram_[offset % chr_ram_.size()] = value;
    }
}

MirrorMode Mapper027::mirror_mode() const {
    return mirror_;
}

void Mapper027::write_irq_control(Byte value) {
    irq_pending_ = 0U;
    irq_prescaler_ = kIrqPrescalerReset;
    irq_control_ = value & 7U;
    if ((irq_control_ & 2U) != 0U)
        irq_counter_ = irq_latch_;
}

void Mapper027::acknowledge_irq_register() {
    irq_pending_ = 0U;
    irq_control_ = (irq_control_ & 5U) | ((irq_control_ & 1U) << 1U);
}

void Mapper027::tick_irq() {
    if (irq_counter_ == 0xFFU) {
        irq_counter_ = irq_latch_;
        irq_pending_ = 1U;
    } else {
        ++irq_counter_;
    }
}

void Mapper027::clock_cpu() {
    if ((irq_control_ & 2U) == 0U)
        return;
    if ((irq_control_ & 4U) != 0U) {
        tick_irq();
    } else if (irq_prescaler_ <= 3U) {
        irq_prescaler_ += kIrqPrescalerReset - 3U;
        tick_irq();
    } else {
        irq_prescaler_ -= 3U;
    }
}

bool Mapper027::irq_pending() const {
    return irq_pending_ != 0U;
}

void Mapper027::acknowledge_irq() {
    irq_pending_ = 0U;
}

std::span<const Byte> Mapper027::battery_ram() const {
    return std::span<const Byte>(prg_ram_).last(nvram_size_);
}

void Mapper027::set_battery_ram(std::span<const Byte> data) {
    const std::size_t count = std::min(data.size(), nvram_size_);
    std::copy_n(data.begin(), count, prg_ram_.end() - static_cast<std::ptrdiff_t>(nvram_size_));
}

void Mapper027::save_state(StateWriter& writer) const {
    writer.write(kStateTag);
    writer.write(static_cast<std::uint32_t>(prg_rom_.size()));
    writer.write(static_cast<std::uint32_t>(chr_rom_.size()));
    writer.write(initial_mirror_);
    writer.write_array(prg_banks_);
    writer.write_array(chr_banks_);
    writer.write(mirror_);
    writer.write(prg_control_);
    writer.write(irq_latch_);
    writer.write(irq_counter_);
    writer.write(irq_prescaler_);
    writer.write(irq_control_);
    writer.write(irq_pending_);
    writer.write_bytes(prg_ram_);
    writer.write_array(chr_ram_);
}

void Mapper027::load_state(StateReader& reader) {
    const auto tag = reader.read<std::uint32_t>();
    const auto prg_size = reader.read<std::uint32_t>();
    const auto chr_size = reader.read<std::uint32_t>();
    const auto initial_mirror = reader.read<MirrorMode>();
    std::array<Byte, 2> prg_banks{};
    std::array<std::uint16_t, 8> chr_banks{};
    reader.read_array(prg_banks);
    reader.read_array(chr_banks);
    const auto mirror = reader.read<MirrorMode>();
    const auto prg_control = reader.read<Byte>();
    const auto irq_latch = reader.read<Byte>();
    const auto irq_counter = reader.read<Byte>();
    const auto irq_prescaler = reader.read<std::uint16_t>();
    const auto irq_control = reader.read<Byte>();
    const auto irq_pending = reader.read<Byte>();
    std::vector<Byte> prg_ram;
    reader.read_bytes(prg_ram, 8192U);
    std::array<Byte, 8192> chr_ram{};
    reader.read_array(chr_ram);

    const bool valid_mirror = mirror == MirrorMode::Horizontal || mirror == MirrorMode::Vertical ||
                              mirror == MirrorMode::SingleLower ||
                              mirror == MirrorMode::SingleUpper;
    const bool valid =
        reader.ok() && tag == kStateTag && prg_size == prg_rom_.size() &&
        chr_size == chr_rom_.size() && initial_mirror == initial_mirror_ &&
        prg_banks[0U] <= 0x1FU && prg_banks[1U] <= 0x1FU &&
        std::ranges::all_of(chr_banks, [](std::uint16_t bank) { return bank <= 0x1FFU; }) &&
        valid_mirror && prg_control <= 3U && irq_prescaler >= 1U &&
        irq_prescaler <= kIrqPrescalerReset && irq_control <= 7U && irq_pending <= 1U &&
        (irq_pending == 0U || (irq_control & 2U) != 0U) && prg_ram.size() == prg_ram_.size();
    if (!valid) {
        reader.fail();
        return;
    }

    prg_banks_ = prg_banks;
    chr_banks_ = chr_banks;
    mirror_ = mirror;
    prg_control_ = prg_control;
    irq_latch_ = irq_latch;
    irq_counter_ = irq_counter;
    irq_prescaler_ = irq_prescaler;
    irq_control_ = irq_control;
    irq_pending_ = irq_pending;
    prg_ram_ = std::move(prg_ram);
    chr_ram_ = chr_ram;
}

} // namespace mapperbus::core
