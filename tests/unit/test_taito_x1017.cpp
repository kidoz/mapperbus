#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <vector>

#include "core/apu/apu.hpp"
#include "core/binary_file.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
INesHeader x1017_header(uint16_t id, bool battery = false) {
    INesHeader header{};
    header.mapper_number = id;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Vertical;
    header.has_battery = battery;
    // The NES 2.0 power-of-two field rounds the ASIC's 5 KiB up to 8 KiB.
    header.prg_nvram_size = battery ? 0x2000U : 0U;
    return header;
}

std::vector<Byte> x1017_rom(std::size_t size, std::size_t bank) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank);
    return data;
}

std::unique_ptr<Mapper> x1017_board(uint16_t id, bool battery = false) {
    register_builtin_mappers();
    auto result =
        MapperRegistry::instance().create(x1017_header(id, battery),
                                          x1017_rom(id == 82U ? 0x20000U : 0x80000U, 0x2000U),
                                          x1017_rom(0x40000U, 0x400U));
    REQUIRE(result);
    return std::move(*result);
}

Cartridge x1017_cartridge(uint16_t id, bool program = false, bool battery = false) {
    register_builtin_mappers();
    const std::size_t prg = id == 82U ? 0x20000U : 0x80000U;
    std::vector<Byte> image(16U + prg + 0x40000U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = static_cast<Byte>(prg / 0x4000U);
    image[5U] = 32U;
    image[6U] = static_cast<Byte>(((id & 15U) << 4U) | 1U | (battery ? 2U : 0U));
    image[7U] = static_cast<Byte>((id & 0xF0U) | 8U);
    image[8U] = static_cast<Byte>(id >> 8U);
    image[10U] = battery ? 0x70U : 7U;
    const auto banks = x1017_rom(prg, 0x2000U);
    std::copy(banks.begin(), banks.end(), image.begin() + 16U);
    if (program) {
        // Original program in the fixed bank; bank writes cannot unmap it.
        // clang-format off
        const std::array<Byte, 44U> code = {
            0xA9U,0xCAU,0x8DU,0xF7U,0x7EU, // Unlock $6000-$67FF.
            0xA9U,0x5AU,0x8DU,0x05U,0x60U,
            0xA9U,0x24U,0x8DU,0xFAU,0x7EU, // Bank 9 in either dump order.
            0xA9U,0x03U,0x8DU,0xF6U,0x7EU, // CHR swap + vertical mirroring.
            0xA9U,0x02U,0x8DU,0xFDU,0x7EU,
            0xA9U,0x00U,0x8DU,0xFFU,0x7EU, // Reload to 48 M2 cycles.
            0xA9U,0x03U,0x8DU,0xFEU,0x7EU, // Start timer.
            0xA9U,0x00U,0x8DU,0xF7U,0x7EU, // Lock RAM again.
            0x58U,                        // CLI
            0x4CU,0x29U,0xE0U};
        const std::array<Byte, 11U> handler = {
            0xE6U,0x02U,                  // INC $02: observable IRQ service.
            0xA9U,0x00U,
            0x8DU,0xFEU,0x7EU,            // Stop counting and mask IRQ.
            0x8DU,0xFFU,0x7EU,            // Acknowledge the pending latch.
            0x40U};                       // RTI
        // clang-format on
        std::copy(code.begin(), code.end(), image.begin() + 16U + prg - 0x2000U);
        std::copy(handler.begin(), handler.end(), image.begin() + 16U + prg - 0x1F00U);
    } else {
        image[16U + prg - 0x2000U] = 0x4CU;
        image[17U + prg - 0x2000U] = 0U;
        image[18U + prg - 0x2000U] = 0xE0U;
    }
    for (std::size_t offset = prg - 6U; offset < prg; offset += 2U) {
        image[16U + offset] = 0U;
        image[17U + offset] = program && offset == prg - 2U ? 0xE1U : 0xE0U;
    }
    const auto chr = x1017_rom(0x40000U, 0x400U);
    std::copy(chr.begin(), chr.end(), image.begin() + 16U + prg);
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}

void x1017_ppu_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
Byte x1017_ppu_read(Ppu& ppu, Address addr) {
    x1017_ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}
} // namespace

