#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <tuple>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
TEST_CASE("Mapper 227 registers the documented cartridge board", "[mappers][mapper227]") {
    register_builtin_mappers();
    INesHeader h{};
    h.mapper_number = 227U;
    h.is_nes2 = true;
    h.submapper = 1U;
    h.chr_ram_size = 8192U;
    h.mirror_mode = MirrorMode::Horizontal;
    auto m = MapperRegistry::instance().create(h, std::vector<Byte>(1048576U), {});
    REQUIRE(m);
}

INesHeader header(Byte sub = 0U, bool battery = false, bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 227U;
    h.is_nes2 = !legacy;
    h.submapper = sub;
    h.has_battery = battery;
    h.chr_ram_size = 8192U;
    h.prg_nvram_size = battery && !legacy ? 8192U : 0U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = MirrorMode::Horizontal;
    return h;
}
std::vector<Byte> rom(std::size_t pages) {
    std::vector<Byte> p(pages * 16384U);
    for (std::size_t a = 0U; a < p.size(); ++a)
        p[a] = static_cast<Byte>(a / 16384U);
    return p;
}
std::unique_ptr<Mapper> board(Byte sub = 0U,
                              bool battery = false,
                              bool legacy = false,
                              std::size_t pages = 64U) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(header(sub, battery, legacy), rom(pages), {});
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
Address command(unsigned outer, unsigned inner, unsigned mode, bool horizontal = false) {
    return static_cast<Address>(0x8000U + (outer / 4U) * 256U + (outer % 4U) * 8U +
                                (inner / 2U) * 32U + (inner % 2U) * 4U + mode +
                                (horizontal ? 2U : 0U));
}
constexpr std::array<Byte, 8> kEven{0U, 0U, 2U, 2U, 4U, 4U, 6U, 6U};
constexpr std::array<Byte, 8> kOdd{1U, 1U, 3U, 3U, 5U, 5U, 7U, 7U};
// Independent mode table: fixed-first, fixed-last, mirrored 16K, aligned 32K.
std::pair<unsigned, unsigned> banks(
    unsigned block, unsigned inner, unsigned mode, Byte sub, bool battery) {
    unsigned low = (mode % 2U) != 0U ? kEven[inner] : inner;
    unsigned high = (mode & 128U) != 0U || battery ? ((mode % 2U) != 0U ? kOdd[inner] : inner)
                                                   : ((mode & 512U) != 0U ? 7U : 0U);
    return {((sub == 2U && low == 0U) ? (block / 4U) * 32U : block * 8U) + low,
            ((sub == 2U && high == 0U) ? (block / 4U) * 32U : block * 8U) + high};
}

