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
INesHeader taito_header(uint16_t id, Byte submapper = 0U) {
    INesHeader header{};
    header.is_nes2 = true;
    header.mapper_number = id;
    header.submapper = submapper;
    header.mirror_mode = MirrorMode::Vertical;
    return header;
}

std::vector<Byte> taito_rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank_size);
    return data;
}

std::unique_ptr<Mapper> taito_board(uint16_t id, Byte submapper = 0U, bool ram = false) {
    register_builtin_mappers();
    auto result =
        MapperRegistry::instance().create(taito_header(id, submapper),
                                          taito_rom(0x80000U, 0x2000U),
                                          ram ? std::vector<Byte>{} : taito_rom(0x80000U, 0x800U));
    REQUIRE(result);
    return std::move(*result);
}

unsigned taito_delay(Byte submapper) {
    return submapper == 0U ? 22U : 6U;
}

void expire_taito_delay(Mapper& mapper, Byte submapper) {
    for (unsigned i = 0U; i < taito_delay(submapper); ++i)
        mapper.clock_cpu();
}

Cartridge taito_cartridge(Byte submapper = 0U, bool irq_program = false, uint16_t id = 48U) {
    register_builtin_mappers();
    std::vector<Byte> image(16U + 0x8000U + 0x2000U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 2U;
    image[5U] = 1U;
    image[6U] = static_cast<Byte>(((id & 15U) << 4U) | 1U);
    image[7U] = static_cast<Byte>((id & 0xF0U) | 8U);
    image[8U] = static_cast<Byte>(static_cast<unsigned>(submapper) << 4U);
    const std::array<Byte, 3U> loop = {0x4CU, 0x00U, 0xE0U};
    std::copy(loop.begin(), loop.end(), image.begin() + 16U + 0x6000U);
    for (std::size_t offset = 0x7FFAU; offset < 0x8000U; offset += 2U)
        image[16U + offset + 1U] = 0xE0U;
    if (irq_program) {
        // clang-format off
        const std::array<Byte, 25U> program = {
            0x78U,                    // SEI
            0xA9U, 0x00U,             // LDA #0
            0x85U, 0x00U,             // STA $00
            0xA9U, 0x08U,             // LDA #8
            0x8DU, 0x01U, 0x20U,      // STA $2001 (render background)
            0xA9U, 0xFFU,             // LDA #$FF
            0x8DU, 0x00U, 0xC0U,      // STA $C000
            0x8DU, 0x01U, 0xC0U,      // STA $C001
            0x8DU, 0x02U, 0xC0U,      // STA $C002
            0x58U,                    // CLI
            0x4CU, 0x16U, 0xE0U};     // JMP $E016
        const std::array<Byte, 8U> handler = {
            0xE6U, 0x00U,             // INC $00
            0xA9U, 0x00U,             // LDA #0
            0x8DU, 0x03U, 0xC0U,      // STA $C003 (disable output)
            0x40U};                   // RTI
        // clang-format on
        std::copy(program.begin(), program.end(), image.begin() + 16U + 0x6000U);
        std::copy(handler.begin(), handler.end(), image.begin() + 16U + 0x6100U);
        image[16U + 0x7FFFU] = 0xE1U;
    }
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}
} // namespace

TEST_CASE("Taito PRG banks use six bits and keep the final sixteen KiB fixed", "[mappers][taito]") {
    for (const uint16_t id : {33U, 48U}) {
        auto mapper = taito_board(id);
        REQUIRE(mapper->read_prg(0x8000U) == 0U);
        REQUIRE(mapper->read_prg(0xA000U) == 1U);
        mapper->write_prg(0x9FFCU, 0xFFU); // Alias of $8000, ROM drives zero.
        mapper->write_prg(0x9FFDU, 0xBEU);
        REQUIRE(mapper->read_prg(0x8000U) == 63U);
        REQUIRE(mapper->read_prg(0xBFFFU) == 62U);
        REQUIRE(mapper->read_prg(0xC000U) == 62U);
        REQUIRE(mapper->read_prg(0xFFFFU) == 63U);
        REQUIRE_FALSE(mapper->maps_prg(0x6000U));
        REQUIRE(mapper->battery_ram().empty());
    }
}

