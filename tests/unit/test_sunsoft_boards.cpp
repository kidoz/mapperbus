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

INesHeader sunsoft_header(uint16_t id, Byte submapper = 0U) {
    INesHeader header{};
    header.is_nes2 = true;
    header.mapper_number = id;
    header.submapper = submapper;
    header.mirror_mode = MirrorMode::Vertical;
    return header;
}

std::vector<Byte> sunsoft_rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank_size);
    return data;
}

std::unique_ptr<Mapper> sunsoft_board(INesHeader header, bool chr_ram = false) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(
        header,
        sunsoft_rom(header.submapper == 1U ? 0x20000U : 0x40000U, 0x4000U),
        chr_ram ? std::vector<Byte>{}
                : sunsoft_rom(header.mapper_number == 67U ? 0x20000U : 0x40000U, 0x800U));
    REQUIRE(result);
    return std::move(*result);
}

Cartridge sunsoft_cartridge(uint16_t id, Byte submapper = 0U) {
    register_builtin_mappers();
    const std::size_t prg_size = submapper == 1U ? 0x20000U : 0x40000U;
    const std::size_t chr_size = id == 67U ? 0x20000U : 0x40000U;
    std::vector<Byte> image(16U + prg_size + chr_size, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = static_cast<Byte>(prg_size / 0x4000U);
    image[5U] = static_cast<Byte>(chr_size / 0x2000U);
    image[6U] = static_cast<Byte>(((id & 15U) << 4U) | 1U);
    image[7U] = static_cast<Byte>((id & 0xF0U) | 8U);
    image[8U] = static_cast<Byte>(static_cast<unsigned>(submapper) << 4U);
    image[10U] = 7U; // 8 KiB volatile PRG RAM.
    for (std::size_t i = 0U; i < chr_size; ++i)
        image[16U + prg_size + i] = static_cast<Byte>(i / 0x400U);
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}

void sunsoft_irq_count(Mapper& mapper, Word count) {
    mapper.write_prg(0xD800U, 0U); // Reset high/low write phase.
    mapper.write_prg(0xC800U, static_cast<Byte>(count >> 8U));
    mapper.write_prg(0xC800U, static_cast<Byte>(count));
}

void ppu_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}

Byte ppu_read(Ppu& ppu, Address addr) {
    ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}

} // namespace

TEST_CASE("Sunsoft 3 and 4 bank PRG and CHR using their own address decoders",
          "[mappers][sunsoft]") {
    for (const uint16_t id : {67U, 68U}) {
        INFO(id);
        auto mapper = sunsoft_board(sunsoft_header(id));
        const Address prg_reg = id == 67U ? 0xFFFFU : 0xF123U;
        REQUIRE(mapper->read_prg(0xC000U) == 15U);
        mapper->write_prg(prg_reg, 0xF5U); // Includes ignored bits, no bus conflict.
        REQUIRE(mapper->read_prg(0x8000U) == 5U);
        REQUIRE(mapper->read_prg(0xBFFFU) == 5U);
        REQUIRE(mapper->read_prg(0xFFFFU) == 15U);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            mapper->write_prg(static_cast<Address>(0x8FFFU + slot * 0x1000U),
                              static_cast<Byte>(0xFCU + slot));
        }
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto expected = (id == 67U ? 60U : 124U) + slot;
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x800U)) == expected);
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x800U + 0x7FFU)) == expected);
            mapper->write_chr(static_cast<Address>(slot * 0x800U), 0U);
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x800U)) == expected);
        }
        mapper->write_prg(prg_reg, 0xFFU);
        REQUIRE(mapper->read_prg(0x8000U) == 15U);
    }
}

TEST_CASE("Sunsoft mirroring registers select all four CIRAM arrangements", "[mappers][sunsoft]") {
    const std::array modes = {MirrorMode::Vertical,
                              MirrorMode::Horizontal,
                              MirrorMode::SingleLower,
                              MirrorMode::SingleUpper};
    for (const uint16_t id : {67U, 68U}) {
        auto mapper = sunsoft_board(sunsoft_header(id));
        for (Byte mode = 0U; mode < 4U; ++mode) {
            mapper->write_prg(id == 67U ? 0xEFFFU : 0xE123U, static_cast<Byte>(mode | 0xE0U));
            REQUIRE(mapper->mirror_mode() == modes[mode]);
        }
    }
}

