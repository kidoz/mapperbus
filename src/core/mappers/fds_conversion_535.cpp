#include "core/mappers/fds_conversion_535.hpp"

#include <algorithm>
#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
FdsConversion535::FdsConversion535(const INesHeader& header,
                                   std::vector<Byte> prg,
                                   std::vector<Byte>)
    : id_(header.mapper_number),
      initial_mirror_(id_ == 538U ? MirrorMode::Vertical : header.mirror_mode),
      prg_(std::move(prg)), ram_(id_ == 538U ? 0U : 8192U), mirror_(initial_mirror_) {}

Result<void> FdsConversion535::validate_geometry(const INesHeader& h,
                                                 std::size_t prg,
                                                 std::size_t chr) {
    if ((h.mapper_number != 535U && h.mapper_number != 538U && h.mapper_number != 539U) ||
        !h.is_nes2 || h.submapper != 0 || h.has_trainer || h.has_battery || h.prg_nvram_size ||
        h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected("Unsupported FDS cartridge conversion header");
    if (prg != 131072U || chr != 0 || h.chr_ram_size != 8192U ||
        (!(h.mapper_number == 535U && h.prg_ram_size == 0U) &&
         h.prg_ram_size != (h.mapper_number == 538U ? 0U : 8192U)))
        return std::unexpected("Unsupported FDS cartridge conversion ROM or RAM population");
    return {};
}

std::optional<std::size_t> FdsConversion535::ram_offset(Address addr) const {
    if (id_ == 535U && addr >= 0xB800U && addr < 0xD800U)
        return addr - 0xB800U;
    if (id_ == 539U) {
        const auto page = addr >> 8U;
        if (page == 0x60 || page == 0x62 || page == 0x64 || page == 0x65)
            return 0x1800U + (addr - 0x6000U);
        if (page == 0x82)
            return 0x1200U + (addr & 0xFFU);
        if ((page >= 0xC0 && page <= 0xD1) || page == 0xDF)
            return addr - 0xC000U;
    }
    return std::nullopt;
}

Byte FdsConversion535::read_prg(Address addr) {
    if (addr < 0x6000U)
        return 0;
    if (auto index = ram_offset(addr))
        return ram_[*index];
    unsigned bank = 12U + ((addr & 0x7FFFU) >> 13U);
    if (id_ == 538U && addr < 0xA000U) {
        if (addr < 0x8000U)
            bank = bank_ | 1U;
        else
            bank = (bank_ & 9U) == 1U ? 10U : (bank_ & 14U);
    } else if (id_ == 539U && addr < 0xC000U) {
        bank = addr < 0x8000U ? 13U : addr < 0xA000U ? 12U : bank_;
    } else if (addr < 0x8000U)
        bank = bank_;
    return prg_[bank * 8192U + (addr & 0x1FFFU)];
}

void FdsConversion535::write_prg(Address addr, Byte value) {
    if (auto index = ram_offset(addr)) {
        ram_[*index] = value;
        return;
    }
    if (id_ == 535U && (addr & 0xF000U) == 0xE000U) {
        irq_enabled_ = (value >> 1U) & 1U;
        irq_pending_ = 0;
        counter_ = 0;
    } else if ((id_ == 535U && addr >= 0xF000U) || (id_ == 538U && (addr & 0xE000U) == 0xC000U) ||
               (id_ == 539U && addr == 0xA000U)) {
        bank_ = value & 15U;
    } else if (id_ == 539U && addr == 0xFFFFU) {
        mirror_ = (value & 8U) ? MirrorMode::Horizontal : MirrorMode::Vertical;
    }
}

void FdsConversion535::clock_cpu() {
    if (irq_enabled_ && ++counter_ == 7560U)
        irq_pending_ = 1;
}

void FdsConversion535::power_on() {
    std::ranges::fill(ram_, 0);
    chr_ram_.fill(0);
    reset();
}

void FdsConversion535::reset() {
    mirror_ = initial_mirror_;
    counter_ = 0;
    irq_enabled_ = irq_pending_ = bank_ = 0;
}

void FdsConversion535::save_state(StateWriter& writer) const {
    writer.write(id_);
    writer.write(initial_mirror_);
    writer.write(mirror_);
    writer.write(bank_);
    if (id_ == 535U) {
        writer.write(irq_enabled_);
        writer.write(irq_pending_);
        writer.write(counter_);
    }
    writer.write_array(chr_ram_);
    writer.write_bytes(ram_);
}

void FdsConversion535::load_state(StateReader& r) {
    const auto id = r.read<std::uint16_t>();
    const auto wiring = r.read<MirrorMode>();
    const auto mirror = r.read<MirrorMode>();
    const auto bank = r.read<Byte>();
    Byte enabled = 0, pending = 0;
    std::uint16_t counter = 0;
    if (id_ == 535U) {
        r.read(enabled);
        r.read(pending);
        r.read(counter);
    }
    decltype(chr_ram_) chr{};
    std::vector<Byte> ram;
    r.read_array(chr);
    r.read_bytes(ram, ram_.size());
    if (!r.ok() || id != id_ || wiring != initial_mirror_ || bank > 15U ||
        (mirror != MirrorMode::Vertical && mirror != MirrorMode::Horizontal) ||
        (id_ != 539U && mirror != wiring) || enabled > 1U || pending > enabled ||
        (!enabled && counter) || ram.size() != ram_.size()) {
        r.fail();
        return;
    }
    mirror_ = mirror;
    bank_ = bank;
    irq_enabled_ = enabled;
    irq_pending_ = pending;
    counter_ = counter;
    chr_ram_ = chr;
    ram_.swap(ram);
}
} // namespace mapperbus::core