TEST_CASE("Taito two-KiB CHR banks preserve all eight bits and do not discard odd bank numbers",
          "[mappers][taito]") {
    for (const uint16_t id : {33U, 48U}) {
        auto mapper = taito_board(id);
        mapper->write_prg(0x9FFEU, 0x81U);
        mapper->write_prg(0x9FFFU, 0xFFU);
        REQUIRE(mapper->read_chr(0U) == 0x81U);
        REQUIRE(mapper->read_chr(0x7FFU) == 0x81U);
        REQUIRE(mapper->read_chr(0x800U) == 0xFFU);
        REQUIRE(mapper->read_chr(0xFFFU) == 0xFFU);
        for (unsigned slot = 0U; slot < 4U; ++slot)
            mapper->write_prg(static_cast<Address>(0xBFFCU + slot), static_cast<Byte>(252U + slot));
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto addr = static_cast<Address>(0x1000U + slot * 0x400U);
            // The 1 KiB windows can reach only the lower 256 KiB.
            REQUIRE(mapper->read_chr(addr) == (252U + slot) / 2U);
            mapper->write_chr(addr, 0U);
            REQUIRE(mapper->read_chr(addr) == (252U + slot) / 2U);
        }
    }
}

TEST_CASE("Taito mapper IDs have distinct mirroring decoders and mapper 33 has no IRQ",
          "[mappers][taito]") {
    auto early = taito_board(33U);
    early->write_prg(0x8000U, 0x40U);
    REQUIRE(early->mirror_mode() == MirrorMode::Horizontal);
    early->write_prg(0xE000U, 0U);
    REQUIRE(early->mirror_mode() == MirrorMode::Horizontal);
    auto late = taito_board(48U);
    late->write_prg(0x8000U, 0x40U);
    REQUIRE(late->mirror_mode() == MirrorMode::Vertical);
    late->write_prg(0xFFFFU, 0x40U); // E003 is not a mirroring register.
    REQUIRE(late->mirror_mode() == MirrorMode::Vertical);
    late->write_prg(0xFFFCU, 0x40U);
    REQUIRE(late->mirror_mode() == MirrorMode::Horizontal);
    late->write_prg(0xE000U, 0U);
    REQUIRE(late->mirror_mode() == MirrorMode::Vertical);
    early->write_prg(0xC000U, 0xFFU);
    early->write_prg(0xC001U, 0U);
    early->write_prg(0xC002U, 0U);
    for (unsigned i = 0U; i < 600U; ++i) {
        early->clock_irq_counter();
        early->clock_cpu();
    }
    REQUIRE_FALSE(early->irq_pending());
}