TEST_CASE("Sunsoft 3 interrupts on underflow then pauses until enabled again",
          "[mappers][sunsoft][irq]") {
    auto mapper = sunsoft_board(sunsoft_header(67U));
    sunsoft_irq_count(*mapper, 2U);
    mapper->write_prg(0xDFFFU, 0x10U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xD800U, 0U); // Does NOT acknowledge.
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC800U, 0U); // Nor does loading the counter.
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC800U, 0U);
    mapper->write_prg(0xF7FFU, 0U); // Acknowledge mirror, mask $8800.
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned i = 0U; i < 65537U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xD800U, 0x10U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending()); // Zero underflows in one cycle.
}

TEST_CASE("Sunsoft 3 acknowledgement aliases preserve banking and count enable",
          "[mappers][sunsoft][irq]") {
    auto mapper = sunsoft_board(sunsoft_header(67U));
    for (unsigned page = 0x8000U; page <= 0xF000U; page += 0x1000U) {
        sunsoft_irq_count(*mapper, 0U);
        mapper->write_prg(0xD800U, 0x10U);
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(static_cast<Address>(page + 0x7FFU), 0xFFU);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->read_prg(0x8000U) == 0U);
        REQUIRE(mapper->read_chr(0U) == 0U);
    }
    sunsoft_irq_count(*mapper, 1U);
    mapper->write_prg(0xD800U, 0x10U);
    mapper->write_prg(0x8000U, 0U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Sunsoft 3 writes directly update the live counter and enable resets byte phase",
          "[mappers][sunsoft][irq]") {
    auto mapper = sunsoft_board(sunsoft_header(67U));
    sunsoft_irq_count(*mapper, 0x0102U);
    mapper->write_prg(0xD800U, 0x10U);
    mapper->clock_cpu();            // 0101
    mapper->write_prg(0xCFFFU, 0U); // High byte only: 0001, not a reload latch.
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x8000U, 0U);
    sunsoft_irq_count(*mapper, 0xFFFFU);
    mapper->write_prg(0xC800U, 0xAAU); // High byte, leaves low-byte phase pending.
    mapper->write_prg(0xD800U, 0U);    // Resets to high-byte phase.
    mapper->write_prg(0xC800U, 0U);
    mapper->write_prg(0xC800U, 1U);
    mapper->write_prg(0xD800U, 0x10U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Sunsoft 3 snapshots retain the half-written count and pending interrupt",
          "[mappers][sunsoft][state]") {
    auto mapper = sunsoft_board(sunsoft_header(67U));
    mapper->write_prg(0xC800U, 0U);
    StateWriter half;
    mapper->save_state(half);
    for (unsigned replay = 0U; replay < 2U; ++replay) {
        StateReader reader(half.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        mapper->write_prg(0xC800U, 1U);
        mapper->write_prg(0xD800U, 0x10U);
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
    }
    StateWriter pending;
    mapper->save_state(pending);
    mapper->write_prg(0x8000U, 0U);
    StateReader reader(pending.data());
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Sunsoft 4 routes CHR ROM nametables through PPU reads without writing CIRAM",
          "[mappers][sunsoft][ppu]") {
    auto cart = sunsoft_cartridge(68U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    ppu_address(ppu, 0x2000U);
    ppu.write_register(7U, 0x55U);
    ppu_address(ppu, 0x2400U);
    ppu.write_register(7U, 0x66U);
    cart.write_prg(0xC123U, 2U);
    cart.write_prg(0xDFFFU, 5U);
    const std::array<std::array<Byte, 4U>, 4U> expected = {
        std::array<Byte, 4U>{130U, 133U, 130U, 133U},
        std::array<Byte, 4U>{130U, 130U, 133U, 133U},
        std::array<Byte, 4U>{130U, 130U, 130U, 130U},
        std::array<Byte, 4U>{133U, 133U, 133U, 133U}};
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        cart.write_prg(0xEFFFU, static_cast<Byte>(0x10U | mode));
        for (unsigned table = 0U; table < 4U; ++table) {
            const auto addr = static_cast<Address>(0x2000U + table * 0x400U);
            REQUIRE(ppu_read(ppu, addr) == expected[mode][table]);
            REQUIRE(ppu_read(ppu, static_cast<Address>(addr + 0x3C0U)) == expected[mode][table]);
            REQUIRE(ppu_read(ppu, static_cast<Address>(addr + 0x1000U)) == expected[mode][table]);
            ppu_address(ppu, addr);
            ppu.write_register(7U, 0x99U);
            REQUIRE(ppu_read(ppu, addr) == expected[mode][table]);
        }
    }
    cart.write_prg(0xE000U, 0U);
    REQUIRE(ppu_read(ppu, 0x2000U) == 0x55U);
    REQUIRE(ppu_read(ppu, 0x2400U) == 0x66U);
    cart.write_prg(0xC000U, 0x82U); // D7 is forced high, not an extra bank bit.
    cart.write_prg(0xE000U, 0x10U);
    REQUIRE(ppu_read(ppu, 0x2000U) == 130U);
    REQUIRE(ppu_read(ppu, 0x3F00U) == 0U); // Palette RAM is not cartridge ROM.
}

TEST_CASE("Sunsoft 4 gates WRAM reads onto CPU open bus and preserves battery data",
          "[mappers][sunsoft][bus]") {
    auto cart = sunsoft_cartridge(68U);
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    bus.write(0U, 0xA5U);
    REQUIRE(bus.read(0x6000U) == 0xA5U);
    bus.write(0x6000U, 0x11U); // Disabled write is ignored.
    bus.write(0xF000U, 0x10U);
    REQUIRE(bus.read(0x6000U) == 0U);
    bus.write(0x6000U, 0x22U);
    bus.write(0xF000U, 0U);
    bus.write(0U, 0x42U);
    REQUIRE(bus.read(0x6000U) == 0x42U);
    bus.write(0xF000U, 0x10U);
    REQUIRE(bus.read(0x6000U) == 0x22U);
    auto header = sunsoft_header(68U);
    header.prg_nvram_size = 0x2000U;
    auto mapper = sunsoft_board(header);
    const std::vector<Byte> battery(0x2000U, 0x44U);
    mapper->set_battery_ram(battery);
    REQUIRE(mapper->battery_ram().size() == battery.size());
    mapper->write_prg(0xF000U, 0x10U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x44U);
    mapper->reset();
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->battery_ram()[0U] == 0x44U);
}

