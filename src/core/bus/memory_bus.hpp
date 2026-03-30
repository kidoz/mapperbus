#pragma once

#include <array>
#include <cstdint>

#include "core/types.hpp"

namespace mapperbus::core {

class Cartridge;
class Fds;
class Ppu;
class Apu;
class Controller;

class MemoryBus {
  public:
    MemoryBus();

    Byte read(Address addr);
    void write(Address addr, Byte value);
    bool poll_nmi();
    bool poll_irq();
    uint32_t take_dma_cycles();

    void connect_cartridge(Cartridge* cartridge) {
        cartridge_ = cartridge;
        update_mappings();
    }
    void connect_ppu(Ppu* ppu) {
        ppu_ = ppu;
        update_mappings();
    }
    void connect_apu(Apu* apu) {
        apu_ = apu;
        update_mappings();
    }
    void connect_controller(Controller* controller) {
        controller_ = controller;
        update_mappings();
    }
    void connect_fds(Fds* fds) {
        fds_ = fds;
        update_mappings();
    }

  private:
    void update_mappings();

    using ReadFunc = Byte (MemoryBus::*)(Address);
    using WriteFunc = void (MemoryBus::*)(Address, Byte);
    std::array<ReadFunc, 64> read_map_{};
    std::array<WriteFunc, 64> write_map_{};

    Byte read_ram(Address addr);
    Byte read_ppu(Address addr);
    Byte read_apu_io(Address addr);
    Byte read_expansion(Address addr);
    Byte read_prg(Address addr);
    Byte read_open_bus(Address addr);

    void write_ram(Address addr, Byte value);
    void write_ppu(Address addr, Byte value);
    void write_apu_io(Address addr, Byte value);
    void write_expansion(Address addr, Byte value);
    void write_prg(Address addr, Byte value);
    void write_open_bus(Address addr, Byte value);

    std::array<Byte, kRamSize> ram_{};
    uint32_t dma_cycles_ = 0;

    Cartridge* cartridge_ = nullptr;
    Ppu* ppu_ = nullptr;
    Apu* apu_ = nullptr;
    Controller* controller_ = nullptr;
    Fds* fds_ = nullptr;
};

} // namespace mapperbus::core
