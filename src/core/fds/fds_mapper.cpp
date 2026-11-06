#include "core/fds/fds_mapper.hpp"

#include <algorithm>

#include "core/fds/fds.hpp"
#include "core/state/state.hpp"

namespace mapperbus::core {

FdsMapper::FdsMapper(std::span<const Byte> bios, const Fds& drive) : drive_(drive) {
    std::ranges::copy(bios, bios_.begin());
}

Byte FdsMapper::read_prg(Address addr) {
    if (addr < 0x6000)
        return 0;
    return addr >= 0xE000 ? bios_[addr - 0xE000] : prg_ram_[addr - 0x6000];
}

void FdsMapper::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0xE000) {
        prg_ram_[addr - 0x6000] = value;
    }
}

bool FdsMapper::maps_prg(Address addr) const {
    return addr >= 0x6000;
}

Byte FdsMapper::read_chr(Address addr) {
    return chr_ram_[addr & 0x1FFFU];
}

void FdsMapper::write_chr(Address addr, Byte value) {
    chr_ram_[addr & 0x1FFFU] = value;
}

MirrorMode FdsMapper::mirror_mode() const {
    return drive_.mirror_mode();
}

void FdsMapper::save_state(StateWriter& writer) const {
    writer.write_array(prg_ram_);
    writer.write_array(chr_ram_);
}

void FdsMapper::load_state(StateReader& reader) {
    reader.read_array(prg_ram_);
    reader.read_array(chr_ram_);
}

} // namespace mapperbus::core