TEST_CASE("Sunsoft 4 dual cartridge base leaves an absent option ROM on open bus",
          "[mappers][sunsoft][bus]") {
    auto mapper = sunsoft_board(sunsoft_header(68U, 1U));
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == 7U);
    mapper->write_prg(0xF000U, 15U);
    REQUIRE(mapper->read_prg(0x8000U) == 7U);
    mapper->write_prg(0xF000U, 7U);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == 7U);
    StateWriter external;
    mapper->save_state(external);
    mapper->reset();
    StateReader reader(external.data());
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0U);
    auto cart = sunsoft_cartridge(68U, 1U);
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    REQUIRE(cart.read_prg(0xC000U) == 0U);
    cart.write_prg(0xF000U, 0U); // Select missing option cartridge.
    bus.write(0U, 0xA5U);
    REQUIRE(bus.read(0x8000U) == 0xA5U);
    cart.write_prg(0x6000U, 0xFFU); // Licensing strobe cannot create a ROM.
    bus.write(0U, 0x42U);
    REQUIRE(bus.read(0x8000U) == 0x42U);
    REQUIRE(cart.prg_read_mask(0xC000U) == 0xFFU);
    cart.write_prg(0xF000U, 8U);
    REQUIRE(cart.prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(bus.read(0x8000U) == 0U);
}

