#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper244_rom.hpp"
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
    h.mapper_number = 244U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}
std::unique_ptr<Mapper> board(bool legacy = false, bool vertical = true, bool all_ff = false) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(header(legacy, vertical),
                                               all_ff ? std::vector<Byte>(131072U, 0xFFU)
                                                      : mapperbus::tests::mapper244_prg(),
                                               mapperbus::tests::mapper244_chr());
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
Byte prg(Byte value) {
    return mapperbus::tests::k244Prg[(value >> 4U) & 3U][value & 3U];
}
Byte chr(Byte value) {
    return mapperbus::tests::k244Chr[(value >> 4U) & 7U][value & 7U];
}
TEST_CASE("Mapper 244 registers ROM-only H/V populations without unneeded memory", "[mapper244]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto m = board(legacy, vertical);
            REQUIRE(m->read_prg(0x9A00U) == 0U);
            REQUIRE(m->read_chr(0U) == 0U);
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(state(*m).size() == 4U);
            REQUIRE(m->battery_ram().empty());
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE_FALSE(m->irq_pending());
            REQUIRE_FALSE(m->configure_board_profile("unknown"));
            REQUIRE_FALSE(m->configure_board_jumpers(0U));
        }
}
TEST_CASE("Mapper 244 all command bytes match independent PRG and CHR truth tables",
          "[mapper244]") {
    auto m = board();
    for (unsigned data = 0; data < 256U; ++data) {
        m->power_on();
        m->write_prg(0x8200U, static_cast<Byte>(data));
        REQUIRE(m->read_prg(0x9A00U) == ((data & 8U) != 0U ? 0U : prg(static_cast<Byte>(data))));
        REQUIRE(m->read_chr(0U) == ((data & 8U) != 0U ? chr(static_cast<Byte>(data)) : 0U));
    }
}
TEST_CASE("Mapper 244 bus conflicts use the old PRG bank before choosing the destination latch",
          "[mapper244]") {
    auto m = board();
    for (Byte initial = 0U; initial < 4U; ++initial)
        for (unsigned mask = 0U; mask < 256U; ++mask)
            for (unsigned data = 0U; data < 256U; ++data) {
                m->write_prg(0x8200U, initial);
                m->write_prg(0x8200U, 0x0DU);
                const Byte bus = static_cast<Byte>(data & mask);
                m->write_prg(static_cast<Address>(0x8100U + mask), static_cast<Byte>(data));
                REQUIRE(m->read_prg(0x9A00U) == ((bus & 8U) != 0U ? initial : prg(bus)));
                REQUIRE(m->read_chr(0U) == ((bus & 8U) != 0U ? chr(bus) : 5U));
            }
    m->write_prg(0x8200U, 0U);
    m->write_prg_cpu(0x8300U, 2U, false);
    REQUIRE(m->read_prg(0x9A00U) == 2U);
    m->write_prg_cpu(0x8300U, 3U, true);
    REQUIRE(m->read_prg(0x9A00U) == 1U);
}
TEST_CASE("Mapper 244 register decode covers every CPU address and retains fixed CIRAM",
          "[mapper244]") {
    for (bool vertical : {false, true}) {
        auto m = board(false, vertical, true);
        for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
            m->power_on();
            m->write_prg(static_cast<Address>(addr), 0x2EU);
            REQUIRE(state(*m)[3] == (addr >= 0x8000U ? 6U : 0U));
            REQUIRE(m->maps_prg(static_cast<Address>(addr)) == (addr >= 0x8000U));
            REQUIRE_FALSE(m->maps_expansion(static_cast<Address>(addr)));
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }
    }
}
TEST_CASE("Mapper 244 maps all ROM cells and ignores every CHR write", "[mapper244]") {
    auto m = board();
    const auto image = mapperbus::tests::mapper244_prg();
    for (Byte bank = 0U; bank < 4U; ++bank) {
        m->write_prg(0x8200U, bank);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
            REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                    image[bank * 32768U + (addr & 0x7FFFU)]);
    }
    for (Byte bank = 0U; bank < 8U; ++bank) {
        m->write_prg(0x8200U, static_cast<Byte>(8U | bank));
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            m->write_chr(static_cast<Address>(addr), 0xFFU);
            REQUIRE(m->read_chr(static_cast<Address>(addr)) == bank);
        }
    }
}
TEST_CASE("Mapper 244 lower cartridge addresses stay on the CPU open bus", "[mapper244]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper244_rom());
    REQUIRE(c);
    MemoryBus bus;
    bus.connect_cartridge(&*c);
    for (Address addr : {0x4020U, 0x4800U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        bus.write(addr, 0xFFU);
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
}
TEST_CASE("Mapper 244 soft reset retains latches and cold power clears them", "[mapper244]") {
    auto m = board();
    m->write_prg(0x8200U, 3U);
    m->write_prg(0x8200U, 0x0FU);
    const auto saved = state(*m);
    m->reset();
    REQUIRE(state(*m) == saved);
    m->power_on();
    REQUIRE(m->read_prg(0x9A00U) == 0U);
    REQUIRE(m->read_chr(0U) == 0U);
    m->set_battery_ram(std::vector<Byte>(8192U, 0xA7U));
    REQUIRE(m->battery_ram().empty());
}
TEST_CASE("Mapper 244 rejects other chip sizes RAM and unsupported native metadata",
          "[mapper244]") {
    register_builtin_mappers();
    const auto reject = [](const INesHeader& h, std::size_t p = 131072U, std::size_t c = 65536U) {
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    };
    for (auto p : {0U, 16384U, 32768U, 65536U, 131071U, 131073U, 262144U})
        reject(header(), p);
    for (auto c : {0U, 8192U, 32768U, 65535U, 65537U, 131072U})
        reject(header(), 131072U, c);
    for (Byte sub = 1U; sub < 16U; ++sub) {
        auto h = header();
        h.submapper = sub;
        reject(h);
    }
    for (bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = true;
        reject(h);
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            h = header(legacy);
            h.mirror_mode = mode;
            reject(h);
        }
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header();
        switch (field) {
        case 0:
            h.prg_ram_size = 8192U;
            break;
        case 1:
            h.prg_nvram_size = 8192U;
            break;
        case 2:
            h.chr_ram_size = 8192U;
            break;
        case 3:
            h.chr_nvram_size = 8192U;
            break;
        }
        reject(h);
    }
}
TEST_CASE("Mapper 244 four-byte snapshots validate bank ranges and fixed mirroring atomically",
          "[mapper244]") {
    auto m = board();
    m->write_prg(0x8200U, 3U);
    m->write_prg(0x8200U, 0x0FU);
    const auto saved = state(*m);
    for (std::size_t size = 0U; size < 4U; ++size) {
        StateReader r{std::span(saved).first(size)};
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader r(bad);
        m->load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(*m) == saved);
    }
    for (unsigned bank = 0U; bank < 256U; ++bank)
        for (std::size_t field : {2U, 3U}) {
            auto bytes = saved;
            bytes[field] = static_cast<Byte>(bank);
            StateReader r(bytes);
            m->load_state(r);
            if (bank < (field == 2U ? 4U : 8U)) {
                REQUIRE(r.ok());
                REQUIRE(state(*m) == bytes);
                StateReader restore(saved);
                m->load_state(restore);
            } else {
                REQUIRE_FALSE(r.ok());
                REQUIRE(state(*m) == saved);
            }
        }
    auto other = board(false, false);
    const auto before = state(*other);
    StateReader mismatched(saved);
    other->load_state(mismatched);
    REQUIRE_FALSE(mismatched.ok());
    REQUIRE(state(*other) == before);
    m->power_on();
    StateReader r(saved);
    m->load_state(r);
    REQUIRE(r.ok());
    REQUIRE(state(*m) == saved);
}
TEST_CASE("Mapper 244 original firmware exercises permutations conflicts RMW CIRAM and replay",
          "[mapper244]") {
    register_builtin_mappers();
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto c = Cartridge::from_data(mapperbus::tests::make_mapper244_rom(legacy, vertical));
            REQUIRE(c);
            Emulator e;
            REQUIRE(e.load_cartridge(std::move(*c), ""));
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x14U) == 0U);
            REQUIRE(e.read_cpu(0x15U) == 0U);
            const auto saved = e.save_state();
            e.step_frame();
            const auto expected = e.save_state();
            REQUIRE(e.load_state(saved));
            e.step_frame();
            REQUIRE(e.save_state() == expected);
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x14U) == 3U);
            REQUIRE(e.read_cpu(0x15U) == 7U);
            e.power_on();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x14U) == 0U);
            REQUIRE(e.read_cpu(0x15U) == 0U);
            const auto before = e.save_state();
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            auto bad = saved;
            bad[bad.size() - tail.data().size() - 4U] ^= 1U;
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE(e.save_state() == before);
            bad = saved;
            bad.resize(bad.size() - 10U);
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE(e.save_state() == before);
        }
}
} // namespace
