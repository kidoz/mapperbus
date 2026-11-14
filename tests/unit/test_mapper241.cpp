#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper241_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(bool legacy = false,
                  bool vertical = true,
                  bool battery = false,
                  bool chr_rom = false) {
    INesHeader h{};
    h.mapper_number = 241U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.has_battery = battery;
    h.prg_ram_size = battery ? 0U : 8192U;
    h.prg_nvram_size = battery ? 8192U : 0U;
    h.chr_ram_size = chr_rom ? 0U : 8192U;
    return h;
}
std::vector<Byte> pages(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t a = 0; a < size; ++a)
        bytes[a] = static_cast<Byte>(a / 32768U);
    return bytes;
}
std::unique_ptr<Mapper> board(std::size_t size = 131072U,
                              bool legacy = false,
                              bool vertical = true,
                              bool battery = false,
                              bool chr_rom = false) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(header(legacy, vertical, battery, chr_rom),
                                               pages(size),
                                               chr_rom ? std::vector<Byte>(8192U, 0x23U)
                                                       : std::vector<Byte>{});
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("Mapper 241 registers both WRAM and CHR populations over full-byte PRG geometry",
          "[mapper241]") {
    for (std::size_t size = 32768U; size <= 8388608U; size *= 2U)
        for (bool legacy : {false, true})
            for (bool vertical : {false, true})
                for (bool battery : {false, true})
                    for (bool chr_rom : {false, true}) {
                        auto m = board(size, legacy, vertical, battery, chr_rom);
                        for (unsigned data = 0; data < 256U; ++data) {
                            m->write_prg(0x8000U, static_cast<Byte>(data));
                            for (Address a : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU})
                                REQUIRE(m->read_prg(a) ==
                                        static_cast<Byte>(data & (size / 32768U - 1U)));
                            REQUIRE(m->mirror_mode() ==
                                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
                        }
                        REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
                        REQUIRE(state(*m).size() == (chr_rom ? 8195U : 16387U));
                        REQUIRE_FALSE(m->has_expansion_audio());
                        REQUIRE_FALSE(m->irq_pending());
                    }
}
TEST_CASE("Mapper 241 decodes CPU addresses without bus conflicts or NINA overlays",
          "[mapper241]") {
    auto m = board(1048576U);
    for (unsigned a = 0; a <= 0xFFFFU; ++a) {
        m->reset();
        m->write_prg(static_cast<Address>(a), 0x8DU);
        REQUIRE(m->maps_prg(static_cast<Address>(a)) == (a >= 0x6000U));
        REQUIRE(m->read_prg(0x8000U) == (a >= 0x8000U ? 13U : 0U));
        REQUIRE_FALSE(m->maps_expansion(static_cast<Address>(a)));
    }
    // A write through a ROM byte zero still latches all data bits.
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == 0U);
    m->write_prg(0x8000U, 0x80U);
    REQUIRE(m->read_prg(0x8000U) == 0U);
    m->write_prg(0x8000U, 31U);
    REQUIRE(m->read_prg(0xFFFFU) == 31U);
    for (unsigned a = 0x6000U; a < 0x8000U; ++a)
        m->write_prg(static_cast<Address>(a), static_cast<Byte>(a));
    for (unsigned a = 0x6000U; a < 0x8000U; ++a)
        REQUIRE(m->read_prg(static_cast<Address>(a)) == static_cast<Byte>(a));
    for (Address a : {0x7FFDU, 0x7FFEU, 0x7FFFU}) {
        m->write_prg(a, 0xA3U);
        REQUIRE(m->read_prg(a) == 0xA3U);
        REQUIRE(m->read_prg(0x8000U) == 31U);
        REQUIRE(m->read_chr(0U) == 0U);
    }
}
TEST_CASE("Mapper 241 keeps fixed CHR and distinguishes soft reset from cold RAM policy",
          "[mapper241]") {
    for (bool battery : {false, true})
        for (bool chr_rom : {false, true}) {
            auto m = board(131072U, false, true, battery, chr_rom);
            for (unsigned a = 0; a < 8192U; ++a)
                m->write_chr(static_cast<Address>(a), static_cast<Byte>(a));
            for (unsigned a = 0; a < 8192U; ++a)
                REQUIRE(m->read_chr(static_cast<Address>(a)) ==
                        (chr_rom ? 0x23U : static_cast<Byte>(a)));
            m->write_prg(0x6000U, 0xA7U);
            m->write_prg(0xFFFFU, 3U);
            m->reset();
            REQUIRE(m->read_prg(0x8000U) == 0U);
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
            REQUIRE(m->read_chr(0x123U) == 0x23U);
            const std::array<Byte, 2> save{0x91U, 0x83U};
            m->set_battery_ram(save);
            REQUIRE(m->read_prg(0x6000U) == (battery ? 0x91U : 0xA7U));
            m->power_on();
            REQUIRE(m->read_prg(0x6000U) == (battery ? 0x91U : 0U));
            REQUIRE(m->read_chr(0x123U) == (chr_rom ? 0x23U : 0U));
            if (battery) {
                std::vector<Byte> oversized(8193U, 0x6AU);
                m->set_battery_ram(oversized);
                REQUIRE(m->read_prg(0x7FFFU) == 0x6AU);
            }
        }
}
TEST_CASE("Mapper 241 leaves absent speech and hack registers on the CPU open bus", "[mapper241]") {
    register_builtin_mappers();
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper241_rom());
    REQUIRE(c);
    MemoryBus bus;
    bus.connect_cartridge(&*c);
    for (Address a : {0x4020U, 0x4800U, 0x4FFFU, 0x5000U, 0x5FFFU}) {
        bus.write(a, 0xBFU);
        REQUIRE(bus.read(a) == 0xBFU);
        bus.write(0U, 0x81U);
        REQUIRE(bus.read(a) == 0x81U);
    }
    Ppu p;
    p.connect_cartridge(&*c);
    p.reset();
    bus.connect_ppu(&p);
    for (unsigned i = 0; i < 256U; ++i)
        bus.write(static_cast<Address>(0x6000U + i), static_cast<Byte>(i ^ 0xA7U));
    bus.write(0x4014U, 0x60U);
    for (unsigned i = 0; i < 256U; ++i) {
        p.write_register(3U, static_cast<Byte>(i));
        // OAM attribute bits 2-4 are physically absent.
        const auto mask = (i & 3U) == 2U ? 0xE3U : 0xFFU;
        REQUIRE(p.read_register(4U) == static_cast<Byte>((i ^ 0xA7U) & mask));
    }
    REQUIRE_FALSE(c->configure_board_profile("lpc"));
    REQUIRE_FALSE(c->configure_board_jumpers(0U));
}
TEST_CASE("Mapper 241 rejects ambiguous memory shapes and unsupported native metadata",
          "[mapper241]") {
    register_builtin_mappers();
    const auto reject = [](const INesHeader& h, std::size_t p, std::size_t c) {
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    };
    for (auto size : {0U, 16384U, 32767U, 32769U, 98304U, 8388609U})
        reject(header(), size, 0U);
    for (auto size : {1U, 4096U, 8193U, 16384U})
        reject(header(), 32768U, size);
    for (Byte sub = 1U; sub < 16U; ++sub) {
        auto h = header();
        h.submapper = sub;
        reject(h, 32768U, 0U);
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        reject(h, 32768U, 0U);
    }
    for (bool battery : {false, true})
        for (bool chr_rom : {false, true}) {
            const auto valid = header(false, true, battery, chr_rom);
            for (unsigned field = 0; field < 5U; ++field) {
                auto h = valid;
                switch (field) {
                case 0:
                    h.prg_ram_size = 4096U;
                    break;
                case 1:
                    h.prg_nvram_size = 4096U;
                    break;
                case 2:
                    h.chr_ram_size = 4096U;
                    break;
                case 3:
                    h.chr_nvram_size = 8192U;
                    break;
                case 4:
                    h.has_battery = !battery;
                    break;
                }
                reject(h, 32768U, chr_rom ? 8192U : 0U);
            }
        }
}
TEST_CASE("Mapper 241 snapshots bind ROM geometry and memory profile and reject atomically",
          "[mapper241]") {
    for (bool chr_rom : {false, true}) {
        auto m = board(131072U, false, true, false, chr_rom);
        m->write_prg(0x8000U, 0xFFU);
        m->write_prg(0x6000U, 0xA7U);
        m->write_chr(0U, 0x5AU);
        const auto saved = state(*m);
        for (std::size_t n : {0U, 1U, 2U, 3U, 8194U}) {
            StateReader r(std::span<const Byte>(saved).first(n));
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
        auto bad = saved;
        bad.pop_back();
        StateReader short_reader(bad);
        m->load_state(short_reader);
        REQUIRE_FALSE(short_reader.ok());
        REQUIRE(state(*m) == saved);
        for (std::size_t bit = 0; bit < 16U; ++bit) {
            bad = saved;
            bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
            StateReader r(bad);
            m->load_state(r);
            REQUIRE_FALSE(r.ok());
            REQUIRE(state(*m) == saved);
        }
        for (unsigned data = 0; data < 256U; ++data) {
            bad = saved;
            bad[2] = static_cast<Byte>(data);
            StateReader r(bad);
            m->load_state(r);
            REQUIRE(r.ok());
            REQUIRE(m->read_prg(0xFFFFU) == data % 4U);
        }
        m->power_on();
        StateReader r(saved);
        m->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(state(*m) == saved);
        for (unsigned profile = 0; profile < 4U; ++profile) {
            auto other = board(profile == 0 ? 262144U : 131072U,
                               false,
                               profile != 1U,
                               profile == 2U,
                               profile == 3U ? !chr_rom : chr_rom);
            const auto before = state(*other);
            StateReader mismatch(saved);
            other->load_state(mismatch);
            REQUIRE_FALSE(mismatch.ok());
            REQUIRE(state(*other) == before);
        }
    }
}
TEST_CASE("Mapper 241 original CPU firmware checks banking WRAM CHR CIRAM reset and replay",
          "[mapper241]") {
    register_builtin_mappers();
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            for (bool battery : {false, true})
                for (bool chr_rom : {false, true}) {
                    const auto bytes =
                        mapperbus::tests::make_mapper241_rom(legacy, vertical, battery, chr_rom);
                    auto c = Cartridge::from_data(bytes);
                    REQUIRE(c);
                    Emulator e;
                    REQUIRE(e.load_cartridge(std::move(*c), ""));
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == 0U);
                    REQUIRE(e.read_cpu(0x15U) == (chr_rom ? 0x23U : 0U));
                    const auto saved = e.save_state();
                    e.step_frame();
                    const auto replay = e.save_state();
                    REQUIRE(e.load_state(saved));
                    e.step_frame();
                    REQUIRE(e.save_state() == replay);
                    e.reset();
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == 0xA7U);
                    REQUIRE(e.read_cpu(0x15U) == (chr_rom ? 0x23U : 0x5AU));
                    e.power_on();
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == (battery ? 0xA7U : 0U));
                    const auto before = e.save_state();
                    StateWriter controllers;
                    Controller{}.save_state(controllers);
                    Controller{}.save_state(controllers);
                    auto bad = saved;
                    const auto mapper_size = chr_rom ? 8195U : 16387U;
                    const auto mapper_offset = bad.size() - controllers.data().size() - mapper_size;
                    bad[mapper_offset] ^= 0x80U;
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                    auto truncated = saved;
                    truncated.resize(truncated.size() - 10U);
                    REQUIRE_FALSE(e.load_state(truncated));
                    REQUIRE(e.save_state() == before);
                }
    // Exercise high NES 2.0 PRG size bits with executable banks 128 and 255.
    auto c = Cartridge::from_data(
        mapperbus::tests::make_mapper241_rom(false, true, false, false, 8388608U));
    REQUIRE(c);
    Emulator e;
    REQUIRE(e.load_cartridge(std::move(*c), ""));
    e.step_frame();
    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
}
} // namespace
