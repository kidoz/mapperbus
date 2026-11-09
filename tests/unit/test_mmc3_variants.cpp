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
#include "core/mappers/mmc3.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
INesHeader variant_header(uint16_t id) {
    INesHeader header{};
    header.mapper_number = id;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::Vertical;
    return header;
}
std::vector<Byte> variant_rom(std::size_t size, std::size_t bank) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = static_cast<Byte>(offset / bank);
    return data;
}
std::unique_ptr<Mapper> variant_board(INesHeader header,
                                      std::size_t chr = 0U,
                                      std::size_t prg = 0x80000U) {
    register_builtin_mappers();
    if (chr == 0U)
        chr = header.mapper_number == 119U ? 0x10000U : 0x40000U;
    auto result = MapperRegistry::instance().create(
        header, variant_rom(prg, 0x2000U), variant_rom(chr, 0x400U));
    REQUIRE(result);
    return std::move(*result);
}
void variant_bank(Mapper& mapper, Byte reg, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x9FFEU, static_cast<Byte>(reg | mode));
    mapper.write_prg(0x9FFFU, value);
}
Cartridge variant_cartridge(uint16_t id, bool battery = false) {
    std::vector<Byte> image(16U + 0x80000U + 0x10000U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 32U;
    image[5] = 8U;
    image[6] = static_cast<Byte>(((id & 15U) << 4U) | (battery ? 2U : 0U));
    image[7] = static_cast<Byte>((id & 0xF0U) | 8U);
    image[10] = battery ? 0x70U : 0U;
    auto prg = variant_rom(0x80000U, 0x2000U);
    // Original CPU program in the fixed final bank configures CHR and writes
    // through PPUDATA. It also enables rendering so the production IRQ seam runs.
    // clang-format off
    const std::array<Byte, 71> code = {
        0xA9,0x66,0x8D,0x05,0x60,
        0xA9,0x02,0x8D,0x00,0x80, 0xA9,0xC3,0x8D,0x01,0x80,
        0xA9,0x10,0x8D,0x06,0x20, 0xA9,0x05,0x8D,0x06,0x20,
        0xA9,0x5A,0x8D,0x07,0x20,
        0xA9,0x10,0x8D,0x06,0x20, 0xA9,0x05,0x8D,0x06,0x20,
        0xAD,0x07,0x20,0xAD,0x07,0x20,0x85,0x10,
        0xA9,0x18,0x8D,0x01,0x20,
        0xA9,0x03,0x8D,0x00,0xC0, 0xA9,0x00,0x8D,0x01,0xC0,
        0xA9,0x00,0x8D,0x01,0xE0, 0x4C,0x44,0xE0};
    // clang-format on
    std::ranges::copy(code, prg.begin() + 0x7E000U);
    for (std::size_t vector = 0x7FFFAU; vector < 0x80000U; vector += 2U) {
        prg[vector] = 0U;
        prg[vector + 1U] = 0xE0U;
    }
    std::ranges::copy(prg, image.begin() + 16U);
    const auto chr = variant_rom(0x10000U, 0x400U);
    std::ranges::copy(chr, image.begin() + 16U + 0x80000U);
    register_builtin_mappers();
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}
void variant_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
Byte variant_read(Ppu& ppu, Address addr) {
    variant_address(ppu, addr);
    (void)ppu.read_register(7U);
    return ppu.read_register(7U);
}
TEST_CASE("TxSROM routes CIRAM from raw CHR A17 in both inversion modes",
          "[mappers][mmc3-variants]") {
    for (const auto size : {0x10000U, 0x20000U, 0x40000U}) {
        auto mapper = variant_board(variant_header(118U), size);
        variant_bank(*mapper, 0U, 0x83U);
        variant_bank(*mapper, 1U, 0x05U);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto addr = static_cast<Address>(0x2005U + slot * 0x400U);
            REQUIRE(mapper->nametable_address(addr) == (slot < 2U ? 0x405U : 5U));
        }
        REQUIRE(mapper->read_chr(0U) == 0x82U % (size / 0x400U));
        REQUIRE(mapper->read_chr(0x400U) == 0x83U % (size / 0x400U));
        mapper->write_prg(0xBFFEU, 1U); // Ordinary MMC3 mirroring is disconnected.
        REQUIRE(mapper->nametable_address(0x2005U) == 0x405U);
        const std::array<Byte, 4> banks = {0x02U, 0x83U, 0x84U, 0x05U};
        for (Byte slot = 0U; slot < 4U; ++slot)
            variant_bank(*mapper, static_cast<Byte>(slot + 2U), banks[slot], 0x80U);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            REQUIRE(mapper->nametable_address(static_cast<Address>(0x2005U + slot * 0x400U)) ==
                    ((banks[slot] & 0x80U) != 0U ? 0x405U : 5U));
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x400U)) ==
                    banks[slot] % (size / 0x400U));
        }
        mapper->write_chr(0U, 0xFFU);
        REQUIRE(mapper->read_chr(0U) == 2U);
        for (unsigned layout = 0U; layout < 16U; ++layout) {
            for (Byte slot = 0U; slot < 4U; ++slot)
                variant_bank(*mapper,
                             static_cast<Byte>(slot + 2U),
                             (layout & (1U << slot)) != 0U ? 0x80U : 0U,
                             0x80U);
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(mapper->nametable_address(static_cast<Address>(0x2005U + slot * 0x400U)) ==
                        ((layout & (1U << slot)) != 0U ? 0x405U : 5U));
        }
    }
}