TEST_CASE("TC0690 delays output and preserves the interrupt latch across disable and enable",
          "[mappers][taito][irq]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = taito_board(48U, submapper);
        mapper->write_prg(0xDFFCU, 0xFFU);
        mapper->write_prg(0xDFFDU, 0U);
        if (submapper == 1U)
            mapper->clock_irq_counter(); // Inferred next-edge reload variant.
        mapper->clock_irq_counter();
        mapper->write_prg(0xDFFEU, 0U);
        for (unsigned i = 1U; i < taito_delay(submapper); ++i) {
            mapper->clock_cpu();
            REQUIRE_FALSE(mapper->irq_pending());
        }
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xDFFFU, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xC002U, 0U);
        REQUIRE(mapper->irq_pending()); // Disable did not acknowledge the latch.
        mapper->acknowledge_irq();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xC001U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("TC0690 counts while output is disabled and latch writes do not reload the counter",
          "[mappers][taito][irq]") {
    auto mapper = taito_board(48U);
    mapper->write_prg(0xC000U, 0xFEU);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();    // FF
    mapper->write_prg(0xC000U, 0U); // Does not alter the live count.
    mapper->clock_irq_counter();    // 00, latched while disabled.
    expire_taito_delay(*mapper, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC002U, 0U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC001U, 0U); // Reload zero and clear pending.
    for (unsigned i = 0U; i < 255U; ++i)
        mapper->clock_irq_counter();
    expire_taito_delay(*mapper, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    expire_taito_delay(*mapper, 0U);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("TC0690 clear cancels delayed IRQ output without disabling counting",
          "[mappers][taito][irq]") {
    auto mapper = taito_board(48U);
    mapper->write_prg(0xC000U, 0xFFU);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xC002U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->write_prg(0xC001U, 0U);
    expire_taito_delay(*mapper, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    expire_taito_delay(*mapper, 0U);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("TC0690 snapshots replay delayed output and submapper reload progress",
          "[mappers][taito][state]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = taito_board(48U, submapper);
        mapper->write_prg(0xC000U, 0xFFU);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xC002U, 0U);
        StateWriter reload;
        mapper->save_state(reload);
        for (unsigned replay = 0U; replay < 2U; ++replay) {
            StateReader reader(reload.data());
            mapper->load_state(reader);
            REQUIRE(reader.ok());
            mapper->clock_irq_counter();
            if (submapper == 1U) {
                expire_taito_delay(*mapper, submapper);
                REQUIRE_FALSE(mapper->irq_pending());
                mapper->clock_irq_counter();
            }
            mapper->clock_cpu();
            StateWriter delayed;
            mapper->save_state(delayed);
            expire_taito_delay(*mapper, submapper);
            REQUIRE(mapper->irq_pending());
            StateReader partial(delayed.data());
            mapper->load_state(partial);
            REQUIRE(partial.ok());
            REQUIRE_FALSE(mapper->irq_pending());
            for (unsigned i = 0U; i < taito_delay(submapper) - 1U; ++i)
                mapper->clock_cpu();
            REQUIRE(mapper->irq_pending());
        }
    }
}

TEST_CASE("TC0690 IRQ receives rendering pulses and its output delay advances during DMA",
          "[mappers][taito][ppu][bus]") {
    auto cart = taito_cartridge();
    MemoryBus bus;
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    bus.connect_cartridge(&cart);
    bus.connect_ppu(&ppu);
    cart.write_prg(0xC000U, 0xFFU);
    cart.write_prg(0xC001U, 0U);
    cart.write_prg(0xC002U, 0U);
    bus.clock_cpu_bus(0U, 120U); // Rendering disabled, no scanline pulses.
    REQUIRE_FALSE(bus.poll_irq());
    ppu.write_register(1U, 8U);
    ppu.step(341U); // Actual PPU callback schedules the delayed latch.
    REQUIRE_FALSE(bus.poll_irq());
    cart.write_prg(0xC003U, 0U); // Output disabled, latch/delay continue.
    REQUIRE(bus.run_oam_dma(0U, 0xE000U, false) == 513U);
    REQUIRE_FALSE(bus.poll_irq());
    cart.write_prg(0xC002U, 0U);
    REQUIRE(bus.poll_irq());
}

TEST_CASE("Taito banked CHR RAM aliases physical pages and survives reset and snapshots",
          "[mappers][taito][state]") {
    for (const uint16_t id : {33U, 48U}) {
        auto mapper = taito_board(id, 0U, true);
        mapper->write_prg(0x8002U, 3U);
        mapper->write_chr(0U, 0xAAU);     // Physical byte $1800.
        mapper->write_chr(0x400U, 0xBBU); // Physical byte $1C00.
        mapper->write_prg(0xA000U, 6U);
        mapper->write_prg(0xA001U, 7U);
        REQUIRE(mapper->read_chr(0x1000U) == 0xAAU);
        REQUIRE(mapper->read_chr(0x1400U) == 0xBBU);
        StateWriter writer;
        mapper->save_state(writer);
        mapper->reset();
        REQUIRE(mapper->read_chr(0U) == 0U);
        mapper->write_prg(0x8002U, 3U);
        REQUIRE(mapper->read_chr(0U) == 0xAAU);
        mapper->write_chr(0U, 0U);
        StateReader reader(writer.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(mapper->read_chr(0U) == 0xAAU);
        REQUIRE(mapper->read_chr(0x1400U) == 0xBBU);
        StateReader truncated(std::span(writer.data()).first(writer.data().size() - 1U));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
    }
}

TEST_CASE("TC0690 CPU IRQ handling replays through rendering and rejects truncated machine state",
          "[mappers][taito][state][integration]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(taito_cartridge(submapper, true), {}));
        emulator.reset();
        REQUIRE(emulator.cpu().pc() == 0xE000U);
        for (unsigned i = 0U; i < 9U; ++i)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(0U) == 0U);
        const auto saved = emulator.save_state();
        for (unsigned i = 0U; i < 200U; ++i)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(0U) == 1U);
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(saved));
        REQUIRE(emulator.read_cpu(0U) == 0U);
        for (unsigned i = 0U; i < 200U; ++i)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(0U) == 1U);
        REQUIRE(emulator.save_state() == expected);
        REQUIRE_FALSE(emulator.load_state(std::span(expected).first(expected.size() / 2U)));
        REQUIRE(emulator.save_state() == expected);
    }
}

