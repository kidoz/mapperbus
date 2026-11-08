#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <vector>

#include "core/binary_file.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
INesHeader ss88006_header() {
    INesHeader header{};
    header.mapper_number = 18U;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Vertical;
    return header;
}

std::vector<Byte> ss88006_rom(std::size_t size, std::size_t bank) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = static_cast<Byte>(offset / bank);
    return data;
}

std::unique_ptr<Mapper> ss88006_board(INesHeader header = ss88006_header()) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(
        header, ss88006_rom(0x80000U, 0x2000U), ss88006_rom(0x40000U, 0x400U));
    REQUIRE(result);
    return std::move(*result);
}

void ss88006_latch(Mapper& mapper, Word value) {
    for (unsigned nibble = 0U; nibble < 4U; ++nibble)
        mapper.write_prg(
            static_cast<Address>(0xEFFCU + nibble),
            static_cast<Byte>(0xF0U | ((static_cast<unsigned>(value) >> (nibble * 4U)) & 15U)));
}

Cartridge ss88006_cartridge(bool program = false, bool battery = false) {
    register_builtin_mappers();
    constexpr std::size_t kPrg = 0x80000U;
    std::vector<Byte> image(16U + kPrg + 0x40000U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 32U;
    image[5U] = 32U;
    image[6U] = static_cast<Byte>(0x21U | (battery ? 2U : 0U));
    image[7U] = 0x18U;
    image[10U] = battery ? 0x70U : 7U;
    const auto prg = ss88006_rom(kPrg, 0x2000U);
    std::copy(prg.begin(), prg.end(), image.begin() + 16U);
    // Original code and IRQ handler in the fixed final 8 KiB bank.
    // clang-format off
    const std::array<Byte, 44U> code = {
        0xA9U,0x03U,0x8DU,0x02U,0x90U, // Enable reads and writes.
        0xA9U,0x5AU,0x8DU,0x05U,0x60U,
        0xA9U,0x0DU,0x8DU,0x00U,0x80U, // Low nibble of bank 45.
        0xA9U,0x02U,0x8DU,0x01U,0x80U, // High two bits.
        0xA9U,0x02U,0x8DU,0x02U,0xF0U, // Lower single-screen CIRAM.
        0xA9U,0x02U,0x8DU,0x00U,0xE0U, // IRQ reload latch = 2.
        0xA9U,0x00U,0x8DU,0x00U,0xF0U, // Copy full latch to counter.
        0xA9U,0x09U,0x8DU,0x01U,0xF0U, // Start four-bit counter.
        0x58U,                        // CLI
        0x4CU,0x29U,0xE0U};
    const std::array<Byte, 8U> handler = {
        0xE6U,0x02U,                  // INC $02
        0xA9U,0x00U,0x8DU,0x01U,0xF0U,// Disable counting and acknowledge.
        0x40U};                       // RTI
    // clang-format on
    if (program) {
        std::copy(code.begin(), code.end(), image.begin() + 16U + kPrg - 0x2000U);
        std::copy(handler.begin(), handler.end(), image.begin() + 16U + kPrg - 0x1F00U);
    } else {
        image[16U + kPrg - 0x2000U] = 0x4CU;
        image[17U + kPrg - 0x2000U] = 0U;
        image[18U + kPrg - 0x2000U] = 0xE0U;
    }
    for (std::size_t vector = kPrg - 6U; vector < kPrg; vector += 2U) {
        image[16U + vector] = 0U;
        image[17U + vector] = program && vector == kPrg - 2U ? 0xE1U : 0xE0U;
    }
    const auto chr = ss88006_rom(0x40000U, 0x400U);
    std::copy(chr.begin(), chr.end(), image.begin() + 16U + kPrg);
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}

void ss88006_ppu_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
Byte ss88006_ppu_read(Ppu& ppu, Address addr) {
    ss88006_ppu_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}
} // namespace

