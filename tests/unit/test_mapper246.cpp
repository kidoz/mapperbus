#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper246_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 246U;
    h.is_nes2 = !legacy;
    h.has_battery = true;
    h.prg_nvram_size = legacy ? 0U : 2048U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}
Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}
Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 13U + offset + (offset >> 8U));
}
std::unique_ptr<Mapper> board(bool legacy = false, bool vertical = true, bool patterned = false) {
    register_builtin_mappers();
    auto prg = mapperbus::tests::mapper246_prg(), chr = mapperbus::tests::mapper246_chr();
    if (patterned)
        for (unsigned offset = 0U; offset < 524288U; ++offset) {
            prg[offset] = prg_cell(offset / 8192U, offset % 8192U);
            chr[offset] = chr_cell(offset / 2048U, offset % 2048U);
        }
    auto m =
        MapperRegistry::instance().create(header(legacy, vertical), std::move(prg), std::move(chr));
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("Mapper 246 powers on with all-high banking and only 2 KiB battery RAM", "[mapper246]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto m = board(legacy, vertical);
            REQUIRE(m->battery_ram().size() == 2048U);
            REQUIRE(state(*m).size() == 2058U);
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) == 63U);
                REQUIRE(m->read_chr(static_cast<Address>(slot * 2048U)) == 255U);
            }
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE_FALSE(m->irq_pending());
            REQUIRE_FALSE(m->configure_board_profile("unknown"));
            REQUIRE_FALSE(m->configure_board_jumpers(0U));
            for (unsigned clock = 0U; clock < 1024U; ++clock) {
                m->clock_cpu();
                m->clock_irq_counter();
            }
            REQUIRE_FALSE(m->irq_pending());
        }
}
TEST_CASE("Mapper 246 decodes every CPU write address and all register aliases narrowly",
          "[mapper246]") {
    auto m = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        m->power_on();
        m->write_prg(static_cast<Address>(addr), 0U);
        const bool register_write = addr >= 0x6000U && addr <= 0x601FU;
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const bool selected = register_write && (addr & 3U) == slot;
            REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                    (selected && (addr & 4U) == 0U ? 0U : 63U));
            REQUIRE(m->read_chr(static_cast<Address>(slot * 2048U)) ==
                    (selected && (addr & 4U) != 0U ? 0U : 255U));
        }
        REQUIRE(m->maps_prg(static_cast<Address>(addr)) ==
                (addr >= 0x8000U || (addr >= 0x6800U && addr <= 0x6FFFU)));
        REQUIRE_FALSE(m->maps_expansion(static_cast<Address>(addr)));
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    }
}
TEST_CASE("Mapper 246 independent bank registers accept every data byte without conflicts",
          "[mapper246]") {
    auto m = board();
    for (unsigned value = 0U; value < 256U; ++value)
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            m->power_on();
            m->write_prg(static_cast<Address>(0x6000U + slot), static_cast<Byte>(value));
            m->write_prg(static_cast<Address>(0x6004U + slot), static_cast<Byte>(value));
            for (unsigned other = 0U; other < 4U; ++other) {
                REQUIRE(m->read_prg(static_cast<Address>(0x9A00U + other * 8192U)) ==
                        (other == slot ? value & 63U : 63U));
                REQUIRE(m->read_chr(static_cast<Address>(other * 2048U)) ==
                        (other == slot ? value : 255U));
            }
        }
}
TEST_CASE("Mapper 246 maps every PRG cell including transient vector A17 overrides",
          "[mapper246]") {
    auto m = board(false, true, true);
    for (Byte bank = 0U; bank < 64U; ++bank) {
        for (unsigned slot = 0U; slot < 4U; ++slot)
            m->write_prg(static_cast<Address>(0x6000U + slot), bank);
        const auto saved = state(*m);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const bool force = addr >= 0xFFE0U && (addr & 4U) != 0U;
            REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                    prg_cell(force ? bank | 16U : bank, addr & 0x1FFFU));
        }
        REQUIRE(state(*m) == saved);
    }
}
TEST_CASE("Mapper 246 maps every CHR cell and pattern writes never change ROM", "[mapper246]") {
    auto m = board(false, true, true);
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        for (unsigned slot = 0U; slot < 4U; ++slot)
            m->write_prg(static_cast<Address>(0x6004U + slot), static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            m->write_chr(static_cast<Address>(addr), static_cast<Byte>(bank ^ 255U));
            REQUIRE(m->read_chr(static_cast<Address>(addr)) == chr_cell(bank, addr & 0x7FFU));
        }
    }
}
TEST_CASE("Mapper 246 register reads and unused RAM windows float on the CPU bus", "[mapper246]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper246_rom());
    REQUIRE(c);
    MemoryBus bus;
    bus.connect_cartridge(&*c);
    for (Address addr : {0x4020U, 0x5000U, 0x6000U, 0x601FU, 0x6020U, 0x67FFU, 0x7000U, 0x7FFFU}) {
        bus.write(addr, 0xA7U);
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    for (unsigned addr = 0x6800U; addr <= 0x6FFFU; ++addr)
        bus.write(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    for (unsigned addr = 0x6800U; addr <= 0x6FFFU; ++addr)
        REQUIRE(bus.read(static_cast<Address>(addr)) == static_cast<Byte>(addr ^ (addr >> 8U)));
    REQUIRE(c->battery_ram().size() == 2048U);
}
TEST_CASE("Mapper 246 soft reset retains latches and cold power retains battery bytes",
          "[mapper246]") {
    auto m = board();
    m->set_battery_ram(std::vector<Byte>(4096U, 0xA7U));
    REQUIRE(m->battery_ram().size() == 2048U);
    for (unsigned r = 0U; r < 8U; ++r)
        m->write_prg(static_cast<Address>(0x6000U + r), static_cast<Byte>(r));
    const auto saved = state(*m);
    m->reset();
    REQUIRE(state(*m) == saved);
    m->power_on();
    REQUIRE(m->read_prg(0xFA00U) == 63U);
    REQUIRE(m->read_chr(0U) == 255U);
    REQUIRE(std::ranges::all_of(m->battery_ram(), [](Byte value) { return value == 0xA7U; }));
    m->set_battery_ram(std::array<Byte, 2>{0x19U, 0x27U});
    REQUIRE(m->read_prg(0x6800U) == 0x19U);
    REQUIRE(m->read_prg(0x6801U) == 0x27U);
    REQUIRE(m->read_prg(0x6802U) == 0xA7U);
}
TEST_CASE("Mapper 246 rejects unsupported ROM populations and native memory metadata",
          "[mapper246]") {
    register_builtin_mappers();
    const auto reject = [](const INesHeader& h, unsigned p = 524288U, unsigned c = 524288U) {
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    };
    for (unsigned size : {0U, 16384U, 131072U, 262144U, 524287U, 524289U, 1048576U}) {
        reject(header(), size);
        reject(header(), 524288U, size);
    }
    for (Byte sub = 1U; sub < 16U; ++sub) {
        auto h = header();
        h.submapper = sub;
        reject(h);
    }
    for (bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = false;
        reject(h);
        h = header(legacy);
        h.mirror_mode = MirrorMode::FourScreen;
        reject(h);
    }
    for (unsigned which = 0U; which < 5U; ++which) {
        auto h = header();
        switch (which) {
        case 0U:
            h.prg_ram_size = 2048U;
            break;
        case 1U:
            h.prg_nvram_size = 0U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        reject(h);
    }
}
TEST_CASE("Mapper 246 snapshots restore registers and battery RAM with atomic rejection",
          "[mapper246]") {
    auto m = board();
    for (unsigned r = 0U; r < 8U; ++r)
        m->write_prg(static_cast<Address>(0x6000U + r), static_cast<Byte>(r + 32U));
    m->write_prg(0x6800U, 0xA7U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 2058U);
    for (unsigned length = 0U; length < saved.size(); ++length) {
        StateReader r(std::span(saved).first(length));
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    for (unsigned field = 0U; field < 4U; ++field)
        for (unsigned value = 64U; value < 256U; ++value) {
            auto corrupt = saved;
            corrupt[2U + field] = static_cast<Byte>(value);
            StateReader r(corrupt);
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto corrupt = saved;
        corrupt[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        StateReader r(corrupt);
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    auto other = board(false, false);
    const auto before = state(*other);
    StateReader wrong_wiring(saved);
    other->load_state(wrong_wiring);
    REQUIRE_FALSE(wrong_wiring.ok());
    REQUIRE(state(*other) == before);
    m->power_on();
    m->write_prg(0x6800U, 0U);
    StateReader r(saved);
    m->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE(state(*m) == saved);
}
TEST_CASE("Mapper 246 original firmware checks vector reads banking CIRAM and machine replay",
          "[mapper246]") {
    register_builtin_mappers();
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto c = Cartridge::from_data(mapperbus::tests::make_mapper246_rom(legacy, vertical));
            REQUIRE(c);
            Emulator e;
            REQUIRE(e.load_cartridge(std::move(*c), ""));
            e.power_on();
            e.step_frame();
            INFO("pc=" << e.cpu().pc());
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x11U) == 1U);
            REQUIRE(e.read_cpu(0x14U) == 63U);
            REQUIRE(e.read_cpu(0x15U) == 255U);
            REQUIRE(e.read_cpu(0x16U) == 0U);
            const auto saved = e.save_state();
            e.step_frame();
            const auto expected = e.save_state();
            REQUIRE(e.load_state(saved));
            e.step_frame();
            REQUIRE(e.save_state() == expected);
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x14U) == 32U);
            REQUIRE(e.read_cpu(0x15U) == 15U);
            REQUIRE(e.read_cpu(0x16U) == 0xA7U);
            e.power_on();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x14U) == 63U);
            REQUIRE(e.read_cpu(0x15U) == 255U);
            REQUIRE(e.read_cpu(0x16U) == 0xA7U);
            const auto before = e.save_state();
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            auto bad = saved;
            bad[bad.size() - tail.data().size() - 2058U] ^= 1U;
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE(e.save_state() == before);
            bad = saved;
            bad.resize(bad.size() - 10U);
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE(e.save_state() == before);
        }
}
} // namespace