TEST_CASE("Mapper 227 decodes all banks, modes, populations and physical ROM sizes",
          "[mappers][mapper227]") {
    for (const auto [sub, battery, legacy] :
         std::array<std::tuple<Byte, bool, bool>, 5>{{{0U, false, false},
                                                      {1U, false, false},
                                                      {2U, false, false},
                                                      {0U, true, false},
                                                      {0U, true, true}}})
        for (std::size_t pages : {1UZ, 3UZ, 64UZ}) {
            auto m = board(sub, battery, legacy, pages);
            for (unsigned block = 0U; block < 8U; ++block)
                for (unsigned inner = 0U; inner < 8U; ++inner)
                    for (unsigned mode : {0U, 1U, 128U, 129U, 512U, 513U, 640U, 641U})
                        for (bool horizontal : {false, true}) {
                            m->write_prg(command(block, inner, mode, horizontal), 0xA7U);
                            const auto [low, high] = banks(block, inner, mode, sub, battery);
                            REQUIRE(m->read_prg(0x8000U) == low % pages);
                            REQUIRE(m->read_prg(0xBFFFU) == low % pages);
                            REQUIRE(m->read_prg(0xC000U) == high % pages);
                            REQUIRE(m->read_prg(0xFFFFU) == high % pages);
                            REQUIRE(m->mirror_mode() ==
                                    (horizontal ? MirrorMode::Horizontal : MirrorMode::Vertical));
                        }
        }
}
TEST_CASE("Mapper 227 ignores write data and all unconnected address lines",
          "[mappers][mapper227]") {
    auto m = board(2U);
    for (unsigned a = 0x8000U; a <= 0xFFFFU; ++a) {
        const unsigned block = (a / 256U % 2U) * 4U + (a / 8U % 4U);
        const unsigned inner = (a / 32U % 4U) * 2U + (a / 4U % 2U);
        const unsigned mode = (a % 2U) + (a / 128U % 2U) * 128U + (a / 512U % 2U) * 512U;
        const auto [low, high] = banks(block, inner, mode, 2U, false);
        m->write_prg(static_cast<Address>(a), static_cast<Byte>(a));
        REQUIRE(m->read_prg(0x8000U) == low);
        REQUIRE(m->read_prg(0xC000U) == high);
        REQUIRE(m->mirror_mode() ==
                ((a / 2U % 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (Address a : {0x8000U, 0x81FFU, 0x87FFU, 0xFFFFU}) {
        m->write_prg(a, 0U);
        const auto saved = state(*m);
        for (unsigned v = 0U; v < 256U; ++v) {
            m->write_prg(a, static_cast<Byte>(v));
            REQUIRE(state(*m) == saved);
        }
    }
}
TEST_CASE("Mapper 227 solder pads replace ROM address bits only in submapper one read mode",
          "[mappers][mapper227]") {
    register_builtin_mappers();
    for (Byte sub : {0U, 1U, 2U}) {
        auto p = rom(64U);
        for (std::size_t a = 0U; a < p.size(); ++a)
            p[a] = static_cast<Byte>((a / 16384U) * 17U + a % 16U);
        auto made = MapperRegistry::instance().create(header(sub), std::move(p), {});
        REQUIRE(made);
        auto& m = **made;
        for (unsigned pads = 0U; pads < 16U; ++pads) {
            const auto result = m.configure_board_profile("menu-" + std::to_string(pads));
            REQUIRE(result.has_value() == (sub == 1U));
            for (unsigned mode : {0U, 0x400U}) {
                m.write_prg(command(5U, 5U, 128U + mode), 0U);
                for (unsigned nibble = 0U; nibble < 16U; ++nibble) {
                    const unsigned expected = sub == 1U && mode != 0U ? pads : nibble;
                    REQUIRE(m.read_prg(static_cast<Address>(0x9B00U + nibble)) ==
                            static_cast<Byte>(45U * 17U + expected));
                    REQUIRE(m.read_prg(static_cast<Address>(0xDB00U + nibble)) ==
                            static_cast<Byte>(45U * 17U + expected));
                }
            }
        }
        const auto saved = state(m);
        REQUIRE_FALSE(m.configure_board_jumpers(16U));
        for (std::string_view invalid : {"",
                                         "menu-",
                                         "menu--1",
                                         "menu-16",
                                         "menu-256",
                                         "menu-1x",
                                         "menu-9999999999999",
                                         "other"})
            REQUIRE_FALSE(m.configure_board_profile(invalid));
        REQUIRE(state(m) == saved);
        m.reset();
        REQUIRE(m.read_prg(0x9B07U) == 7U);
        m.write_prg(0x8400U, 0U);
        REQUIRE(m.read_prg(0x9B07U) == (sub == 1U ? 15U : 7U));
    }
}
TEST_CASE("Mapper 227 owns only fitted RAM and protects every CHR cell in multicart NROM modes",
          "[mappers][mapper227]") {
    for (Byte sub : {0U, 1U, 2U})
        for (bool battery : {false, true}) {
            if (battery && sub != 0U)
                continue;
            auto m = board(sub, battery);
            REQUIRE(m->battery_ram().size() == (battery ? 8192U : 0U));
            for (unsigned a = 0U; a < 8192U; ++a) {
                REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
                m->write_chr(static_cast<Address>(a), 0x39U);
            }
            for (unsigned mode : {128U, 129U, 640U, 641U}) {
                m->write_prg(command(7U, 5U, mode), 0U);
                for (unsigned a = 0U; a < 8192U; ++a) {
                    m->write_chr(static_cast<Address>(a), 0xA7U);
                    REQUIRE(m->read_chr(static_cast<Address>(a)) == (battery ? 0xA7U : 0x39U));
                }
            }
            m->reset();
            REQUIRE(m->read_prg(0x8000U) == 0U);
            REQUIRE(m->read_prg(0xC000U) == 0U);
            REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
            for (unsigned a = 0U; a < 8192U; ++a) {
                REQUIRE(m->read_chr(static_cast<Address>(a)) == (battery ? 0xA7U : 0x39U));
                m->write_chr(static_cast<Address>(a), 0x5AU);
                REQUIRE(m->read_chr(static_cast<Address>(a)) == 0x5AU);
            }
        }
}
TEST_CASE("Mapper 227 WRAM is unbanked, writable and persistent only on battery-backed boards",
          "[mappers][mapper227]") {
    for (bool legacy : {false, true}) {
        auto m = board(0U, true, legacy);
        for (unsigned a = 0x6000U; a < 0x8000U; ++a) {
            REQUIRE(m->maps_prg(static_cast<Address>(a)));
            REQUIRE(m->read_prg(static_cast<Address>(a)) == 0U);
            m->write_prg(static_cast<Address>(a), static_cast<Byte>(a ^ (a / 256U)));
        }
        const std::vector<Byte> saved(m->battery_ram().begin(), m->battery_ram().end());
        m->write_prg(0x87FFU, 0U);
        for (unsigned a = 0x6000U; a < 0x8000U; ++a)
            REQUIRE(m->read_prg(static_cast<Address>(a)) == static_cast<Byte>(a ^ (a / 256U)));
        m->reset();
        auto copy = board(0U, true, legacy);
        copy->set_battery_ram(saved);
        REQUIRE(std::ranges::equal(copy->battery_ram(), saved));
        const std::array<Byte, 2> partial{0xC3U, 0x5AU};
        copy->set_battery_ram(partial);
        REQUIRE(copy->read_prg(0x6000U) == 0xC3U);
        REQUIRE(copy->read_prg(0x6001U) == 0x5AU);
        REQUIRE(copy->read_prg(0x6002U) == saved[2U]);
        const std::vector<Byte> oversized(16384U, 0xA7U);
        copy->set_battery_ram(oversized);
        REQUIRE(copy->battery_ram().size() == 8192U);
        REQUIRE(copy->read_prg(0x7FFFU) == 0xA7U);
        REQUIRE(std::ranges::equal(m->battery_ram(), saved));
    }
}
TEST_CASE("Mapper 227 validates header populations and decoder limits", "[mappers][mapper227]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header();
        std::size_t p = 32768U, c = 0U;
        switch (kind) {
        case 0:
            h.submapper = 3U;
            break;
        case 1:
            h.chr_ram_size = 0U;
            break;
        case 2:
            h.chr_ram_size = 16384U;
            break;
        case 3:
            h.chr_nvram_size = 8192U;
            break;
        case 4:
            h.prg_ram_size = 8192U;
            break;
        case 5:
            h.prg_nvram_size = 8192U;
            break;
        case 6:
            h.has_battery = true;
            break;
        case 7:
            h = header(1U, true);
            break;
        case 8:
            h = header(2U, true);
            break;
        case 9:
            h = header(0U, true);
            h.prg_nvram_size = 4096U;
            break;
        case 10:
            p = 8192U;
            break;
        case 11:
            p = 1064960U;
            break;
        case 12:
            --p;
            break;
        case 13:
            c = 8192U;
            break;
        case 14:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 15:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 16:
            p = 0U;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
}
TEST_CASE("Mapper 227 snapshots validate fitted hardware and commit RAM only after validation",
          "[mappers][mapper227][state]") {
    for (Byte sub : {0U, 1U, 2U})
        for (bool battery : {false, true}) {
            if (battery && sub != 0U)
                continue;
            auto m = board(sub, battery);
            if (sub == 1U)
                REQUIRE(m->configure_board_jumpers(9U));
            m->write_chr(0x123U, 0x39U);
            m->write_prg(0x6123U, 0x5AU);
            const auto reset_ram = state(*m);
            m->write_prg(0x87FFU, 0U);
            const auto saved = state(*m);
            REQUIRE(saved.size() == (battery ? 16388U : 8196U));
            auto copy = board(sub, battery);
            if (sub == 1U)
                REQUIRE(copy->configure_board_jumpers(9U));
            StateReader reader(saved);
            copy->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(reader.remaining() == 0U);
            REQUIRE(state(*copy) == saved);
            for (std::size_t n : {0UZ, 1UZ, 2UZ, 3UZ, 4UZ, 4096UZ, saved.size() - 1U}) {
                StateReader truncated(std::span(saved).first(n));
                m->load_state(truncated);
                REQUIRE_FALSE(truncated.ok());
                REQUIRE(state(*m) == reset_ram);
            }
            for (std::size_t offset : {0UZ, 1UZ, 3UZ}) {
                auto bad = saved;
                bad[offset] ^= 0x80U;
                bad[4U + 0x123U] = 0xA7U;
                StateReader invalid(bad);
                m->load_state(invalid);
                REQUIRE_FALSE(invalid.ok());
                REQUIRE(state(*m) == reset_ram);
            }
            if (sub == 1U) {
                REQUIRE(copy->configure_board_jumpers(8U));
                StateReader wrong_pads(saved);
                copy->load_state(wrong_pads);
                REQUIRE_FALSE(wrong_pads.ok());
            }
            auto other = board(sub == 0U ? 2U : 0U);
            StateReader wrong_board(saved);
            other->load_state(wrong_board);
            REQUIRE_FALSE(wrong_board.ok());
            StateReader restore(saved);
            m->load_state(restore);
            REQUIRE(restore.ok());
            REQUIRE(state(*m) == saved);
        }
}

std::vector<Byte> image(Byte sub, bool battery, bool legacy, Byte pads = 0U) {
    auto p = rom(64U);
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
    const auto paddr = [&store](Address a) {
        store(0x2006U, static_cast<Byte>(a >> 8U));
        store(0x2006U, static_cast<Byte>(a));
    };
    const auto pwrite = [&store, &paddr](Address a, Byte v) {
        paddr(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &paddr, &expect](Address a, Byte v) {
        paddr(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    for (unsigned block : {0U, 3U, 7U})
        for (unsigned inner : {0U, 5U})
            for (unsigned mode : {0U, 1U, 128U, 129U, 512U, 513U}) {
                store(command(block, inner, mode), 0xA7U);
                const auto [low, high] = banks(block, inner, mode, sub, battery);
                expect(0x9A00U, static_cast<Byte>(low));
                expect(0xFA00U, static_cast<Byte>(high));
            }
    store(0x8000U, 0U);
    pwrite(0x123U, 0x39U);
    store(0x8080U, 0U);
    pwrite(0x123U, 0xA7U);
    pread(0x123U, battery ? 0xA7U : 0x39U);
    store(0x8000U, 0U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0x8002U, 0U);
    pread(0x2400U, 0x19U);
    store(0x8000U, 0U);
    pread(0x2400U, 0x27U);
    if (sub == 1U) {
        // Solder pads affect opcode fetches too. Run this probe from CPU RAM,
        // then clear m before returning to the replicated ROM firmware.
        const auto continuation = static_cast<Address>(0xE000U + code.size() + 103U);
        const std::array<Byte, 20> routine{0xA9U,
                                           0U,
                                           0x8DU,
                                           0U,
                                           0x84U,
                                           0xADU,
                                           7U,
                                           0x9BU,
                                           0xC9U,
                                           static_cast<Byte>(0xA0U + pads),
                                           0xD0U,
                                           0xFEU,
                                           0xA9U,
                                           0U,
                                           0x8DU,
                                           0U,
                                           0x80U,
                                           0x4CU,
                                           static_cast<Byte>(continuation),
                                           static_cast<Byte>(continuation >> 8U)};
        for (std::size_t a = 0U; a < routine.size(); ++a)
            store(static_cast<Address>(0x200U + a), routine[a]);
        code.insert(code.end(), {0x4CU, 0U, 2U});
    }
    if (battery) {
        store(0x6123U, 0x5AU);
        expect(0x6123U, 0x5AU);
        code.insert(code.end(), {0xEEU, 0xFFU, 0x7FU}); // Persistent boot counter.
        store(0x6000U, 0xC7U);
    }
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    for (std::size_t base = 0U; base < p.size(); base += 16384U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
        for (unsigned nibble = 0U; nibble < 16U; ++nibble)
            p[base + 0x1B00U + nibble] = static_cast<Byte>(0xA0U + nibble);
        for (std::size_t v = base + 0x3FFAU; v < base + 16384U; v += 2U) {
            p[v] = 0U;
            p[v + 1U] = 0xE0U;
        }
    }
    std::vector<Byte> data(16U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = 64U;
    data[6] = static_cast<Byte>(0x30U | (battery ? 2U : 0U));
    data[7] = legacy ? 0xE0U : 0xE8U;
    data[8] = static_cast<Byte>(sub << 4U);
    data[10] = battery && !legacy ? 0x70U : 0U;
    data[11] = legacy ? 0U : 7U;
    data.insert(data.end(), p.begin(), p.end());
    return data;
}
Cartridge cartridge(Byte sub = 0U, bool battery = false, bool legacy = false, Byte pads = 0U) {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(sub, battery, legacy, pads));
    REQUIRE(c);
    if (sub == 1U)
        REQUIRE(c->configure_board_jumpers(pads));
    return std::move(*c);
}
TEST_CASE("Mapper 227 CPU fixtures exercise all populations, solder-pad reads and atomic replay",
          "[mappers][mapper227][state]") {
    for (const auto [sub, battery, legacy, pads] :
         std::array<std::tuple<Byte, bool, bool, Byte>, 8>{{{0U, false, false, 0U},
                                                            {0U, false, true, 0U},
                                                            {0U, true, false, 0U},
                                                            {0U, true, true, 0U},
                                                            {1U, false, false, 0U},
                                                            {1U, false, false, 5U},
                                                            {1U, false, false, 15U},
                                                            {2U, false, false, 0U}}}) {
        Emulator e;
        REQUIRE(e.load_cartridge(cartridge(sub, battery, legacy, pads), ""));
        e.reset();
        e.step_frame();
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        if (battery)
            REQUIRE(e.read_cpu(0x6000U) == 0xC7U);
        const auto early = e.save_state();
        e.step_frame();
        const auto future = e.save_state();
        REQUIRE(e.load_state(early));
        e.step_frame();
        REQUIRE(e.save_state() == future);
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        const std::size_t payload = battery ? 16388U : 8196U;
        const auto start = early.size() - tail.data().size() - payload;
        for (std::size_t offset : {0UZ, 1UZ, 3UZ}) {
            auto bad = early;
            bad[start + offset] ^= 0x80U;
            bad[start + 4U + 0x123U] = 0U;
            if (battery)
                bad[start + 8196U + 0x123U] = 0U;
            const auto before = e.save_state();
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE(e.save_state() == before);
        }
        auto truncated = early;
        truncated.pop_back();
        const auto before = e.save_state();
        REQUIRE_FALSE(e.load_state(truncated));
        REQUIRE(e.save_state() == before);
        e.reset();
        REQUIRE(e.read_cpu(0xFA00U) == 0U);
        const auto reset = e.save_state();
        REQUIRE(reset[start + 4U + 0x123U] == (battery ? 0xA7U : 0x39U));
        if (battery)
            REQUIRE(e.read_cpu(0x6123U) == 0x5AU);
        e.step_frame();
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
    }
}
TEST_CASE("Mapper 227 leaves absent CPU RAM and expansion pins floating", "[mappers][mapper227]") {
    for (Byte sub : {0U, 1U, 2U}) {
        auto c = cartridge(sub);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        for (unsigned value = 0U; value < 256U; ++value)
            for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
                bus.write(a, 0xFFU);
                bus.write(0x10U, static_cast<Byte>(value));
                REQUIRE(bus.read(a) == value);
            }
        auto m = board(sub);
        const auto saved = state(*m);
        for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
            const auto addr = static_cast<Address>(a);
            REQUIRE_FALSE(m->maps_prg(addr));
            REQUIRE_FALSE(m->maps_expansion(addr));
            m->write_prg(addr, 0xFFU);
            m->write_expansion(addr, 0xFFU);
        }
        REQUIRE(state(*m) == saved);
        REQUIRE_FALSE(m->has_expansion_audio());
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
        REQUIRE_FALSE(m->irq_pending());
    }
}
} // namespace
