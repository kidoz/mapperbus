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
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header() {
    INesHeader h{};
    h.mapper_number = 513U;
    h.is_nes2 = true;
    h.has_battery = true;
    h.chr_nvram_size = 32768U;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> banks(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 8192U);
    return data;
}
std::unique_ptr<Mapper> board(std::size_t size = 0x180000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(header(), banks(size), {});
    REQUIRE(r);
    return std::move(*r);
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    m.write_prg(0x9FFEU, static_cast<Byte>(reg | mode));
    m.write_prg(0x9FFFU, value);
}
void ppu_addr(Ppu& ppu, Address addr) {
    ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
    ppu.write_register(6U, static_cast<Byte>(addr));
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
struct Scratch {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("mapperbus-513-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() {
        std::filesystem::create_directories(path);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
std::vector<Byte> image(std::size_t size = 0x180000U, bool defaults = false) {
    std::vector<Byte> rom(16U + size);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = static_cast<Byte>(size / 16384U);
    rom[6] = 0x13U;
    rom[7] = 8U;
    rom[8] = 2U;
    if (!defaults)
        rom[11] = 0x90U;
    const auto prg = banks(size);
    std::ranges::copy(prg, rom.begin() + 16);
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
    store(0x11U, 0U);
    store(0x12U, 0U);
    set_bank(2U, 31U);
    address(0x1003U);
    code.insert(code.end(), {0xADU, 7U, 0x20U, 0xADU, 7U, 0x20U, 0x18U, 0x69U, 1U, 0x85U, 0x10U});
    address(0x1003U);
    code.insert(code.end(), {0xA5U, 0x10U, 0x8DU, 7U, 0x20U});
    set_bank(2U, 0U);
    pwrite(0x1000U, 0xA5U);
    set_bank(2U, 31U);
    pwrite(0x1000U, 0x5AU);
    set_bank(6U, 3U);
    set_bank(7U, 5U);
    constexpr std::array<Byte, 6> kBanks = {0U, 2U, 4U, 5U, 6U, 7U};
    for (Byte r = 0U; r < 6U; ++r)
        set_bank(r, static_cast<Byte>(0x80U | kBanks[r]));
    expect(0x8000U, 131U);
    expect(0xA000U, 133U);
    expect(0xC000U, 62U);
    set_bank(2U, 0xC4U);
    address(0x1000U);
    expect(0x8000U, size == 0x180000U ? 131U : 195U);
    set_bank(2U, 0x84U);
    expect(0x8000U, 131U);
    set_bank(0U, 0x40U);
    address(0U);
    expect(0x8000U, 67U);
    address(0x1000U);
    expect(0x8000U, 131U);
    store(0x8000U, 0x86U);
    expect(0x8000U, 67U); // Inversion changes active pins immediately.
    for (Byte r = 0U; r < 6U; ++r)
        set_bank(r, kBanks[r]);
    store(0x8000U, 0x46U);
    expect(0x8000U, 62U);
    expect(0xC000U, 3U);
    store(0x8000U, 6U);
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xADU, 0x11U, 0U, 0xC9U, 0xD3U, 0xD0U, 0xF9U});
    store(0x2001U, 0U);
    code.insert(code.end(), 8U, 0xEAU);
    set_bank(2U, 31U);
    pwrite(0x1004U, 0xC7U);
    store(0x12U, 0xC7U);
    store(0x2001U, 0x18U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x400U);
    std::ranges::copy(code, rom.begin() + 16 + 0x7E000U);
    constexpr std::array<Byte, 12> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0xA9U, 0xD3U, 0x85U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(kIrq, rom.begin() + 16 + 0x7E400U);
    for (std::size_t v = 16U + 0x7FFFAU; v < 16U + 0x80000U; v += 2U) {
        rom[v] = 0U;
        rom[v + 1U] = v == 16U + 0x7FFFCU ? 0xE0U : 0xE4U;
    }
    return rom;
}
Cartridge cartridge(std::size_t size = 0x180000U, bool defaults = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(size, defaults));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("Sachen 513 outer banks follow every CHR slot while fixed PRG windows stay in bank zero",
          "[mappers][sachen513]") {
    constexpr std::array<Byte, 6> kRegs = {0x02U, 0x46U, 0x8AU, 0xCEU, 0x52U, 0x96U};
    constexpr std::array<unsigned, 8> kOuter = {0, 0, 1, 1, 2, 3, 1, 2};
    for (const std::size_t size : {0x180000U, 0x200000U}) {
        auto m = board(size);
        for (Byte r = 0U; r < 6U; ++r)
            bank(*m, r, kRegs[r]);
        bank(*m, 7U, 0xC5U);
        for (Byte mode : {0U, 0x40U, 0x80U, 0xC0U}) {
            bank(*m, 6U, 0xC3U, mode);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                m->observe_ppu_address(
                    static_cast<Address>(slot * 1024U | ((slot & 1U) != 0U ? 0x2000U : 0U)));
                unsigned outer = kOuter[slot ^ ((mode & 0x80U) != 0U ? 4U : 0U)];
                if (size == 0x180000U && outer == 3U)
                    outer = 2U;
                REQUIRE(m->read_prg(0x8000U) == ((mode & 0x40U) != 0U ? 62U : outer * 64U + 3U));
                REQUIRE(m->read_prg(0xA000U) == outer * 64U + 5U);
                REQUIRE(m->read_prg(0xC000U) == ((mode & 0x40U) != 0U ? outer * 64U + 3U : 62U));
                REQUIRE(m->read_prg(0xE000U) == 63U);
            }
        }
    }
}
TEST_CASE("Sachen 513 bank output reacts to register writes without cached CHR reads driving pins",
          "[mappers][sachen513]") {
    auto m = board(0x200000U);
    bank(*m, 6U, 3U);
    bank(*m, 0U, 0U);
    bank(*m, 2U, 0x80U);
    m->observe_ppu_address(0x1000U);
    REQUIRE(m->read_prg(0x8000U) == 131U);
    static_cast<void>(m->read_chr(0U));
    static_cast<void>(m->read_chr(0x800U));
    REQUIRE(m->read_prg(0x8000U) == 131U);
    bank(*m, 2U, 0xC0U);
    REQUIRE(m->read_prg(0x8000U) == 195U);
    m->write_prg(0x8000U, 0x86U);
    REQUIRE(m->read_prg(0x8000U) == 3U);
    bank(*m, 0U, 0x40U, 0x80U);
    REQUIRE(m->read_prg(0x8000U) == 67U);
}
TEST_CASE(
    "Sachen 513 maps all 32 battery CHR pages, ignores bit 5 and preserves paired-bank wiring",
    "[mappers][sachen513]") {
    auto m = board();
    REQUIRE(m->battery_ram().size() == 32768U);
    for (Byte page = 0U; page < 32U; ++page) {
        bank(*m, 2U, page);
        m->write_chr(0x1000U, static_cast<Byte>(0x80U + page));
    }
    for (Byte page = 0U; page < 32U; ++page) {
        bank(*m, 2U, static_cast<Byte>(page | 0xE0U));
        REQUIRE(m->read_chr(0x1000U) == 0x80U + page);
        REQUIRE(m->battery_ram()[static_cast<std::size_t>(page) * 1024U] == 0x80U + page);
    }
    bank(*m, 0U, 0xDFU);
    REQUIRE(m->read_chr(0U) == 0x9EU);
    REQUIRE(m->read_chr(0x400U) == 0x9FU);
    m->write_prg(0x8000U, 0x80U);
    REQUIRE(m->read_chr(0x1000U) == 0x9EU);
    REQUIRE(m->read_chr(0x1400U) == 0x9FU);
    const std::vector<Byte> before(m->battery_ram().begin(), m->battery_ram().end());
    for (Byte value : {0U, 0x80U, 0xC0U}) {
        m->write_prg(0xA001U, value);
        REQUIRE_FALSE(m->maps_prg(0x6000U));
        m->write_prg(0x6000U, 0U);
    }
    REQUIRE(std::ranges::equal(before, m->battery_ram()));
}
TEST_CASE("Sachen 513 actual PPU address pins select PRG banks during sprite fetches and PPUDATA",
          "[mappers][sachen513]") {
    auto c = cartridge();
    c.write_prg(0x8000U, 6U);
    c.write_prg(0x8001U, 3U);
    for (Byte r = 0U; r < 6U; ++r) {
        c.write_prg(0x8000U, r);
        c.write_prg(0x8001U, r < 2U ? 0x40U : 0x80U);
    }
    Ppu ppu;
    ppu.connect_cartridge(&c);
    ppu_addr(ppu, 0U);
    REQUIRE(c.read_prg(0x8000U) == 67U);
    ppu_addr(ppu, 0x1000U);
    static_cast<void>(ppu.read_register(7U));
    REQUIRE(c.read_prg(0x8000U) == 131U);
    static_cast<void>(c.read_chr(0U));
    REQUIRE(c.read_prg(0x8000U) == 131U);
    ppu_addr(ppu, 0x2000U);
    REQUIRE(c.read_prg(0x8000U) == 67U);
    ppu.write_register(0U, 8U);
    ppu.write_register(1U, 0x18U);
    ppu.step_dots(259U);
    REQUIRE(c.read_prg(0x8000U) == 67U);
    ppu.step_dots(1U);
    REQUIRE(c.read_prg(0x8000U) == 131U);
    c.write_prg(0x8000U, 0x86U);
    REQUIRE(c.read_prg(0x8000U) == 67U);
}
TEST_CASE("Sachen 513 MMC3 IRQ and H/V CIRAM are independent of outer PRG pins",
          "[mappers][sachen513]") {
    auto m = board();
    bank(*m, 0U, 0x80U);
    m->observe_ppu_address(0U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    m->write_prg(0xA000U, 1U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    m->write_prg(0xA000U, 0U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    bus.write(0x10U, 0xA5U);
    REQUIRE(bus.read(0x6000U) == 0xA5U);
}
TEST_CASE("Sachen 513 state retains NVRAM, PPU selection and partial IRQs with bounded loads",
          "[mappers][sachen513]") {
    auto m = board();
    bank(*m, 6U, 3U);
    bank(*m, 2U, 5U);
    m->write_chr(0x1000U, 0xB7U);
    bank(*m, 4U, 0xC2U);
    m->observe_ppu_address(0x1800U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    auto copy = board();
    StateReader r(saved);
    copy->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE(copy->read_prg(0x8000U) == 131U);
    REQUIRE(copy->read_chr(0x1000U) == 0xB7U);
    copy->clock_irq_counter();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    copy->reset();
    bank(*copy, 2U, 5U);
    REQUIRE(copy->read_chr(0x1000U) == 0xB7U);
    for (const std::size_t offset : std::array<std::size_t, 3>{0U, 32776U, saved.size() - 1U}) {
        auto bad = saved;
        bad[offset] = 0xFFU;
        StateReader invalid(bad);
        copy->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(copy->battery_ram().size() == 32768U);
        static_cast<void>(copy->read_chr(0U));
    }
    StateReader short_state(std::span{saved}.first(saved.size() - 1U));
    copy->load_state(short_state);
    REQUIRE_FALSE(short_state.ok());
}
TEST_CASE("Sachen 513 NES 2.0 declarations bound ROM and identify battery pattern memory",
          "[mappers][sachen513]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 12U; ++kind) {
        auto h = header();
        std::size_t prg = 0x180000U, chr = 0U;
        switch (kind) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.submapper = 1U;
            break;
        case 2U:
            h.has_battery = false;
            break;
        case 3U:
            h.chr_nvram_size = 16384U;
            break;
        case 4U:
            h.chr_ram_size = 32768U;
            break;
        case 5U:
            h.prg_nvram_size = 8192U;
            break;
        case 6U:
            h.prg_ram_size = 8192U;
            break;
        case 7U:
            prg = 262144U;
            break;
        case 8U:
            prg = 0x200000U + 8192U;
            break;
        case 9U:
            prg = 0x180000U - 8192U;
            break;
        case 10U:
            chr = 8192U;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(prg), std::vector<Byte>(chr)));
    }
    auto h = header();
    h.chr_nvram_size = 0U;
    auto r = MapperRegistry::instance().create(h, std::vector<Byte>(524288U), {});
    REQUIRE(r);
    REQUIRE((*r)->battery_ram().size() == 32768U);
}
TEST_CASE(
    "Sachen 513 original CPU program switches outer banks, handles IRQ and replays machine state",
    "[mappers][sachen513]") {
    for (const std::size_t size : {0x180000U, 0x200000U}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(cartridge(size), ""));
        emulator.reset();
        emulator.step_frame();
        REQUIRE(emulator.read_cpu(0x12U) == 0xC7U);
        REQUIRE(emulator.read_cpu(0x11U) == 0xD3U);
        REQUIRE(emulator.read_cpu(0x10U) == 1U);
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
        bad[bad.size() - tail.data().size() - 1U] = 8U;
        const auto before = emulator.save_state();
        REQUIRE_FALSE(emulator.load_state(bad));
        REQUIRE((emulator.save_state() == before));
    }
}
TEST_CASE("Sachen 513 battery files contain physical CHR pages and reload on power-up",
          "[mappers][sachen513][battery]") {
    Scratch scratch;
    const auto path = (scratch.path / "513.sav").string();
    {
        Emulator writer;
        REQUIRE(writer.load_cartridge(cartridge(), path));
        writer.reset();
        writer.step_frame();
        REQUIRE(writer.read_cpu(0x12U) == 0xC7U);
        REQUIRE(writer.flush_persistent_data());
    }
    auto bytes = read_binary_file_capped(path, 32768U, "513 CHR NVRAM");
    REQUIRE(bytes);
    REQUIRE(bytes->size() == 32768U);
    REQUIRE((*bytes)[0] == 0xA5U);
    REQUIRE((*bytes)[31UZ * 1024U] == 0x5AU);
    REQUIRE((*bytes)[31UZ * 1024U + 3U] == 1U);
    REQUIRE((*bytes)[31UZ * 1024U + 4U] == 0xC7U);
    Emulator reader;
    REQUIRE(reader.load_cartridge(cartridge(0x180000U, true), path));
    reader.reset();
    reader.step_frame();
    REQUIRE(reader.read_cpu(0x10U) == 2U);
    REQUIRE(reader.read_cpu(0x12U) == 0xC7U);
    REQUIRE(write_binary_file_atomic(path, std::vector<Byte>(32769U), "oversize 513 save"));
    const auto before = reader.save_state();
    REQUIRE_FALSE(reader.load_battery_ram(path));
    REQUIRE((reader.save_state() == before));
}
} // namespace
