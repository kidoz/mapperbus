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
    h.mapper_number = 198U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.has_battery = true;
    h.prg_ram_size = legacy ? 0U : 4096U;
    h.prg_nvram_size = 8192U;
    h.chr_ram_size = 8192U;
    return h;
}
std::vector<Byte> rom() {
    std::vector<Byte> data(0xA0000U);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>(i / 8192U);
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header()) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, rom(), {});
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
TEST_CASE("Mapper 198 switches to the second chip and fixes banks 4E and 4F",
          "[mappers][mapper198]") {
    auto m = board();
    bank(*m, 6U, 0x40U);
    REQUIRE(m->read_prg(0x8000U) == 0x40U);
    REQUIRE(m->read_prg(0xC000U) == 0x4EU);
    REQUIRE(m->read_prg(0xE000U) == 0x4FU);
}
TEST_CASE("Mapper 198 auxiliary RAM is separate from battery memory", "[mappers][mapper198]") {
    auto m = board();
    REQUIRE(m->maps_expansion(0x5000U));
    m->write_expansion(0x5FFFU, 0x73U);
    m->write_prg(0x6000U, 0x59U);
    m->set_battery_ram(std::vector<Byte>(8192U, 0xA7U));
    REQUIRE(m->read_expansion(0x5FFFU) == 0x73U);
    REQUIRE(m->read_prg(0x6000U) == 0xA7U);
}

