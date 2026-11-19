#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper242_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(bool legacy = false, bool battery = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 242U;
    h.is_nes2 = !legacy;
    h.has_battery = battery;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.prg_nvram_size = battery && !legacy ? 8192U : 0U;
    h.chr_ram_size = 8192U;
    return h;
}
std::unique_ptr<Mapper> board(bool legacy = false,
                              bool battery = false,
                              bool et = false,
                              Byte pads = 0U) {
    register_builtin_mappers();
    std::vector<Byte> prg(et ? 655360U : 524288U);
    for (unsigned i = 0; i < prg.size(); ++i)
        prg[i] = mapperbus::tests::mapper242_byte(i);
    auto m = MapperRegistry::instance().create(header(legacy, battery), std::move(prg), {});
    REQUIRE(m);
    REQUIRE((*m)->configure_board_jumpers(pads));
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("Mapper 242 all address bits match documented bank pins for three populations",
          "[mapper242]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        const bool battery = population == 1U, et = population == 2U;
        for (bool legacy : {false, true}) {
            auto m = board(legacy, battery, et);
            REQUIRE(m->read_prg(0x8000U) == mapperbus::tests::mapper242_byte(et ? 524288U : 0U));
            REQUIRE(m->read_prg(0xC000U) == m->read_prg(0x8000U));
            for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
                m->reset();
                m->write_prg(static_cast<Address>(addr), 0xA7U);
                const auto latch = static_cast<Address>(addr >= 0x8000U ? addr & 0x7FFU : 0U);
                for (Address cpu : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU})
                    REQUIRE(m->read_prg(cpu) ==
                            mapperbus::tests::mapper242_byte(
                                mapperbus::tests::mapper242_offset(cpu, latch, battery, et, 0U)));
                REQUIRE(m->mirror_mode() ==
                        ((latch & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
                REQUIRE(m->maps_prg(static_cast<Address>(addr)) ==
                        (addr >= 0x8000U || (battery && addr >= 0x6000U)));
            }
            REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
            REQUIRE(state(*m).size() == (battery ? 16388U : 8196U));
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE_FALSE(m->irq_pending());
        }
    }
}
TEST_CASE("Mapper 242 data is ignored and menu pads replace all five low ROM pins", "[mapper242]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        const bool battery = population == 1U, et = population == 2U;
        auto m = board(false, battery, et);
        for (Byte pads = 0U; pads < 32U; ++pads) {
            REQUIRE(m->configure_board_profile("menu-" + std::to_string(pads)));
            for (Address latch :
                 {0x000U, 0x080U, 0x100U, 0x180U, 0x201U, 0x301U, 0x47CU, 0x57DU, 0x7FFU}) {
                for (unsigned data = 0U; data < 256U; ++data) {
                    m->write_prg(static_cast<Address>(0x8000U | latch), static_cast<Byte>(data));
                    for (Address cpu : {0x9A00U, 0x9A1FU, 0xFA07U, 0xFA1FU})
                        REQUIRE(m->read_prg(cpu) ==
                                mapperbus::tests::mapper242_byte(mapperbus::tests::mapper242_offset(
                                    cpu, latch, battery, et, pads)));
                }
            }
            const auto saved = state(*m);
            for (auto profile : {"menu-",
                                 "menu--1",
                                 "menu-32",
                                 "menu-256",
                                 "menu-999999999999",
                                 "menu-1x",
                                 "unknown"}) {
                REQUIRE_FALSE(m->configure_board_profile(profile));
                REQUIRE(state(*m) == saved);
            }
            REQUIRE_FALSE(m->configure_board_jumpers(32U));
            REQUIRE(state(*m) == saved);
            m->reset();
            m->write_prg(0x8100U, 0U);
            REQUIRE(m->read_prg(0x8000U) ==
                    mapperbus::tests::mapper242_byte((et ? 524288U : 0U) + pads));
        }
    }
}
TEST_CASE(
    "Mapper 242 ET-113 chip select is independent of L and ignores outer pins on replacement ROM",
    "[mapper242]") {
    auto m = board(false, false, true);
    for (Address latch : {0x000U, 0x060U, 0x200U, 0x260U}) {
        m->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        REQUIRE(m->read_prg(0x8000U) == mapperbus::tests::mapper242_byte(524288U));
        REQUIRE(m->read_prg(0xC000U) == mapperbus::tests::mapper242_byte(
                                            524288U + ((latch & 0x200U) != 0U ? 7U : 0U) * 16384U));
    }
    m->write_prg(0x8460U, 0U);
    REQUIRE(m->read_prg(0x8000U) == mapperbus::tests::mapper242_byte(24U * 16384U));
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == mapperbus::tests::mapper242_byte(524288U));
}
TEST_CASE("Mapper 242 protects multicart CHR RAM in NROM modes while RPG RAM stays writable",
          "[mapper242]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        const bool battery = population == 1U;
        auto m = board(false, battery, population == 2U);
        for (unsigned latch = 0U; latch < 2048U; ++latch) {
            m->write_prg(0x8000U, 0U);
            m->write_chr(0x1234U, 0x5AU);
            m->write_prg(static_cast<Address>(0x8000U | latch), 0U);
            m->write_chr(0x1234U, 0xA7U);
            REQUIRE(m->read_chr(0x1234U) == (battery || (latch & 0x80U) == 0U ? 0xA7U : 0x5AU));
        }
        m->write_prg(0x8000U, 0U);
        for (unsigned a = 0U; a < 8192U; ++a)
            m->write_chr(static_cast<Address>(a), static_cast<Byte>(a));
        for (unsigned a = 0U; a < 8192U; ++a)
            REQUIRE(m->read_chr(static_cast<Address>(a)) == static_cast<Byte>(a));
        if (battery) {
            for (unsigned a = 0x6000U; a < 0x8000U; ++a)
                m->write_prg(static_cast<Address>(a), static_cast<Byte>(a));
            for (unsigned a = 0x6000U; a < 0x8000U; ++a)
                REQUIRE(m->read_prg(static_cast<Address>(a)) == static_cast<Byte>(a));
        }
        m->write_prg(0x6000U, 0x91U);
        m->reset();
        REQUIRE(m->read_chr(0x1234U) == 0x34U);
        m->power_on();
        REQUIRE(m->read_chr(0x1234U) == 0U);
        REQUIRE(m->read_prg(0x6000U) == (battery ? 0x91U : 0U));
        std::vector<Byte> oversized(8193U, 0x7BU);
        m->set_battery_ram(oversized);
        REQUIRE(m->read_prg(0x7FFFU) == (battery ? 0x7BU : 0U));
    }
}
TEST_CASE("Mapper 242 base boards and lower expansion registers leave the bus undriven",
          "[mapper242]") {
    register_builtin_mappers();
    for (bool battery : {false, true}) {
        auto c = Cartridge::from_data(mapperbus::tests::make_mapper242_rom(false, battery));
        REQUIRE(c);
        MemoryBus bus;
        bus.connect_cartridge(&*c);
        for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            bus.write(addr, 0xA7U);
            bus.write(0U, 0x3CU);
            REQUIRE(bus.read(addr) == (battery && addr >= 0x6000U ? 0xA7U : 0x3CU));
        }
    }
}
TEST_CASE("Mapper 242 rejects unsupported chips metadata and invented submappers", "[mapper242]") {
    register_builtin_mappers();
    const auto reject = [](const INesHeader& h, std::size_t p, std::size_t c = 0U) {
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    };
    for (auto p : {0U, 16384U, 32768U, 131072U, 524287U, 524289U, 655359U, 655361U, 1048576U})
        reject(header(), p);
    reject(header(false, true), 655360U);
    for (auto c : {1U, 8192U, 16384U})
        reject(header(), 524288U, c);
    for (Byte sub = 1U; sub < 16U; ++sub) {
        auto h = header();
        h.submapper = sub;
        reject(h, 524288U);
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        reject(h, 524288U);
    }
    for (bool battery : {false, true})
        for (unsigned field = 0U; field < 5U; ++field) {
            auto h = header(false, battery);
            switch (field) {
            case 0:
                h.prg_ram_size = 8192U;
                break;
            case 1:
                h.prg_nvram_size = battery ? 0U : 8192U;
                break;
            case 2:
                h.chr_ram_size = 0U;
                break;
            case 3:
                h.chr_ram_size = 32768U;
                break;
            case 4:
                h.chr_nvram_size = 8192U;
                break;
            }
            reject(h, 524288U);
        }
}
TEST_CASE("Mapper 242 snapshots bind population and pads and reject malformed states atomically",
          "[mapper242]") {
    for (unsigned population = 0U; population < 3U; ++population) {
        auto m = board(false, population == 1U, population == 2U, 19U);
        m->write_chr(0x1234U, 0xA7U);
        m->write_prg(0x6000U, 0x91U);
        m->write_prg(0xFFFFU, 0U);
        const auto saved = state(*m);
        for (std::size_t n : {0U, 1U, 2U, 3U, 8195U}) {
            StateReader r{std::span(saved).first(n)};
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
        auto truncated = saved;
        truncated.pop_back();
        StateReader short_reader(truncated);
        m->load_state(short_reader);
        REQUIRE_FALSE(short_reader.ok());
        REQUIRE(state(*m) == saved);
        for (unsigned bit = 0U; bit < 16U; ++bit) {
            auto bad = saved;
            bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
            StateReader r(bad);
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
        auto bad = saved;
        bad[3] = 0xFFU;
        StateReader invalid_latch(bad);
        m->load_state(invalid_latch);
        REQUIRE_FALSE(invalid_latch.ok());
        REQUIRE(state(*m) == saved);
        for (unsigned other = 0U; other < 3U; ++other)
            if (other != population) {
                auto different = board(false, other == 1U, other == 2U, 19U);
                const auto before = state(*different);
                StateReader r(saved);
                different->load_state(r);
                REQUIRE_FALSE(r.ok());
                REQUIRE(state(*different) == before);
            }
        m->power_on();
        StateReader r(saved);
        m->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(state(*m) == saved);
        REQUIRE(m->configure_board_jumpers(20U));
        const auto before = state(*m);
        StateReader pads(saved);
        m->load_state(pads);
        REQUIRE_FALSE(pads.ok());
        REQUIRE(state(*m) == before);
    }
}
TEST_CASE("Mapper 242 original CPU firmware checks all populations modes pads CIRAM and replay",
          "[mapper242]") {
    register_builtin_mappers();
    for (unsigned population = 0U; population < 3U; ++population)
        for (bool legacy : {false, true})
            for (Byte pads : {0U, 19U, 31U}) {
                const bool battery = population == 1U, et = population == 2U;
                auto c = Cartridge::from_data(
                    mapperbus::tests::make_mapper242_rom(legacy, battery, et, pads));
                REQUIRE(c);
                REQUIRE(c->configure_board_jumpers(pads));
                Emulator e;
                REQUIRE(e.load_cartridge(std::move(*c), ""));
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0U);
                const auto saved = e.save_state();
                e.step_frame();
                const auto replay = e.save_state();
                REQUIRE(e.load_state(saved));
                e.step_frame();
                REQUIRE(e.save_state() == replay);
                e.reset();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0x7BU);
                e.power_on();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0U);
                if (battery)
                    REQUIRE(e.read_cpu(0x15U) == 0xA7U);
                const auto before = e.save_state();
                auto bad = saved;
                StateWriter tail;
                Controller{}.save_state(tail);
                Controller{}.save_state(tail);
                const auto mapper_offset =
                    bad.size() - tail.data().size() - (battery ? 16388U : 8196U);
                bad[mapper_offset] ^= 1U;
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == before);
                bad = saved;
                bad.resize(bad.size() - 10U);
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == before);
            }
}
} // namespace
