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
INesHeader board_header() {
    INesHeader h{};
    h.mapper_number = 512U;
    h.is_nes2 = true;
    h.has_battery = true;
    h.mirror_mode = MirrorMode::Vertical;
    h.nametable_bits = 1U;
    h.prg_nvram_size = 8192U;
    h.chr_ram_size = 8192U;
    return h;
}
std::vector<Byte> banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / unit);
    return data;
}
std::unique_ptr<Mapper> board(std::size_t prg = 524288U) {
    register_builtin_mappers();
    auto r =
        MapperRegistry::instance().create(board_header(), banks(prg, 8192U), banks(262144U, 1024U));
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
struct Scratch {
    std::filesystem::path path =
        std::filesystem::temp_directory_path() /
        ("mapperbus-512-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch() {
        std::filesystem::create_directories(path);
    }
    ~Scratch() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};
std::vector<Byte> image(bool defaults = false) {
    constexpr std::size_t kPrg = 262144U, kChr = 262144U;
    std::vector<Byte> rom(16U + kPrg + kChr);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 16U;
    rom[5] = 32U;
    rom[6] = 3U;
    rom[7] = 8U;
    rom[8] = 2U;
    if (!defaults) {
        rom[10] = 0x70U;
        rom[11] = 7U;
    }
    const auto prg = banks(kPrg, 8192U), chr = banks(kChr, 1024U);
    std::ranges::copy(prg, rom.begin() + 16);
    std::ranges::copy(chr, rom.begin() + 16 + kPrg);
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
    };
    const auto expect = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xADU,
                     static_cast<Byte>(addr),
                     static_cast<Byte>(addr >> 8U),
                     0xC9U,
                     value,
                     0xD0U,
                     0xFEU});
    };
    const auto pwrite = [&store](Address addr, Byte value) {
        store(0x2006U, static_cast<Byte>(addr >> 8U));
        store(0x2006U, static_cast<Byte>(addr));
        store(0x2007U, value);
    };
    const auto pread = [&store, &code, &expect](Address addr, Byte value) {
        store(0x2006U, static_cast<Byte>(addr >> 8U));
        store(0x2006U, static_cast<Byte>(addr));
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, value);
    };
    store(0x8000U, 6U);
    store(0x8001U, 3U);
    expect(0x8000U, 3U);
    store(0x6000U, 0U);
    store(0xA001U, 0U);
    store(0x7FFFU, 0x5AU);
    pwrite(0x2000U, 0x31U);
    pwrite(0x2400U, 0x32U);
    store(0x4100U, 1U);
    for (unsigned i = 0U; i < 4U; ++i)
        pwrite(static_cast<Address>(0x2000U + i * 1024U), static_cast<Byte>(0xA1U + i));
    store(0x4100U, 2U);
    pwrite(0U, 0xB7U);
    store(0x4100U, 3U);
    pread(0x2000U, 0x31U);
    store(0x4100U, 1U);
    pread(0x2800U, 0xA3U);
    store(0x4100U, 2U);
    pread(0U, 0xB7U);
    store(0x4100U, 0U);
    pread(0U, 0U);
    code.insert(code.end(), {0xEEU, 2U, 0x60U}); // Counts actual cartridge power-ups.
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U); // BG $0000 / sprites $1000: real MMC3 A12 edges.
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU});
    code.insert(code.end(), {0xADU, 0U, 0x60U, 0xC9U, 0xA7U, 0xD0U, 0xF9U});
    store(0x4100U, 1U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xEEU, 1U, 0x60U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x300U);
    std::ranges::copy(code, rom.begin() + 16 + kPrg - 8192U);
    constexpr std::array<Byte, 13> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0xA9U, 0xA7U, 0x8DU, 0U, 0x60U, 0x68U, 0x40U};
    std::ranges::copy(kIrq, rom.begin() + 16 + kPrg - 8192U + 0x300U);
    for (std::size_t v = 16U + kPrg - 6U; v < 16U + kPrg; v += 2U) {
        rom[v] = 0U;
        rom[v + 1U] = v == 16U + kPrg - 4U ? 0xE0U : 0xE3U;
    }
    return rom;
}
Cartridge cartridge(bool defaults = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(defaults));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("Sachen 512 shares MMC3 PRG banking and masks physical register pins",
          "[mappers][sachen512]") {
    auto m = board();
    bank(*m, 6U, 0x83U);
    bank(*m, 7U, 0x45U);
    REQUIRE(m->read_prg(0x8000U) == 3U);
    REQUIRE(m->read_prg(0xA000U) == 5U);
    REQUIRE(m->read_prg(0xC000U) == 62U);
    REQUIRE(m->read_prg(0xE000U) == 63U);
    bank(*m, 6U, 8U, 0x40U);
    REQUIRE(m->read_prg(0x8000U) == 62U);
    REQUIRE(m->read_prg(0xC000U) == 8U);
    auto uneven = board(73728U);
    bank(*uneven, 6U, 0xC8U);
    REQUIRE(uneven->read_prg(0x8000U) == 8U);
    REQUIRE(uneven->read_prg(0xE000U) == 8U);
}
TEST_CASE("Sachen 512 RAM modes follow CHR bank outputs while keeping ROM read-only",
          "[mappers][sachen512]") {
    auto m = board();
    bank(*m, 0U, 5U);
    bank(*m, 1U, 0x83U);
    bank(*m, 2U, 0xFCU);
    for (Byte mode : {0U, 1U}) {
        m->write_expansion(0x4100U, mode);
        REQUIRE(m->read_chr(0U) == 4U);
        REQUIRE(m->read_chr(0x400U) == 5U);
        REQUIRE(m->read_chr(0x800U) == 0x82U);
        REQUIRE(m->read_chr(0x1000U) == 0xFCU);
        m->write_chr(0U, 0xAAU);
        REQUIRE(m->read_chr(0U) == 4U);
    }
    m->write_expansion(0x4100U, 2U);
    bank(*m, 2U, 0U);
    m->write_chr(0x1000U, 0x11U);
    bank(*m, 2U, 4U);
    REQUIRE(m->read_chr(0x1000U) == 0x11U);
    bank(*m, 0U, 2U);
    m->write_chr(0U, 0x33U);
    m->write_chr(0x400U, 0x44U);
    bank(*m, 2U, 3U);
    REQUIRE(m->read_chr(0x1000U) == 0x44U);
    bank(*m, 2U, 3U, 0x80U);
    REQUIRE(m->read_chr(0U) == 0x44U);
    REQUIRE(m->read_chr(0x1000U) == 0x33U);
    REQUIRE(m->read_chr(0x1400U) == 0x44U);
    m->write_expansion(0x4100U, 3U);
    REQUIRE(m->read_chr(0U) == 0x44U);
    m->write_expansion(0x4100U, 0U);
    REQUIRE(m->read_chr(0U) == 3U);
}
TEST_CASE("Sachen 512 has six independent nametables and separate persistent pattern data",
          "[mappers][sachen512]") {
    auto c = cartridge();
    Ppu ppu;
    ppu.connect_cartridge(&c);
    ppu_write(ppu, 0x2005U, 0x11U);
    ppu_write(ppu, 0x2405U, 0x22U);
    c.write_expansion(0x4100U, 1U);
    REQUIRE(c.mirror_mode() == MirrorMode::FourScreen);
    for (unsigned i = 0U; i < 4U; ++i)
        ppu_write(ppu, static_cast<Address>(0x2005U + i * 1024U), static_cast<Byte>(0x40U + i));
    for (unsigned i = 0U; i < 4U; ++i)
        REQUIRE(ppu_read(ppu, static_cast<Address>(0x2005U + i * 1024U)) == 0x40U + i);
    REQUIRE(ppu_read(ppu, 0x3005U) == 0x40U);
    ppu_write(ppu, 0x3F00U, 0x2AU);
    REQUIRE(ppu.palette_ram()[0] == 0x2AU);
    for (Byte mode : {2U, 3U}) {
        c.write_expansion(0x4100U, mode);
        REQUIRE(ppu_read(ppu, 0x2005U) == 0x11U);
        REQUIRE(ppu_read(ppu, 0x2405U) == 0x22U);
        ppu_write(ppu, 0x10U, 0xB7U);
    }
    c.write_expansion(0x4100U, 1U);
    c.write_prg(0xA000U, 1U);
    REQUIRE(ppu_read(ppu, 0x2005U) == 0x40U);
    REQUIRE(ppu_read(ppu, 0x2C05U) == 0x43U);
    c.write_expansion(0x4100U, 0U);
    REQUIRE(c.mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(ppu_read(ppu, 0x2405U) == 0x11U);
    REQUIRE(ppu_read(ppu, 0x2805U) == 0x22U);
    c.write_expansion(0x4100U, 2U);
    REQUIRE(ppu_read(ppu, 0x10U) == 0xB7U);
    REQUIRE(ppu.palette_ram()[0] == 0x2AU);
}
TEST_CASE("Sachen 512 mode ports are write-only and WRAM enable cannot be cleared",
          "[mappers][sachen512]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    REQUIRE(c.maps_prg(0x6000U));
    bus.write(0x6000U, 0x5AU);
    REQUIRE(bus.read(0x6000U) == 0x5AU);
    bus.write(0xA001U, 0U);
    REQUIRE(c.maps_prg(0x6000U));
    bus.write(0x6000U, 0xA5U);
    REQUIRE(bus.read(0x6000U) == 0xA5U);
    bus.write(0xA001U, 0x40U);
    bus.write(0x6000U, 0U);
    REQUIRE(bus.read(0x6000U) == 0xA5U);
    bus.write(0xA001U, 0U);
    bus.write(0x6000U, 0x3CU);
    REQUIRE(bus.read(0x6000U) == 0x3CU);
    for (Address a : {0x4100U, 0x43FFU, 0x5FFFU}) {
        bus.write(a, 0xFDU);
        REQUIRE(c.mirror_mode() == MirrorMode::FourScreen);
        bus.write(0x10U, 0xA5U);
        REQUIRE(bus.read(a) == 0xA5U);
    }
    bus.write(0x4200U, 0U);
    REQUIRE(c.mirror_mode() == MirrorMode::FourScreen);
    bus.write(0x6100U, 2U);
    REQUIRE(bus.read(0x6100U) == 2U);
    REQUIRE(c.mirror_mode() != MirrorMode::FourScreen);
    bus.write(0x7FFFU, 1U);
    REQUIRE(bus.read(0x7FFFU) == 1U);
    REQUIRE(c.mirror_mode() == MirrorMode::FourScreen);
}
TEST_CASE("Sachen 512 uses MMC3 IRQ counting in every graphics mode and during real PPU rendering",
          "[mappers][sachen512]") {
    auto m = board();
    for (Byte mode = 0U; mode < 4U; ++mode) {
        m->write_expansion(0x4100U, mode);
        m->write_prg(0xE000U, 0U);
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
    }
    auto c = cartridge();
    Ppu ppu;
    ppu.connect_cartridge(&c);
    c.write_prg(0xC000U, 2U);
    c.write_prg(0xC001U, 0U);
    c.write_prg(0xE001U, 0U);
    ppu.step_dots(341U * 4U);
    REQUIRE_FALSE(c.irq_pending());
    ppu.write_register(0U, 8U);
    ppu.write_register(1U, 0x18U);
    ppu.step_dots(341U * 2U);
    REQUIRE_FALSE(c.irq_pending());
    ppu.step_dots(260U);
    REQUIRE(c.irq_pending());
}
TEST_CASE(
    "Sachen 512 snapshots retain all RAM pools and partial IRQ counts and validate new fields",
    "[mappers][sachen512]") {
    auto m = board();
    m->write_prg(0x6000U, 0x5AU);
    m->write_expansion(0x4100U, 1U);
    REQUIRE(m->write_nametable(0x2800U, 0x73U));
    m->write_expansion(0x4100U, 2U);
    m->write_chr(0U, 0xB7U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    auto clone = board();
    StateReader r(saved);
    clone->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE(clone->read_prg(0x6000U) == 0x5AU);
    REQUIRE(clone->read_chr(0U) == 0xB7U);
    clone->clock_irq_counter();
    REQUIRE_FALSE(clone->irq_pending());
    clone->clock_irq_counter();
    REQUIRE(clone->irq_pending());
    clone->write_expansion(0x4100U, 1U);
    REQUIRE(clone->read_nametable(0x2800U) == 0x73U);
    clone->reset();
    REQUIRE_FALSE(clone->irq_pending());
    REQUIRE(clone->read_prg(0x6000U) == 0x5AU);
    clone->write_expansion(0x4100U, 1U);
    REQUIRE(clone->read_nametable(0x2800U) == 0x73U);
    clone->write_expansion(0x4100U, 2U);
    REQUIRE(clone->read_chr(0U) == 0xB7U);
    for (const std::size_t offset :
         std::array<std::size_t, 3>{saved.size() - 1U, saved.size() - 2U, 16384U}) {
        auto bad = saved;
        bad[offset] = 0xFFU;
        StateReader invalid(bad);
        clone->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
    }
    auto bad = saved;
    bad[bad.size() - 2U] = 0U;
    StateReader disabled(bad);
    clone->load_state(disabled);
    REQUIRE_FALSE(disabled.ok());
    StateReader truncated(std::span{saved}.first(saved.size() - 1U));
    clone->load_state(truncated);
    REQUIRE_FALSE(truncated.ok());
}
TEST_CASE("Sachen 512 NES 2.0 metadata accounts for both PPU RAM pools and mandatory WRAM",
          "[mappers][sachen512]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 11U; ++kind) {
        auto h = board_header();
        std::size_t prg = 262144U, chr = 262144U;
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
            h.prg_nvram_size = 16384U;
            break;
        case 4U:
            h.prg_ram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 4096U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        case 7U:
            prg = 16384U;
            break;
        case 8U:
            prg = 524288U + 8192U;
            break;
        case 9U:
            chr = 0U;
            break;
        default:
            chr = 262144U + 1024U;
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(prg), std::vector<Byte>(chr)));
    }
    auto default_cart = cartridge(true);
    REQUIRE(default_cart.battery_ram().size() == 8192U);
    for (Byte wiring : {8U, 9U}) {
        auto h = board_header();
        h.mirror_mode = MirrorMode::FourScreen;
        h.nametable_bits = wiring;
        auto r = MapperRegistry::instance().create(
            h, std::vector<Byte>(32768U), std::vector<Byte>(8192U));
        REQUIRE(r);
        REQUIRE((*r)->mirror_mode() ==
                (wiring == 8U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        (*r)->write_expansion(0x4100U, 1U);
        REQUIRE((*r)->mirror_mode() == MirrorMode::FourScreen);
    }
}
TEST_CASE("Sachen 512 original CPU program handles IRQs and replays full machine state atomically",
          "[mappers][sachen512]") {
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(cartridge(), ""));
    emulator.reset();
    emulator.step_frame();
    REQUIRE(emulator.read_cpu(0x6000U) == 0xA7U);
    REQUIRE(emulator.read_cpu(0x6002U) == 1U);
    REQUIRE(emulator.read_cpu(0x7FFFU) == 0x5AU);
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
    bad[bad.size() - tail.data().size() - 1U] = 4U;
    const auto before = emulator.save_state();
    REQUIRE_FALSE(emulator.load_state(bad));
    REQUIRE((emulator.save_state() == before));
}
TEST_CASE("Sachen 512 persists only WRAM and reloads it across cartridge power-ups",
          "[mappers][sachen512][battery]") {
    Scratch scratch;
    const auto path = (scratch.path / "512.sav").string();
    {
        Emulator writer;
        REQUIRE(writer.load_cartridge(cartridge(), path));
        writer.reset();
        writer.step_frame();
        REQUIRE(writer.flush_persistent_data());
        auto data = read_binary_file_capped(path, 8192U, "512 battery");
        REQUIRE(data);
        REQUIRE(data->size() == 8192U);
        REQUIRE((*data)[0] == 0xA7U);
        REQUIRE((*data)[2] == 1U);
        REQUIRE(data->back() == 0x5AU);
    }
    Emulator reader;
    REQUIRE(reader.load_cartridge(cartridge(), path));
    REQUIRE(reader.read_cpu(0x6002U) == 1U);
    reader.reset();
    reader.step_frame();
    REQUIRE(reader.read_cpu(0x6002U) == 2U);
    REQUIRE(reader.flush_persistent_data());
    for (unsigned size : {8193U}) {
        REQUIRE(write_binary_file_atomic(path, std::vector<Byte>(size), "invalid 512 save"));
        const auto before = reader.save_state();
        REQUIRE_FALSE(reader.load_battery_ram(path));
        REQUIRE((reader.save_state() == before));
    }
}
} // namespace