TEST_CASE("Mapper 198 all PRG bytes use two-chip decoding instead of linear ROM modulo",
          "[mappers][mapper198]") {
    for (bool legacy : {false, true}) {
        auto m = board(header(legacy));
        for (Byte mode : {0U, 0x40U})
            for (unsigned value = 0U; value < 256U; ++value) {
                m->write_prg(0x9FFEU, static_cast<Byte>(6U | mode));
                m->write_prg(0x9FFFU, static_cast<Byte>(value));
                bank(*m, 7U, static_cast<Byte>(value ^ 0x35U), mode);
                const auto chip_bank = [](unsigned data) {
                    const auto raw = data % 128U;
                    return raw < 64U ? raw : 64U + raw % 16U;
                };
                const std::array<unsigned, 4> expected = {mode == 0U ? chip_bank(value) : 78U,
                                                          chip_bank(value ^ 0x35U),
                                                          mode == 0U ? 78U : chip_bank(value),
                                                          79U};
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                            expected[slot]);
                    REQUIRE(m->read_prg(static_cast<Address>(0x9FFFU + slot * 8192U)) ==
                            expected[slot]);
                }
            }
    }
}
TEST_CASE("Mapper 198 all CHR bytes bank physical RAM through paired and inverted slots",
          "[mappers][mapper198]") {
    constexpr std::array<Address, 6> kSlots = {0U, 0x800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
    for (bool legacy : {false, true}) {
        auto m = board(header(legacy));
        std::array<Byte, 8> ram{};
        for (Byte mode : {0U, 0x80U})
            for (Byte reg = 0U; reg < 6U; ++reg)
                for (unsigned value = 0U; value < 256U; ++value) {
                    bank(*m, reg, static_cast<Byte>(value), mode);
                    const auto base = kSlots[reg] ^ (mode == 0U ? 0U : 0x1000U);
                    for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                        const auto page = (reg < 2U ? (value & 0xFEU) + half : value) % 8U;
                        const auto a = static_cast<Address>(base + half * 1024U + 0x3FFU);
                        REQUIRE(m->read_chr(a) == ram[page]);
                        ram[page] = static_cast<Byte>(value ^ 0xA7U);
                        m->write_chr(a, ram[page]);
                        REQUIRE(m->read_chr(a) == ram[page]);
                    }
                }
    }
}
TEST_CASE("Mapper 198 every RAM byte has independent ownership and survives reset",
          "[mappers][mapper198]") {
    auto m = board();
    for (unsigned page = 0U; page < 8U; ++page) {
        bank(*m, 2U, static_cast<Byte>(page));
        for (unsigned byte = 0U; byte < 1024U; ++byte)
            m->write_chr(static_cast<Address>(0x1000U + byte),
                         static_cast<Byte>(page * 37U + byte));
    }
    for (unsigned i = 0U; i < 8192U; ++i)
        m->write_prg(static_cast<Address>(0x6000U + i), static_cast<Byte>(i ^ (i >> 8U)));
    for (unsigned i = 0U; i < 4096U; ++i)
        m->write_expansion(static_cast<Address>(0x5000U + i), static_cast<Byte>((i >> 4U) ^ 0x73U));
    const auto saved = state(*m);
    m->reset();
    for (unsigned i = 0U; i < 8192U; ++i)
        REQUIRE(m->read_prg(static_cast<Address>(0x6000U + i)) == static_cast<Byte>(i ^ (i >> 8U)));
    for (unsigned i = 0U; i < 4096U; ++i)
        REQUIRE(m->read_expansion(static_cast<Address>(0x5000U + i)) ==
                static_cast<Byte>((i >> 4U) ^ 0x73U));
    for (unsigned page = 0U; page < 8U; ++page) {
        bank(*m, 2U, static_cast<Byte>(page));
        for (unsigned byte = 0U; byte < 1024U; ++byte)
            REQUIRE(m->read_chr(static_cast<Address>(0x1000U + byte)) ==
                    static_cast<Byte>(page * 37U + byte));
    }
    auto copy = board();
    StateReader r(saved);
    copy->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE((state(*copy) == saved));
}
TEST_CASE("Mapper 198 all A001 bytes gate only battery WRAM", "[mappers][mapper198]") {
    auto m = board();
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        m->write_prg(0xA001U, 0x80U);
        m->write_prg(0x6000U, 0x59U);
        m->write_prg(0xBFFFU, static_cast<Byte>(gate));
        m->write_prg(0x6000U, 0xA7U);
        m->write_expansion(0x5000U, static_cast<Byte>(gate));
        m->write_expansion(0x5FFFU, static_cast<Byte>(gate ^ 0x73U));
        bank(*m, 2U, 0xFEU);
        m->write_chr(0x13FFU, static_cast<Byte>(gate ^ 0x39U));
        REQUIRE(m->maps_prg(0x6000U) == ((gate & 0x80U) != 0U));
        REQUIRE(m->read_expansion(0x5000U) == gate);
        REQUIRE(m->read_expansion(0x5FFFU) == (gate ^ 0x73U));
        REQUIRE(m->read_chr(0x13FFU) == (gate ^ 0x39U));
        m->write_prg(0xA001U, 0x80U);
        REQUIRE(m->read_prg(0x6000U) == ((gate & 0xC0U) == 0x80U ? 0xA7U : 0x59U));
    }
}
TEST_CASE("Mapper 198 only its 8 KiB NVRAM is exposed to battery storage", "[mappers][mapper198]") {
    auto m = board();
    bank(*m, 2U, 6U);
    m->write_chr(0x1000U, 0x39U);
    m->write_expansion(0x5000U, 0x73U);
    m->write_expansion(0x5FFFU, 0x59U);
    m->write_prg(0xA001U, 0U);
    std::vector<Byte> battery(8192U);
    for (std::size_t i = 0U; i < battery.size(); ++i)
        battery[i] = static_cast<Byte>(i ^ (i >> 8U));
    m->set_battery_ram(battery);
    REQUIRE(std::ranges::equal(m->battery_ram(), battery));
    REQUIRE(m->read_chr(0x1000U) == 0x39U);
    REQUIRE(m->read_expansion(0x5000U) == 0x73U);
    REQUIRE(m->read_expansion(0x5FFFU) == 0x59U);
    m->write_prg(0xA001U, 0x80U);
    for (unsigned i = 0U; i < 8192U; ++i)
        REQUIRE(m->read_prg(static_cast<Address>(0x6000U + i)) == battery[i]);
    auto fresh = board();
    fresh->set_battery_ram(m->battery_ram());
    REQUIRE(std::ranges::equal(fresh->battery_ram(), battery));
    bank(*fresh, 2U, 6U);
    REQUIRE(fresh->read_chr(0x1000U) == 0U);
    REQUIRE(fresh->read_expansion(0x5000U) == 0U);
    REQUIRE(fresh->read_expansion(0x5FFFU) == 0U);
}
TEST_CASE("Mapper 198 snapshots retain seven PRG bits, all RAM, mirroring and IRQ progress",
          "[mappers][mapper198][state]") {
    for (bool legacy : {false, true}) {
        auto m = board(header(legacy));
        bank(*m, 6U, 0x7FU, 0xC0U);
        bank(*m, 7U, 0x70U, 0xC0U);
        bank(*m, 2U, 0xFEU, 0xC0U);
        m->write_chr(0U, 0x39U);
        m->write_prg(0x6000U, 0x59U);
        m->write_expansion(0x5FFFU, 0x73U);
        m->write_prg(0xA000U, 1U);
        m->write_prg(0xA001U, 0xC0U);
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
        REQUIRE((state(*copy) == saved));
        REQUIRE(copy->read_prg(0xC000U) == 0x4FU);
        REQUIRE(copy->read_prg(0xA000U) == 0x40U);
        REQUIRE(copy->read_chr(0U) == 0x39U);
        REQUIRE(copy->read_expansion(0x5FFFU) == 0x73U);
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
        for (std::size_t length : {0UZ, 16399UZ, saved.size() - 1U}) {
            StateReader short_state(std::span(saved).first(length));
            copy->load_state(short_state);
            REQUIRE_FALSE(short_state.ok());
        }
        for (std::size_t offset : {16392UZ, 16393UZ, 16399UZ, 16400UZ, 16401UZ, 16408UZ}) {
            auto bad = saved;
            bad[offset] |= 0x80U;
            if (offset == 16399U || offset == 16400U)
                bad[offset] ^= 0xFFU;
            StateReader malformed(bad);
            copy->load_state(malformed);
            REQUIRE_FALSE(malformed.ok());
            copy->reset();
            copy->write_expansion(0x5FFFU, 0xA7U);
            REQUIRE(copy->read_expansion(0x5FFFU) == 0xA7U);
        }
    }
}
TEST_CASE("Mapper 198 validates its fixed ROM and RAM population", "[mappers][mapper198]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 16U; ++kind) {
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
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 8192U;
            break;
        case 4U:
            h.prg_nvram_size = 0U;
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
            p.resize(0x80000U);
            break;
        case 12U:
            p.resize(0xC0000U);
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
    set_bank(6U, 0x40U);
    set_bank(7U, 0x4DU);
    expect(0x9A00U, 0x40U);
    expect(0xBA00U, 0x4DU);
    expect(0xDA00U, 0x4EU);
    expect(0xFA00U, 0x4FU);
    set_bank(6U, 0x40U, 0x40U);
    expect(0x9A00U, 0x4EU);
    expect(0xDA00U, 0x40U);
    for (const auto [raw, expected] : std::array<std::array<Byte, 2>, 5>{
             {{0x50U, 0x40U}, {0x7FU, 0x4FU}, {0x80U, 0U}, {0xBFU, 0x3FU}, {0xFFU, 0x4FU}}}) {
        set_bank(6U, raw, 0x40U);
        expect(0xDA00U, expected);
    }
    for (Byte page = 0U; page < 8U; ++page) {
        set_bank(2U, page);
        pwrite(0x1000U, static_cast<Byte>(0x70U + page));
    }
    set_bank(0U, 3U);
    pread(0U, 0x72U);
    pread(0x400U, 0x73U);
    set_bank(1U, 5U);
    pread(0x800U, 0x74U);
    pread(0xC00U, 0x75U);
    set_bank(2U, 0xFEU, 0x80U);
    pread(0U, 0x76U);
    pwrite(0U, 0xA7U);
    pread(0U, 0xA7U);
    pread(0x1000U, 0x72U);
    store(0x6000U, 0x59U);
    store(0x5000U, 0x39U);
    store(0x5FFFU, 0x73U);
    for (Byte gate : {0U, 0xC0U}) {
        store(0xA001U, gate);
        store(0x6000U, 0U);
        store(0x5000U, static_cast<Byte>(gate ^ 0x39U));
        store(0x5FFFU, static_cast<Byte>(gate ^ 0x73U));
        expect(0x5000U, static_cast<Byte>(gate ^ 0x39U));
        expect(0x5FFFU, static_cast<Byte>(gate ^ 0x73U));
        pwrite(0U, static_cast<Byte>(gate ^ 0xA7U));
        pread(0U, static_cast<Byte>(gate ^ 0xA7U));
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
    store(0x6000U, 0xA7U); // Battery marker is reached only after CPU checks pass.
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    constexpr std::array<Byte, 13> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0xE6U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(code, p.begin() + 0x9E000U);
    std::ranges::copy(kIrq, p.begin() + 0x9F800U);
    for (std::size_t v = 0x9FFFAU; v < 0xA0000U; v += 2U) {
        p[v] = 0U;
        p[v + 1U] = v == 0x9FFFCU ? 0xE0U : 0xF8U;
    }
    std::vector<Byte> result(16U + p.size());
    result[0] = 'N';
    result[1] = 'E';
    result[2] = 'S';
    result[3] = 0x1AU;
    result[4] = 40U;
    result[6] = static_cast<Byte>(0x62U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0xC0U : 0xC8U;
    result[10] = legacy ? 0U : 0x76U;
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
TEST_CASE("Mapper 198 auxiliary RAM drives the full bus while disabled WRAM reads stay open",
          "[mappers][mapper198]") {
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
TEST_CASE("Mapper 198 original CPU fixtures check banks, all RAM types, IRQs and state integrity",
          "[mappers][mapper198][state]") {
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
            REQUIRE(e.read_cpu(0x6000U) == 0xA7U);
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
            for (std::size_t offset : {16399UZ, 16401UZ, 16408UZ}) {
                auto bad = early;
                bad[start + 8192U + 6144U + 13U] ^= 0xA5U;
                bad[start + 16409U + 4095U] ^= 0xA5U;
                bad[start + offset] ^= 0xFFU;
                const auto before = e.save_state();
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE((e.save_state() == before));
            }
        }
}
} // namespace
