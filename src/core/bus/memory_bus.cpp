#include "core/bus/memory_bus.hpp"

#include "core/apu/apu.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/fds/fds.hpp"
#include "core/input/controller.hpp"
#include "core/ppu/ppu.hpp"

namespace mapperbus::core {

MemoryBus::MemoryBus() {
    ram_.fill(0);
}

Byte MemoryBus::read(Address addr) {
    if (addr < 0x2000) {
        // Internal RAM with mirroring
        return ram_[addr & 0x07FF];
    }
    if (addr < 0x4000) {
        // PPU registers, mirrored every 8 bytes
        if (ppu_) {
            return ppu_->read_register(addr & 0x0007);
        }
        return 0;
    }
    if (addr < 0x4018) {
        // APU and I/O registers
        if (addr == 0x4016 || addr == 0x4017) {
            if (controller_) {
                return controller_->read(addr - 0x4016);
            }
            return 0;
        }
        if (apu_) {
            return apu_->read_register(addr);
        }
        return 0;
    }
    // $4040-$4092: FDS audio registers
    if (addr >= 0x4040 && addr <= 0x4092 && fds_ && fds_->is_loaded()) {
        return fds_->read(addr);
    }
    // $4018-$5FFF: Expansion area (cartridge expansion audio + PRG RAM)
    if (addr < 0x6000) {
        if (cartridge_) {
            return cartridge_->read_expansion(addr);
        }
        return 0;
    }
    // $6000-$FFFF: Cartridge PRG space
    if (cartridge_) {
        return cartridge_->read_prg(addr);
    }
    return 0;
}

void MemoryBus::write(Address addr, Byte value) {
    if (addr < 0x2000) {
        ram_[addr & 0x07FF] = value;
        return;
    }
    if (addr < 0x4000) {
        if (ppu_) {
            ppu_->write_register(addr & 0x0007, value);
        }
        return;
    }
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
        return;
    }
    // $4018-$5FFF: Expansion area
    if (addr < 0x6000) {
        if (cartridge_) {
            cartridge_->write_expansion(addr, value);
        }
        return;
    }
    // $6000-$FFFF: Cartridge PRG space
    if (cartridge_) {
        cartridge_->write_prg(addr, value);
    }
}

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
