#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "../support/one_bus_rom.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/mappers/one_bus.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header() {
    INesHeader h{};
    h.mapper_number = 256U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
Byte cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + (bank >> 8U) * 11U + offset + (offset >> 8U));
}
std::vector<Byte> rom(unsigned size, unsigned granularity) {
    std::vector<Byte> data(size);
    for (unsigned i = 0U; i < size; ++i)
        data[i] = cell(i / granularity, i % granularity);
    return data;
}
OneBus board(unsigned prg = 8388608U, unsigned chr = 4194304U) {
    REQUIRE(OneBus::validate_geometry(header(), prg, chr));
    return OneBus(header(), rom(prg, 8192U), rom(chr, 1024U));
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void edge(OneBus& m, unsigned low = 3U) {
    m.observe_ppu_a12(false);
    for (unsigned i = 0U; i < low; ++i)
        m.clock_cpu();
    m.observe_ppu_a12(true);
}
TEST_CASE("OneBus native CPU and PPU registers decode exact apertures", "[one-bus]") {
    auto m = board(131072U, 131072U);
    const auto initial = state(m);
    REQUIRE(initial.size() == 30U);
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        m.reset();
        m.write_expansion(static_cast<Address>(addr), 0xD5U);
        auto expected = initial;
        if (addr == 0x4100U)
            expected[10] = 0xD5U;
        if (addr == 0x4101U)
            expected[26] = 0xD5U;
        if (addr == 0x4104U)
            expected[28] = 1U;
        if (addr == 0x4105U)
            expected[11] = 0xC5U;
        if (addr == 0x4106U)
            expected[12] = 1U;
        if (addr >= 0x4107U && addr <= 0x4109U)
            expected[13U + addr - 0x4107U] = 0xD5U;
        if (addr == 0x410AU)
            expected[16] = 0xD5U;
        if (addr == 0x410BU)
            expected[17] = 0xC5U;
        REQUIRE(state(m) == expected);
        REQUIRE(m.maps_expansion(static_cast<Address>(addr)) ==
                (addr >= 0x4100U && addr <= 0x410BU));
        REQUIRE(m.expansion_read_mask(static_cast<Address>(addr)) == 0U);
    }
    for (unsigned addr = 0x2000U; addr < 0x4000U; ++addr) {
        m.reset();
        const bool native = addr >= 0x2010U && addr <= 0x201FU;
        REQUIRE(m.write_ppu_register(static_cast<Address>(addr), 0xD5U) == native);
        REQUIRE(m.read_ppu_register(static_cast<Address>(addr), 0xA7U) ==
                (native ? std::optional<Byte>(0xA7U) : std::nullopt));
        auto expected = initial;
        if (addr >= 0x2012U && addr <= 0x2017U)
            expected[18U + addr - 0x2012U] = 0xD5U;
        if (addr == 0x2018U)
            expected[24] = 0x50U;
        if (addr == 0x201AU)
            expected[25] = 0xD5U;
        REQUIRE(state(m) == expected);
    }
    REQUIRE_FALSE(m.maps_prg(0x6000U));
    REQUIRE_FALSE(m.has_expansion_audio());
    REQUIRE(m.battery_ram().empty());
}
TEST_CASE("OneBus forwards every MMC3 alias and native selection while FWEN gates all ports",
          "[one-bus]") {
    auto m = board(131072U, 131072U);
    m.write_expansion(0x4105U, 0x20U);
    const auto initial = state(m);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        m.reset();
        m.write_expansion(0x4105U, 0x20U);
        m.write_prg(static_cast<Address>(addr), 0xA3U);
        auto expected = initial;
        if (addr >= 0x8000U)
            switch (addr & 0xE001U) {
            case 0x8000U:
                expected[11] = 0xA3U;
                break;
            case 0x8001U:
                expected[22] = 0xA3U;
                break;
            case 0xA000U:
                expected[12] = 1U;
                break;
            case 0xC000U:
                expected[26] = 0xA3U;
                break;
            case 0xE001U:
                expected[28] = 1U;
                break;
            default:
                break;
            }
        REQUIRE(state(m) == expected);
    }
    for (unsigned index = 0U; index < 8U; ++index) {
        m.reset();
        m.write_expansion(0x4105U, static_cast<Byte>(index));
        m.write_prg(0x9FFFU, 0xFFU);
        auto expected = state(m);
        REQUIRE(expected[index < 6U ? (index < 2U ? 22U + index : 16U + index) : 7U + index] ==
                0xFFU);
    }
    m.write_expansion(0x410BU, 8U);
    const auto disabled = state(m);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        m.write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        REQUIRE(state(m) == disabled);
    }
    m.write_expansion(0x4107U, 9U);
    REQUIRE(state(m)[13] == 9U); // Native registers remain active.
}
TEST_CASE("OneBus PRG masks middle substitution outer pins and detailed COMR6 table", "[one-bus]") {
    auto m = board();
    constexpr std::array<unsigned, 8> kKeep{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
    for (unsigned mode = 0U; mode < 8U; ++mode)
        for (unsigned value = 0U; value < 256U; ++value)
            for (unsigned outer : {0U, 1U, 3U, 7U, 15U})
                for (Byte middle : {0U, 0x55U, 0xAAU, 0xFFU})
                    for (bool third : {false, true})
                        for (bool swap : {false, true}) {
                            m.write_expansion(0x4100U, static_cast<Byte>(outer << 4U));
                            m.write_expansion(0x4105U, swap ? 0x40U : 0U);
                            m.write_expansion(0x4107U, static_cast<Byte>(value));
                            m.write_expansion(0x4108U, 0x59U);
                            m.write_expansion(0x4109U, 0xA6U);
                            m.write_expansion(0x410AU, middle);
                            m.write_expansion(0x410BU,
                                              static_cast<Byte>(mode | (third ? 0x40U : 0U)));
                            const unsigned pages[4] = {swap ? (third ? 0xA6U : 254U) : value,
                                                       0x59U,
                                                       swap    ? value
                                                       : third ? 0xA6U
                                                               : 254U,
                                                       255U};
                            for (unsigned slot = 0U; slot < 4U; ++slot) {
                                const auto page = ((outer * 256U) + (pages[slot] & kKeep[mode]) +
                                                   (middle & (255U ^ kKeep[mode]))) %
                                                  1024U;
                                REQUIRE(m.read_prg(static_cast<Address>(0x8000U + slot * 8192U +
                                                                        0x123U)) ==
                                        cell(page, 0x123U));
                            }
                        }
    // Patterned full-cell reads catch offset/window mistakes that bank sentinels miss.
    m.write_expansion(0x4100U, 0x30U);
    m.write_expansion(0x410BU, 0x47U);
    for (Byte control : {0U, 0x40U}) {
        m.write_expansion(0x4105U, control);
        const unsigned pages[4] = {
            control == 0U ? 255U : 0xA6U, 0x59U, control == 0U ? 0xA6U : 255U, 255U};
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
            REQUIRE(m.read_prg(static_cast<Address>(addr)) ==
                    cell(768U + pages[(addr - 0x8000U) / 8192U], addr & 8191U));
    }
}
TEST_CASE("OneBus CHR masks intermediate outer banks pair alignment and inversion", "[one-bus]") {
    auto m = board();
    constexpr std::array<unsigned, 8> kKeep{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
    for (unsigned mode = 0U; mode < 8U; ++mode)
        for (unsigned value = 0U; value < 256U; ++value)
            for (unsigned intermediate = 0U; intermediate < 8U; ++intermediate)
                for (unsigned outer : {0U, 1U, 7U, 15U})
                    for (bool flip : {false, true}) {
                        m.write_expansion(0x4100U, static_cast<Byte>(outer));
                        m.write_expansion(0x4105U, flip ? 0x80U : 0U);
                        const auto middle = static_cast<Byte>((value & 248U) | mode);
                        m.write_ppu_register(0x201AU, middle);
                        m.write_ppu_register(0x2018U, static_cast<Byte>(intermediate << 4U));
                        std::array<unsigned, 6> banks{};
                        for (unsigned r = 0U; r < 6U; ++r) {
                            banks[r] = (value + r * 17U) & 255U;
                            m.write_ppu_register(static_cast<Address>(0x2012U + r),
                                                 static_cast<Byte>(banks[r]));
                        }
                        const unsigned pages[8] = {banks[4] & 254U,
                                                   banks[4] | 1U,
                                                   banks[5] & 254U,
                                                   banks[5] | 1U,
                                                   banks[0],
                                                   banks[1],
                                                   banks[2],
                                                   banks[3]};
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            const auto page = (outer * 2048U + intermediate * 256U +
                                               (pages[slot ^ (flip ? 4U : 0U)] & kKeep[mode]) +
                                               (middle & (255U ^ kKeep[mode]) & 248U)) %
                                              4096U;
                            const auto addr = static_cast<Address>(slot * 1024U + 0x123U);
                            m.write_chr(addr, 0xFFU);
                            REQUIRE(m.read_chr(addr) == cell(page, 0x123U));
                        }
                    }
    auto shared = board(131072U, 0U);
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        shared.write_ppu_register(0x2012U, static_cast<Byte>(bank));
        const unsigned physical = (bank * 1024U + 0x123U) % 131072U;
        REQUIRE(shared.read_chr(0x1123U) == cell(physical / 8192U, physical % 8192U));
    }
}
TEST_CASE("OneBus IRQ clocks select qualified A12 edges or HSYNC and survive snapshots",
          "[one-bus]") {
    auto m = board(131072U, 131072U);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        m.reset();
        m.write_expansion(0x4101U, static_cast<Byte>(latch));
        m.write_expansion(0x4102U, 0U);
        m.write_expansion(0x4104U, 0U);
        m.on_ppu_hblank();
        REQUIRE_FALSE(m.irq_pending());
        edge(m);
        REQUIRE(m.irq_pending() == (latch == 0U));
        for (unsigned n = 0U; n < latch; ++n) {
            edge(m);
            REQUIRE(m.irq_pending() == (n + 1U == latch));
        }
        m.write_expansion(0x4103U, 0U);
        REQUIRE_FALSE(m.irq_pending());
    }
    for (unsigned low = 0U; low <= 3U; ++low) {
        m.reset();
        m.write_expansion(0x4104U, 0U);
        edge(m, low);
        REQUIRE(m.irq_pending() == (low == 3U));
    }
    m.reset();
    m.write_expansion(0x4101U, 2U);
    m.write_expansion(0x4104U, 0U);
    edge(m);
    m.observe_ppu_a12(false);
    m.clock_cpu();
    m.clock_cpu();
    const auto partial = state(m);
    m.reset();
    StateReader r(partial);
    m.load_state(r);
    REQUIRE(r.ok());
    m.clock_cpu();
    m.observe_ppu_a12(true);
    REQUIRE_FALSE(m.irq_pending());
    edge(m);
    REQUIRE(m.irq_pending());
    m.write_expansion(0x4103U, 0U);
    m.write_expansion(0x410BU, 0x80U);
    m.write_expansion(0x4101U, 0U);
    m.write_expansion(0x4104U, 0U);
    edge(m);
    REQUIRE_FALSE(m.irq_pending());
    m.on_ppu_hblank();
    REQUIRE(m.irq_pending());
    const auto pending = state(m);
    m.write_expansion(0x4103U, 0U);
    StateReader p(pending);
    m.load_state(p);
    REQUIRE(p.ok());
    REQUIRE(m.irq_pending());
    m.reset();
    REQUIRE_FALSE(m.irq_pending());
    REQUIRE(state(m).size() == 30U);
}
TEST_CASE("OneBus state stages small fields and rejects truncation corruption and ROM identity",
          "[one-bus]") {
    auto m = board(131072U, 131072U);
    m.write_expansion(0x4100U, 0xA7U);
    m.write_expansion(0x4107U, 0x95U);
    m.write_ppu_register(0x2012U, 0xFFU);
    const auto saved = state(m);
    for (unsigned length = 0U; length < saved.size(); ++length) {
        StateReader r{std::span(saved).first(length)};
        m.load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(m) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<unsigned, Byte>, 11>{{{0U, 1U},
                                                                                 {1U, 0U},
                                                                                 {2U, 1U},
                                                                                 {6U, 1U},
                                                                                 {11U, 8U},
                                                                                 {12U, 2U},
                                                                                 {17U, 0x10U},
                                                                                 {24U, 1U},
                                                                                 {28U, 2U},
                                                                                 {29U, 3U},
                                                                                 {29U, 8U}}}) {
        INFO("invalid offset=" << offset << " value=" << +value);
        auto bad = saved;
        bad[10] ^= 0xFFU;
        bad[offset] = value;
        StateReader r(bad);
        m.load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(m) == saved);
    }
    for (auto geometry : {std::pair{65536U, 131072U}, std::pair{131072U, 0U}}) {
        auto other = board(geometry.first, geometry.second);
        const auto before = state(other);
        StateReader r(saved);
        other.load_state(r);
        REQUIRE_FALSE(r.ok());
        REQUIRE(state(other) == before);
    }
    m.reset();
    StateReader r(saved);
    m.load_state(r);
    REQUIRE(r.ok());
    REQUIRE(r.remaining() == 0U);
    REQUIRE(state(m) == saved);
}
TEST_CASE("OneBus component bounds inputs and restricts the loader to the VT02 profile",
          "[one-bus]") {
    for (unsigned prg :
         {32768U, 65536U, 131072U, 262144U, 524288U, 1048576U, 2097152U, 4194304U, 8388608U})
        for (unsigned chr : {0U, 8192U, 65536U, 1048576U, 4194304U})
            REQUIRE(OneBus::validate_geometry(header(), prg, chr));
    for (unsigned size : {0U, 16384U, 49152U, 16777216U})
        REQUIRE_FALSE(OneBus::validate_geometry(header(), size, 0U));
    for (unsigned variant = 0U; variant < 8U; ++variant) {
        auto h = header();
        switch (variant) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.submapper = 1U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = 8192U;
            break;
        case 4U:
            h.prg_nvram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 8192U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(OneBus::validate_geometry(h, 131072U, 0U));
    }
    for (unsigned sub = 1U; sub < 16U; ++sub) {
        auto h = header();
        h.submapper = static_cast<uint8_t>(sub);
        REQUIRE_FALSE(OneBus::validate_geometry(h, 131072U, 0U));
    }
    for (unsigned chr : {4096U, 12288U, 8388608U})
        REQUIRE_FALSE(OneBus::validate_geometry(header(), 131072U, chr));
    register_builtin_mappers();
    auto unsupported = MapperRegistry::instance().create(header(), std::vector<Byte>(32768U), {});
    REQUIRE_FALSE(unsupported);
    REQUIRE(unsupported.error().find("VT02") != std::string::npos);
}
Cartridge component_cartridge(bool shared, bool vertical = true) {
    register_builtin_mappers();
    auto result =
        Cartridge::from_data(mapperbus::tests::make_one_bus_component_rom(shared, vertical));
    REQUIRE(result);
    return std::move(*result);
}
TEST_CASE("OneBus PPU HSYNC callbacks cover blanked active vblank and pre-render scanlines",
          "[one-bus]") {
    for (bool rendering : {false, true})
        for (Region region : {Region::NTSC, Region::PAL, Region::Dendy})
            for (unsigned line :
                 {0U, 241U, static_cast<unsigned>(timing_for_region(region).pre_render_scanline)}) {
                auto c = component_cartridge(false);
                Ppu ppu;
                ppu.set_region(region);
                ppu.reset();
                ppu.connect_cartridge(&c);
                c.write_expansion(0x410BU, 0x80U);
                ppu.write_register(0U, 8U);
                ppu.write_register(1U, rendering ? 0x18U : 0U);
                ppu.step_dots(line * 341U + 255U);
                c.write_expansion(0x4103U, 0U);
                c.write_expansion(0x4104U, 0U);
                REQUIRE_FALSE(c.irq_pending());
                ppu.step_dots(1U);
                REQUIRE(c.irq_pending());
                c.write_expansion(0x4103U, 0U);
                c.write_expansion(0x4104U, 0U);
                ppu.step_dots(340U);
                REQUIRE_FALSE(c.irq_pending());
                ppu.step_dots(1U);
                REQUIRE(c.irq_pending());
            }
}
TEST_CASE(
    "OneBus CPU component firmware verifies shared split ROM CIRAM both IRQ sources and replay",
    "[one-bus]") {
    for (bool shared : {false, true})
        for (bool vertical : {false, true}) {
            auto c = component_cartridge(shared, vertical);
            Emulator e;
            REQUIRE(e.load_cartridge(std::move(c), ""));
            e.power_on();
            e.step_frame();
            INFO("pc=" << e.cpu().pc() << " hsync=" << +e.read_cpu(0x14U)
                       << " a12=" << +e.read_cpu(0x11U));
            REQUIRE(e.read_cpu(0x12U) == 0xC8U);
            REQUIRE(e.read_cpu(0x14U) >= 3U);
            REQUIRE(e.read_cpu(0x11U) >= 3U);
            const auto saved = e.save_state();
            e.step_frame();
            const auto expected = e.save_state();
            REQUIRE(e.load_state(saved));
            e.step_frame();
            REQUIRE(e.save_state() == expected);
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC8U);
            e.power_on();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC8U);
            const auto before = e.save_state();
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            auto bad = before;
            bad[bad.size() - tail.data().size() - 30U + 11U] |= 8U;
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE(e.save_state() == before);
            bad = before;
            bad.resize(20U);
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE(e.save_state() == before);
        }
}
TEST_CASE("OneBus machine snapshots replay while either native IRQ source is active", "[one-bus]") {
    for (bool shared : {false, true}) {
        Emulator e;
        REQUIRE(e.load_cartridge(component_cartridge(shared), ""));
        e.power_on();
        for (bool a12 : {false, true}) {
            bool checkpoint = false;
            for (unsigned instruction = 0U; instruction < 20000U; ++instruction) {
                static_cast<void>(e.step_instruction());
                const bool first_phase_done = e.read_cpu(0x14U) >= 3U;
                if (first_phase_done == a12 && e.read_cpu(0x11U) == 1U) {
                    checkpoint = true;
                    break;
                }
            }
            INFO("shared=" << shared << " a12=" << a12 << " pc=" << e.cpu().pc());
            REQUIRE(checkpoint);
            const auto saved = e.save_state();
            for (unsigned instruction = 0U; instruction < 100U; ++instruction)
                static_cast<void>(e.step_instruction());
            const auto expected = e.save_state();
            REQUIRE(e.load_state(saved));
            for (unsigned instruction = 0U; instruction < 100U; ++instruction)
                static_cast<void>(e.step_instruction());
            REQUIRE(e.save_state() == expected);
        }
        e.step_frame();
        REQUIRE(e.read_cpu(0x12U) == 0xC8U);
    }
}
} // namespace

TEST_CASE("VT02 profiles select cartridge memory through the NES 2.0 loader", "[one-bus]") {
    register_builtin_mappers();
    for (auto id : {270U, 436U}) {
        auto image = mapperbus::tests::make_one_bus_component_rom(true);
        image[6] = static_cast<Byte>(id << 4U);
        image[7] = (id & 0xF0U) | 11U;
        image[8] = id >> 8U;
        if (id == 270) {
            image[10] = 7;
            image[11] = 7;
        }
        auto c = Cartridge::from_data(image);
        REQUIRE(c);
        if (id == 270) {
            c->write_prg(0x6000, 0x6A);
            CHECK(c->read_prg(0x6000) == 0x6A);
            c->write_expansion(0x4242, 1);
            c->write_chr(0xFFF, 0x5C);
            CHECK(c->read_chr(0xFFF) == 0x5C);
        } else {
            c->write_expansion(0x4107, 1);
            CHECK(c->read_prg(0x8000) == 8);
        }
        image[13] = 10;
        CHECK_FALSE(Cartridge::from_data(image));
    }
}