TEST_CASE("X1-017 distinguishes legacy and physical PRG wiring with fully decoded registers",
          "[mappers][x1017]") {
    const std::array<Byte, 6U> physical = {32U, 16U, 8U, 4U, 2U, 1U};
    for (const uint16_t id : {82U, 552U}) {
        auto mapper = x1017_board(id);
        for (unsigned slot = 0U; slot < 3U; ++slot) {
            for (unsigned bit = 0U; bit < 8U; ++bit) {
                mapper->write_prg(static_cast<Address>(0x7EFAU + slot),
                                  static_cast<Byte>(1U << bit));
                const unsigned expected = id == 82U
                                              ? (bit >= 2U && bit < 6U ? 1U << (bit - 2U) : 0U)
                                              : (bit < 6U ? physical[bit] : 0U);
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * 0x2000U)) ==
                        expected);
            }
        }
        mapper->write_prg(0x7EFAU, 0x24U);
        for (const Address ignored : {0x7DFAU, 0x7E7AU, 0x7EEAU, 0x7FFAU, 0x8000U, 0xFFFFU})
            mapper->write_prg(ignored, 0U);
        REQUIRE(mapper->read_prg(0x8000U) == 9U);
        REQUIRE(mapper->read_prg(0xFFFFU) == (id == 82U ? 15U : 63U));
    }
}

