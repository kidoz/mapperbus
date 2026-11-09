#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
INesHeader rambo_header(uint16_t id = 64U) {
    INesHeader header{};
    header.mapper_number = id;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Horizontal;
    return header;
}
std::vector<Byte> rambo_rom(std::size_t size, std::size_t bank) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = static_cast<Byte>(offset / bank);
    return data;
}
std::unique_ptr<Mapper> rambo_board(uint16_t id = 64U,
                                    std::size_t prg = 0x40000U,
                                    std::size_t chr = 0x40000U) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(
        rambo_header(id), rambo_rom(prg, 0x2000U), rambo_rom(chr, 0x400U));
    REQUIRE(result);
    return std::move(*result);
}
void rambo_bank(Mapper& mapper, Byte reg, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x9FFEU, static_cast<Byte>(reg | mode));
    mapper.write_prg(0x9FFFU, value);
}

void rambo_ticks(Mapper& mapper, unsigned cycles) {
    for (unsigned i = 0U; i < cycles; ++i)
        mapper.clock_cpu_falling_edge();
}
void rambo_start(Mapper& mapper, Byte latch, Byte mode) {
    mapper.write_prg(0xE000U, 0U);
    mapper.write_prg(0xC000U, latch);
    mapper.write_prg(0xC001U, mode);
    mapper.clock_cpu_falling_edge(); // Complete the C001 write cycle (t = 0).
    mapper.write_prg(0xE001U, 0U);
}
void rambo_rise(Mapper& mapper, unsigned low = 16U) {
    mapper.observe_ppu_a12(false);
    rambo_ticks(mapper, low);
    mapper.observe_ppu_a12(true);
    mapper.clock_cpu_falling_edge();
}

