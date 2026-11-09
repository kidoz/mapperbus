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
INesHeader lrog_header() {
    INesHeader header{};
    header.mapper_number = 77U;
    header.is_nes2 = true;
    header.mirror_mode = MirrorMode::FourScreen;
    header.chr_ram_size = 0x2000U;
    return header;
}
std::vector<Byte> lrog_rom(std::size_t size, std::size_t bank) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        rom[offset] = static_cast<Byte>(offset / bank);
    return rom;
}
std::vector<Byte> lrog_prg() {
    auto rom = lrog_rom(0x80000U, 0x8000U);
    for (std::size_t bank = 0U; bank < rom.size(); bank += 0x8000U) {
        rom[bank + 0x100U] = 0xFFU;
        rom[bank + 0x101U] = 0U;
        rom[bank + 0x102U] = 0xA5U;
    }
    return rom;
}
std::unique_ptr<Mapper> lrog_board(INesHeader header = lrog_header()) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(header, lrog_prg(), lrog_rom(0x8000U, 0x800U));
    REQUIRE(result);
    return std::move(*result);
}
std::vector<Byte> lrog_program() {
    std::vector<Byte> code;
    const auto append = [&](std::initializer_list<Byte> bytes) {
        code.insert(code.end(), bytes.begin(), bytes.end());
    };
    const auto address = [&](Address addr) {
        append({0xA9U,
                static_cast<Byte>(addr >> 8U),
                0x8DU,
                6U,
                0x20U,
                0xA9U,
                static_cast<Byte>(addr),
                0x8DU,
                6U,
                0x20U});
    };
    const auto write = [&](Address addr, Byte value) {
        address(addr);
        append({0xA9U, value, 0x8DU, 7U, 0x20U});
    };
    const auto read = [&](Address addr, Byte result) {
        address(addr);
        append({0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x85U, result});
    };
    // Original 6502 code is duplicated in every 32 KiB bank, so selecting
    // bank 3 cannot unmap the instruction stream or the reset vectors.
    write(0x0800U, 0xA5U);
    write(0x2000U, 0x5AU);
    write(0x2800U, 0x3CU);
    append({0xA9U, 0x53U, 0x8DU, 0U, 0x81U});
    read(0U, 0x10U);
    read(0x0800U, 0x11U);
    read(0x2000U, 0x12U);
    read(0x2800U, 0x13U);
    read(0x30A7U, 0x14U);
    const auto loop = static_cast<Address>(0x8000U + code.size());
    append({0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x100U);
    return code;
}
Cartridge lrog_cartridge(bool program = false) {
    auto prg = lrog_prg();
    const auto code = program ? lrog_program() : std::vector<Byte>{0x4CU, 0U, 0x80U};
    for (std::size_t bank = 0U; bank < prg.size(); bank += 0x8000U) {
        std::copy(code.begin(), code.end(), prg.begin() + static_cast<std::ptrdiff_t>(bank));
        for (std::size_t vector = 0x7FFAU; vector < 0x8000U; vector += 2U) {
            prg[bank + vector] = 0U;
            prg[bank + vector + 1U] = 0x80U;
        }
    }
    std::vector<Byte> image(16U + prg.size() + 0x8000U, 0U);
    image[0U] = 'N';
    image[1U] = 'E';
    image[2U] = 'S';
    image[3U] = 0x1AU;
    image[4U] = 32U;
    image[5U] = 4U;
    image[6U] = 0xD8U;
    image[7U] = 0x48U;
    image[11U] = 7U;
    std::copy(prg.begin(), prg.end(), image.begin() + 16U);
    const auto chr = lrog_rom(0x8000U, 0x800U);
    std::copy(chr.begin(), chr.end(), image.begin() + 16U + prg.size());
    register_builtin_mappers();
    auto result = Cartridge::from_data(image);
    REQUIRE(result);
    return std::move(*result);
}
void lrog_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
void lrog_write(Ppu& ppu, Address addr, Byte value) {
    lrog_address(ppu, addr);
    ppu.write_register(7U, value);
}
Byte lrog_read(Ppu& ppu, Address addr) {
    lrog_address(ppu, addr);
    (void)ppu.read_register(7U);
    // Read the same address again, including the $3EFF/palette boundary.
    lrog_address(ppu, addr);
    return ppu.read_register(7U);
}
} // namespace

