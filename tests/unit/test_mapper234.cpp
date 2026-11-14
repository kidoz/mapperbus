#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper234_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
TEST_CASE("Mapper 234 registers the Maxi 15 board", "[mappers][mapper234]") {
    register_builtin_mappers();
    INesHeader h{};
    h.mapper_number = 234U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Horizontal;
    auto m = MapperRegistry::instance().create(
        h, std::vector<Byte>(524288U), std::vector<Byte>(524288U));
    REQUIRE(m);
}

constexpr std::array<std::size_t, 3> kPrgPages{16U, 32U, 32U};
constexpr std::array<std::size_t, 3> kChrPages{64U, 64U, 128U};
INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 234U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = MirrorMode::Horizontal;
    return h;
}
std::vector<Byte> rom(std::size_t pages, std::size_t size) {
    std::vector<Byte> data(pages * size);
    for (std::size_t a = 0U; a < data.size(); ++a)
        data[a] = static_cast<Byte>(a / size);
    return data;
}
std::unique_ptr<Mapper> board(unsigned population = 0U, bool legacy = false) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(
        header(legacy), rom(kPrgPages[population], 32768U), rom(kChrPages[population], 8192U));
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
struct Banks {
    unsigned prg;
    unsigned chr;
    bool prg_off;
    bool chr_off;
};
Banks banks(unsigned outer, unsigned inner, unsigned population) {
    const unsigned block = outer % 16U;
    const unsigned prg = outer / 64U % 2U != 0U ? block / 2U * 2U + inner % 2U : block;
    const unsigned chr =
        outer / 64U % 2U != 0U ? block / 2U * 8U + inner / 16U % 8U : block * 4U + inner / 16U % 4U;
    const bool upper = outer / 32U % 2U != 0U;
    const bool inhibit = outer / 16U % 2U != 0U;
    const bool prg_off = upper && (inhibit || population == 0U);
    const bool chr_off = population != 1U && upper && (inhibit || population == 0U);
    return {
        prg + (upper ? 16U : 0U), chr + (upper && population != 1U ? 64U : 0U), prg_off, chr_off};
}
TEST_CASE("Mapper 234 decodes CNROM NINA and chip enable pins across all supported populations",
          "[mappers][mapper234]") {
    for (unsigned population = 0U; population < 3U; ++population)
        for (bool legacy : {false, true}) {
            auto m = board(population, legacy);
            for (unsigned outer = 0U; outer < 256U; ++outer) {
                m->reset();
                m->observe_cpu_read(0xFF80U, static_cast<Byte>(outer));
                for (unsigned chr = 0U; chr < 8U; ++chr)
                    for (unsigned prg = 0U; prg < 2U; ++prg) {
                        const unsigned inner = chr * 16U + prg;
                        m->observe_cpu_read(0xFFE8U, static_cast<Byte>(inner));
                        const auto expected = banks(outer, inner, population);
                        REQUIRE(m->prg_read_mask(0x9A00U) == (expected.prg_off ? 0U : 0xFFU));
                        REQUIRE(m->read_prg(0x8000U) == (expected.prg_off ? 0U : expected.prg));
                        REQUIRE(m->read_prg(0xFFFFU) == (expected.prg_off ? 0U : expected.prg));
                        REQUIRE(m->read_chr(0U) == (expected.chr_off ? 0U : expected.chr));
                        REQUIRE(m->read_chr(0x1FFFU) == (expected.chr_off ? 0xFFU : expected.chr));
                        REQUIRE(m->mirror_mode() ==
                                (outer >= 128U ? MirrorMode::Horizontal : MirrorMode::Vertical));
                    }
            }
        }
}
TEST_CASE("Mapper 234 locks only on Q q or block bits and keeps the inner register writable",
          "[mappers][mapper234]") {
    auto m = board(2U);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        m->reset();
        m->observe_cpu_read(0xFFC0U, 3U);
        m->observe_cpu_read(0xFF80U, static_cast<Byte>(outer));
        m->observe_cpu_read(0xFFC0U, 0U);
        m->observe_cpu_read(0xFF80U, 1U);
        m->observe_cpu_read(0xFFE8U, 0xFFU);
        const auto saved = state(*m);
        const bool locked = outer % 64U != 0U;
        REQUIRE(saved[2U] == (locked ? outer : 1U));
        REQUIRE(saved[3U] == 0x71U);
        REQUIRE(saved[4U] == (locked ? 3U : 0U));
    }
    m->reset();
    m->observe_cpu_read(0xFFE8U, 0x41U);
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_chr(0U) == 0U);
    m->observe_cpu_read(0xFF80U, 0x40U);
    REQUIRE(m->read_prg(0x8000U) == 1U);
    REQUIRE(m->read_chr(0U) == 4U);
    m->observe_cpu_read(0xFF80U, 0x80U);
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_chr(0U) == 0U);
    m->observe_cpu_read(0xFF80U, 0x41U);
    m->observe_cpu_read(0xFF80U, 0U);
    REQUIRE(state(*m)[2U] == 0x41U);
    REQUIRE(m->read_prg(0x8000U) == 1U);
}
TEST_CASE("Mapper 234 decodes exact register ranges on the completed read strobe",
          "[mappers][mapper234]") {
    auto m = board();
    for (unsigned a = 0U; a <= 0xFFFFU; ++a) {
        m->reset();
        m->observe_cpu_read(static_cast<Address>(a), 0xC5U);
        const auto saved = state(*m);
        REQUIRE(saved[2U] == (a >= 0xFF80U && a <= 0xFF9FU ? 0xC5U : 0U));
        REQUIRE(saved[3U] == (a >= 0xFFE8U && a <= 0xFFF7U ? 0x41U : 0U));
        REQUIRE(saved[4U] == (a >= 0xFFC0U && a <= 0xFFDFU ? 1U : 0U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        m->reset();
        m->observe_cpu_read(0xFFE8U, static_cast<Byte>(value));
        REQUIRE(state(*m)[3U] == (value & 0x71U));
        m->observe_cpu_read(0xFFC0U, static_cast<Byte>(value));
        REQUIRE(state(*m)[4U] == (value & 3U));
    }
}
TEST_CASE("Mapper 234 write conflicts sample the old ROM without producing a read strobe",
          "[mappers][mapper234]") {
    register_builtin_mappers();
    auto p = rom(16U, 32768U);
    constexpr std::array<Byte, 8> kMasks{0U, 1U, 2U, 0x0FU, 0x30U, 0x55U, 0xAAU, 0xFFU};
    for (Address base : {0xFF80U, 0xFFC0U, 0xFFE8U})
        for (unsigned i = 0U; i < kMasks.size(); ++i)
            p[(base & 0x7FFFU) + i] = kMasks[i];
    auto m = MapperRegistry::instance().create(header(), std::move(p), rom(64U, 8192U));
    REQUIRE(m);
    for (Address base : {0xFF80U, 0xFFC0U, 0xFFE8U})
        for (unsigned mask = 0U; mask < kMasks.size(); ++mask)
            for (unsigned value = 0U; value < 256U; ++value) {
                (*m)->reset();
                const auto addr = static_cast<Address>(base + mask);
                REQUIRE((*m)->read_prg(addr) == kMasks[mask]);
                REQUIRE(state(**m)[2U] == 0U);
                (*m)->write_prg(addr, static_cast<Byte>(value));
                const auto saved = state(**m);
                const unsigned effective = value & kMasks[mask];
                REQUIRE(saved[2U] == (base == 0xFF80U ? effective : 0U));
                REQUIRE(saved[3U] == (base == 0xFFE8U ? (effective & 0x71U) : 0U));
                REQUIRE(saved[4U] == (base == 0xFFC0U ? (effective & 3U) : 0U));
            }
    (*m)->reset();
    (*m)->observe_cpu_read(0xFF80U, 0x30U);
    (*m)->write_prg(0xFFE8U, 0x41U);
    REQUIRE(state(**m)[3U] == 0x41U); // Undriven ROM cannot force write data low.
}
std::vector<Byte> image(unsigned population = 0U) {
    const auto p = rom(kPrgPages[population], 32768U);
    const auto c = rom(kChrPages[population], 8192U);
    std::vector<Byte> bytes(16U);
    bytes[0] = 'N';
    bytes[1] = 'E';
    bytes[2] = 'S';
    bytes[3] = 0x1AU;
    bytes[4] = static_cast<Byte>(kPrgPages[population] * 2U);
    bytes[5] = static_cast<Byte>(kChrPages[population]);
    bytes[6] = 0xA0U;
    bytes[7] = 0xE8U;
    bytes.insert(bytes.end(), p.begin(), p.end());
    bytes.insert(bytes.end(), c.begin(), c.end());
    return bytes;
}
std::vector<Byte> component(const Cartridge& c) {
    StateWriter w;
    c.save_state(w);
    return w.take();
}
TEST_CASE("Mapper 234 CPU bus returns the old ROM byte before outer and inner latches change",
          "[mappers][mapper234]") {
    register_builtin_mappers();
    auto bytes = image();
    bytes[16U + 0x7F88U] = 0x0BU;
    bytes[16U + 11U * 32768U + 0x7F88U] = 5U;
    bytes[16U + 0x7F80U] = 0x40U;
    bytes[16U + 0x7FF0U] = 1U;
    bytes[16U + 32768U + 0x7FF0U] = 0x70U;
    auto c = Cartridge::from_data(bytes);
    REQUIRE(c);
    MemoryBus bus;
    bus.connect_cartridge(&*c);
    REQUIRE(c->read_prg(0xFF88U) == 0x0BU);
    REQUIRE(component(*c)[2U] == 0U);
    REQUIRE(bus.read(0xFF88U) == 0x0BU);
    REQUIRE(c->read_prg(0x9A00U) == 11U);
    REQUIRE(bus.read(0xFF88U) == 5U);
    REQUIRE(component(*c)[2U] == 0x0BU);
    c->reset();
    REQUIRE(bus.read(0xFF80U) == 0x40U);
    REQUIRE(bus.read(0xFFF0U) == 1U);
    REQUIRE(c->read_prg(0x9A00U) == 1U);
    REQUIRE(bus.read(0xFFF0U) == 0x70U);
    REQUIRE(c->read_prg(0x9A00U) == 0U);
}
void paddr(Ppu& p, Address a) {
    (void)p.read_register(2U);
    p.write_register(6U, static_cast<Byte>(a >> 8U));
    p.write_register(6U, static_cast<Byte>(a));
}
void pwrite(Ppu& p, Address a, Byte v) {
    paddr(p, a);
    p.write_register(7U, v);
}
Byte pread(Ppu& p, Address a) {
    paddr(p, a);
    (void)p.read_register(7U);
    paddr(p, a);
    return p.read_register(7U);
}
TEST_CASE("Mapper 234 models CPU and PPU ROM gates including permanently enabled Australian CHR",
          "[mappers][mapper234][ppu]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        auto bytes = image(population);
        bytes[16U + 0x7F9FU] = 0xFFU;
        auto c = Cartridge::from_data(bytes);
        REQUIRE(c);
        MemoryBus bus;
        bus.connect_cartridge(&*c);
        Ppu p;
        p.connect_cartridge(&*c);
        p.reset();
        c->write_prg(0xFF9FU, 0x30U);
        for (unsigned value = 0U; value < 256U; ++value) {
            bus.write(0x10U, static_cast<Byte>(value));
            REQUIRE(bus.read(0x9A00U) == value);
        }
        REQUIRE(pread(p, 0x123U) == (population == 1U ? 0U : 0x23U));
        REQUIRE(pread(p, 0x1FFFU) == (population == 1U ? 0U : 0xFFU));
        pwrite(p, 0x123U, 0xA7U);
        REQUIRE(pread(p, 0x123U) == (population == 1U ? 0U : 0x23U));
        pwrite(p, 0x2000U, 0x19U);
        pwrite(p, 0x2400U, 0x27U);
        REQUIRE(pread(p, 0x2400U) == 0x27U);
        bus.write(0xFFF7U, 0x31U);
        REQUIRE(component(*c)[3U] == 0x31U);
        bus.write(0x10U, 0xAAU);
        REQUIRE(bus.read(0xFFF0U) == 0xAAU);
        REQUIRE(component(*c)[3U] == 0x20U);
        REQUIRE(pread(p, 0x123U) == (population == 1U ? 2U : 0x23U));
        c->reset();
        REQUIRE(c->read_prg(0x9A00U) == 0U);
        REQUIRE(pread(p, 0x123U) == 0U);
        auto m = board(population);
        for (unsigned a = 0U; a < 8192U; ++a) {
            m->write_chr(static_cast<Address>(a), 0xA7U);
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
        }
        const auto saved = state(*m);
        for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
            const auto addr = static_cast<Address>(a);
            REQUIRE_FALSE(m->maps_prg(addr));
            REQUIRE_FALSE(m->maps_expansion(addr));
            m->write_prg(addr, 0xFFU);
            m->write_expansion(addr, 0xFFU);
        }
        REQUIRE(state(*m) == saved);
        REQUIRE(m->battery_ram().empty());
        REQUIRE_FALSE(m->has_expansion_audio());
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
        REQUIRE_FALSE(m->irq_pending());
    }
}
TEST_CASE("Mapper 234 snapshots retain lock gates and CIC latch with population validation",
          "[mappers][mapper234][state]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        auto m = board(population);
        m->observe_cpu_read(0xFFC0U, 3U);
        m->observe_cpu_read(0xFF80U, 0x30U);
        m->observe_cpu_read(0xFFF0U, 0x71U);
        const auto saved = state(*m);
        REQUIRE(saved.size() == 5U);
        auto copy = board(population);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        for (std::size_t n = 0U; n < saved.size(); ++n) {
            StateReader truncated(std::span(saved).first(n));
            m->load_state(truncated);
            REQUIRE_FALSE(truncated.ok());
            REQUIRE(state(*m) == saved);
        }
        for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 6>{
                 {{0U, 0x80U}, {1U, 0U}, {3U, 2U}, {3U, 8U}, {3U, 0x80U}, {4U, 4U}}}) {
            auto bad = saved;
            bad[offset] = value;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == saved);
        }
        auto other = board((population + 1U) % 3U);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        m->reset();
        copy->reset();
        REQUIRE(state(*m) == state(*copy));
        const auto reset = state(*m);
        REQUIRE(reset[2U] == 0U);
        REQUIRE(reset[3U] == 0U);
        REQUIRE(reset[4U] == 0U);
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    }
}
TEST_CASE("Mapper 234 rejects unsupported ROM and RAM populations", "[mappers][mapper234]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 19U; ++kind) {
        auto h = header();
        std::size_t p = 524288U, c = 524288U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 8192U;
            break;
        case 3U:
            h.prg_nvram_size = 8192U;
            break;
        case 4U:
            h.chr_ram_size = 8192U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 8U:
            p = 0U;
            break;
        case 9U:
            p = 262144U;
            break;
        case 10U:
            p = 589824U;
            break;
        case 11U:
            p = 1572864U;
            break;
        case 12U:
            c = 0U;
            break;
        case 13U:
            c = 262144U;
            break;
        case 14U:
            c = 1572864U;
            break;
        case 15U:
            c = 1048576U;
            break;
        case 16U:
            h = header(true);
            h.has_battery = true;
            break;
        case 17U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 18U:
            --p;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
}