TEST_CASE("TxSROM arbitrary nametable arrangements reach CIRAM through the PPU",
          "[mappers][mmc3-variants][ppu]") {
    auto cart = variant_cartridge(118U);
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    for (Byte slot = 0U; slot < 4U; ++slot) {
        cart.write_prg(0x8000U, static_cast<Byte>(0x82U + slot));
        cart.write_prg(0x8001U, 0U);
    }
    variant_address(ppu, 0x2005U);
    ppu.write_register(7U, 0x11U);
    cart.write_prg(0x8001U, 0x80U); // Only fourth table now selects upper CIRAM.
    variant_address(ppu, 0x2C05U);
    ppu.write_register(7U, 0x22U);
    for (unsigned slot = 0U; slot < 4U; ++slot)
        REQUIRE(variant_read(ppu, static_cast<Address>(0x2005U + slot * 0x400U)) ==
                (slot == 3U ? 0x22U : 0x11U));
    REQUIRE(variant_read(ppu, 0x3C05U) == 0x22U);
    REQUIRE(ppu.vram()[5U] == 0x11U);
    REQUIRE(ppu.vram()[0x405U] == 0x22U);
}

TEST_CASE("TQROM selects ROM and eight aliased writable RAM pages before ROM wrapping",
          "[mappers][mmc3-variants]") {
    for (const auto size : {0x4000U, 0x8000U, 0x10000U, 0x20000U}) {
        auto mapper = variant_board(variant_header(119U), size);
        for (unsigned page = 0U; page < 8U; ++page) {
            variant_bank(*mapper, 2U, static_cast<Byte>(0x40U | page));
            mapper->write_chr(0x1005U, static_cast<Byte>(0xA0U | page));
        }
        for (unsigned bank = 0U; bank < 256U; ++bank) {
            variant_bank(*mapper, 2U, static_cast<Byte>(bank));
            if ((bank & 0x40U) != 0U) {
                REQUIRE(mapper->read_chr(0x1005U) == (0xA0U | (bank & 7U)));
            } else {
                const auto physical = (bank & 0x3FU) | ((bank & 0x80U) >> 1U);
                REQUIRE(mapper->read_chr(0x1005U) == physical % (size / 0x400U));
                mapper->write_chr(0x1005U, 0xFFU);
                REQUIRE(mapper->read_chr(0x1005U) == physical % (size / 0x400U));
            }
        }
        REQUIRE(mapper->battery_ram().empty());
        mapper->reset();
        variant_bank(*mapper, 2U, 0xC7U);
        REQUIRE(mapper->read_chr(0x1005U) == 0xA7U);
    }
}