TEST_CASE("SS88006 splits three six-bit PRG banks into mirrored nibble registers",
          "[mappers][ss88006]") {
    auto mapper = ss88006_board();
    constexpr std::array<Address, 3U> kRegs = {0x8000U, 0x8002U, 0x9000U};
    for (unsigned slot = 0U; slot < kRegs.size(); ++slot) {
        const auto addr = static_cast<Address>(0x8000U + slot * 0x2000U);
        REQUIRE(mapper->read_prg(addr) == slot);
        mapper->write_prg(kRegs[slot], 0xFDU);
        mapper->write_prg(static_cast<Address>(kRegs[slot] + 1U), 0xFEU);
        REQUIRE(mapper->read_prg(addr) == 45U);
        mapper->write_prg(static_cast<Address>(kRegs[slot] | 0xFFCU), 0xF3U);
        REQUIRE(mapper->read_prg(addr) == 35U);
        mapper->write_prg(static_cast<Address>(kRegs[slot] | 0xFFDU), 0xFFU);
        REQUIRE(mapper->read_prg(addr) == 51U);
    }
    mapper->write_prg(0x9003U, 0U); // Not a fourth PRG or RAM control register.
    mapper->write_prg(0x7FFFU, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == 51U);
    REQUIRE(mapper->read_prg(0xA000U) == 51U);
    REQUIRE(mapper->read_prg(0xC000U) == 51U);
    REQUIRE(mapper->read_prg(0xFFFFU) == 63U);
}

TEST_CASE("SS88006 selects eight one-KiB CHR banks through all data nibbles",
          "[mappers][ss88006][ppu]") {
    auto cart = ss88006_cartridge();
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto reg = static_cast<Address>(0xA000U + (slot / 2U) * 0x1000U + (slot & 1U) * 2U);
        cart.write_prg(static_cast<Address>(reg | 0xFFCU), static_cast<Byte>(0xF0U | slot));
        cart.write_prg(static_cast<Address>(reg | 0xFFDU), 0xFFU);
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * 0x400U);
        REQUIRE(ss88006_ppu_read(ppu, addr) == 240U + slot);
        cart.write_chr(addr, 0U);
        REQUIRE(cart.read_chr(addr) == 240U + slot);
    }
}

TEST_CASE("SS88006 supports four CIRAM arrangements through PPU register accesses",
          "[mappers][ss88006][ppu]") {
    auto cart = ss88006_cartridge();
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    cart.write_prg(0xFFFEU, 1U);
    ss88006_ppu_address(ppu, 0x2000U);
    ppu.write_register(7U, 0x11U);
    ss88006_ppu_address(ppu, 0x2400U);
    ppu.write_register(7U, 0x22U);
    const std::array<MirrorMode, 4U> modes = {MirrorMode::Horizontal,
                                              MirrorMode::Vertical,
                                              MirrorMode::SingleLower,
                                              MirrorMode::SingleUpper};
    const std::array<std::array<Byte, 4U>, 4U> expected = {{{0x11U, 0x11U, 0x22U, 0x22U},
                                                            {0x11U, 0x22U, 0x11U, 0x22U},
                                                            {0x11U, 0x11U, 0x11U, 0x11U},
                                                            {0x22U, 0x22U, 0x22U, 0x22U}}};
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        cart.write_prg(0xF002U, static_cast<Byte>(0xFCU | mode));
        REQUIRE(cart.mirror_mode() == modes[mode]);
        for (unsigned table = 0U; table < 4U; ++table)
            REQUIRE(ss88006_ppu_read(ppu, static_cast<Address>(0x2000U + table * 0x400U)) ==
                    expected[mode][table]);
    }
}

