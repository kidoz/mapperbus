#include "core/bus/memory_bus.hpp"

#include "core/apu/apu.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/fds/fds.hpp"
#include "core/input/controller.hpp"
#include "core/ppu/ppu.hpp"

namespace mapperbus::core {

MemoryBus::MemoryBus() {
    ram_.fill(0);
    update_mappings();
}

Byte MemoryBus::read(Address addr) {
    Byte value = (this->*(read_map_[addr >> 10]))(addr);
    open_bus_ = value;
    return value;
}

void MemoryBus::write(Address addr, Byte value) {
    open_bus_ = value;
    (this->*(write_map_[addr >> 10]))(addr, value);
}

void MemoryBus::update_mappings() {
    for (int i = 0; i < 8; ++i) {
        read_map_[i] = &MemoryBus::read_ram;
        write_map_[i] = &MemoryBus::write_ram;
    }
    for (int i = 8; i < 16; ++i) {
        read_map_[i] = &MemoryBus::read_ppu;
        write_map_[i] = &MemoryBus::write_ppu;
    }
    read_map_[16] = &MemoryBus::read_apu_io;
    write_map_[16] = &MemoryBus::write_apu_io;
    for (int i = 17; i < 24; ++i) {
        read_map_[i] = &MemoryBus::read_expansion;
        write_map_[i] = &MemoryBus::write_expansion;
    }
    for (int i = 24; i < 64; ++i) {
        read_map_[i] = &MemoryBus::read_prg;
        write_map_[i] = &MemoryBus::write_prg;
    }
}

Byte MemoryBus::read_ram(Address addr) {
    return ram_[addr & 0x07FF];
}

Byte MemoryBus::read_ppu(Address addr) {
    return ppu_ ? ppu_->read_register(addr & 0x0007) : 0;
}

Byte MemoryBus::read_apu_io(Address addr) {
    if (addr < 0x4018) {
        if (addr == 0x4016 || addr == 0x4017) {
            if (controller_) {
                return static_cast<Byte>((open_bus_ & 0xE0) | controller_->read(addr - 0x4016));
            }
            return open_bus_;
        }
        return apu_ ? apu_->read_register(addr) : 0;
    }
    // $4040-$4092: FDS audio registers
    if (addr >= 0x4040 && addr <= 0x4092 && fds_ && fds_->is_loaded()) {
        return fds_->read(addr);
    }
    return open_bus_;
}

Byte MemoryBus::read_expansion(Address addr) {
    if (cartridge_) {
        if (!cartridge_->maps_expansion(addr)) {
            return open_bus_;
        }
        return cartridge_->read_expansion(addr);
    }
    return open_bus_;
}

Byte MemoryBus::read_prg(Address addr) {
    if (cartridge_) {
        if (!cartridge_->maps_prg(addr)) {
            return open_bus_;
        }
        return cartridge_->read_prg(addr);
    }
    return open_bus_;
}

Byte MemoryBus::read_open_bus(Address) {
    return open_bus_;
}

void MemoryBus::write_ram(Address addr, Byte value) {
    ram_[addr & 0x07FF] = value;
}

void MemoryBus::write_ppu(Address addr, Byte value) {
    if (ppu_) {
        ppu_->write_register(addr & 0x0007, value);
    }
}

void MemoryBus::write_apu_io(Address addr, Byte value) {
    if (addr < 0x4018) {
        if (addr == 0x4014) {
            if (ppu_) {
                Address base = static_cast<Address>(value << 8);
                for (uint16_t offset = 0; offset < 0x100; ++offset) {
                    ppu_->write_oam_dma(read(static_cast<Address>(base + offset)));
                }
                dma_cycles_ += 513;
            }
            return;
        }
        if (addr == 0x4016) {
            if (controller_) {
                controller_->write(value);
            }
            return;
        }
        if (apu_) {
            apu_->write_register(addr, value);
        }
        return;
    }
    // $4040-$4092: FDS audio registers
    if (addr >= 0x4040 && addr <= 0x4092 && fds_ && fds_->is_loaded()) {
        fds_->write(addr, value);
    }
}

void MemoryBus::write_expansion(Address addr, Byte value) {
    if (cartridge_) {
        cartridge_->write_expansion(addr, value);
    }
}

void MemoryBus::write_prg(Address addr, Byte value) {
    if (cartridge_) {
        cartridge_->write_prg(addr, value);
    }
}

void MemoryBus::write_open_bus(Address, Byte) {}

bool MemoryBus::poll_nmi() {
    return ppu_ ? ppu_->consume_nmi() : false;
}

bool MemoryBus::poll_irq() {
    bool irq = false;
    if (cartridge_) {
        irq |= cartridge_->irq_pending();
    }
    if (apu_) {
        irq |= apu_->irq_pending();
    }
    return irq;
}

uint32_t MemoryBus::take_dma_cycles() {
    uint32_t cycles = dma_cycles_;
    dma_cycles_ = 0;
    return cycles;
}

} // namespace mapperbus::core