Cartridge rambo_cartridge(uint16_t id, Byte mode = 1U) {
    std::vector<Byte> image(16U + 0x40000U + 0x40000U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 16U;
    image[5] = 32U;
    image[6] = static_cast<Byte>((id & 15U) << 4U);
    image[7] = static_cast<Byte>((id & 0xF0U) | 8U);
    auto prg = rambo_rom(0x40000U, 0x2000U);
    // Original program and IRQ handler, independent of commercial games.
    std::vector<Byte> code = {0x78U};
    const auto store = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
    };
    store(0xE000U, 0U);
    for (const auto [reg, value] :
         std::array<std::array<Byte, 2>, 4>{{{0U, 0x83U}, {6U, 5U}, {7U, 6U}, {15U, 7U}}}) {
        store(0x8000U, static_cast<Byte>(0x20U | reg));
        store(0x8001U, value);
    }
    store(0x2006U, 0U);
    store(0x2006U, 5U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, 0x10U});
    if (mode == 0U) {
        store(0x2000U, 8U);
        store(0x2001U, 0x18U);
    }
    store(0xC000U, mode != 0U ? 15U : 1U);
    store(0xC001U, mode);
    store(0xE001U, 0U);
    code.push_back(0x58U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(), {0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    std::ranges::copy(code, prg.begin() + 0x3E000U);
    const std::array<Byte, 13> handler = {
        0xE6U, 0x20U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0x40U, 0xEAU, 0xEAU};
    std::ranges::copy(handler, prg.begin() + 0x3E100U);
    prg[0x3FFFAU] = 0U;
    prg[0x3FFFBU] = 0xE1U;
    prg[0x3FFFCU] = 0U;
    prg[0x3FFFDU] = 0xE0U;
    prg[0x3FFFEU] = 0U;
    prg[0x3FFFFU] = 0xE1U;
    std::ranges::copy(prg, image.begin() + 16U);
    std::ranges::copy(rambo_rom(0x40000U, 0x400U), image.begin() + 16U + 0x40000U);
    register_builtin_mappers();
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}

TEST_CASE("RAMBO-1 CPU IRQ first expiry and period are linear for every latch byte",
          "[mappers][rambo1][timing]") {
    for (const uint16_t id : {64U, 158U}) {
        for (unsigned latch = 0U; latch < 256U; ++latch) {
            INFO(id << " latch " << latch);
            auto mapper = rambo_board(id, 0x8000U, 0x2000U);
            rambo_start(*mapper, static_cast<Byte>(latch), 1U);
            const auto period = 4U * (latch + 1U);
            rambo_ticks(*mapper, period + 1U);
            REQUIRE_FALSE(mapper->irq_pending());
            mapper->clock_cpu_falling_edge();
            REQUIRE(mapper->irq_pending());
            mapper->write_prg(0xE000U, 0U);
            mapper->write_prg(0xE001U, 0U);
            rambo_ticks(*mapper, period - 1U);
            REQUIRE_FALSE(mapper->irq_pending());
            mapper->clock_cpu_falling_edge();
            REQUIRE(mapper->irq_pending());
        }
    }
}

TEST_CASE("RAMBO-1 CPU timer latches on cycle five and samples enable at expiry",
          "[mappers][rambo1][timing]") {
    auto mapper = rambo_board();
    rambo_start(*mapper, 0U, 1U);
    rambo_ticks(*mapper, 4U);
    mapper->write_prg(0xDFFEU, 2U);
    mapper->clock_cpu_falling_edge(); // t = 5: pick the new latch.
    mapper->write_prg(0xC000U, 7U);   // Changes subsequent periods, not the loaded interval.
    rambo_ticks(*mapper, 8U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xFFFEU, 0U); // Expiry at t = 14 is lost while disabled.
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
    rambo_ticks(*mapper, 31U);
    mapper->write_prg(0xFFFFU, 0U); // Enable in the next expiry's cycle is in time.
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC001U, 0U); // Cancel the running CPU timer.
    mapper->clock_cpu_falling_edge();
    rambo_ticks(*mapper, 2048U);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("RAMBO-1 mode switches retain the filter and implement early CPU expiry",
          "[mappers][rambo1][timing]") {
    for (const bool full : {false, true}) {
        auto mapper = rambo_board();
        rambo_start(*mapper, 2U, 0U);
        if (full)
            rambo_ticks(*mapper, 16U);
        rambo_start(*mapper, 2U, 1U);
        const auto first = full ? 14U : 10U;
        rambo_ticks(*mapper, first - 1U);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu_falling_edge();
        REQUIRE(mapper->irq_pending());
    }
}

TEST_CASE("RAMBO-1 filters A12 on M2 falls and delays scanline IRQ by one fall",
          "[mappers][rambo1][timing]") {
    for (const unsigned low : {0U, 1U, 15U, 16U, 17U, 32U}) {
        auto mapper = rambo_board();
        mapper->observe_ppu_a12(true);
        rambo_start(*mapper, 0U, 0U); // Short filter: no swallowed edge.
        rambo_rise(*mapper, low);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xE001U, 0U); // Enable at the delivery fall is sufficient.
        mapper->clock_cpu_falling_edge();
        REQUIRE(mapper->irq_pending() == (low >= 16U));
    }
    auto mapper = rambo_board();
    rambo_start(*mapper, 0U, 0U);
    rambo_ticks(*mapper, 16U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(false); // Glitch between M2 samples.
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(true);
    mapper->clock_cpu_falling_edge();
    mapper->write_prg(0xE000U, 0U); // Disable in the delivery cycle prevents assertion.
    mapper->clock_cpu_falling_edge();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("RAMBO-1 swallows a full-filter first rise and never repeats latch-zero expiry",
          "[mappers][rambo1][timing]") {
    for (const Byte latch : {0U, 1U, 2U, 4U}) {
        auto mapper = rambo_board();
        rambo_ticks(*mapper, 16U);
        rambo_start(*mapper, latch, 0U);
        for (unsigned pulse = 0U; pulse < static_cast<unsigned>(latch) + 2U; ++pulse) {
            rambo_rise(*mapper);
            REQUIRE_FALSE(mapper->irq_pending());
            mapper->clock_cpu_falling_edge();
            REQUIRE(mapper->irq_pending() == (pulse == static_cast<unsigned>(latch) + 1U));
        }
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        for (unsigned pulse = 0U; pulse < static_cast<unsigned>(latch) + 1U; ++pulse) {
            rambo_rise(*mapper);
            mapper->clock_cpu_falling_edge();
        }
        REQUIRE(mapper->irq_pending() == (latch != 0U));
        if (latch == 0U) {
            for (unsigned pulse = 0U; pulse < 260U; ++pulse) {
                rambo_rise(*mapper);
                mapper->clock_cpu_falling_edge();
            }
            REQUIRE_FALSE(mapper->irq_pending());
        }
    }
}

TEST_CASE("RAMBO-1 cold timer runs while disabled and console reset preserves its phase",
          "[mappers][rambo1][timing]") {
    auto mapper = rambo_board();
    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    rambo_ticks(*mapper, 1017U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->reset();
    mapper->clock_cpu_falling_edge();
    REQUIRE(mapper->irq_pending());
    rambo_start(*mapper, 0U, 0U);
    for (unsigned i = 0U; i < 500U; ++i) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
    }
    REQUIRE_FALSE(mapper->irq_pending()); // Only the physical falling-edge hook counts.
}

TEST_CASE("RAMBO-1 CPU program services IRQs and replays snapshots with atomic rejection",
          "[mappers][rambo1][state][integration]") {
    for (const auto [id, mode] :
         std::array<std::array<uint16_t, 2>, 4>{{{64U, 0U}, {64U, 1U}, {158U, 0U}, {158U, 1U}}}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(rambo_cartridge(id, static_cast<Byte>(mode)), {}));
        emulator.reset();
        REQUIRE(emulator.cpu().pc() == 0xE000U);
        for (unsigned i = 0U; i < 200U; ++i)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(0x10U) == 0x83U);
        REQUIRE(emulator.read_cpu(0x20U) != 0U);
        REQUIRE(emulator.read_cpu(0x8000U) == 5U);
        REQUIRE(emulator.read_cpu(0xC000U) == 7U);
        const auto saved = emulator.save_state();
        for (unsigned i = 0U; i < 200U; ++i)
            emulator.step_instruction();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(saved));
        for (unsigned i = 0U; i < 200U; ++i)
            emulator.step_instruction();
        REQUIRE(emulator.save_state() == expected);
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        StateWriter board_state;
        rambo_cartridge(id).save_state(board_state);
        const auto start = expected.size() - tail.data().size() - board_state.data().size();
        for (const std::size_t field : {0U, 11U, 12U, 15U, 19U, 20U, 21U, 23U, 25U}) {
            auto corrupt = expected;
            corrupt[start + field] = 0xFFU;
            REQUIRE_FALSE(emulator.load_state(corrupt));
            REQUIRE(emulator.save_state() == expected);
        }
        REQUIRE_FALSE(emulator.load_state(std::span(saved).first(saved.size() / 2U)));
        REQUIRE(emulator.save_state() == expected);
    }
}