TEST_CASE("SS88006 RAM chip enable and write permission are separate active-high bits",
          "[mappers][ss88006][bus]") {
    auto cart = ss88006_cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    REQUIRE_FALSE(cart.maps_prg(0x6000U));
    for (unsigned control = 0U; control < 256U; ++control) {
        bus.write(0x9002U, 3U);
        bus.write(0x6000U, 0x55U);
        bus.write(0x9FFEU, static_cast<Byte>(control));
        REQUIRE(cart.maps_prg(0x6000U) == ((control & 1U) != 0U));
        REQUIRE(cart.maps_prg(0x7FFFU) == ((control & 1U) != 0U));
        bus.write(0x6000U, 0x3CU);
        bus.write(0U, 0xA5U);
        const Byte expected = (control & 1U) == 0U ? 0xA5U : ((control & 2U) != 0U ? 0x3CU : 0x55U);
        REQUIRE(bus.read(0x6000U) == expected);
        bus.write(0x9002U, 1U);
        REQUIRE(bus.read(0x6000U) == ((control & 3U) == 3U ? 0x3CU : 0x55U));
    }
    auto absent = ss88006_board();
    absent->write_prg(0x9002U, 3U);
    absent->write_prg(0x6000U, 0x77U);
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->battery_ram().empty());
}

TEST_CASE("SS88006 IRQ widths prioritize the shortest selected counter and wrap repeatedly",
          "[mappers][ss88006][irq]") {
    for (unsigned size = 0U; size < 8U; ++size) {
        INFO("Size bits " << size);
        auto mapper = ss88006_board();
        const unsigned mask = (size & 4U) != 0U   ? 15U
                              : (size & 2U) != 0U ? 255U
                              : (size & 1U) != 0U ? 4095U
                                                  : 65535U;
        ss88006_latch(*mapper, 0x1232U);
        mapper->write_prg(0xFFFCU, 0xFFU);
        mapper->write_prg(0xFFFDU, static_cast<Byte>(0xF1U | (size << 1U)));
        for (unsigned i = 0U; i < (0x1232U & mask); ++i)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF001U, static_cast<Byte>(1U | (size << 1U))); // Ack without reload.
        REQUIRE_FALSE(mapper->irq_pending());
        for (unsigned i = 0U; i < mask; ++i)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending()); // Counting did not stop.
    }
}

TEST_CASE("SS88006 retains high counter bits and reloads the full word in narrow modes",
          "[mappers][ss88006][irq]") {
    auto mapper = ss88006_board();
    ss88006_latch(*mapper, 0x1232U);
    mapper->write_prg(0xF000U, 0U);
    mapper->write_prg(0xF001U, 9U);
    for (unsigned i = 0U; i < 3U; ++i)
        mapper->clock_cpu(); // $1232 -> $123F.
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF001U, 1U); // 16-bit mode, same counter, clear pending.
    for (unsigned i = 0U; i < 0x123FU; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF001U, 8U); // Pause in four-bit mode.
    ss88006_latch(*mapper, 0x4321U);
    mapper->write_prg(0xF000U, 0U);
    for (unsigned i = 0U; i < 100U; ++i)
        mapper->clock_cpu();
    mapper->write_prg(0xF001U, 1U); // Full reload preserved $4321 while paused.
    ss88006_latch(*mapper, 0U);     // Changing the latch cannot rewind the live count.
    for (unsigned i = 0U; i < 0x4321U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF000U, 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending()); // Zero wraps on the next M2.
}

