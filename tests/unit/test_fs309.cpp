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
INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 199U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.has_battery = true;
    h.prg_nvram_size = legacy ? 8192U : 32768U;
    h.chr_ram_size = 8192U;
    return h;
}
std::vector<Byte> rom(std::size_t size = 0x100000U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 8192U);
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(), std::size_t p = 0x100000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, rom(p), {});
    REQUIRE(r);
    return std::move(*r);
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(reg | mode));
    m.write_prg(0x8001U, value);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
constexpr std::size_t kRamPrefix = 8U + 32768U + 8192U;
constexpr std::size_t kTag = kRamPrefix + 15U;
TEST_CASE("FS309 uses conventional PRG banking across the full 1 MiB ROM", "[mappers][fs309]") {
    auto m = board();
    bank(*m, 6U, 0x50U);
    REQUIRE(m->read_prg(0x8000U) == 0x50U);
    REQUIRE(m->read_prg(0xC000U) == 0x7EU);
    REQUIRE(m->read_prg(0xE000U) == 0x7FU);
}
TEST_CASE("FS309 exposes its whole SRAM chip and keeps CHR RAM unbanked", "[mappers][fs309]") {
    auto m = board();
    m->write_chr(0x400U, 0x73U);
    bank(*m, 0U, 0xFEU, 0x80U);
    bank(*m, 2U, 0xA7U, 0x80U);
    REQUIRE(m->read_chr(0x400U) == 0x73U);
    m->write_expansion(0x5000U, 0x39U);
    REQUIRE(m->battery_ram().size() == 32768U);
    REQUIRE(m->battery_ram()[0x5000U] == 0x39U);
}