TEST_CASE("TQROM aligns two-KiB CHR banks and swaps all eight windows",
          "[mappers][mmc3-variants]") {
    auto mapper = variant_board(variant_header(119U), 0x20000U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xBFFEU, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xA000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Byte page = 0U; page < 8U; ++page) {
        variant_bank(*mapper, 2U, static_cast<Byte>(0x40U | page));
        mapper->write_chr(0x1000U, static_cast<Byte>(0xA0U | page));
    }
    const std::array<Byte, 6> regs = {0x45U, 0x83U, 0xFFU, 0x03U, 0xC8U, 0x84U};
    const std::array<Byte, 8> expected = {0xA4U, 0xA5U, 66U, 67U, 0xA7U, 3U, 0xA0U, 68U};
    for (const Byte mode : {0U, 0x80U}) {
        for (Byte reg = 0U; reg < 6U; ++reg)
            variant_bank(*mapper, reg, regs[reg], mode);
        for (unsigned slot = 0U; slot < 8U; ++slot)
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x400U)) ==
                    expected[slot ^ (mode != 0U ? 4U : 0U)]);
    }
}

TEST_CASE("MMC3 variants share six-bit PRG banking, register aliases and IRQ progress",
          "[mappers][mmc3-variants][timing]") {
    for (const uint16_t id : {118U, 119U}) {
        auto mapper = variant_board(variant_header(id), 0x10000U, 0xA000U);
        variant_bank(*mapper, 6U, 0xC1U);
        REQUIRE(mapper->read_prg(0x8000U) == 1U); // Mask before modulo on five banks.
        variant_bank(*mapper, 7U, 0xFEU, 0x40U);
        REQUIRE(mapper->read_prg(0x8000U) == 3U);
        REQUIRE(mapper->read_prg(0xA000U) == 2U);
        REQUIRE(mapper->read_prg(0xC000U) == 1U);
        REQUIRE(mapper->read_prg(0xE000U) == 4U);
        mapper->write_prg(0xDFFEU, 2U);
        mapper->write_prg(0xDFFFU, 0U);
        mapper->write_prg(0xFFFFU, 0U);
        mapper->clock_irq_counter();
        mapper->clock_irq_counter();
        REQUIRE_FALSE(mapper->irq_pending());
        StateWriter saved;
        mapper->save_state(saved);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xFFFEU, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        StateReader reader(saved.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xC000U, 0U);
        mapper->acknowledge_irq();
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending()); // MMC3B zero latch repeats.
    }
}

TEST_CASE("TxSROM RAM gates, physical mirroring and NVRAM spans honor metadata",
          "[mappers][mmc3-variants][battery]") {
    auto header = variant_header(118U);
    REQUIRE_FALSE(variant_board(header)->maps_prg(0x6000U));
    header.prg_ram_size = 0x400U;
    header.prg_nvram_size = 0x400U;
    header.has_battery = true;
    auto mapper = variant_board(header);
    mapper->write_prg(0x6005U, 0x11U);
    mapper->write_prg(0x6405U, 0x22U);
    REQUIRE(mapper->read_prg(0x6805U) == 0x11U);
    REQUIRE(mapper->read_prg(0x6C05U) == 0x22U);
    REQUIRE(mapper->battery_ram().size() == 0x400U);
    REQUIRE(mapper->battery_ram()[5U] == 0x22U);
    mapper->write_prg(0xBFFFU, 0xC0U);
    mapper->write_prg(0x6405U, 0x33U);
    REQUIRE(mapper->read_prg(0x6405U) == 0x22U);
    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6005U) == 0x11U);
    REQUIRE(mapper->read_prg(0x6405U) == 0x22U);
    const std::array<Byte, 2> replacement = {0x44U, 0x55U};
    mapper->set_battery_ram(replacement);
    REQUIRE(mapper->read_prg(0x6400U) == 0x44U);
    REQUIRE(mapper->read_prg(0x6005U) == 0x11U);
    header.is_nes2 = false;
    header.prg_ram_size = 0x2000U;
    header.prg_nvram_size = 0U;
    REQUIRE(variant_board(header)->battery_ram().size() == 0x2000U);
    header.has_battery = false;
    REQUIRE_FALSE(variant_board(header)->maps_prg(0x6000U));
}