TEST_CASE("X1-017 swaps CHR halves, aligns two-KiB windows and routes CIRAM A10",
          "[mappers][x1017][ppu]") {
    for (const uint16_t id : {82U, 552U}) {
        auto cart = x1017_cartridge(id);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        const std::array<Byte, 6U> banks = {0x83U, 0xFFU, 0xF8U, 0xF9U, 0xFAU, 0xFBU};
        const std::array<Byte, 8U> expected = {130U, 131U, 254U, 255U, 248U, 249U, 250U, 251U};
        for (unsigned i = 0U; i < banks.size(); ++i)
            cart.write_prg(static_cast<Address>(0x7EF0U + i), banks[i]);
        for (const Byte mode : {0U, 2U}) {
            cart.write_prg(0x7EF6U, mode);
            for (unsigned page = 0U; page < 8U; ++page) {
                const auto addr =
                    static_cast<Address>((page * 0x400U) ^ (mode != 0U ? 0x1000U : 0U));
                REQUIRE(x1017_ppu_read(ppu, addr) == expected[page]);
                cart.write_chr(addr, 0U);
                REQUIRE(cart.read_chr(addr) == expected[page]);
            }
        }
        cart.write_prg(0x7EF6U, 1U);
        x1017_ppu_address(ppu, 0x2000U);
        ppu.write_register(7U, 0x11U);
        x1017_ppu_address(ppu, 0x2400U);
        ppu.write_register(7U, 0x22U);
        cart.write_prg(0x7EF6U, 0U);
        REQUIRE(cart.mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(x1017_ppu_read(ppu, 0x2400U) == 0x11U);
        REQUIRE(x1017_ppu_read(ppu, 0x2800U) == 0x22U);
        cart.write_prg(0x7EF6U, 0xFFU);
        REQUIRE(cart.mirror_mode() == MirrorMode::Vertical);
        REQUIRE(x1017_ppu_read(ppu, 0x2400U) == 0x22U);
    }
}

TEST_CASE("X1-017 independently protects each physical RAM region with an exact byte key",
          "[mappers][x1017][bus]") {
    constexpr std::array<Address, 3U> starts = {0x6000U, 0x6800U, 0x7000U};
    constexpr std::array<Address, 3U> ends = {0x67FFU, 0x6FFFU, 0x73FFU};
    constexpr std::array<Byte, 3U> keys = {0xCAU, 0x69U, 0x84U};
    for (const uint16_t id : {82U, 552U}) {
        auto cart = x1017_cartridge(id);
        MemoryBus bus;
        bus.connect_cartridge(&cart);
        for (unsigned region = 0U; region < 3U; ++region) {
            for (unsigned key = 0U; key < 256U; ++key) {
                bus.write(static_cast<Address>(0x7EF7U + region), static_cast<Byte>(key));
                REQUIRE(cart.maps_prg(starts[region]) == (key == keys[region]));
                REQUIRE(cart.maps_prg(ends[region]) == (key == keys[region]));
            }
            bus.write(static_cast<Address>(0x7EF7U + region), keys[region]);
            bus.write(starts[region], static_cast<Byte>(0xA0U + region));
            bus.write(ends[region], static_cast<Byte>(0xB0U + region));
        }
        for (unsigned region = 0U; region < 3U; ++region) {
            bus.write(static_cast<Address>(0x7EF7U + region), 0U);
            for (unsigned other = 0U; other < 3U; ++other) {
                if (other != region) {
                    REQUIRE(cart.maps_prg(starts[other]));
                    REQUIRE(bus.read(starts[other]) == 0xA0U + other);
                }
            }
            bus.write(starts[region], 0U);
            bus.write(0U, 0xFFU);
            REQUIRE(bus.read(starts[region]) == 0U);
            bus.write(static_cast<Address>(0x7E77U + region), keys[region]); // No A7 aliases.
            REQUIRE(bus.read(starts[region]) == 0U);
            bus.write(static_cast<Address>(0x7EF7U + region), keys[region]);
            REQUIRE(bus.read(starts[region]) == 0xA0U + region);
            REQUIRE(bus.read(ends[region]) == 0xB0U + region);
        }
        for (const Address hole : {0x7400U, 0x7E00U, 0x7EF0U, 0x7EFFU, 0x7F00U, 0x7FFFU}) {
            REQUIRE_FALSE(cart.maps_prg(hole));
            bus.write(0U, 0xFFU);
            REQUIRE(bus.read(hole) == 0U);
        }
    }
}

TEST_CASE("X1-017 pull-downs clear floating CPU bits while preserving driven data",
          "[mappers][x1017][bus]") {
    for (const uint16_t id : {82U, 552U}) {
        auto cart = x1017_cartridge(id);
        MemoryBus bus;
        Apu apu;
        Controller controller;
        bus.connect_cartridge(&cart);
        bus.connect_apu(&apu);
        bus.connect_controller(&controller);
        controller.set_button_state(0, Button::A, true);
        bus.write(0x4016U, 1U);
        for (const bool timed : {false, true}) {
            auto read = [&](Address addr) { return timed ? bus.read_cpu(addr) : bus.read(addr); };
            for (const Address floating :
                 {0x4000U, 0x4014U, 0x4018U, 0x4020U, 0x5000U, 0x6000U, 0x7EFFU}) {
                bus.write(0U, 0xFFU);
                REQUIRE(read(floating) == 0U);
            }
            bus.write(0U, 0xFFU);
            REQUIRE(read(0x4015U) == 0U); // Including undriven bit 5.
            bus.write(0U, 0xFFU);
            REQUIRE(read(0x4016U) == 1U); // Driven controller D0 survives.
            bus.write(0U, 0xA5U);
            REQUIRE(read(0U) == 0xA5U); // CPU RAM still drives all eight bits.
        }
        bus.connect_cartridge(nullptr);
        bus.write(0U, 0xA5U);
        REQUIRE(bus.read(0x5000U) == 0xA5U);
    }
}

TEST_CASE("X1-017 IRQ reload paths, output gating and unsupported clock mode follow M2",
          "[mappers][x1017][irq]") {
    for (const uint16_t id : {82U, 552U}) {
        for (const unsigned latch : {0U, 1U, 255U}) {
            for (const bool acknowledge : {false, true}) {
                auto mapper = x1017_board(id);
                mapper->write_prg(0x7EFDU, static_cast<Byte>(latch));
                mapper->write_prg(0x7EFEU, 0U);
                if (acknowledge)
                    mapper->write_prg(0x7EFFU, 0xFFU);
                const unsigned delay = latch == 0U ? (acknowledge ? 1U : 17U)
                                                   : (latch + (acknowledge ? 1U : 2U)) * 16U;
                mapper->write_prg(0x7EFEU, 7U); // Bit 2 prevents counting.
                for (unsigned i = 0U; i < delay + 10U; ++i)
                    mapper->clock_cpu();
                REQUIRE_FALSE(mapper->irq_pending());
                mapper->write_prg(0x7EFEU, 1U); // Count with output disabled.
                for (unsigned i = 1U; i < delay; ++i)
                    mapper->clock_cpu();
                mapper->write_prg(0x7EFEU, 3U);
                REQUIRE_FALSE(mapper->irq_pending());
                mapper->clock_cpu();
                REQUIRE(mapper->irq_pending());
                mapper->write_prg(0x7EFEU, 0U); // Reload does not acknowledge.
                REQUIRE_FALSE(mapper->irq_pending());
                mapper->write_prg(0x7EFEU, 2U);
                REQUIRE(mapper->irq_pending());
                mapper->write_prg(0x7EFFU, 0U);
                REQUIRE_FALSE(mapper->irq_pending());
            }
        }
    }
}

TEST_CASE("X1-017 IRQ is clocked during DMA and observes the bus interrupt sample boundary",
          "[mappers][x1017][irq][bus]") {
    auto cart = x1017_cartridge(552U);
    MemoryBus bus;
    Ppu ppu;
    bus.connect_cartridge(&cart);
    bus.connect_ppu(&ppu);
    cart.write_prg(0x7EFDU, 0U);
    cart.write_prg(0x7EFFU, 0U);
    cart.write_prg(0x7EFEU, 3U);
    REQUIRE_FALSE(bus.poll_irq());
    bus.clock_cpu_bus(0U);
    REQUIRE(bus.poll_irq());
    REQUIRE((bus.interrupt_sample() & 1U) == 0U);
    bus.clock_cpu_bus(0U);
    REQUIRE((bus.interrupt_sample() & 1U) == 1U);
    cart.write_prg(0x7EFFU, 0U);
    REQUIRE(bus.run_oam_dma(0U, 0xE000U, false) >= 513U);
    REQUIRE(bus.poll_irq());
    cart.write_prg(0x7EFFU, 0U);
    cart.write_prg(0x7EFEU, 0U);
    bus.clock_cpu_bus(0U);
    REQUIRE_FALSE(bus.poll_irq());
}

TEST_CASE("X1-017 latch writes retain the active countdown and mode changes pause it",
          "[mappers][x1017][irq]") {
    for (const uint16_t id : {82U, 552U}) {
        auto mapper = x1017_board(id);
        mapper->write_prg(0x7EFDU, 1U);
        mapper->write_prg(0x7EFEU, 0U);    // 48 cycles.
        mapper->write_prg(0x7EFEU, 0xFBU); // Unused control bits are ignored.
        for (unsigned i = 0U; i < 5U; ++i)
            mapper->clock_cpu();
        mapper->write_prg(0x7EFDU, 0U); // Affects the next reload only.
        mapper->write_prg(0x7EFEU, 7U);
        for (unsigned i = 0U; i < 100U; ++i)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0x7EFEU, 3U);
        for (unsigned i = 0U; i < 42U; ++i)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0x7EFFU, 0U); // New latch reloads to one cycle.
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
    }
}