TEST_CASE("FS309 all PRG bytes wrap a conventional ROM in both MMC3 modes", "[mappers][fs309]") {
    for (bool legacy : {false, true})
        for (std::size_t size : {32768UZ, 98304UZ, 0x80000UZ, 0xA0000UZ, 0x100000UZ}) {
            auto m = board(header(legacy), size);
            const auto count = size / 8192U;
            for (Byte mode : {0U, 0x40U})
                for (unsigned value = 0U; value < 256U; ++value) {
                    m->write_prg(0x9FFEU, static_cast<Byte>(6U | mode));
                    m->write_prg(0x9FFFU, static_cast<Byte>(value));
                    bank(*m, 7U, static_cast<Byte>(value ^ 0x35U), mode);
                    const std::array<std::size_t, 4> expected = {
                        mode == 0U ? (value % 128U) % count : count - 2U,
                        ((value ^ 0x35U) % 128U) % count,
                        mode == 0U ? count - 2U : (value % 128U) % count,
                        count - 1U};
                    for (unsigned slot = 0U; slot < 4U; ++slot)
                        REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U +
                                                                 0x321U)) == expected[slot]);
                }
        }
}
TEST_CASE("FS309 every CHR register value and inversion mode leaves RAM unbanked",
          "[mappers][fs309]") {
    auto m = board();
    std::array<Byte, 8192> expected{};
    for (unsigned a = 0U; a < expected.size(); ++a) {
        expected[a] = static_cast<Byte>(a ^ (a >> 8U));
        m->write_chr(static_cast<Address>(a), expected[a]);
    }
    for (Byte mode : {0U, 0x80U})
        for (Byte reg = 0U; reg < 6U; ++reg)
            for (unsigned value = 0U; value < 256U; ++value) {
                bank(*m, reg, static_cast<Byte>(value), mode);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto a = static_cast<Address>(slot * 1024U + 0x3FFU);
                    REQUIRE(m->read_chr(a) == expected[a]);
                    expected[a] = static_cast<Byte>(value ^ (slot * 17U));
                    m->write_chr(a, expected[a]);
                    REQUIRE(m->read_chr(a) == expected[a]);
                    REQUIRE(m->read_chr(a ^ 0x1000U) == expected[a ^ 0x1000U]);
                }
            }
    m->reset();
    for (unsigned a = 0U; a < expected.size(); ++a)
        REQUIRE(m->read_chr(static_cast<Address>(a)) == expected[a]);
}
TEST_CASE("FS309 SRAM preserves both mapped windows and the unexposed chip contents",
          "[mappers][fs309]") {
    std::vector<Byte> data(32768U);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>(i ^ (i >> 8U));
    for (bool legacy : {false, true}) {
        auto m = board(header(legacy));
        m->set_battery_ram(data);
        REQUIRE(std::ranges::equal(m->battery_ram(), data));
        for (unsigned a = 0x5000U; a < 0x8000U; ++a) {
            const auto addr = static_cast<Address>(a);
            REQUIRE((a < 0x6000U ? m->read_expansion(addr) : m->read_prg(addr)) == data[a]);
            const auto value = static_cast<Byte>(data[a] ^ 0xA7U);
            if (a < 0x6000U)
                m->write_expansion(addr, value);
            else
                m->write_prg(addr, value);
            REQUIRE(m->battery_ram()[a] == value);
        }
        m->reset();
        for (std::size_t a = 0U; a < data.size(); ++a)
            REQUIRE(m->battery_ram()[a] ==
                    (a < 0x5000U ? data[a] : static_cast<Byte>(data[a] ^ 0xA7U)));
        m->write_chr(0x400U, 0x73U);
        auto copy = board();
        copy->set_battery_ram(m->battery_ram());
        REQUIRE(std::ranges::equal(copy->battery_ram(), m->battery_ram()));
        REQUIRE(copy->read_chr(0x400U) == 0U);
    }
}
TEST_CASE("FS309 A001 gates the base window while the low battery window remains accessible",
          "[mappers][fs309]") {
    auto m = board();
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        m->write_prg(0xA001U, 0x80U);
        m->write_prg(0x6000U, 0x59U);
        m->write_prg(0xBFFFU, static_cast<Byte>(gate));
        m->write_prg(0x6000U, 0xA7U);
        m->write_expansion(0x5000U, static_cast<Byte>(gate));
        m->write_expansion(0x5FFFU, static_cast<Byte>(gate ^ 0x73U));
        m->write_chr(0x400U, static_cast<Byte>(gate ^ 0x39U));
        REQUIRE(m->maps_prg(0x6000U) == ((gate & 0x80U) != 0U));
        REQUIRE(m->read_expansion(0x5000U) == gate);
        REQUIRE(m->read_expansion(0x5FFFU) == (gate ^ 0x73U));
        REQUIRE(m->battery_ram()[0x5000U] == gate);
        REQUIRE(m->battery_ram()[0x5FFFU] == (gate ^ 0x73U));
        REQUIRE(m->read_chr(0x400U) == (gate ^ 0x39U));
        m->write_prg(0xA001U, 0x80U);
        REQUIRE(m->read_prg(0x6000U) == ((gate & 0xC0U) == 0x80U ? 0xA7U : 0x59U));
    }
    bank(*m, 6U, 0x50U);
    m->write_expansion(0x5FF3U, 2U); // FS309 SRAM, not a mapper-176 mode register.
    REQUIRE(m->read_expansion(0x5FF3U) == 2U);
    REQUIRE(m->read_prg(0x8000U) == 0x50U);
    for (Address a : {0x4020U, 0x4100U, 0x4FFFU}) {
        REQUIRE_FALSE(m->maps_expansion(a));
        m->write_expansion(a, 0xFFU);
    }
}
TEST_CASE("FS309 snapshots retain the full chip, unbanked CHR, seven PRG bits and IRQ progress",
          "[mappers][fs309][state]") {
    for (bool legacy : {false, true}) {
        auto m = board(header(legacy));
        std::vector<Byte> data(32768U, 0xD9U);
        m->set_battery_ram(data);
        m->write_expansion(0x5000U, 0x73U);
        m->write_prg(0x6000U, 0x59U);
        m->write_chr(0x400U, 0x39U);
        bank(*m, 6U, 0x7FU, 0xC0U);
        bank(*m, 7U, 0x50U, 0xC0U);
        bank(*m, 0U, 0xFEU, 0xC0U);
        m->write_prg(0xA000U, 1U);
        m->write_prg(0xA001U, 0xC0U);
        m->write_prg(0xC000U, 2U);
        m->write_prg(0xC001U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        const auto saved = state(*m);
        REQUIRE(saved.size() == kTag + 2U);
        auto copy = board();
        StateReader r(saved);
        copy->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(r.remaining() == 0U);
        REQUIRE((state(*copy) == saved));
        REQUIRE(copy->read_prg(0xC000U) == 0x7FU);
        REQUIRE(copy->read_prg(0xA000U) == 0x50U);
        REQUIRE(copy->read_chr(0x400U) == 0x39U);
        REQUIRE(copy->read_expansion(0x5000U) == 0x73U);
        REQUIRE(copy->battery_ram()[0U] == 0xD9U);
        copy->write_prg(0x6000U, 0U);
        REQUIRE(copy->read_prg(0x6000U) == 0x59U);
        REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
        copy->clock_irq_counter();
        REQUIRE_FALSE(copy->irq_pending());
        copy->clock_irq_counter();
        REQUIRE(copy->irq_pending());
        copy->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(copy->irq_pending());
        copy->write_prg(0xC000U, 0U);
        copy->write_prg(0xE001U, 0U);
        for (unsigned i = 0U; i < 3U; ++i) {
            copy->clock_irq_counter();
            REQUIRE(copy->irq_pending());
            copy->acknowledge_irq();
        }
        for (std::size_t length : {0UZ, 32775UZ, kTag, saved.size() - 1U}) {
            StateReader short_state(std::span(saved).first(length));
            copy->load_state(short_state);
            REQUIRE_FALSE(short_state.ok());
        }
        for (std::size_t offset : {0UZ, 7UZ, kRamPrefix + 8U, kRamPrefix + 9U, kTag, kTag + 1U}) {
            auto bad = saved;
            bad[offset] |= 0x80U;
            if (offset == kTag || offset == kTag + 1U)
                bad[offset] ^= 0xFFU;
            StateReader malformed(bad);
            copy->load_state(malformed);
            REQUIRE_FALSE(malformed.ok());
            copy->reset();
            REQUIRE(copy->battery_ram().size() == 32768U);
            copy->write_prg(0x7FFFU, 0xA7U);
            REQUIRE(copy->read_prg(0x7FFFU) == 0xA7U);
            copy->write_expansion(0x5FFFU, 0x73U);
            REQUIRE(copy->read_expansion(0x5FFFU) == 0x73U);
        }
    }
}
TEST_CASE("FS309 validates the SRAM density and ROM/CHR population", "[mappers][fs309]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header();
        auto p = rom();
        std::vector<Byte> c;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = false;
            break;
        case 2U:
            h.prg_ram_size = 4096U;
            break;
        case 3U:
            h.prg_nvram_size = 0U;
            break;
        case 4U:
            h.prg_nvram_size = 8192U;
            break;
        case 5U:
            h.prg_nvram_size = 16384U;
            break;
        case 6U:
            h.chr_ram_size = 0U;
            break;
        case 7U:
            h.chr_ram_size = 16384U;
            break;
        case 8U:
            h.chr_nvram_size = 8192U;
            break;
        case 9U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 10U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 11U:
            p.resize(16384U);
            break;
        case 12U:
            p.resize(0x102000U);
            break;
        case 13U:
            p.pop_back();
            break;
        case 14U:
            c.resize(8192U);
            break;
        case 15U:
            h.is_nes2 = false;
            h.has_battery = false;
            break;
        case 16U:
            h.prg_nvram_size = 65536U;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(MapperRegistry::instance().create(h, std::move(p), std::move(c)));
    }
}