TEST_CASE("MMC3 variant CPU open bus, PPUDATA and rendering IRQ use production seams",
          "[mappers][mmc3-variants][integration]") {
    for (const uint16_t id : {118U, 119U}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(variant_cartridge(id), ""));
        emulator.reset();
        for (unsigned instruction = 0U; instruction < 40U; ++instruction)
            emulator.step_instruction();
        REQUIRE(emulator.read_cpu(0x10U) == (id == 119U ? 0x5AU : 3U));
        const auto before = emulator.save_state();
        emulator.step_frame();
        const auto after = emulator.save_state();
        REQUIRE(emulator.load_state(before));
        emulator.step_frame();
        REQUIRE(emulator.save_state() == after);
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        StateWriter board_state;
        variant_cartridge(id).save_state(board_state);
        const auto mapper_start = after.size() - tail.data().size() - board_state.data().size();
        for (const std::size_t field : {16384U, 16385U, 16394U, 16397U, 16398U}) {
            auto bad = after;
            bad[mapper_start + field] = 0xFFU;
            REQUIRE_FALSE(emulator.load_state(bad));
            REQUIRE(emulator.save_state() == after);
        }
        auto cart = variant_cartridge(id);
        MemoryBus bus;
        bus.connect_cartridge(&cart);
        REQUIRE_FALSE(cart.maps_prg(0x6000U));
        bus.write(0U, 0xA5U);
        REQUIRE(bus.read(0x6000U) == 0xA5U);
        cart.write_prg(0xC000U, 3U);
        cart.write_prg(0xC001U, 0U);
        cart.write_prg(0xE001U, 0U);
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        ppu.write_register(1U, 0x18U);
        ppu.step_dots(341U * 6U);
        REQUIRE(cart.irq_pending());
    }
}

TEST_CASE("TxSROM battery RAM persists through real save files and machine reloads",
          "[mappers][mmc3-variants][battery]") {
    const auto root = std::filesystem::temp_directory_path() /
                      ("mapperbus-txsrom-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup {
        std::filesystem::path path;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{root};
    std::filesystem::create_directories(root);
    const auto path = root / "118.sav";
    {
        Emulator writer;
        REQUIRE(writer.load_cartridge(variant_cartridge(118U, true), path.string()));
        writer.reset();
        writer.step_instruction();
        writer.step_instruction();
        REQUIRE(writer.read_cpu(0x6005U) == 0x66U);
        writer.flush_battery_ram();
    }
    const auto bytes = read_binary_file_capped(path.string(), 0x2000U, "battery");
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 0x2000U);
    REQUIRE((*bytes)[5U] == 0x66U);
    Emulator reader;
    REQUIRE(reader.load_cartridge(variant_cartridge(118U, true), path.string()));
    reader.reset();
    REQUIRE(reader.read_cpu(0x6005U) == 0x66U);
    const auto before = reader.save_state();
    REQUIRE(write_binary_file_atomic(path.string(), std::vector<Byte>(0x2001U), "battery"));
    REQUIRE_FALSE(reader.load_battery_ram(path.string()));
    REQUIRE(reader.save_state() == before);
}