TEST_CASE("LROG017 banks 32-KiB PRG and two-KiB CHR with pre-write bus conflicts",
          "[mappers][lrog017]") {
    auto mapper = lrog_board();
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        const auto value = static_cast<Byte>(1U << bit);
        mapper->write_prg(0x8100U, value);
        REQUIRE(mapper->read_prg(0x8000U) == (value & 15U));
        REQUIRE(mapper->read_prg(0xFFFFU) == (value & 15U));
        REQUIRE(mapper->read_chr(0x7FFU) == (value >> 4U));
    }
    mapper->write_prg(0x8100U, 0xFFU);
    REQUIRE(mapper->read_prg(0x8000U) == 15U);
    REQUIRE(mapper->read_chr(0U) == 15U);
    mapper->write_prg(0x8102U, 0xFFU);
    REQUIRE(mapper->read_prg(0x8000U) == 5U);
    REQUIRE(mapper->read_chr(0U) == 10U);
    mapper->write_prg(0x8101U, 0xFFU);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_chr(0U) == 0U);
    mapper->write_prg(0x6000U, 0xFFU);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("LROG017 preserves every byte of its shared eight-KiB cartridge VRAM across reset",
          "[mappers][lrog017][state]") {
    auto mapper = lrog_board();
    for (unsigned addr = 0x800U; addr < 0x2000U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>((addr >> 3U) ^ addr));
    for (unsigned offset = 0U; offset < 0x800U; ++offset)
        REQUIRE(mapper->write_nametable(static_cast<Address>(0x2000U + offset),
                                        static_cast<Byte>(offset ^ 0xA5U)));
    mapper->write_prg(0x8100U, 0xF7U);
    mapper->write_chr(0x7FFU, 0xFFU);
    REQUIRE(mapper->read_chr(0x7FFU) == 15U);
    StateWriter saved;
    mapper->save_state(saved);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_chr(0U) == 0U);
    for (unsigned addr = 0x800U; addr < 0x2000U; ++addr)
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                static_cast<Byte>((addr >> 3U) ^ addr));
    for (unsigned offset = 0U; offset < 0x800U; ++offset)
        REQUIRE(mapper->read_nametable(static_cast<Address>(0x2000U + offset)) ==
                static_cast<Byte>(offset ^ 0xA5U));
    mapper->write_chr(0x800U, 0U);
    mapper->write_nametable(0x2000U, 0U);
    StateReader reader(saved.data());
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    StateWriter restored;
    mapper->save_state(restored);
    REQUIRE(restored.data() == saved.data());
    for (const std::size_t field : {0U, 1U}) {
        auto invalid = saved.data();
        invalid[field] = 16U;
        StateReader bad(invalid);
        mapper->load_state(bad);
        REQUIRE_FALSE(bad.ok());
    }
    StateReader truncated(std::span(saved.data()).first(saved.data().size() - 1U));
    mapper->load_state(truncated);
    REQUIRE_FALSE(truncated.ok());
}

TEST_CASE("LROG017 routes the first nametable pair to cartridge RAM and the second to CIRAM",
          "[mappers][lrog017][ppu]") {
    auto cart = lrog_cartridge();
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    REQUIRE(cart.mirror_mode() == MirrorMode::FourScreen);
    for (unsigned table = 0U; table < 4U; ++table) {
        lrog_write(
            ppu, static_cast<Address>(0x2000U + table * 0x400U), static_cast<Byte>(0x10U + table));
        lrog_write(
            ppu, static_cast<Address>(0x23C0U + table * 0x400U), static_cast<Byte>(0x20U + table));
    }
    REQUIRE(cart.read_nametable(0x2000U) == 0x10U);
    REQUIRE(cart.read_nametable(0x2400U) == 0x11U);
    REQUIRE_FALSE(cart.read_nametable(0x2800U));
    REQUIRE_FALSE(cart.read_nametable(0x2C00U));
    REQUIRE(ppu.vram()[0U] == 0x12U);
    REQUIRE(ppu.vram()[0x400U] == 0x13U);
    for (unsigned table = 0U; table < 4U; ++table) {
        REQUIRE(lrog_read(ppu, static_cast<Address>(0x2000U + table * 0x400U)) == 0x10U + table);
        REQUIRE(lrog_read(ppu, static_cast<Address>(0x23C0U + table * 0x400U)) == 0x20U + table);
    }
    cart.write_prg(0x8100U, 0xF7U);
    for (unsigned table = 0U; table < 4U; ++table)
        REQUIRE(lrog_read(ppu, static_cast<Address>(0x2000U + table * 0x400U)) == 0x10U + table);
    lrog_write(ppu, 0x0800U, 0xA5U);
    lrog_write(ppu, 0x1FFFU, 0x5AU);
    REQUIRE(lrog_read(ppu, 0x0800U) == 0xA5U);
    REQUIRE(lrog_read(ppu, 0x1FFFU) == 0x5AU);
    REQUIRE(lrog_read(ppu, 0x2000U) == 0x10U); // Pattern and nametable VRAM do not alias.
}