TEST_CASE("X1-017 CPU register writes and active timer replay whole-machine snapshots",
          "[mappers][x1017][state][integration]") {
    for (const uint16_t id : {82U, 552U}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(x1017_cartridge(id, true), {}));
        emulator.reset();
        REQUIRE(emulator.cpu().pc() == 0xE000U);
        for (unsigned i = 0U; i < 14U; ++i)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(0x6005U) == 0x5AU);
        const auto saved = emulator.save_state();
        auto finish = [&] {
            for (unsigned i = 0U; i < 40U; ++i)
                emulator.step_instruction();
            REQUIRE(emulator.read_cpu(0x8000U) == 9U);
            REQUIRE(emulator.read_cpu(0x6005U) == 0U);
            REQUIRE(emulator.read_cpu(2U) == 1U);
        };
        finish();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(saved));
        finish();
        REQUIRE(emulator.save_state() == expected);
        REQUIRE_FALSE(emulator.load_state(std::span(saved).first(saved.size() / 2U)));
        REQUIRE(emulator.save_state() == expected);
    }
}

TEST_CASE("X1-017 component state preserves protected RAM and validates bounded timer fields",
          "[mappers][x1017][state]") {
    for (const uint16_t id : {82U, 552U}) {
        auto mapper = x1017_board(id, true);
        mapper->write_prg(0x7EF9U, 0x84U);
        mapper->write_prg(0x73FFU, 0xAAU);
        mapper->write_prg(0x7EFDU, 1U);
        mapper->write_prg(0x7EFFU, 0U);
        mapper->write_prg(0x7EFEU, 3U);
        for (unsigned i = 0U; i < 31U; ++i)
            mapper->clock_cpu();
        StateWriter saved;
        mapper->save_state(saved);
        mapper->reset();
        REQUIRE_FALSE(mapper->maps_prg(0x73FFU));
        REQUIRE(mapper->battery_ram().back() == 0xAAU);
        StateReader reader(saved.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(mapper->read_prg(0x73FFU) == 0xAAU);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        StateWriter pending;
        mapper->save_state(pending);
        mapper->write_prg(0x7EFFU, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        StateReader pending_reader(pending.data());
        mapper->load_state(pending_reader);
        REQUIRE(pending_reader.ok());
        REQUIRE(mapper->irq_pending());
        StateWriter replayed;
        mapper->save_state(replayed);
        REQUIRE(replayed.data() == pending.data());
        // Layout: PRG[3], CHR[6], mode, permissions[3], latch, control,
        // pending, uint16 counter, physical 5 KiB RAM.
        for (const std::size_t offset : {0U, 3U, 4U, 9U, 10U, 14U, 15U, 17U}) {
            auto corrupt = saved.data();
            corrupt[offset] = 0xFFU;
            StateReader bad(corrupt);
            mapper->load_state(bad);
            REQUIRE_FALSE(bad.ok());
        }
        StateReader truncated(std::span(saved.data()).first(saved.data().size() - 1U));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
    }
}

TEST_CASE("X1-017 factory bounds ROM, header RAM declarations and supported variants",
          "[mappers][x1017]") {
    register_builtin_mappers();
    auto create = [](INesHeader header, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(
            header, std::vector<Byte>(prg), std::vector<Byte>(chr));
    };
    for (const uint16_t id : {82U, 552U}) {
        auto header = x1017_header(id);
        REQUIRE(create(header, 0x8000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x4000U, 0x2000U));
        REQUIRE_FALSE(create(header, id == 82U ? 0x40000U : 0x100000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8001U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x80000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x2001U));
        REQUIRE_FALSE(create(header, 0x8000U, 0U));
        header.submapper = 1U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = x1017_header(id);
        header.prg_ram_size = 0x4000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = x1017_header(id);
        header.prg_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = x1017_header(id);
        header.chr_ram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = x1017_header(id, true);
        auto persistent = create(header, 0x8000U, 0x2000U);
        REQUIRE(persistent);
        REQUIRE((*persistent)->battery_ram().size() == 0x1400U);
        REQUIRE(x1017_board(id)->battery_ram().empty());
    }
}

TEST_CASE("X1-017 battery files contain exactly the five-KiB internal memory",
          "[mappers][x1017][battery]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("mapperbus-x1017-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{root};
    std::filesystem::create_directories(root);
    for (const uint16_t id : {82U, 552U}) {
        const auto path = root / (std::to_string(id) + ".sav");
        {
            Emulator writer;
            REQUIRE(writer.load_cartridge(x1017_cartridge(id, true, true), path.string()));
            writer.reset();
            for (unsigned i = 0U; i < 4U; ++i)
                writer.step_instruction();
            writer.flush_battery_ram();
        }
        auto bytes = read_binary_file_capped(path.string(), 0x1400U, "battery");
        REQUIRE(bytes);
        REQUIRE(bytes->size() == 0x1400U);
        REQUIRE((*bytes)[5U] == 0x5AU);
        Emulator reader;
        REQUIRE(reader.load_cartridge(x1017_cartridge(id, true, true), path.string()));
        reader.reset();
        REQUIRE(reader.read_cpu(0x6005U) == 0U);
        reader.step_instruction();
        reader.step_instruction();
        REQUIRE(reader.read_cpu(0x6005U) == 0x5AU);
        const auto saved = reader.save_state();
        REQUIRE(write_binary_file_atomic(path.string(), std::vector<Byte>(0x1401U), "battery"));
        REQUIRE_FALSE(reader.load_battery_ram(path.string()));
        REQUIRE(reader.save_state() == saved);
    }
}