TEST_CASE("RAMBO-1 receives PPUDATA, sprite-bank A12 and idle VRAM pins independently of pixels",
          "[mappers][rambo1][ppu]") {
    const auto pin = [](const Cartridge& cart) {
        StateWriter state;
        cart.save_state(state);
        return state.data()[20U];
    };
    auto cart = rambo_cartridge(64U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    const auto address = [&ppu](Address addr) {
        ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
        ppu.write_register(6U, static_cast<Byte>(addr));
    };
    address(0x0FFFU);
    REQUIRE(pin(cart) == 0U);
    ppu.write_register(7U, 0U);
    REQUIRE(pin(cart) == 1U); // Increment crosses A12.
    address(0x1FFFU);
    (void)ppu.read_register(7U);
    REQUIRE(pin(cart) == 0U);
    ppu.write_register(0U, 0x20U);
    ppu.write_register(3U, 0U);
    for (const Byte value : {0U, 1U, 0U, 0U})
        ppu.write_register(4U, value);
    ppu.write_register(1U, 0x18U);
    ppu.step_dots(260U);
    REQUIRE(pin(cart) == 1U); // 8x16 slot zero uses the tile's low bit.
    ppu.step_dots(8U);
    REQUIRE(pin(cart) == 0U); // Next occupied slot uses the other table.
    ppu.write_register(0U, 0x10U);
    ppu.step_dots(241U * 341U - 268U + 10U);
    REQUIRE((ppu.loopy_v() & 0x1000U) == 0U);
    REQUIRE(pin(cart) == 0U); // In vblank, v drives the bus instead of the pattern table.
    ppu.write_register(0U, 0U);
    REQUIRE(pin(cart) == 0U); // A control write does not change v's A12.
    address(0x1000U);
    ppu.step_dots(10U);
    REQUIRE(pin(cart) == 1U); // Both polarities follow v while the fetch circuit idles.
    address(0x2000U);
    REQUIRE(pin(cart) == 0U); // CPU address writes override the idle bus in vblank.
    ppu.step_dots(10U);
    REQUIRE(pin(cart) == 0U);
}

TEST_CASE("RAMBO-1 observes low A12 during the final dummy nametable fetches",
          "[mappers][rambo1][ppu][timing]") {
    for (const uint16_t id : {64U, 158U}) {
        auto cart = rambo_cartridge(id);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        ppu.write_register(0U, 0x18U);
        ppu.write_register(1U, 0x1EU);
        const auto pin = [&cart] {
            StateWriter state;
            cart.save_state(state);
            return state.data()[20U];
        };
        ppu.step_dots(335U);
        REQUIRE(pin() == 1U);
        ppu.step_dots(1U);
        REQUIRE(pin() == 0U);
        ppu.step_dots(8U); // Through the dummy fetches, idle dot and next line's NT/AT.
        REQUIRE(pin() == 0U);
        ppu.step_dots(1U);
        REQUIRE(pin() == 1U);
    }
}

TEST_CASE("RAMBO-1 production IRQ follows table selection and manual PPUADDR pulses",
          "[mappers][rambo1][ppu][bus]") {
    for (const Byte tables : {0U, 8U, 0x10U}) {
        auto cart = rambo_cartridge(64U);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        MemoryBus bus;
        bus.connect_ppu(&ppu);
        bus.connect_cartridge(&cart);
        ppu.write_register(0U, tables);
        ppu.write_register(1U, 0x18U);
        cart.write_prg(0xE000U, 0U);
        cart.write_prg(0xC000U, 1U);
        cart.write_prg(0xC001U, 0U);
        bus.clock_cpu_bus(0U);
        cart.write_prg(0xE001U, 0U);
        bus.clock_cpu_bus(0U, 600U);
        REQUIRE(cart.irq_pending() == (tables != 0U));
    }
    auto cart = rambo_cartridge(158U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    MemoryBus bus;
    bus.connect_ppu(&ppu);
    bus.connect_cartridge(&cart);
    bus.write(0xE000U, 0U);
    bus.write(0xC000U, 0U);
    bus.write(0xC001U, 0U);
    bus.clock_cpu_bus(0U);
    bus.write(0xE001U, 0U);
    bus.clock_cpu_bus(0U, 16U);
    bus.write(0x2006U, 0x10U);
    bus.write(0x2006U, 0U);
    bus.clock_cpu_bus(0U);
    REQUIRE_FALSE(cart.irq_pending());
    bus.clock_cpu_bus(0U);
    REQUIRE(cart.irq_pending());
    bus.write(0xE000U, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        const std::array<Byte, 4> regs = {0U, 8U, 1U, 9U};
        cart.write_prg(0x8000U, static_cast<Byte>(0x20U | regs[slot]));
        cart.write_prg(0x8001U, slot == 3U ? 0x80U : 0U);
    }
    const auto address = [&ppu](Address addr) {
        ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
        ppu.write_register(6U, static_cast<Byte>(addr));
    };
    address(0x2005U);
    ppu.write_register(7U, 0x11U);
    address(0x2C05U);
    ppu.write_register(7U, 0x22U);
    address(0x3C05U);
    (void)ppu.read_register(7U);
    REQUIRE(ppu.read_register(7U) == 0x22U);
    REQUIRE(ppu.vram()[5U] == 0x11U);
    REQUIRE(ppu.vram()[0x405U] == 0x22U);
}

TEST_CASE("RAMBO-1 clocks M2 throughout OAM DMA", "[mappers][rambo1][bus][timing]") {
    auto cart = rambo_cartridge(64U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    MemoryBus bus;
    bus.connect_ppu(&ppu);
    bus.connect_cartridge(&cart);
    bus.write(0xE000U, 0U);
    bus.write(0xC000U, 120U);
    bus.write(0xC001U, 1U);
    bus.clock_cpu_bus(0U);
    bus.write(0xE001U, 0U);
    REQUIRE_FALSE(bus.poll_irq());
    REQUIRE(bus.run_oam_dma(0U, 0xE000U, false) >= 513U);
    REQUIRE(bus.poll_irq());
}

TEST_CASE("RAMBO-1 latch zero expires exactly once with both pattern tables at 1000",
          "[mappers][rambo1][ppu][timing]") {
    for (const uint16_t id : {64U, 158U}) {
        auto cart = rambo_cartridge(id);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        MemoryBus bus;
        bus.connect_ppu(&ppu);
        bus.connect_cartridge(&cart);
        ppu.write_register(0U, 0x18U);
        ppu.write_register(1U, 0x1EU);
        // Reach vblank with v's A12 low, like the hardware G6 probe. Arm
        // after the previous frame so the initial filter state is meaningful.
        bus.clock_cpu_bus(0U, (241U * 341U + 2U) / 3U);
        REQUIRE((ppu.loopy_v() & 0x1000U) == 0U);
        cart.write_prg(0xE000U, 0U);
        cart.write_prg(0xC000U, 0U);
        cart.write_prg(0xC001U, 0U);
        bus.clock_cpu_bus(0U);
        cart.write_prg(0xE001U, 0U);
        unsigned interrupts = 0U;
        for (unsigned cycle = 0U; cycle < 8U * 29'781U; ++cycle) {
            bus.clock_cpu_bus(0U);
            if (cart.irq_pending()) {
                ++interrupts;
                cart.write_prg(0xE000U, 0U);
                cart.write_prg(0xE001U, 0U);
            }
        }
        REQUIRE(interrupts == 1U);
    }
}

TEST_CASE("RAMBO-1 snapshots retain partial CPU intervals, A12 filter and pending delivery",
          "[mappers][rambo1][state]") {
    for (const Byte mode : {0U, 1U}) {
        auto mapper = rambo_board();
        rambo_start(*mapper, 0U, mode);
        if (mode == 0U)
            rambo_rise(*mapper);
        else
            rambo_ticks(*mapper, 5U);
        StateWriter saved;
        mapper->save_state(saved);
        mapper->clock_cpu_falling_edge();
        REQUIRE(mapper->irq_pending());
        StateReader reader(saved.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu_falling_edge();
        REQUIRE(mapper->irq_pending());
        for (const std::size_t offset :
             {0U, 1U, 2U, 11U, 12U, 15U, 16U, 17U, 18U, 19U, 20U, 21U, 22U, 23U, 25U}) {
            auto corrupt = saved.data();
            corrupt[offset] = 0xFFU;
            StateReader invalid(corrupt);
            mapper->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(mapper->read_prg(0xE000U) == 31U);
        }
        StateReader truncated(std::span(saved.data()).first(saved.data().size() - 1U));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
    }
}

TEST_CASE("RAMBO-1 factories reject unsupported memory and submapper declarations",
          "[mappers][rambo1][cartridge]") {
    register_builtin_mappers();
    const auto valid = [](INesHeader h, std::size_t prg = 0x40000U, std::size_t chr = 0x40000U) {
        return MapperRegistry::instance()
            .create(h, std::vector<Byte>(prg), std::vector<Byte>(chr))
            .has_value();
    };
    for (const uint16_t id : {64U, 158U}) {
        auto h = rambo_header(id);
        REQUIRE(valid(h));
        REQUIRE_FALSE(valid(h, 0x4000U));
        REQUIRE_FALSE(valid(h, 0x42000U));
        REQUIRE_FALSE(valid(h, 0x8001U));
        REQUIRE_FALSE(valid(h, 0x8000U, 0U));
        REQUIRE_FALSE(valid(h, 0x8000U, 0x401U));
        REQUIRE_FALSE(valid(h, 0x8000U, 0x40400U));
        h.submapper = 1U;
        REQUIRE_FALSE(valid(h));
        h.submapper = 0U;
        h.has_battery = true;
        REQUIRE_FALSE(valid(h));
        h.has_battery = false;
        h.prg_ram_size = 0x2000U;
        REQUIRE_FALSE(valid(h));
        h.prg_ram_size = 0U;
        h.prg_nvram_size = 0x2000U;
        REQUIRE_FALSE(valid(h));
        h.prg_nvram_size = 0U;
        h.chr_ram_size = 0x2000U;
        REQUIRE_FALSE(valid(h));
        h.chr_ram_size = 0U;
        h.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(valid(h));
        h.chr_nvram_size = 0U;
        h.mirror_mode = MirrorMode::FourScreen;
        REQUIRE_FALSE(valid(h));
    }
}

TEST_CASE("RAMBO-1 decodes five-bit PRG banks, RF, aliases and immediate mode changes",
          "[mappers][rambo1]") {
    for (const uint16_t id : {64U, 158U}) {
        auto mapper = rambo_board(id);
        for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
            REQUIRE(mapper->read_prg(addr) == 31U);
        rambo_bank(*mapper, 6U, 0xE3U);
        rambo_bank(*mapper, 7U, 0xC5U);
        rambo_bank(*mapper, 15U, 0x89U);
        REQUIRE(mapper->read_prg(0x8000U) == 3U);
        REQUIRE(mapper->read_prg(0xA000U) == 5U);
        REQUIRE(mapper->read_prg(0xC000U) == 9U);
        mapper->write_prg(0x8000U, 0x50U); // P takes effect before the next data write.
        REQUIRE(mapper->read_prg(0x8000U) == 9U);
        REQUIRE(mapper->read_prg(0xC000U) == 3U);
        for (Byte reg = 10U; reg < 15U; ++reg)
            rambo_bank(*mapper, reg, 0xFFU, 0x40U);
        REQUIRE(mapper->read_prg(0xE000U) == 31U);
        REQUIRE(mapper->read_prg(0xC000U) == 3U);
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->battery_ram().empty());
        mapper->write_prg(0xBFFFU, 0x80U); // No MMC3 RAM gate is wired.
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        REQUIRE_FALSE(mapper->has_expansion_audio());
    }
    auto odd = rambo_board(64U, 0xA000U, 0x1400U);
    REQUIRE(odd->read_prg(0xE000U) == 1U); // Fixed $1F, not the last bank of an odd-sized ROM.
    rambo_bank(*odd, 6U, 0xE3U);
    REQUIRE(odd->read_prg(0x8000U) == 3U); // Mask before modulo.
}

TEST_CASE("RAMBO-1 keeps R8 and R9 with K clear and supports all four CHR layouts",
          "[mappers][rambo1]") {
    const std::array<Byte, 8> regs = {0U, 1U, 2U, 3U, 4U, 5U, 8U, 9U};
    const std::array<Byte, 8> banks = {0x83U, 0x95U, 0xA7U, 0xB9U, 0xCBU, 0xDDU, 0xEFU, 0xF1U};
    const std::array<Byte, 8> paired = {0x82U, 0x83U, 0x94U, 0x95U, 0xA7U, 0xB9U, 0xCBU, 0xDDU};
    const std::array<Byte, 8> singles = {0x83U, 0xEFU, 0x95U, 0xF1U, 0xA7U, 0xB9U, 0xCBU, 0xDDU};
    for (const uint16_t id : {64U, 158U}) {
        auto mapper = rambo_board(id);
        for (unsigned slot = 0U; slot < 8U; ++slot)
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x400U)) == 0U);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            rambo_bank(*mapper, regs[slot], banks[slot]);
        }
        for (const Byte mode : {0U, 0x20U, 0x80U, 0xA0U}) {
            mapper->write_prg(0x8000U, mode);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto expected = (mode & 0x20U) != 0U ? singles : paired;
                const auto addr = static_cast<Address>(slot * 0x400U);
                REQUIRE(mapper->read_chr(addr) ==
                        expected[slot ^ ((mode & 0x80U) != 0U ? 4U : 0U)]);
                mapper->write_chr(addr, 0U);
                REQUIRE(mapper->read_chr(addr) != 0U);
            }
        }
    }
}