TEST_CASE("LROG017 leaves upper nametables undriven and keeps palette buffering separate",
          "[mappers][lrog017][ppu]") {
    auto cart = lrog_cartridge();
    Ppu ppu;
    ppu.connect_cartridge(&cart);
    for (const Address addr : {0x3000U, 0x30FFU, 0x3100U, 0x37FFU, 0x3800U, 0x3EFFU}) {
        const auto below = static_cast<Address>(addr - 0x1000U);
        lrog_write(ppu, below, 0x5AU);
        lrog_write(ppu, addr, 0xA5U);
        REQUIRE(lrog_read(ppu, below) == 0x5AU);
        REQUIRE(lrog_read(ppu, addr) == static_cast<Byte>(addr));
    }
    lrog_write(ppu, 0x0800U, 0x5AU);
    REQUIRE(lrog_read(ppu, 0x0800U) == 0x5AU);
    lrog_address(ppu, 0x30A7U);
    REQUIRE(ppu.read_register(7U) == 0x5AU); // Return the previous PPUDATA buffer first.
    REQUIRE(ppu.read_register(7U) == 0xA7U);
    REQUIRE(ppu.read_register(0U) == 0xA7U); // I/O latch is distinct from the new $A8 read buffer.
    lrog_write(ppu, 0x2F00U, 0x5BU);
    lrog_write(ppu, 0x3F00U, 0x2DU);
    lrog_address(ppu, 0x3F00U);
    REQUIRE(ppu.read_register(7U) == 0x2DU);
    lrog_address(ppu, 0x3000U);
    REQUIRE(ppu.read_register(7U) == 0x5BU); // Palette refill came from CIRAM $2F00.
    REQUIRE(lrog_read(ppu, 0x2F00U) == 0x5BU);
    REQUIRE(lrog_read(ppu, 0x3F10U) == 0x2DU); // Palette mirror still works.
}

TEST_CASE("LROG017 renders all four nametables with ROM and RAM background tiles",
          "[mappers][lrog017][ppu][integration]") {
    auto cart = lrog_cartridge();
    std::array<uint32_t, 4U> colors{};
    for (unsigned selected = 0U; selected < 4U; ++selected) {
        Ppu ppu;
        ppu.connect_cartridge(&cart);
        ppu.reset();
        // ROM bank zero supplies blank tile 0. RAM tile 128 is solid color 2.
        for (unsigned row = 0U; row < 8U; ++row) {
            lrog_write(ppu, static_cast<Address>(0x0800U + row), 0U);
            lrog_write(ppu, static_cast<Address>(0x0808U + row), 0xFFU);
        }
        for (unsigned table = 0U; table < 4U; ++table) {
            lrog_address(ppu, static_cast<Address>(0x2000U + table * 0x400U));
            for (unsigned tile = 0U; tile < 960U; ++tile)
                ppu.write_register(7U, (table & 1U) != 0U ? 128U : 0U);
        }
        lrog_write(ppu, 0x3F00U, 0x0FU);
        lrog_write(ppu, 0x3F02U, 0x16U);
        (void)ppu.read_register(2U);
        ppu.write_register(0U, static_cast<Byte>(selected));
        ppu.write_register(5U, 0U);
        ppu.write_register(5U, 0U);
        ppu.write_register(1U, 0x0AU);
        // The first pre-render copies the prepared scroll into the live address.
        ppu.step_dots(2U * 341U * 262U);
        colors[selected] = ppu.frame_buffer().pixels[16U * 256U + 16U];
    }
    REQUIRE(colors[0U] != colors[1U]);
    REQUIRE(colors[0U] == colors[2U]);
    REQUIRE(colors[1U] == colors[3U]);
}