TEST_CASE("SS88006 clocks its IRQ during DMA without rendering or expansion audio",
          "[mappers][ss88006][irq][bus]") {
    auto cart = ss88006_cartridge();
    MemoryBus bus;
    Ppu ppu;
    bus.connect_cartridge(&cart);
    bus.connect_ppu(&ppu);
    cart.write_prg(0xE000U, 0U);
    cart.write_prg(0xE001U, 0U);
    cart.write_prg(0xE002U, 2U);
    cart.write_prg(0xF000U, 0U);
    cart.write_prg(0xF001U, 1U); // 512 wraps on cycle 513.
    bus.clock_cpu_bus(0U, 512U);
    REQUIRE_FALSE(bus.poll_irq());
    bus.clock_cpu_bus(0U);
    REQUIRE(bus.poll_irq());
    REQUIRE((bus.interrupt_sample() & 1U) == 0U);
    bus.clock_cpu_bus(0U);
    REQUIRE((bus.interrupt_sample() & 1U) == 1U);
    cart.write_prg(0xF000U, 0U);
    REQUIRE(bus.run_oam_dma(0U, 0xE000U, false) >= 513U);
    REQUIRE(bus.poll_irq());
    cart.write_prg(0xF001U, 0U);
    REQUIRE_FALSE(bus.poll_irq());
}

TEST_CASE("SS88006 optional RAM honors NES 2.0 sizes, NVRAM offsets and legacy defaults",
          "[mappers][ss88006][battery][state]") {
    auto header = ss88006_header();
    header.has_battery = true;
    header.prg_ram_size = 0x800U;
    header.prg_nvram_size = 0x800U;
    auto mapper = ss88006_board(header);
    mapper->write_prg(0x9002U, 3U);
    mapper->write_prg(0x6000U, 0x11U);
    mapper->write_prg(0x6800U, 0x22U);
    REQUIRE(mapper->read_prg(0x7000U) == 0x11U);
    REQUIRE(mapper->read_prg(0x7800U) == 0x22U);
    REQUIRE(mapper->battery_ram().size() == 0x800U);
    REQUIRE(mapper->battery_ram()[0U] == 0x22U);
    mapper->set_battery_ram(std::vector<Byte>(0x800U, 0x33U));
    REQUIRE(mapper->read_prg(0x6000U) == 0x11U);
    REQUIRE(mapper->read_prg(0x6800U) == 0x33U);
    mapper->reset();
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x9002U, 1U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x11U);
    header = ss88006_header();
    header.is_nes2 = false;
    header.prg_ram_size = 0x2000U;
    auto legacy = ss88006_board(header);
    legacy->write_prg(0x9002U, 3U);
    REQUIRE(legacy->maps_prg(0x6000U));
    REQUIRE(legacy->battery_ram().empty());
}

TEST_CASE("SS88006 CPU IRQ service replays a snapshot with an active narrow counter",
          "[mappers][ss88006][state][integration]") {
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(ss88006_cartridge(true), {}));
    emulator.reset();
    REQUIRE(emulator.cpu().pc() == 0xE000U);
    for (unsigned i = 0U; i < 16U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0x8000U) == 45U);
    REQUIRE(emulator.read_cpu(0x6005U) == 0x5AU);
    const auto saved = emulator.save_state();
    auto finish = [&] {
        for (unsigned i = 0U; i < 40U; ++i)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(2U) == 1U);
        REQUIRE(emulator.read_cpu(0x6005U) == 0x5AU);
    };
    finish();
    const auto expected = emulator.save_state();
    REQUIRE(emulator.load_state(saved));
    finish();
    REQUIRE(emulator.save_state() == expected);
    REQUIRE_FALSE(emulator.load_state(std::span(saved).first(saved.size() / 2U)));
    REQUIRE(emulator.save_state() == expected);
}