TEST_CASE("MMC3 variant state rejects invalid flags and preserves raw chip-selection bits",
          "[mappers][mmc3-variants][state]") {
    for (const uint16_t id : {118U, 119U}) {
        auto mapper = variant_board(variant_header(id));
        variant_bank(*mapper, 2U, 0xC3U, 0x80U);
        mapper->write_chr(5U, 0x5AU);
        StateWriter saved;
        mapper->save_state(saved);
        mapper->reset();
        StateReader restored(saved.data());
        mapper->load_state(restored);
        REQUIRE(restored.ok());
        REQUIRE(restored.remaining() == 0U);
        REQUIRE(mapper->read_chr(5U) == (id == 119U ? 0x5AU : 0xC3U));
        // Fixed arrays, select, mode, eight banks, mirror, counter, latch, IRQ/RAM flags.
        for (const std::size_t offset : {16384U, 16385U, 16392U, 16393U, 16394U, 16397U, 16398U}) {
            auto corrupt = saved.data();
            corrupt[offset] = 0xFFU;
            StateReader invalid(corrupt);
            mapper->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(mapper->read_prg(0xE000U) == 63U);
        }
        auto truncated = saved.data();
        truncated.pop_back();
        StateReader short_reader(truncated);
        mapper->load_state(short_reader);
        REQUIRE_FALSE(short_reader.ok());
    }
}

TEST_CASE("MMC3 variant factories reject unsupported wiring and memory geometry",
          "[mappers][mmc3-variants][cartridge]") {
    register_builtin_mappers();
    auto valid = [](INesHeader header, std::size_t prg = 0x20000U, std::size_t chr = 0x10000U) {
        return MapperRegistry::instance()
            .create(header, std::vector<Byte>(prg), std::vector<Byte>(chr))
            .has_value();
    };
    for (const uint16_t id : {118U, 119U}) {
        auto header = variant_header(id);
        REQUIRE(valid(header));
        REQUIRE_FALSE(valid(header, 0x4000U));
        REQUIRE_FALSE(valid(header, 0x82000U));
        REQUIRE_FALSE(valid(header, 0x8001U));
        REQUIRE_FALSE(valid(header, 0x8000U, 0U));
        REQUIRE_FALSE(valid(header, 0x8000U, 0x401U));
        REQUIRE_FALSE(valid(header, 0x8000U, id == 118U ? 0x40400U : 0x20400U));
        header.submapper = 1U;
        REQUIRE_FALSE(valid(header));
        header.submapper = 0U;
        header.mirror_mode = MirrorMode::FourScreen;
        REQUIRE_FALSE(valid(header));
        header.mirror_mode = MirrorMode::Vertical;
        header.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(valid(header));
        header.chr_nvram_size = 0U;
        header.chr_ram_size = 0x1000U;
        REQUIRE_FALSE(valid(header));
        header.chr_ram_size = 0U;
        if (id == 119U) {
            header.chr_ram_size = 0x2000U;
            REQUIRE(valid(header));
            header.prg_ram_size = 0x2000U;
            REQUIRE_FALSE(valid(header));
        } else {
            header.prg_ram_size = 0x4000U;
            REQUIRE_FALSE(valid(header));
            header.prg_ram_size = 0x1800U;
            REQUIRE_FALSE(valid(header));
            header.prg_ram_size = 0U;
            header.prg_nvram_size = 0x2000U;
            REQUIRE_FALSE(valid(header));
            header.has_battery = true;
            REQUIRE(valid(header));
        }
    }
}

TEST_CASE("Mapper 4 retains its original snapshot layout and header-independent RAM",
          "[mappers][mmc3-variants][state]") {
    Mmc3 mapper(variant_header(4U), variant_rom(0x80000U, 0x2000U), {});
    mapper.write_prg(0x6005U, 0xA5U);
    mapper.write_chr(5U, 0x5AU);
    variant_bank(mapper, 2U, 0xC3U, 0x80U);
    StateWriter saved;
    mapper.save_state(saved);
    REQUIRE(saved.data().size() == 16452U);
    REQUIRE(saved.data()[5U] == 0xA5U);
    REQUIRE(saved.data()[8197U] == 0x5AU);
    REQUIRE(saved.data()[16384U] == 2U);
    REQUIRE(saved.data()[16386U] == 1U);
    REQUIRE(mapper.read_chr(5U) == 0x5AU);
    StateReader reader(saved.data());
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(mapper.read_chr(5U) == 0x5AU);
}

} // namespace