TEST_CASE("LROG017 CPU/PPU accesses replay cartridge RAM, CIRAM and buffered unmapped reads",
          "[mappers][lrog017][state][integration]") {
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(lrog_cartridge(true), {}));
    emulator.reset();
    REQUIRE(emulator.cpu().pc() == 0x8000U);
    for (unsigned i = 0U; i < 20U; ++i)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0x8200U) == 3U);
    const auto saved = emulator.save_state();
    const auto finish = [&] {
        for (unsigned i = 0U; i < 60U; ++i)
            emulator.step_instruction();
        const std::array<Byte, 5U> expected = {5U, 0xA5U, 0x5AU, 0x3CU, 0xA7U};
        for (unsigned i = 0U; i < expected.size(); ++i)
            REQUIRE(emulator.read_cpu(static_cast<Address>(0x10U + i)) == expected[i]);
    };
    finish();
    const auto expected = emulator.save_state();
    REQUIRE(emulator.load_state(saved));
    finish();
    REQUIRE(emulator.save_state() == expected);
    REQUIRE_FALSE(emulator.load_state(std::span(saved).first(saved.size() / 2U)));
    REQUIRE(emulator.save_state() == expected);
    // Corrupt the mapper's first bank field and verify whole-machine rollback.
    StateWriter tail;
    emulator.expansion_controller().save_state(tail);
    emulator.additional_expansion_controller().save_state(tail);
    auto mapper = lrog_board();
    StateWriter payload;
    mapper->save_state(payload);
    auto invalid = saved;
    invalid[invalid.size() - tail.data().size() - payload.data().size()] = 16U;
    REQUIRE_FALSE(emulator.load_state(invalid));
    REQUIRE(emulator.save_state() == expected);
}

TEST_CASE("LROG017 factory bounds physical memories and ignores legacy implied CPU RAM",
          "[mappers][lrog017]") {
    register_builtin_mappers();
    const auto create = [](INesHeader header, std::size_t prg, std::size_t chr) {
        return MapperRegistry::instance().create(
            header, std::vector<Byte>(prg), std::vector<Byte>(chr));
    };
    auto header = lrog_header();
    REQUIRE(create(header, 0x8000U, 0x800U));
    REQUIRE_FALSE(create(header, 0x4000U, 0x800U));
    REQUIRE_FALSE(create(header, 0x100000U, 0x800U));
    REQUIRE_FALSE(create(header, 0x8001U, 0x800U));
    REQUIRE_FALSE(create(header, 0x8000U, 0U));
    REQUIRE_FALSE(create(header, 0x8000U, 0x10000U));
    REQUIRE_FALSE(create(header, 0x8000U, 0x801U));
    header.submapper = 1U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x800U));
    header = lrog_header();
    header.prg_ram_size = 0x2000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x800U));
    header = lrog_header();
    header.has_battery = true;
    REQUIRE_FALSE(create(header, 0x8000U, 0x800U));
    header = lrog_header();
    header.chr_ram_size = 0x1000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x800U));
    header = lrog_header();
    header.chr_ram_size = 0x4000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x800U));
    header = lrog_header();
    header.chr_nvram_size = 0x2000U;
    REQUIRE_FALSE(create(header, 0x8000U, 0x800U));
    for (const std::size_t declared : {0U, 0x1800U, 0x2000U}) {
        header = lrog_header();
        header.chr_ram_size = declared;
        auto mandatory = create(header, 0x8000U, 0x800U);
        REQUIRE(mandatory);
        (*mandatory)->write_chr(0x1FFFU, 0xA5U);
        REQUIRE((*mandatory)->read_chr(0x1FFFU) == 0xA5U);
    }
    for (const MirrorMode mirror : {MirrorMode::Vertical, MirrorMode::Horizontal}) {
        header = lrog_header();
        header.mirror_mode = mirror;
        header.is_nes2 = false;
        header.prg_ram_size = 0x2000U;
        header.chr_ram_size = 0U;
        auto legacy = create(header, 0x8000U, 0x800U);
        REQUIRE(legacy);
        REQUIRE((*legacy)->mirror_mode() == MirrorMode::FourScreen);
        REQUIRE_FALSE((*legacy)->maps_prg(0x6000U));
    }
}
