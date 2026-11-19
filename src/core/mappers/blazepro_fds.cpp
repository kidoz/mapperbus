#include "core/mappers/blazepro_fds.hpp"

#include <utility>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
constexpr uint32_t kChangeInterval = 1789772U * 3;
constexpr uint32_t kEjectDelay = 50000;
} // namespace
BlazeProFds::BlazeProFds(const INesHeader&, std::vector<Byte> prg, std::vector<Byte>)
    : prg_(std::move(prg)) {
    power_on();
}
Result<void> BlazeProFds::validate_geometry(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (!h.is_nes2 || h.mapper_number != 469 || h.submapper || h.has_battery || h.has_trainer ||
        prg != 8 * 1024 * 1024 || chr || h.prg_ram_size != 32768 || h.chr_ram_size != 8192 ||
        h.chr_nvram_size || h.prg_nvram_size ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical))
        return std::unexpected(
            "BlazePro FDS requires 8 MiB of ROM, 32 KiB of work RAM and 8 KiB of CHR RAM");
    return {};
}
bool BlazeProFds::maps_prg(Address addr) const {
    return addr >= (disk_mode() ? 0x6000 : 0x8000);
}
Byte BlazeProFds::read_prg(Address addr) {
    if (!maps_prg(addr))
        return 0;
    if (!disk_mode())
        return prg_[regs_.bank * 32768U + (addr & 32767)];
    if (addr < 0xE000)
        return work_[addr - 0x6000];
    const unsigned bank = ((regs_.bank & 31U) << 5) | 7U;
    return prg_[bank * 8192U + (addr & 8191)];
}
void BlazeProFds::write_prg(Address addr, Byte value) {
    if (disk_mode() && addr >= 0x6000 && addr < 0xE000)
        work_[addr - 0x6000] = value;
}
MirrorMode BlazeProFds::mirror_mode() const {
    return !disk_mode() || (regs_.control & 8) ? MirrorMode::Horizontal : MirrorMode::Vertical;
}
bool BlazeProFds::maps_expansion(Address addr) const {
    switch (addr) {
    case 0x5020:
    case 0x5021:
    case 0x5022:
    case 0x5025:
    case 0x5030:
    case 0x5032:
    case 0x5100:
    case 0x5102:
    case 0x5103:
    case 0x5110:
    case 0x5700:
    case 0x5701:
        return true;
    default:
        return false;
    }
}
Byte BlazeProFds::expansion_read_mask(Address addr) const {
    if (addr == 0x5030 || addr == 0x5032)
        return 0xFF;
    return addr == 0x5103 && regs_.side >= 2 && regs_.side < 18 ? 0xFF : 0;
}
Byte BlazeProFds::drive_status() {
    // The FPGA's conversion BIOS polls this port after a long pause between sides.
    if (regs_.eject_delay == 0) {
        switch (regs_.change) {
        case Change::Ready:
            if (regs_.poll_interval == kChangeInterval) {
                regs_.change = Change::Eject;
                regs_.eject_delay = kEjectDelay;
            }
            break;
        case Change::Eject:
            regs_.inserted = false;
            regs_.change = Change::Insert;
            regs_.eject_delay = kEjectDelay;
            break;
        case Change::Insert:
            regs_.inserted = true;
            regs_.side ^= 1;
            regs_.change = Change::Ready;
            break;
        }
    }
    regs_.poll_interval = 0;
    return regs_.inserted ? 0 : 5;
}
Byte BlazeProFds::read_expansion(Address addr) {
    if (addr == 0x5032)
        return drive_status();
    if (addr == 0x5030) {
        const Byte status = regs_.timer_irq ? 1 : regs_.data_irq ? 2 : 0;
        regs_.timer_irq = regs_.data_irq = false;
        return status;
    }
    if (addr == 0x5103 && expansion_read_mask(addr)) {
        const unsigned block = ((regs_.side - 2U) << 1U) | (regs_.pointer >> 15U);
        return prg_[block * 0x40000U + 0x38000U + (regs_.pointer & 0x7FFFU)];
    }
    return 0;
}
void BlazeProFds::write_expansion(Address addr, Byte value) {
    switch (addr) {
    case 0x5020:
        regs_.reload = (regs_.reload & 0xFF00) | value;
        break;
    case 0x5021:
        regs_.reload = (regs_.reload & 0x00FF) | (value << 8);
        break;
    case 0x5022:
        regs_.timer_control = value & 3;
        if (value & 2)
            regs_.timer = regs_.reload;
        else
            regs_.timer_irq = false;
        break;
    case 0x5025:
        regs_.phase = 0;
        regs_.control = value;
        break;
    case 0x5100:
        regs_.pointer = 0xFFFB;
        regs_.poll_interval = 0;
        break;
    case 0x5102:
        ++regs_.pointer;
        regs_.data_irq = false;
        break;
    case 0x5110:
        regs_.side = value;
        regs_.poll_interval = 0;
        break;
    case 0x5700:
        regs_.mode = value;
        break;
    case 0x5701:
        regs_.bank = value;
        break;
    }
}
void BlazeProFds::clock_cpu() {
    if (regs_.timer_control & 2) {
        if (regs_.timer)
            --regs_.timer;
        else {
            regs_.timer_irq = true;
            if (regs_.timer_control & 1)
                regs_.timer = regs_.reload;
            else
                regs_.timer_control = 0;
        }
    }
    regs_.phase += 3;
    if (regs_.phase >= 448) {
        regs_.phase -= 448;
        regs_.data_irq = true;
    }
    if (regs_.poll_interval < kChangeInterval)
        ++regs_.poll_interval;
    if (regs_.eject_delay)
        --regs_.eject_delay;
}
bool BlazeProFds::irq_pending() const {
    return regs_.timer_irq || (regs_.data_irq && (regs_.control & 0x80));
}
void BlazeProFds::power_on() {
    work_.fill(0);
    chr_.fill(0);
    reset();
}
void BlazeProFds::reset() {
    regs_ = Registers{};
}
void BlazeProFds::save_state(StateWriter& w) const {
    w.write<Word>(469);
    w.write(regs_.mode);
    w.write(regs_.bank);
    w.write(regs_.side);
    w.write(regs_.control);
    w.write(regs_.pointer);
    w.write(regs_.phase);
    w.write(regs_.timer);
    w.write(regs_.reload);
    w.write(regs_.timer_control);
    w.write<Byte>(regs_.data_irq);
    w.write<Byte>(regs_.timer_irq);
    w.write<Byte>(regs_.inserted);
    w.write(regs_.change);
    w.write(regs_.poll_interval);
    w.write(regs_.eject_delay);
    w.write_array(work_);
    w.write_array(chr_);
}
void BlazeProFds::load_state(StateReader& r) {
    const auto tag = r.read<Word>();
    Registers regs;
    r.read(regs.mode);
    r.read(regs.bank);
    r.read(regs.side);
    r.read(regs.control);
    r.read(regs.pointer);
    r.read(regs.phase);
    r.read(regs.timer);
    r.read(regs.reload);
    r.read(regs.timer_control);
    auto data = r.read<Byte>(), timer = r.read<Byte>(), inserted = r.read<Byte>();
    r.read(regs.change);
    r.read(regs.poll_interval);
    r.read(regs.eject_delay);
    decltype(work_) work{};
    decltype(chr_) chr{};
    r.read_array(work);
    r.read_array(chr);
    if (!r.ok() || tag != 469 || regs.phase >= 448 || regs.timer_control > 3 || data > 1 ||
        timer > 1 || inserted > 1 || regs.change > Change::Insert ||
        regs.poll_interval > kChangeInterval || regs.eject_delay > kEjectDelay) {
        r.fail();
        return;
    }
    regs.data_irq = data;
    regs.timer_irq = timer;
    regs.inserted = inserted;
    regs_ = regs;
    work_ = work;
    chr_ = chr;
}
} // namespace mapperbus::core
