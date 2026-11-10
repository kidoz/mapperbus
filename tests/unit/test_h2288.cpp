#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::array<Byte, 8> kWrittenIndices = {0U, 2U, 6U, 1U, 7U, 3U, 4U, 5U};
constexpr std::array<Address, 8> kPorts = {
    0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U};
INesHeader header() {
    INesHeader h{};
    h.mapper_number = 123U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / unit);
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(),
                              std::size_t p = 0x80000U,
                              std::size_t c = 0x40000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, banks(p, 8192U), banks(c, 1024U));
    REQUIRE(r);
    return std::move(*r);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void write(Mapper& m, unsigned port, Byte value, Address alias = 0U) {
    const Byte native =
        port == 0U ? static_cast<Byte>((value & 0xF8U) | kWrittenIndices[value & 7U]) : value;
    m.write_prg(static_cast<Address>(kPorts[port] | alias), native);
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    write(m, 0U, static_cast<Byte>(reg | mode));
    write(m, 1U, value);
}
TEST_CASE("H2288 all MMC3 port data values use only the documented bank-index permutation",
          "[mappers][h2288]") {
    auto m = board();
    auto h = header();
    h.mapper_number = 119U;
    auto ref = board(h, 0x80000U, 0x20000U);
    for (unsigned port = 0U; port < 8U; ++port)
        for (unsigned value = 0U; value < 256U; ++value) {
            m->reset();
            ref->reset();
            bank(*m, 2U, 9U);
            ref->write_prg(0x8000U, 2U);
            ref->write_prg(0x8001U, 9U);
            write(*m, port, static_cast<Byte>(value));
            ref->write_prg(kPorts[port], static_cast<Byte>(value));
            auto expected = state(*ref);
            expected.push_back(0U);
            REQUIRE((state(*m) == expected));
        }
    for (unsigned port = 0U; port < 8U; ++port)
        for (Address alias : {0U, 2U, 0x2A4U, 0x1FFEU}) {
            m->reset();
            ref->reset();
            write(*m, port, 0xC7U, alias);
            ref->write_prg(kPorts[port], 0xC7U);
            auto expected = state(*ref);
            expected.push_back(0U);
            REQUIRE((state(*m) == expected));
        }
}
TEST_CASE("H2288 all override bytes use the distinct raw PRG bit order in both MMC3 modes",
          "[mappers][h2288]") {
    auto m = board();
    bank(*m, 7U, 37U);
    // Pack written pins 0,2,4,5 and select the literal truth-table bank.
    constexpr std::array<unsigned, 4> kPins = {0U, 2U, 4U, 5U};
    constexpr std::array<unsigned, 16> kBanks = {
        0U, 1U, 4U, 5U, 2U, 3U, 6U, 7U, 8U, 9U, 12U, 13U, 10U, 11U, 14U, 15U};
    for (Byte swap : {0U, 0x40U}) {
        bank(*m, 6U, 35U, swap);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            m->write_expansion(0x5800U, static_cast<Byte>(raw));
            unsigned packed = 0U;
            for (unsigned i = 0U; i < 4U; ++i)
                packed |= ((raw >> kPins[i]) & 1U) << i;
            const unsigned selected = kBanks[packed];
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                unsigned expected = 0U;
                if ((raw & 0x40U) == 0U) {
                    const std::array<unsigned, 4> pages =
                        swap != 0U ? std::array<unsigned, 4>{62U, 37U, 35U, 63U}
                                   : std::array<unsigned, 4>{35U, 37U, 62U, 63U};
                    expected = pages[slot];
                } else if ((raw & 2U) == 0U)
                    expected = 2U * selected + (slot & 1U);
                else
                    expected = 2U * (selected & 14U) + slot;
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) == expected);
            }
        }
    }
}
TEST_CASE("H2288 decoder aliases bypass RAM gates and never create a CHR outer register",
          "[mappers][h2288]") {
    auto m = board();
    bank(*m, 6U, 35U);
    bank(*m, 0U, 0x83U);
    bank(*m, 2U, 0xE5U);
    for (Byte gate : {0U, 0x80U, 0xC0U}) {
        write(*m, 3U, gate);
        for (unsigned a = 0x5800U; a <= 0x5FFFU; ++a) {
            m->write_expansion(static_cast<Address>(a), 0x75U); // bank 15, NROM-128
            REQUIRE(m->read_prg(0x8000U) == 30U);
            m->write_expansion(static_cast<Address>(a), 0U);
            REQUIRE(m->read_prg(0x8000U) == 35U);
        }
        for (Address a : {0x5000U, 0x57FFU, 0x6000U, 0x6001U, 0x7FFFU}) {
            REQUIRE_FALSE(m->maps_expansion(a));
            m->write_expansion(a, 0x75U);
            m->write_prg(a, 0x75U);
            REQUIRE(m->read_prg(0x8000U) == 35U);
        }
        for (Byte inversion : {0U, 0x80U}) {
            write(*m, 0U, inversion);
            const Address pair = inversion != 0U ? 0x1000U : 0U;
            REQUIRE(m->read_chr(pair) == 0x82U);
            REQUIRE(m->read_chr(static_cast<Address>(pair + 1024U)) == 0x83U);
            REQUIRE(m->read_chr(static_cast<Address>(pair ^ 0x1000U)) == 0xE5U);
            m->write_chr(pair, 0U);
            REQUIRE(m->read_chr(pair) == 0x82U);
        }
        write(*m, 0U, 6U);
    }
    m->write_expansion(0x5800U, 0x75U);
    bank(*m, 6U, 7U);
    bank(*m, 7U, 9U);
    REQUIRE(m->read_prg(0x8000U) == 30U);
    m->write_expansion(0x5800U, 0U);
    REQUIRE(m->read_prg(0x8000U) == 7U);
    REQUIRE(m->read_prg(0xA000U) == 9U);
    REQUIRE_FALSE(m->configure_board_jumpers(0U));
    REQUIRE(m->battery_ram().empty());
}
TEST_CASE("H2288 keeps the default MMC3 repeating zero-latch IRQ profile", "[mappers][h2288]") {
    auto m = board();
    write(*m, 4U, 0U);
    write(*m, 7U, 0U);
    for (unsigned i = 0U; i < 4U; ++i) {
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        write(*m, 6U, 0U);
        REQUIRE_FALSE(m->irq_pending());
        write(*m, 7U, 0U);
    }
    write(*m, 4U, 2U);
    write(*m, 5U, 0U);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    write(*m, 4U, 0U); // A changed latch preserves the in-flight counter.
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
}
TEST_CASE("H2288 snapshots retain the override and partial IRQ and reject invalid payloads",
          "[mappers][h2288]") {
    auto m = board();
    bank(*m, 2U, 0xE5U, 0x80U);
    m->write_expansion(0x5FFFU, 0x75U);
    write(*m, 4U, 2U);
    write(*m, 5U, 0U);
    write(*m, 7U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    auto copy = board();
    StateReader r(saved);
    copy->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE((state(*copy) == saved));
    REQUIRE(copy->read_prg(0x8000U) == 30U);
    REQUIRE(copy->read_chr(0U) == 0xE5U);
    copy->clock_irq_counter();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    for (std::size_t offset : {16384UZ, 16385UZ, 16392UZ, 16394UZ, saved.size() - 1U}) {
        auto bad = saved;
        bad[offset] = 0xFFU;
        StateReader invalid(bad);
        copy->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
    }
    StateReader short_state(std::span{saved}.first(saved.size() - 1U));
    copy->load_state(short_state);
    REQUIRE_FALSE(short_state.ok());
    copy->reset();
    REQUIRE(copy->read_prg(0x8000U) == 0U);
    REQUIRE(copy->read_chr(0U) == 0U);
}
TEST_CASE("H2288 validates ROM-only geometry and folds non-power-of-two ROMs", "[mappers][h2288]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 13U; ++kind) {
        auto h = header();
        std::size_t p = 0x80000U, c = 0x40000U;
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
            p = 16384U;
            break;
        case 7U:
            p = 0x82000U;
            break;
        case 8U:
            c = 0U;
            break;
        case 9U:
            c = 0x40400U;
            break;
        case 10U:
            c = 8193U;
            break;
        case 11U:
            p = 32769U;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
    auto h = header();
    h.is_nes2 = false;
    h.prg_ram_size = 8192U;
    auto legacy = board(h);
    REQUIRE_FALSE(legacy->maps_prg(0x6000U));
    auto small = board(header(), 32768U, 8192U);
    small->write_expansion(0x5800U, 0x75U);
    REQUIRE(small->read_prg(0x8000U) == 2U);
    auto odd = board(header(), 98304U, 196608U);
    bank(*odd, 6U, 63U);
    bank(*odd, 2U, 255U);
    REQUIRE(odd->read_prg(0x8000U) == 3U);
    REQUIRE(odd->read_chr(0x1000U) == 63U);
    odd->write_expansion(0x5800U, 0x75U);
    REQUIRE(odd->read_prg(0x8000U) == 6U);
}
// Original test program replicated in every 16 KiB page: an override may
// change every CPU window while the program continues at the same address.
std::vector<Byte> image(bool legacy = false) {
    auto p = banks(0x80000U, 8192U);
    const auto c = banks(0x40000U, 1024U);
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 32U;
    rom[5] = 32U;
    rom[6] = 0xB1U;
    rom[7] = legacy ? 0x70U : 0x78U;
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address a, Byte v) {
        code.insert(code.end(),
                    {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    };
    const auto mapper_write = [&store](unsigned port, Byte value) {
        const Byte native =
            port == 0U ? static_cast<Byte>((value & 0xF8U) | kWrittenIndices[value & 7U]) : value;
        store(kPorts[port], native);
    };
    const auto expect = [&code](Address a, Byte v) {
        code.insert(
            code.end(),
            {0xADU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U), 0xC9U, v, 0xD0U, 0xFEU});
    };
    const auto set_bank = [&mapper_write](Byte r, Byte v) {
        mapper_write(0U, r);
        mapper_write(1U, v);
    };
    const auto override_prg = [&store](Byte b, bool wide, Address a = 0x5800U) {
        store(a,
              static_cast<Byte>(0x40U | (wide ? 2U : 0U) | ((b & 8U) << 2U) | ((b & 2U) << 3U) |
                                (b & 4U) | (b & 1U)));
    };
    const auto pages = [&expect](std::array<Byte, 4> expected) {
        for (unsigned i = 0U; i < 4U; ++i)
            expect(static_cast<Address>(0x9A00U + i * 8192U), expected[i]);
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
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    set_bank(6U, 35U);
    set_bank(7U, 37U);
    pages({35U, 37U, 62U, 63U});
    store(0x5800U, 0x88U); // Both bits unused; neither enables the override.
    pages({35U, 37U, 62U, 63U});
    mapper_write(0U, 0x46U);
    pages({62U, 37U, 35U, 63U});
    override_prg(0U, false);
    pages({0U, 1U, 0U, 1U});
    mapper_write(3U, 0U);
    override_prg(15U, false, 0x5FFFU);
    pages({30U, 31U, 30U, 31U});
    store(0x6000U, 0U);
    store(0x6001U, 1U);
    pages({30U, 31U, 30U, 31U});
    override_prg(1U, true, 0x5801U);
    pages({0U, 1U, 2U, 3U});
    override_prg(11U, true);
    pages({20U, 21U, 22U, 23U});
    set_bank(6U, 7U);
    set_bank(7U, 9U);
    pages({20U, 21U, 22U, 23U});
    store(0x5800U, 0U);
    pages({7U, 9U, 62U, 63U});
    set_bank(0U, 0x83U);
    set_bank(2U, 0xE5U);
    pread(0U, 0x82U);
    pread(0x400U, 0x83U);
    pread(0x1000U, 0xE5U);
    mapper_write(0U, 0x80U);
    pread(0U, 0xE5U);
    pread(0x1000U, 0x82U);
    pwrite(0U, 0U);
    pread(0U, 0xE5U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    mapper_write(2U, 1U);
    pread(0x2400U, 0x19U);
    mapper_write(2U, 0U);
    pread(0x2400U, 0x27U);
    override_prg(0U, false);
    mapper_write(4U, 0U);
    mapper_write(5U, 0U);
    mapper_write(7U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    // Wait for two automatic zero-latch IRQs, acknowledging and re-enabling
    // in the handler without a forced reload between them.
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 2U, 0x90U, 0xFAU});
    mapper_write(6U, 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x800U);
    constexpr std::array<Byte, 13> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0xE6U, 0x11U, 0x68U, 0x40U};
    for (std::size_t page = 0U; page < p.size(); page += 0x4000U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(page + 0x2000U));
        std::ranges::copy(kIrq, p.begin() + static_cast<std::ptrdiff_t>(page + 0x2800U));
        for (std::size_t v = 0x3FFAU; v < 0x4000U; v += 2U) {
            p[page + v] = 0U;
            p[page + v + 1U] = v == 0x3FFCU ? 0xE0U : 0xE8U;
        }
    }
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(bool legacy = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(legacy));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("H2288 write-only controls and absent WRAM preserve every CPU open-bus bit",
          "[mappers][h2288]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (Address a : {0x57FFU, 0x5800U, 0x5801U, 0x5FFFU, 0x6000U, 0x6001U, 0x7FFFU})
        for (Byte value : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 255U}) {
            bus.write(0x10U, value);
            REQUIRE(bus.read(a) == value);
        }
    REQUIRE_FALSE(c.has_battery());
    REQUIRE(c.battery_ram().empty());
}
TEST_CASE("H2288 original CPU program verifies banking, PPU and repeated IRQs before replay",
          "[mappers][h2288]") {
    for (bool legacy : {false, true}) {
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(cartridge(legacy), ""));
        emulator.reset();
        emulator.step_frame();
        REQUIRE(emulator.read_cpu(0x12U) == 0xC7U);
        REQUIRE(emulator.read_cpu(0x11U) >= 2U);
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
        bad[bad.size() - tail.data().size() - 1U] = 0x50U;
        const auto before = emulator.save_state();
        REQUIRE_FALSE(emulator.load_state(bad));
        REQUIRE((emulator.save_state() == before));
    }
}
} // namespace