TEST_CASE("Sunsoft 3 pauses its full sixteen-bit counter and clocks during OAM DMA",
          "[mappers][sunsoft][irq][bus]") {
    auto mapper = sunsoft_board(sunsoft_header(67U));
    sunsoft_irq_count(*mapper, 0xFFFFU);
    mapper->write_prg(0xD800U, 0x10U);
    for (unsigned i = 0U; i < 65535U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xD800U, 0U);
    for (unsigned i = 0U; i < 10U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xD800U, 0x10U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    auto cart = sunsoft_cartridge(67U);
    MemoryBus bus;
    Ppu ppu;
    bus.connect_cartridge(&cart);
    bus.connect_ppu(&ppu);
    cart.write_prg(0xC800U, 2U);
    cart.write_prg(0xC800U, 1U); // 513: IRQ must occur on cycle 514.
    cart.write_prg(0xD800U, 0x10U);
    for (unsigned i = 0U; i < 600U; ++i) {
        cart.clock_audio();
        cart.clock_irq_counter();
    }
    REQUIRE_FALSE(bus.poll_irq());
    REQUIRE(bus.run_oam_dma(0U, 0x8000U, false) == 513U);
    REQUIRE_FALSE(bus.poll_irq());
    bus.clock_cpu_bus(0U);
    REQUIRE(bus.poll_irq());
    cart.acknowledge_irq();
    REQUIRE(bus.poll_irq());
    bus.write(0x8000U, 0U);
    REQUIRE_FALSE(bus.poll_irq());
}

TEST_CASE("Sunsoft writable memories retain banks and distinguish volatile and persistent bytes",
          "[mappers][sunsoft][state]") {
    for (const uint16_t id : {67U, 68U}) {
        auto header = sunsoft_header(id);
        header.prg_ram_size = 0x1000U;
        header.prg_nvram_size = 0x1000U;
        auto mapper = sunsoft_board(header, id == 67U);
        mapper->write_prg(id == 67U ? 0xF800U : 0xF000U, 0x10U);
        mapper->write_prg(0x6000U, 0x11U);
        mapper->write_prg(0x7000U, 0x22U);
        REQUIRE(mapper->battery_ram().size() == 0x1000U);
        REQUIRE(mapper->battery_ram()[0U] == 0x22U);
        if (id == 67U) {
            mapper->write_prg(0x8800U, 3U);
            mapper->write_chr(0U, 0x33U);
            mapper->write_prg(0x9800U, 7U); // Same physical CHR RAM page.
            REQUIRE(mapper->read_chr(0x800U) == 0x33U);
        } else {
            mapper->write_prg(0xC000U, 0x12U);
            mapper->write_prg(0xD000U, 0x34U);
            mapper->write_prg(0xE000U, 0x13U);
        }
        StateWriter writer;
        mapper->save_state(writer);
        mapper->reset();
        StateReader reader(writer.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(mapper->read_prg(0x6000U) == 0x11U);
        REQUIRE(mapper->read_prg(0x7000U) == 0x22U);
        StateWriter replay;
        mapper->save_state(replay);
        REQUIRE(replay.data() == writer.data());
        if (id == 67U)
            REQUIRE(mapper->read_chr(0x800U) == 0x33U);
        else
            REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
        StateReader short_reader(std::span(writer.data()).first(writer.data().size() - 1U));
        mapper->load_state(short_reader);
        REQUIRE_FALSE(short_reader.ok());
    }
}

TEST_CASE("Sunsoft factory rejects unknown variants and unsupported memory geometry",
          "[mappers][sunsoft]") {
    register_builtin_mappers();
    auto create = [](INesHeader header, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(
            header, std::vector<Byte>(prg), std::vector<Byte>(chr));
    };
    for (const uint16_t id : {67U, 68U}) {
        const auto header = sunsoft_header(id);
        REQUIRE_FALSE(create(sunsoft_header(id, 2U), 0x8000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x4000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8001U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x80000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x800U + 1U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x80000U));
        auto ram = header;
        ram.prg_ram_size = 0x2000U;
        ram.prg_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(ram, 0x8000U, 0x2000U));
        ram = header;
        ram.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(ram, 0x8000U, 0x2000U));
    }
    REQUIRE_FALSE(create(sunsoft_header(67U, 1U), 0x8000U, 0x2000U));
    REQUIRE_FALSE(create(sunsoft_header(67U), 0x8000U, 0x40000U));
    REQUIRE_FALSE(create(sunsoft_header(68U), 0x8000U, 0U));
    REQUIRE_FALSE(create(sunsoft_header(68U, 1U), 0x40000U, 0x2000U));
}