TEST_CASE("Taito factories reject unsupported pin configurations and memory sizes",
          "[mappers][taito]") {
    register_builtin_mappers();
    auto create = [](INesHeader header, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(
            header, std::vector<Byte>(prg), std::vector<Byte>(chr));
    };
    for (const uint16_t id : {33U, 48U}) {
        auto header = taito_header(id);
        REQUIRE_FALSE(create(taito_header(id, 2U), 0x8000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x4000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x100000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8001U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x100000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x2001U));
        header.prg_ram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = taito_header(id);
        header.prg_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = taito_header(id);
        header.chr_ram_size = 0x4000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0U));
        header = taito_header(id);
        header.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0U));
    }
    REQUIRE_FALSE(create(taito_header(33U, 1U), 0x8000U, 0x2000U));
}

TEST_CASE("Taito snapshots reject corrupt bank, mirroring and interrupt state",
          "[mappers][taito][state]") {
    for (const uint16_t id : {33U, 48U}) {
        auto mapper = taito_board(id);
        StateWriter writer;
        mapper->save_state(writer);
        // Two PRG registers, six CHR registers, mirroring, reload, count,
        // reload flag, enable, latched IRQ, delay, then fixed-size CHR RAM.
        for (const std::size_t offset : {0U, 1U, 8U, 11U, 12U, 13U, 14U}) {
            auto corrupt = writer.data();
            corrupt[offset] = 0xFFU;
            StateReader reader(corrupt);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
        }
    }
}

TEST_CASE("Taito mirroring changes reach the production PPU CIRAM decoder",
          "[mappers][taito][ppu]") {
    for (const uint16_t id : {33U, 48U}) {
        auto cart = taito_cartridge(0U, false, id);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        auto address = [&ppu](Address addr) {
            ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
            ppu.write_register(6U, static_cast<Byte>(addr));
        };
        auto read = [&ppu, &address](Address addr) {
            address(addr);
            (void)ppu.read_register(7U);
            return ppu.read_register(7U);
        };
        address(0x2000U);
        ppu.write_register(7U, 0x11U);
        address(0x2400U);
        ppu.write_register(7U, 0x22U);
        cart.write_prg(id == 33U ? 0x8000U : 0xE000U, 0x40U);
        REQUIRE(read(0x2000U) == 0x11U);
        REQUIRE(read(0x2400U) == 0x11U);
        REQUIRE(read(0x2800U) == 0x22U);
        REQUIRE(read(0x2C00U) == 0x22U);
        cart.write_prg(id == 33U ? 0x8000U : 0xE000U, 0U);
        REQUIRE(read(0x2000U) == 0x11U);
        REQUIRE(read(0x2400U) == 0x22U);
        REQUIRE(read(0x2800U) == 0x11U);
        REQUIRE(read(0x2C00U) == 0x22U);
    }
}