TEST_CASE("Mapper 158 routes all CIRAM layouts through R8 and R9 before ROM wrapping",
          "[mappers][rambo1]") {
    auto mapper = rambo_board(158U, 0x40000U, 0x10000U);
    const std::array<Byte, 4> regs = {0U, 8U, 1U, 9U};
    for (unsigned layout = 0U; layout < 16U; ++layout) {
        for (unsigned slot = 0U; slot < 4U; ++slot)
            rambo_bank(*mapper, regs[slot], (layout & (1U << slot)) != 0U ? 0x83U : 3U, 0x20U);
        mapper->write_prg(0xA000U, 1U);
        for (unsigned slot = 0U; slot < 4U; ++slot)
            REQUIRE(mapper->nametable_address(static_cast<Address>(0x2005U + slot * 0x400U)) ==
                    ((layout & (1U << slot)) != 0U ? 0x405U : 5U));
        REQUIRE(mapper->read_chr(0U) == 3U);
    }
    rambo_bank(*mapper, 2U, 0x80U, 0xA0U);
    rambo_bank(*mapper, 3U, 0U, 0xA0U);
    REQUIRE(mapper->nametable_address(0x2005U) == 0x405U);
    REQUIRE(mapper->nametable_address(0x2405U) == 5U);
    auto normal = rambo_board();
    REQUIRE(normal->mirror_mode() == MirrorMode::Vertical);
    normal->write_prg(0xA000U, 0xFFU);
    REQUIRE(normal->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(normal->nametable_address(0x2000U));
}

TEST_CASE("RAMBO-1 retains mapper registers and pending IRQ on the console reset line",
          "[mappers][rambo1][state]") {
    auto mapper = rambo_board();
    REQUIRE(mapper->irq_pending());
    rambo_bank(*mapper, 15U, 7U, 0xE0U);
    StateWriter saved;
    mapper->save_state(saved);
    mapper->reset();
    StateWriter reset;
    mapper->save_state(reset);
    REQUIRE(reset.data() == saved.data());
    mapper->write_prg(0xE001U, 0U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC001U, 1U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
}

} // namespace
