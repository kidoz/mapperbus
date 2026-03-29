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
    }
    void connect_ppu(Ppu* ppu) {
        ppu_ = ppu;
    }
    void connect_apu(Apu* apu) {
        apu_ = apu;
    }
    void connect_controller(Controller* controller) {
        controller_ = controller;
    }
    void connect_fds(Fds* fds) {
        fds_ = fds;
    }

  private:
    std::array<Byte, kRamSize> ram_{};
    uint32_t dma_cycles_ = 0;

    Cartridge* cartridge_ = nullptr;
    Ppu* ppu_ = nullptr;
    Apu* apu_ = nullptr;
    Controller* controller_ = nullptr;
    Fds* fds_ = nullptr;
};

} // namespace mapperbus::core
