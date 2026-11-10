#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header() {
    INesHeader h{};
    h.mapper_number = 262U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::FourScreen;
    h.chr_ram_size = 8192U;
    return h;
}
std::vector<Byte> prg(std::size_t size = 0x40000U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 8192U);
    return data;
}
std::vector<Byte> chr(std::size_t size = 0x80000U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 1024U);
    for (std::size_t page = 0U; page < size / 1024U; ++page)
        data[page * 1024U + 1U] = static_cast<Byte>(0x40U + (page >> 8U));
    return data;
}
std::unique_ptr<Mapper> board(std::size_t prg_size = 0x40000U, std::size_t chr_size = 0x80000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(header(), prg(prg_size), chr(chr_size));
    REQUIRE(r);
    return std::move(*r);
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    m.write_prg(0x9FFEU, static_cast<Byte>(reg | mode));
    m.write_prg(0x9FFFU, value);
}
void ppu_address(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
void ppu_write(Ppu& ppu, Address addr, Byte value) {
    ppu_address(ppu, addr);
    ppu.write_register(7U, value);
}
Byte ppu_read(Ppu& ppu, Address addr) {
    ppu_address(ppu, addr);
    static_cast<void>(ppu.read_register(7U));
    return ppu.read_register(7U);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
std::vector<Byte> image(bool jumper = false, bool defaults = false) {
    auto p = prg();
    const auto c = chr();
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 16U;
    rom[5] = 64U;
    rom[6] = 0x68U;
    rom[7] = 8U;
    rom[8] = 1U;
    if (!defaults)
        rom[11] = 7U;
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address a, Byte v) {
        code.insert(code.end(),
                    {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    };
    const auto expect = [&code](Address a, Byte v) {
        code.insert(
            code.end(),
            {0xADU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U), 0xC9U, v, 0xD0U, 0xFEU});
    };
    const auto set_bank = [&store](Byte r, Byte v) {
        store(0x8000U, r);
        store(0x8001U, v);
    };
    const auto address = [&store](Address a) {
        store(0x2006U, static_cast<Byte>(a >> 8U));
        store(0x2006U, static_cast<Byte>(a));
    };
    const auto pwrite = [&address, &store](Address a, Byte v) {
        address(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &address, &expect](Address a, Byte v) {
        address(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x11U, 0U);
    store(0x12U, 0U);
    code.insert(code.end(),
                {0xADU,
                 0U,
                 0x41U,
                 0x29U,
                 0x80U,
                 0x85U,
                 0x14U,
                 0xC9U,
                 static_cast<Byte>(jumper ? 0x80U : 0U),
                 0xD0U,
                 0xFEU});
    set_bank(6U, 3U);
    set_bank(7U, 5U);
    expect(0x8000U, 3U);
    expect(0xA000U, 5U);
    expect(0xC000U, 30U);
    store(0x8000U, 0x46U);
    expect(0x8000U, 30U);
    expect(0xC000U, 3U);
    constexpr std::array<Byte, 6> kRegs = {2U, 6U, 9U, 10U, 11U, 12U};
    for (Byte r = 0U; r < 6U; ++r)
        set_bank(r, kRegs[r]);
    store(0x4100U, 8U);
    pread(1U, 0x41U);
    pread(0x801U, 0x40U);
    pread(0x1001U, 0x40U);
    pread(0x1801U, 0x40U);
    store(0x4100U, 1U);
    pread(1U, 0x40U);
    pread(0x1001U, 0x41U);
    store(0x8000U, 0x80U);
    pread(0U, 9U);
    pread(1U, 0x40U);
    pread(0x1000U, 2U);
    pread(0x1001U, 0x41U);
    store(0x8000U, 0U);
    store(0x4100U, 0x40U);
    set_bank(2U, 7U);
    pwrite(0x1000U, 0x73U);
    set_bank(2U, 0U);
    pwrite(0x1000U, 0x64U);
    set_bank(2U, 7U);
    pread(0x1000U, 0x73U);
    store(0x4100U, 0U);
    pwrite(0x1000U, 0U);
    pread(0x1000U, 7U);
    store(0x4100U, 0x4FU);
    pread(0x1000U, 0x73U);
    for (unsigned page = 0U; page < 4U; ++page)
        pwrite(static_cast<Address>(0x2000U + page * 1024U), static_cast<Byte>(0x90U + page));
    store(0xA000U, 1U);
    for (unsigned page = 0U; page < 4U; ++page)
        pread(static_cast<Address>(0x2000U + page * 1024U), static_cast<Byte>(0x90U + page));
    store(0x6100U, 8U); // Mode alias in the CPU cartridge window, with no WRAM.
    pread(1U, 0x41U);
    store(0x4100U, 0x40U);
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xADU, 0x11U, 0U, 0xC9U, 0xD3U, 0xD0U, 0xF9U});
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x800U);
    std::ranges::copy(code, p.begin() + 0x3E000U);
    constexpr std::array<Byte, 12> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0xA9U, 0xD3U, 0x85U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(kIrq, p.begin() + 0x3E800U);
    for (std::size_t v = 0x3FFFAU; v < 0x40000U; v += 2U) {
        p[v] = 0U;
        p[v + 1U] = v == 0x3FFFCU ? 0xE0U : 0xE8U;
    }
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(bool jumper = false, bool defaults = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(jumper, defaults));
    REQUIRE(r);
    REQUIRE(r->configure_board_jumpers(jumper ? 1U : 0U));
    return std::move(*r);
}
TEST_CASE("Sachen 262 outer CHR pins follow physical PPU regions through inversion",
          "[mappers][sachen262]") {
    auto m = board();
    constexpr std::array<Byte, 6> kRegs = {3U, 7U, 9U, 10U, 11U, 12U};
    constexpr std::array<Byte, 8> kInner = {2U, 3U, 6U, 7U, 9U, 10U, 11U, 12U};
    constexpr std::array<unsigned, 4> kBits = {3U, 2U, 0U, 1U};
    for (Byte r = 0U; r < 6U; ++r)
        bank(*m, r, kRegs[r]);
    for (Byte mode : {0U, 0x80U}) {
        m->write_prg(0x8000U, mode);
        for (Byte outer = 0U; outer < 16U; ++outer) {
            m->write_expansion(0x4100U, outer);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto a = static_cast<Address>(slot * 1024U);
                REQUIRE(m->read_chr(a) == kInner[slot ^ ((mode & 0x80U) != 0U ? 4U : 0U)]);
                REQUIRE(m->read_chr(static_cast<Address>(a + 1U)) ==
                        0x40U + ((static_cast<unsigned>(outer) >> kBits[slot / 2U]) & 1U));
            }
        }
    }
    auto small = board(0x8000U, 0x30000U);
    bank(*small, 2U, 0xFFU);
    small->write_expansion(0x4100U, 1U);
    REQUIRE(small->read_chr(0x1000U) == 0x7FU); // Page 511 wraps to 127 in 192 KiB.
}
TEST_CASE("Sachen 262 retains banked pattern RAM independently of ROM and four-screen VRAM",
          "[mappers][sachen262]") {
    auto m = board();
    m->write_expansion(0x4100U, 0x4FU);
    for (Byte page = 0U; page < 8U; ++page) {
        bank(*m, 2U, static_cast<Byte>(0xF8U | page));
        m->write_chr(0x1000U, static_cast<Byte>(0xA0U + page));
    }
    for (Byte page = 0U; page < 8U; ++page) {
        bank(*m, 2U, page);
        REQUIRE(m->read_chr(0x1000U) == 0xA0U + page);
    }
    bank(*m, 0U, 7U);
    REQUIRE(m->read_chr(0U) == 0xA6U);
    REQUIRE(m->read_chr(0x400U) == 0xA7U);
    m->write_prg(0x8000U, 0x80U);
    REQUIRE(m->read_chr(0x1000U) == 0xA6U);
    bank(*m, 2U, 7U);
    m->write_expansion(0x4100U, 0U);
    m->write_chr(0x1000U, 0xEEU);
    REQUIRE(m->read_chr(0x1000U) == 7U);
    m->write_expansion(0x4100U, 0xF0U); // Unused bits 7,5,4 are ignored.
    REQUIRE(m->read_chr(0x1000U) == 0xA7U);
    REQUIRE(m->write_nametable(0x2000U, 0x51U));
    REQUIRE(m->read_chr(0x1000U) == 0xA7U);
    m->reset();
    bank(*m, 2U, 7U);
    m->write_expansion(0x4100U, 0x40U);
    REQUIRE(m->read_chr(0x1000U) == 0xA7U);
    REQUIRE(m->read_nametable(0x2000U) == 0x51U);
    REQUIRE(m->battery_ram().empty());
}
TEST_CASE("Sachen 262 four-screen nametables survive MMC3 writes, mode changes and palette access",
          "[mappers][sachen262]") {
    auto c = cartridge();
    Ppu ppu;
    ppu.connect_cartridge(&c);
    for (unsigned page = 0U; page < 4U; ++page)
        ppu_write(
            ppu, static_cast<Address>(0x2005U + page * 1024U), static_cast<Byte>(0x90U + page));
    ppu_write(ppu, 0x3F00U, 0x2AU);
    for (Byte mode : {0U, 0x40U, 0x4FU}) {
        c.write_expansion(0x4100U, mode);
        for (Byte value : {0U, 1U}) {
            c.write_prg(0xA000U, value);
            REQUIRE(c.mirror_mode() == MirrorMode::FourScreen);
            for (unsigned page = 0U; page < 4U; ++page) {
                REQUIRE(ppu_read(ppu, static_cast<Address>(0x2005U + page * 1024U)) ==
                        0x90U + page);
                REQUIRE(ppu_read(ppu, static_cast<Address>(0x3005U + page * 1024U)) ==
                        0x90U + page);
            }
        }
    }
    ppu_write(ppu, 0x3005U, 0xB7U);
    REQUIRE(ppu_read(ppu, 0x2005U) == 0xB7U);
    ppu_address(ppu, 0x3F00U);
    REQUIRE((ppu.read_register(7U) & 0x3FU) == 0x2AU);
}
TEST_CASE("Sachen 262 title jumper drives only D7 on expansion and cartridge aliases",
          "[mappers][sachen262]") {
    auto m = board();
    for (Address addr : {0x4100U, 0x41FFU, 0x5FFFU, 0x6100U, 0x7FFFU}) {
        REQUIRE(m->maps_expansion(addr));
        REQUIRE(m->expansion_read_mask(addr) == 0x80U);
    }
    REQUIRE_FALSE(m->maps_expansion(0x4000U));
    REQUIRE_FALSE(m->maps_expansion(0x4200U));
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    REQUIRE_FALSE(m->maps_prg(0x4100U));
    REQUIRE(m->maps_prg(0x6100U));
    REQUIRE(m->prg_read_mask(0x6100U) == 0x80U);
    REQUIRE(m->read_prg(0x6100U) == 0U);
    REQUIRE(m->configure_board_jumpers(1U));
    REQUIRE_FALSE(m->configure_board_jumpers(2U));
    REQUIRE(m->read_prg(0x6100U) == 0x80U);
    m->reset();
    REQUIRE(m->read_expansion(0x4100U) == 0x80U);
    bank(*m, 2U, 0U);
    m->write_prg(0xA001U, 0U);
    m->write_prg(0x6100U, 1U);
    REQUIRE(m->read_chr(0x1001U) == 0x41U);
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (Byte jumper : {0U, 1U}) {
        REQUIRE(c.configure_board_jumpers(jumper));
        for (Address addr : {0x4100U, 0x51FFU, 0x6100U, 0x7FFFU}) {
            bus.write(0x10U, 0x5AU);
            REQUIRE(bus.read(addr) == (jumper != 0U ? 0xDAU : 0x5AU));
        }
    }
    bus.write(0x10U, 0xA5U);
    REQUIRE(bus.read(0x6000U) == 0xA5U);
    bus.write(0x6100U, 1U);
    REQUIRE(c.read_chr(0x1001U) == 0x41U);
}
TEST_CASE("Sachen 262 retains normal MMC3 PRG masks, swaps and IRQ progression",
          "[mappers][sachen262]") {
    auto m = board(0x80000U);
    bank(*m, 6U, 0xC3U);
    bank(*m, 7U, 0xFFU);
    for (Byte outer : {0U, 0x4FU}) {
        m->write_expansion(0x4100U, outer);
        for (Byte mode : {0U, 0x40U}) {
            m->write_prg(0x8000U, mode);
            REQUIRE(m->read_prg(0x8000U) == (mode != 0U ? 62U : 3U));
            REQUIRE(m->read_prg(0xA000U) == 63U);
            REQUIRE(m->read_prg(0xC000U) == (mode != 0U ? 3U : 62U));
            REQUIRE(m->read_prg(0xE000U) == 63U);
        }
    }
    m->write_prg(0xDFFEU, 2U);
    m->write_prg(0xDFFFU, 0U);
    m->write_prg(0xFFFFU, 0U);
    m->clock_irq_counter();
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xFFFEU, 0U);
    REQUIRE_FALSE(m->irq_pending());
}
TEST_CASE("Sachen 262 snapshots retain all RAM, graphics selection, jumper and partial IRQ",
          "[mappers][sachen262]") {
    auto m = board();
    REQUIRE(m->configure_board_jumpers(1U));
    m->write_expansion(0x4100U, 0x40U);
    bank(*m, 2U, 7U);
    m->write_chr(0x1000U, 0xC7U);
    REQUIRE(m->write_nametable(0x2C05U, 0xB3U));
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(copy->read_chr(0x1000U) == 0xC7U);
    REQUIRE(copy->read_nametable(0x2C05U) == 0xB3U);
    REQUIRE(copy->read_expansion(0x4100U) == 0x80U);
    copy->clock_irq_counter();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    for (std::size_t offset : {16384UZ, 16394UZ, 16399UZ, saved.size() - 2U, saved.size() - 1U}) {
        auto bad = saved;
        bad[offset] = 0xFFU;
        StateReader invalid(bad);
        copy->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        static_cast<void>(copy->read_nametable(0x2000U));
    }
    StateReader short_state(std::span{saved}.first(saved.size() - 1U));
    copy->load_state(short_state);
    REQUIRE_FALSE(short_state.ok());
}
TEST_CASE("Sachen 262 geometry distinguishes pattern RAM from fixed VRAM", "[mappers][sachen262]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 14U; ++kind) {
        auto h = header();
        std::size_t p = 0x40000U, c = 0x80000U;
        switch (kind) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.submapper = 1U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 4096U;
            break;
        case 4U:
            h.chr_ram_size = 16384U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.prg_ram_size = 8192U;
            break;
        case 7U:
            h.prg_nvram_size = 8192U;
            break;
        case 8U:
            c = 0U;
            break;
        case 9U:
            p = 16384U;
            break;
        case 10U:
            p = 0x80000U + 8192U;
            break;
        case 11U:
            c = 0x80000U + 1024U;
            break;
        case 12U:
            c = 8193U;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
    for (MirrorMode mirror :
         {MirrorMode::Horizontal, MirrorMode::Vertical, MirrorMode::FourScreen}) {
        auto h = header();
        h.mirror_mode = mirror;
        h.chr_ram_size = 0U;
        auto r = MapperRegistry::instance().create(h, prg(0x8000U), chr(8192U));
        REQUIRE(r);
        REQUIRE((*r)->mirror_mode() == MirrorMode::FourScreen);
    }
}
TEST_CASE("Sachen 262 original CPU program checks banks, jumper, VRAM and IRQ before replay",
          "[mappers][sachen262]") {
    for (bool jumper : {false, true}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(cartridge(jumper, jumper), ""));
        emulator.reset();
        emulator.step_frame();
        REQUIRE(emulator.read_cpu(0x12U) == 0xC7U);
        REQUIRE(emulator.read_cpu(0x11U) == 0xD3U);
        REQUIRE(emulator.read_cpu(0x14U) == (jumper ? 0x80U : 0U));
        const auto early = emulator.save_state();
        emulator.step_frame();
        const auto future = emulator.save_state();
        REQUIRE(emulator.load_state(early));
        emulator.step_frame();
        REQUIRE((emulator.save_state() == future));
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        auto bad = early;
        bad[bad.size() - tail.data().size() - 1U] = 2U;
        const auto before = emulator.save_state();
        REQUIRE_FALSE(emulator.load_state(bad));
        REQUIRE((emulator.save_state() == before));
    }
}
} // namespace