TEST_CASE("Sunsoft state rejects corrupt bank registers, mirroring and IRQ flags",
          "[mappers][sunsoft][state]") {
    for (const uint16_t id : {67U, 68U}) {
        auto mapper = sunsoft_board(sunsoft_header(id));
        StateWriter writer;
        mapper->save_state(writer);
        // PRG, four CHR banks, two NT banks, NT control, 16-bit count,
        // low-byte phase, enable, pending, then fixed RAM arrays.
        for (const std::size_t offset : {0U, 1U, 5U, 7U, 10U, 11U, 12U}) {
            auto corrupted = writer.data();
            corrupted[offset] = 0xFFU;
            StateReader reader(corrupted);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
        }
    }
}

TEST_CASE("Sunsoft 3 CPU handles its IRQ and replays a running-counter snapshot",
          "[mappers][sunsoft][state][integration]") {
    register_builtin_mappers();
    std::vector<Byte> image(16U + 0x8000U + 0x2000U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 2U;
    image[5U] = 1U;
    image[6U] = 0x30U;
    image[7U] = 0x48U;
    // clang-format off
    const std::array<Byte, 22U> program = {
        0x78U,                    // SEI
        0xA9U, 0x00U,             // LDA #0
        0x85U, 0x00U,             // STA $00
        0x8DU, 0x00U, 0xC8U,      // STA $C800 (high)
        0xA9U, 0x20U,             // LDA #32
        0x8DU, 0x00U, 0xC8U,      // STA $C800 (low)
        0xA9U, 0x10U,             // LDA #$10
        0x8DU, 0x00U, 0xD8U,      // STA $D800
        0x58U,                    // CLI
        0x4CU, 0x13U, 0x80U};     // JMP $8013
    const std::array<Byte, 8U> handler = {
        0xE6U, 0x00U,             // INC $00
        0xA9U, 0x00U,             // LDA #0
        0x8DU, 0x00U, 0x80U,      // STA $8000 (acknowledge)
        0x40U};                   // RTI
    // clang-format on
    std::copy(program.begin(), program.end(), image.begin() + 16U);
    std::copy(handler.begin(), handler.end(), image.begin() + 16U + 0x100U);
    for (std::size_t offset = 16U + 0x7FFAU; offset < 16U + 0x8000U; offset += 2U) {
        image[offset] = 0U;
        image[offset + 1U] = offset == 16U + 0x7FFEU ? 0x81U : 0x80U;
    }
    auto cartridge = Cartridge::from_data(image);
    REQUIRE(cartridge);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(std::move(*cartridge), {}));
    emulator.reset();
    REQUIRE(emulator.cpu().pc() == 0x8000U);
    for (unsigned i = 0U; i < 8U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0U) == 0U);
    const auto running = emulator.save_state();
    for (unsigned i = 0U; i < 30U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0U) == 1U);
    const auto expected = emulator.save_state();
    REQUIRE(emulator.load_state(running));
    REQUIRE(emulator.read_cpu(0U) == 0U);
    for (unsigned i = 0U; i < 30U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0U) == 1U);
    REQUIRE(emulator.save_state() == expected);
    REQUIRE_FALSE(emulator.load_state(std::span(expected).first(expected.size() / 2U)));
    REQUIRE(emulator.save_state() == expected);
}