TEST_CASE(
    "SS88006 state preserves counter, pending IRQ and RAM permissions and rejects malformed fields",
    "[mappers][ss88006][state]") {
    auto header = ss88006_header();
    header.prg_ram_size = 0x2000U;
    auto mapper = ss88006_board(header);
    mapper->write_prg(0x9002U, 3U);
    mapper->write_prg(0x7FFFU, 0xA5U);
    ss88006_latch(*mapper, 1U);
    mapper->write_prg(0xF000U, 0U);
    mapper->write_prg(0xF001U, 9U);
    mapper->clock_cpu();
    StateWriter active;
    mapper->save_state(active);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    StateWriter pending;
    mapper->save_state(pending);
    mapper->reset();
    REQUIRE_FALSE(mapper->maps_prg(0x7FFFU));
    StateReader reader(active.data());
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x7FFFU) == 0xA5U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    StateWriter replayed;
    mapper->save_state(replayed);
    REQUIRE(replayed.data() == pending.data());
    mapper->write_prg(0xF001U, 0U);
    StateReader pending_reader(pending.data());
    mapper->load_state(pending_reader);
    REQUIRE(pending_reader.ok());
    REQUIRE(mapper->irq_pending());
    // PRG[3], CHR[8], mirroring, RAM control, uint16 latch/counter, IRQ control/pending.
    for (const std::size_t offset : {0U, 1U, 2U, 11U, 12U, 17U, 18U}) {
        auto corrupt = active.data();
        corrupt[offset] = 0xFFU;
        StateReader bad(corrupt);
        mapper->load_state(bad);
        REQUIRE_FALSE(bad.ok());
    }
    StateReader truncated(std::span(active.data()).first(active.data().size() - 1U));
    mapper->load_state(truncated);
    REQUIRE_FALSE(truncated.ok());
}

TEST_CASE("SS88006 factory rejects unsupported memories and variants", "[mappers][ss88006]") {
    register_builtin_mappers();
    auto create = [](INesHeader header, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(
            header, std::vector<Byte>(prg), std::vector<Byte>(chr));
    };
    auto header = ss88006_header();
    REQUIRE(create(header, 0x8000U, 0x2000U));
    REQUIRE_FALSE(create(header, 0x4000U, 0x2000U));
    REQUIRE_FALSE(create(header, 0x100000U, 0x2000U));
    REQUIRE_FALSE(create(header, 0x8001U, 0x2000U));
    REQUIRE_FALSE(create(header, 0x8000U, 0x80000U));
    REQUIRE_FALSE(create(header, 0x8000U, 0x2001U));
    REQUIRE_FALSE(create(header, 0x8000U, 0U));
    header.submapper = 1U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
    header = ss88006_header();
    header.prg_ram_size = 0x4000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
    header = ss88006_header();
    header.has_battery = true;
    header.prg_ram_size = 0x2000U;
    header.prg_nvram_size = 0x2000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
    header = ss88006_header();
    header.prg_nvram_size = 0x2000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
    header = ss88006_header();
    header.chr_ram_size = 0x2000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
    header = ss88006_header();
    header.chr_nvram_size = 0x2000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
    header = ss88006_header();
    header.mirror_mode = MirrorMode::FourScreen;
    REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
}

TEST_CASE("SS88006 battery files persist exactly declared NVRAM and restore with RAM locked",
          "[mappers][ss88006][battery]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("mapperbus-ss88006-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "board.sav";
    {
        Emulator writer;
        REQUIRE(writer.load_cartridge(ss88006_cartridge(true, true), path.string()));
        writer.reset();
        for (unsigned i = 0U; i < 4U; ++i)
            writer.step_instruction();
        writer.flush_battery_ram();
    }
    auto bytes = read_binary_file_capped(path.string(), 0x2000U, "battery");
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 0x2000U);
    REQUIRE((*bytes)[5U] == 0x5AU);
    Emulator reader;
    REQUIRE(reader.load_cartridge(ss88006_cartridge(true, true), path.string()));
    reader.reset();
    REQUIRE(reader.read_cpu(0x6005U) != 0x5AU);
    reader.step_instruction();
    reader.step_instruction();
    REQUIRE(reader.read_cpu(0x6005U) == 0x5AU);
    const auto saved = reader.save_state();
    REQUIRE(write_binary_file_atomic(path.string(), std::vector<Byte>(0x2001U), "battery"));
    REQUIRE_FALSE(reader.load_battery_ram(path.string()));
    REQUIRE(reader.save_state() == saved);
}