std::vector<Byte> image(bool legacy = false, bool vertical = true) {
    auto p = rom();
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
    const auto set_bank = [&store](Byte reg, Byte value, Byte mode = 0U) {
        store(0x8000U, static_cast<Byte>(reg | mode));
        store(0x8001U, value);
    };
    const auto address = [&store](Address a) {
        store(0x2006U, static_cast<Byte>(a >> 8U));
        store(0x2006U, static_cast<Byte>(a));
    };
    const auto pwrite = [&store, &address](Address a, Byte v) {
        address(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &expect, &address](Address a, Byte v) {
        address(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    set_bank(6U, 0x50U);
    set_bank(7U, 0x7DU);
    expect(0x9A00U, 0x50U);
    expect(0xBA00U, 0x7DU);
    expect(0xDA00U, 0x7EU);
    expect(0xFA00U, 0x7FU);
    set_bank(6U, 0x50U, 0x40U);
    expect(0x9A00U, 0x7EU);
    expect(0xDA00U, 0x50U);
    set_bank(6U, 0xFFU, 0x40U);
    expect(0xDA00U, 0x7FU);
    set_bank(6U, 0x80U, 0x40U);
    expect(0xDA00U, 0U);
    const std::array<Address, 4> locations = {0U, 0x400U, 0x1000U, 0x1FFFU};
    const std::array<Byte, 4> values = {0x31U, 0x73U, 0x59U, 0xA7U};
    for (std::size_t i = 0U; i < locations.size(); ++i)
        pwrite(locations[i], values[i]);
    constexpr std::array<Byte, 6> kScrambledChr = {0x6BU, 0xF1U, 3U, 0x7DU, 0x99U, 0xABU};
    for (Byte mode : {0U, 0x80U}) {
        for (Byte reg = 0U; reg < 6U; ++reg)
            set_bank(reg, kScrambledChr[reg], mode);
        for (std::size_t i = 0U; i < locations.size(); ++i)
            pread(locations[i], values[i]);
    }
    store(0xA001U, 0x80U);
    store(0x6000U, 0x59U);
    for (Byte gate : {0U, 0xC0U}) {
        store(0xA001U, gate);
        store(0x6000U, 0U);
        store(0x5000U, static_cast<Byte>(gate ^ 0x39U));
        store(0x5FFFU, static_cast<Byte>(gate ^ 0x73U));
        expect(0x5000U, static_cast<Byte>(gate ^ 0x39U));
        expect(0x5FFFU, static_cast<Byte>(gate ^ 0x73U));
        pwrite(0x400U, static_cast<Byte>(gate ^ 0xA7U));
        pread(0x400U, static_cast<Byte>(gate ^ 0xA7U));
        if (gate == 0xC0U)
            expect(0x6000U, 0x59U);
        store(0xA001U, 0x80U);
        expect(0x6000U, 0x59U);
    }
    store(0xA000U, 0U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0xA000U, 1U);
    pread(0x2400U, 0x19U);
    store(0xA000U, 0U);
    pread(0x2400U, 0x27U);
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 1U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    store(0xC000U, 0U);
    store(0xE001U, 0U);
    code.insert(code.end(), {0xA5U, 0x11U, 0xC9U, 3U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    store(0x5000U, 0xA7U); // Mark the persistent low window after all CPU checks.
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    constexpr std::array<Byte, 13> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0xE6U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(code, p.begin() + 0xFE000U);
    std::ranges::copy(kIrq, p.begin() + 0xFF800U);
    for (std::size_t v = 0xFFFFAU; v < 0x100000U; v += 2U) {
        p[v] = 0U;
        p[v + 1U] = v == 0xFFFFCU ? 0xE0U : 0xF8U;
    }
    std::vector<Byte> result(16U + p.size());
    result[0] = 'N';
    result[1] = 'E';
    result[2] = 'S';
    result[3] = 0x1AU;
    result[4] = 64U;
    result[6] = static_cast<Byte>(0x72U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0xC0U : 0xC8U;
    result[10] = legacy ? 0U : 0x90U;
    result[11] = legacy ? 0U : 7U;
    std::ranges::copy(p, result.begin() + 16U);
    return result;
}
Cartridge cartridge(bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(legacy, vertical));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("FS309 low SRAM drives the full bus while disabled base WRAM remains open",
          "[mappers][fs309]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    bus.write(0x5000U, 0x39U);
    bus.write(0x5FFFU, 0x73U);
    bus.write(0xA001U, 0U);
    for (Byte value : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 255U}) {
        for (Address a : {0x4020U, 0x4100U, 0x4FFFU, 0x6000U, 0x7FFFU}) {
            bus.write(0x10U, value);
            REQUIRE(bus.read(a) == value);
        }
        bus.write(0x10U, value);
        REQUIRE(bus.read(0x5000U) == 0x39U);
        bus.write(0x10U, value);
        REQUIRE(bus.read(0x5FFFU) == 0x73U);
    }
    auto m = board();
    for (unsigned a = 0x4000U; a < 0x8000U; ++a)
        REQUIRE(m->maps_expansion(static_cast<Address>(a)) == (a >= 0x5000U && a < 0x6000U));
}
TEST_CASE(
    "FS309 original CPU fixtures check fixed CHR, both SRAM windows, IRQs and state integrity",
    "[mappers][fs309][state]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            INFO(legacy);
            INFO(vertical);
            Emulator e;
            REQUIRE(e.load_cartridge(cartridge(legacy, vertical), ""));
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x11U) >= 3U);
            REQUIRE(e.read_cpu(0x5000U) == 0xA7U);
            REQUIRE(e.read_cpu(0x6000U) == 0x59U);
            REQUIRE(e.read_cpu(0x5FFFU) == 0xB3U);
            const auto early = e.save_state();
            e.step_frame();
            const auto future = e.save_state();
            REQUIRE(e.load_state(early));
            e.step_frame();
            REQUIRE((e.save_state() == future));
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            const auto start = early.size() - tail.data().size() - state(*board()).size();
            for (std::size_t offset : {0UZ, 7UZ, kTag}) {
                auto bad = early;
                bad[start + 8U + 0x5000U] ^= 0xA5U;
                bad[start + 8U + 32768U + 0x400U + 13U] ^= 0xA5U;
                bad[start + offset] ^= 0xFFU;
                const auto before = e.save_state();
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE((e.save_state() == before));
            }
        }
}
} // namespace
