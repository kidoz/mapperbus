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
// Inverse of the two published pin tables: canonical MMC3 ports/indices
// select these native addresses and written indices (NESdev r4858).
constexpr std::array<std::array<Address, 8>, 2> kNativePorts = {
    {{0xA000U, 0xC000U, 0x8001U, 0x8000U, 0xA001U, 0xC001U, 0xE000U, 0xE001U},
     {0xA000U, 0x8001U, 0xC000U, 0x8000U, 0xC001U, 0xA001U, 0xE000U, 0xE001U}}};
constexpr std::array<std::array<Byte, 8>, 2> kWrittenIndices = {
    {{0U, 2U, 6U, 1U, 7U, 3U, 4U, 5U}, {0U, 5U, 1U, 3U, 7U, 2U, 4U, 6U}}};
constexpr std::array<Address, 8> kMmc3Ports = {
    0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U};
INesHeader header(Byte sub = 0U) {
    INesHeader h{};
    h.mapper_number = 114U;
    h.submapper = sub;
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
std::vector<Byte> chr(std::size_t size = 0x80000U) {
    auto data = banks(size, 1024U);
    for (std::size_t page = 0U; page < size / 1024U; ++page)
        data[page * 1024U + 1U] = static_cast<Byte>(0x40U + (page >> 8U));
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(),
                              std::size_t p = 0x80000U,
                              std::size_t c = 0x20000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, banks(p, 8192U), chr(c));
    REQUIRE(r);
    return std::move(*r);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void write(Mapper& m, Byte sub, unsigned port, Byte value, Address alias = 0U) {
    const Byte native =
        port == 0U ? static_cast<Byte>((value & 0xF8U) | kWrittenIndices[sub][value & 7U]) : value;
    m.write_prg(static_cast<Address>(kNativePorts[sub][port] | alias), native);
}
void bank(Mapper& m, Byte sub, Byte reg, Byte value, Byte mode = 0U) {
    write(m, sub, 0U, static_cast<Byte>(reg | mode));
    write(m, sub, 1U, value);
}
TEST_CASE("Scrambled 114 both submappers map all register data and bank-index bits",
          "[mappers][scrambled114]") {
    for (Byte sub : {0U, 1U}) {
        auto m = board(header(sub));
        auto h = header();
        h.mapper_number = 119U;
        auto ref = board(h);
        for (unsigned port = 0U; port < 8U; ++port)
            for (unsigned value = 0U; value < 256U; ++value) {
                m->reset();
                ref->reset();
                bank(*m, sub, 2U, 9U);
                ref->write_prg(0x8000U, 2U);
                ref->write_prg(0x8001U, 9U);
                write(*m, sub, port, static_cast<Byte>(value));
                ref->write_prg(kMmc3Ports[port], static_cast<Byte>(value));
                auto expected = state(*ref);
                expected.insert(expected.end(), {0U, 0U, sub});
                REQUIRE((state(*m) == expected));
            }
    }
}
TEST_CASE("Scrambled 114 register aliases preserve one-to-one latch and reload routing",
          "[mappers][scrambled114]") {
    for (Byte sub : {0U, 1U}) {
        auto m = board(header(sub));
        auto h = header();
        h.mapper_number = 119U;
        auto ref = board(h);
        for (unsigned port = 0U; port < 8U; ++port)
            for (Address alias : {0U, 2U, 0x2A4U, 0x1FFEU}) {
                m->reset();
                ref->reset();
                write(*m, sub, port, 0xC7U, alias);
                ref->write_prg(kMmc3Ports[port], 0xC7U);
                auto expected = state(*ref);
                expected.insert(expected.end(), {0U, 0U, sub});
                REQUIRE((state(*m) == expected));
            }
        m->reset();
        write(*m, sub, 4U, 2U);
        write(*m, sub, 5U, 5U);
        write(*m, sub, 7U, 0U);
        m->clock_irq_counter();
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
    }
}
TEST_CASE("Scrambled 114 NROM control ignores bits 6/4 while MMC3 retains its own PRG A18",
          "[mappers][scrambled114]") {
    for (Byte sub : {0U, 1U}) {
        auto m = board(header(sub));
        bank(*m, sub, 6U, 35U);
        bank(*m, sub, 7U, 37U);
        for (Byte swap : {0U, 0x40U}) {
            bank(*m, sub, 6U, 35U, swap);
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                m->write_prg(0x7FFEU, static_cast<Byte>(raw)); // Tentative $E001 alias.
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    unsigned expected = 0U;
                    if ((raw & 0x80U) == 0U) {
                        const std::array<unsigned, 4> pages =
                            swap != 0U ? std::array<unsigned, 4>{62U, 37U, 35U, 63U}
                                       : std::array<unsigned, 4>{35U, 37U, 62U, 63U};
                        expected = pages[slot];
                    } else if ((raw & 0x20U) == 0U)
                        expected = 2U * (raw & 15U) + (slot & 1U);
                    else
                        expected = 2U * (raw & 14U) + slot;
                    REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) == expected);
                }
            }
        }
        m->write_prg(0x6000U, 0x8FU);
        bank(*m, sub, 6U, 7U);
        bank(*m, sub, 7U, 9U);
        REQUIRE(m->read_prg(0x8000U) == 30U);
        m->write_prg(0x6000U, 0U);
        REQUIRE(m->read_prg(0x8000U) == 7U);
        REQUIRE(m->read_prg(0xA000U) == 9U);
    }
}
TEST_CASE("Scrambled 114 CHR outer bit and low control aliases bypass RAM gates",
          "[mappers][scrambled114]") {
    for (Byte sub : {0U, 1U}) {
        auto m = board(header(sub), 0x80000U, 0x80000U);
        bank(*m, sub, 0U, 0x83U);
        bank(*m, sub, 2U, 0xE5U);
        for (Byte gate : {0U, 0x80U, 0xC0U}) {
            write(*m, sub, 3U, gate);
            for (Byte outer : {0U, 1U, 0xFEU, 0xFFU}) {
                m->write_prg(0x6003U, outer);
                for (Byte inversion : {0U, 0x80U}) {
                    write(*m, sub, 0U, inversion);
                    const Address pair = inversion != 0U ? 0x1000U : 0U;
                    REQUIRE(m->read_chr(pair) == 0x82U);
                    REQUIRE(m->read_chr(static_cast<Address>(pair + 1024U)) == 0x83U);
                    REQUIRE(m->read_chr(static_cast<Address>(pair + 1U)) == 0x40U + (outer & 1U));
                    REQUIRE(m->read_chr(static_cast<Address>(pair ^ 0x1000U)) == 0xE5U);
                    m->write_chr(pair, 0U);
                    REQUIRE(m->read_chr(pair) == 0x82U);
                }
            }
            m->write_prg(0x6002U, 0x8FU);
            REQUIRE(m->read_prg(0x8000U) == 30U);
        }
        REQUIRE_FALSE(m->maps_prg(0x6002U));
        REQUIRE_FALSE(m->configure_board_jumpers(0U));
        REQUIRE(m->battery_ram().empty());
    }
}
TEST_CASE(
    "Scrambled 114 MMC3A zero reload triggers once when forced and never repeats automatically",
    "[mappers][scrambled114]") {
    for (Byte sub : {0U, 1U}) {
        auto m = board(header(sub));
        write(*m, sub, 4U, 0U);
        write(*m, sub, 7U, 0U);
        for (unsigned i = 0U; i < 4U; ++i) {
            m->clock_irq_counter();
            REQUIRE_FALSE(m->irq_pending());
        }
        write(*m, sub, 5U, 0U);
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->acknowledge_irq();
        for (unsigned i = 0U; i < 4U; ++i) {
            m->clock_irq_counter();
            REQUIRE_FALSE(m->irq_pending());
        }
        write(*m, sub, 4U, 2U);
        write(*m, sub, 5U, 0U);
        m->clock_irq_counter();
        write(*m, sub, 4U, 0U); // Changing the latch does not discard an in-flight count.
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->acknowledge_irq();
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
    }
}
TEST_CASE("Scrambled 114 snapshots validate wiring identity, override bits and partial IRQ state",
          "[mappers][scrambled114]") {
    for (Byte sub : {0U, 1U}) {
        auto m = board(header(sub), 0x80000U, 0x80000U);
        bank(*m, sub, 2U, 0x65U);
        m->write_prg(0x6000U, 0x8DU);
        m->write_prg(0x6001U, 1U);
        write(*m, sub, 4U, 2U);
        write(*m, sub, 5U, 0U);
        write(*m, sub, 7U, 0U);
        m->clock_irq_counter();
        const auto saved = state(*m);
        auto copy = board(header(sub), 0x80000U, 0x80000U);
        StateReader r(saved);
        copy->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(r.remaining() == 0U);
        REQUIRE(copy->read_prg(0x8000U) == 26U);
        REQUIRE(copy->read_chr(0x1001U) == 0x41U);
        copy->clock_irq_counter();
        REQUIRE_FALSE(copy->irq_pending());
        copy->clock_irq_counter();
        REQUIRE(copy->irq_pending());
        auto other = board(header(static_cast<Byte>(sub ^ 1U)));
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        for (std::size_t offset : {16384UZ,
                                   16385UZ,
                                   16392UZ,
                                   16394UZ,
                                   saved.size() - 3U,
                                   saved.size() - 2U,
                                   saved.size() - 1U}) {
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
        REQUIRE(copy->read_chr(1U) == 0x40U);
    }
}
TEST_CASE("Scrambled 114 geometry accepts only assigned wirings and rejects duplicate mapper 182",
          "[mappers][scrambled114]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 12U; ++kind) {
        auto h = header();
        std::size_t p = 0x80000U, c = 0x80000U;
        switch (kind) {
        case 0U:
            h.submapper = 2U;
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
            c = 0x80400U;
            break;
        case 10U:
            c = 8193U;
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
    auto small = board(header(1U), 32768U, 8192U);
    small->write_prg(0x6000U, 0xCFU);
    REQUIRE(small->read_prg(0x8000U) == 2U);
    h = header();
    h.mapper_number = 182U;
    auto duplicate = MapperRegistry::instance().create(h, banks(0x80000U, 8192U), chr());
    REQUIRE_FALSE(duplicate);
    REQUIRE(duplicate.error().find("Unsupported mapper") != std::string::npos);
}
std::vector<Byte> image(Byte sub = 0U, bool legacy = false) {
    auto p = banks(0x80000U, 8192U);
    const auto c = chr();
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 32U;
    rom[5] = 64U;
    rom[6] = 0x21U;
    rom[7] = legacy ? 0x70U : 0x78U;
    rom[8] = legacy ? 0U : static_cast<Byte>(sub << 4U);
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address a, Byte v) {
        code.insert(code.end(),
                    {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    };
    const auto mapper_write = [sub, &store](unsigned port, Byte value) {
        const Byte native =
            port == 0U ? static_cast<Byte>((value & 0xF8U) | kWrittenIndices[sub][value & 7U])
                       : value;
        store(kNativePorts[sub][port], native);
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
    set_bank(6U, 3U);
    set_bank(7U, 5U);
    expect(0x9A00U, 3U);
    expect(0xBA00U, 5U);
    expect(0xDA00U, 62U);
    expect(0xFA00U, 63U);
    store(0x6000U, 0x40U);
    expect(0x9A00U, 3U); // Bit 6 is not a PRG outer pin.
    set_bank(6U, 35U);
    expect(0x9A00U, 35U);
    mapper_write(0U, 0x46U);
    expect(0x9A00U, 62U);
    expect(0xDA00U, 35U);
    store(0x6002U, 0x80U);
    expect(0x9A00U, 0U);
    expect(0xBA00U, 1U);
    expect(0xDA00U, 0U);
    expect(0xFA00U, 1U);
    mapper_write(3U, 0U);
    store(0x6000U, 0xCFU);
    expect(0x9A00U, 30U);
    expect(0xFA00U, 31U);
    store(0x7FFEU, 0xA1U);
    expect(0x9A00U, 0U);
    expect(0xBA00U, 1U);
    expect(0xDA00U, 2U);
    expect(0xFA00U, 3U);
    set_bank(6U, 7U);
    set_bank(7U, 9U);
    expect(0x9A00U, 0U);
    store(0x6000U, 0U);
    expect(0x9A00U, 7U);
    expect(0xBA00U, 9U);
    set_bank(0U, 3U);
    set_bank(2U, 0x65U);
    pread(0x1000U, 0x65U);
    pread(0x1001U, 0x40U);
    store(0x6003U, 0xFFU);
    pread(0x1001U, 0x41U);
    mapper_write(0U, 0x80U);
    pread(0U, 0x65U);
    pread(1U, 0x41U);
    pread(0x1000U, 2U);
    store(0x6001U, 0U);
    pread(1U, 0x40U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    mapper_write(2U, 1U);
    pread(0x2400U, 0x19U);
    mapper_write(2U, 0U);
    pread(0x2400U, 0x27U);
    store(0x6000U, 0x80U); // IRQs still operate under NROM override.
    mapper_write(4U, 0U);
    mapper_write(5U, 0U);
    mapper_write(7U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 1U, 0x90U, 0xFAU});
    // Let eleven scanlines pass. A zero automatic reload must not repeat.
    code.insert(code.end(), {0xA2U, 0U, 0xCAU, 0xD0U, 0xFDU});
    expect(0x11U, 1U);
    mapper_write(6U, 0U);
    mapper_write(4U, 2U);
    mapper_write(5U, 0U);
    mapper_write(7U, 0U);
    code.insert(code.end(), {0xA5U, 0x11U, 0xC9U, 2U, 0x90U, 0xFAU});
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
Cartridge cartridge(Byte sub = 0U, bool legacy = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(sub, legacy));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("Scrambled 114 low reads are undriven, including control aliases",
          "[mappers][scrambled114]") {
    for (Byte sub : {0U, 1U}) {
        auto c = cartridge(sub);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        for (Address a : {0x6000U, 0x6001U, 0x6002U, 0x6003U, 0x7FFFU}) {
            bus.write(0x10U, 0xA5U);
            REQUIRE(bus.read(a) == 0xA5U);
        }
        REQUIRE_FALSE(c.has_battery());
        REQUIRE(c.battery_ram().empty());
    }
}
TEST_CASE("Scrambled 114 original CPU programs verify native banking and non-repeating IRQs before "
          "replay",
          "[mappers][scrambled114]") {
    for (unsigned profile = 0U; profile < 3U; ++profile) {
        const Byte sub = profile == 1U ? 1U : 0U;
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(cartridge(sub, profile == 2U), ""));
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
        bad[bad.size() - tail.data().size() - 1U] = static_cast<Byte>(sub ^ 1U);
        const auto before = emulator.save_state();
        REQUIRE_FALSE(emulator.load_state(bad));
        REQUIRE((emulator.save_state() == before));
    }
}
} // namespace