Cartridge cartridge(unsigned population, Byte outer, bool write_outer, bool floating, bool legacy) {
    register_builtin_mappers();
    REQUIRE(mapperbus::tests::mapper234_boot(population, outer, write_outer, floating).size() <
            0x1800U);
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper234_rom(
        population, outer, write_outer, floating, legacy, (outer & 0x80U) != 0U));
    REQUIRE(c);
    return std::move(*c);
}
void exercise_cpu(unsigned population, Byte outer, bool write_outer, bool floating, bool legacy) {
    Emulator e;
    REQUIRE(e.load_cartridge(cartridge(population, outer, write_outer, floating, legacy), ""));
    e.power_on();
    e.step_frame();
    REQUIRE(e.read_cpu(0x12U) == (floating ? 0xD7U : 0xC7U));
    const auto early = e.save_state();
    StateWriter tail;
    Controller{}.save_state(tail);
    Controller{}.save_state(tail);
    const auto start = early.size() - tail.data().size() - 5U;
    REQUIRE(early[start + 2U] == outer);
    REQUIRE(early[start + 3U] == 0x71U);
    REQUIRE(early[start + 4U] == 2U);
    e.step_frame();
    const auto future = e.save_state();
    REQUIRE(e.load_state(early));
    e.step_frame();
    REQUIRE(e.save_state() == future);
    for (const auto [offset, value] :
         std::array<std::pair<std::size_t, Byte>, 4>{{{0U, 0x80U}, {1U, 0U}, {3U, 2U}, {4U, 4U}}}) {
        auto bad = early;
        bad[start + offset] = value;
        const auto before = e.save_state();
        REQUIRE_FALSE(e.load_state(bad));
        REQUIRE(e.save_state() == before);
    }
    auto truncated = early;
    truncated.resize(start + 4U);
    const auto before = e.save_state();
    REQUIRE_FALSE(e.load_state(truncated));
    REQUIRE(e.save_state() == before);
    e.reset();
    REQUIRE(e.read_cpu(0x9A00U) == 0U);
    const auto reset = e.save_state();
    REQUIRE(reset[start + 2U] == 0U);
    REQUIRE(reset[start + 3U] == 0U);
    REQUIRE(reset[start + 4U] == 0U);
    e.step_frame();
    REQUIRE(e.read_cpu(0x12U) == (floating ? 0xD7U : 0xC7U));
    e.power_on();
    e.step_frame();
    REQUIRE(e.read_cpu(0x12U) == (floating ? 0xD7U : 0xC7U));
}
TEST_CASE(
    "Mapper 234 CPU fixtures verify read and write strobes banking CIRAM and locked state replay",
    "[mappers][mapper234][state]") {
    for (unsigned population = 0U; population < 3U; ++population)
        for (bool legacy : {false, true})
            for (bool nina : {false, true})
                for (bool write_outer : {false, true}) {
                    const auto outer =
                        static_cast<Byte>((nina ? 0xC7U : 0x0BU) | (population != 0U ? 0x20U : 0U));
                    exercise_cpu(population, outer, write_outer, false, legacy);
                }
}
TEST_CASE("Mapper 234 CPU RAM firmware continues while PRG ROM is disabled and reset reconnects it",
          "[mappers][mapper234][state]") {
    for (unsigned population = 0U; population < 3U; ++population)
        for (bool legacy : {false, true})
            exercise_cpu(population, 0x30U, false, true, legacy);
}
void replace_boot(std::vector<Byte>& bytes, const std::vector<Byte>& code) {
    REQUIRE(code.size() < 0x1800U);
    for (unsigned page = 0U; page < bytes[4U] / 2U; ++page)
        std::ranges::copy(
            code, bytes.begin() + static_cast<std::ptrdiff_t>(16U + page * 32768U + 0x6000U));
}
TEST_CASE("Mapper 234 clocks registers on opcode fetches and page-crossing dummy reads",
          "[mappers][mapper234]") {
    register_builtin_mappers();
    SECTION("Opcode byte selects the second PRG and CHR chips before its operand fetch") {
        auto bytes = mapperbus::tests::make_mapper234_rom(2U);
        // LDA #$55; STA $14; JMP $0200, with every byte fetched from the register window.
        const std::array<Byte, 7> instructions{0xA9U, 0x55U, 0x85U, 0x14U, 0x4CU, 0U, 2U};
        for (unsigned page = 0U; page < 32U; ++page)
            std::ranges::copy(instructions,
                              bytes.begin() +
                                  static_cast<std::ptrdiff_t>(16U + page * 32768U + 0x7F80U));
        const std::array<Byte, 35> ram{
            0xA9U, 1U,    0x8DU, 6U,    0x20U, 0xA9U, 0x23U, 0x8DU, 6U,    0x20U, 0xADU, 7U,
            0x20U, 0xADU, 7U,    0x20U, 0xC9U, 100U,  0xD0U, 0xFEU, 0xA9U, 0xC7U, 0x85U, 0x12U,
            0xE6U, 0x13U, 0x4CU, 0x18U, 2U,    0xEAU, 0xEAU, 0xEAU, 0xEAU, 0xEAU, 0xEAU};
        std::vector<Byte> boot{0x78U, 0xA9U, 0x40U, 0x8DU, 0x17U, 0x40U};
        for (std::size_t i = 0U; i < ram.size(); ++i) {
            const auto addr = static_cast<Address>(0x200U + i);
            boot.insert(
                boot.end(),
                {0xA9U, ram[i], 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
        }
        boot.insert(boot.end(), {0x4CU, 0x80U, 0xFFU});
        replace_boot(bytes, boot);
        auto c = Cartridge::from_data(bytes);
        REQUIRE(c);
        Emulator e;
        REQUIRE(e.load_cartridge(std::move(*c), ""));
        e.power_on();
        e.step_frame();
        REQUIRE(e.read_cpu(0x14U) == 0x55U);
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        REQUIRE(e.read_cpu(0x9A00U) == 25U);
    }
    SECTION("Only the dummy read touches a register when absolute X wraps to console RAM") {
        auto bytes = mapperbus::tests::make_mapper234_rom();
        for (unsigned page = 0U; page < 16U; ++page)
            bytes[16U + page * 32768U + 0x7F80U] = 0x0BU;
        const std::vector<Byte> code{0x78U, 0xA9U, 0x40U, 0x8DU, 0x17U, 0x40U, 0xA9U, 0x55U, 0x85U,
                                     0x80U, 0xA2U, 0x82U, 0xBDU, 0xFEU, 0xFFU, 0x85U, 0x14U, 0xADU,
                                     0U,    0x9AU, 0xC9U, 11U,   0xD0U, 0xFEU, 0xA9U, 0xC7U, 0x85U,
                                     0x12U, 0xE6U, 0x13U, 0x4CU, 0x1CU, 0xE0U};
        replace_boot(bytes, code);
        auto c = Cartridge::from_data(bytes);
        REQUIRE(c);
        Emulator e;
        REQUIRE(e.load_cartridge(std::move(*c), ""));
        e.power_on();
        e.step_frame();
        REQUIRE(e.read_cpu(0x14U) == 0x55U);
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        REQUIRE(e.read_cpu(0x9A00U) == 11U);
    }
}
} // namespace
