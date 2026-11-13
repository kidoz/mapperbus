#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
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
INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 221U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    h.chr_ram_size = 8192U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}
std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 16384U);
    return data;
}
std::unique_ptr<Mapper> board(bool legacy = false, std::size_t size = 2097152U) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(header(legacy), rom(size), {});
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
Address control(unsigned block, unsigned mode, bool horizontal = false) {
    return static_cast<Address>(0x8000U + (block % 8U) * 4U + (block / 8U) * 512U + mode +
                                (horizontal ? 1U : 0U));
}
// Independent inner-bank truth tables for the three documented modes.
constexpr std::array<Byte, 8> kEven = {0U, 0U, 2U, 2U, 4U, 4U, 6U, 6U};
constexpr std::array<Byte, 8> kOdd = {1U, 1U, 3U, 3U, 5U, 5U, 7U, 7U};
TEST_CASE("Mapper 221 selects mirrored, aligned and fixed-last PRG windows",
          "[mappers][mapper221]") {
    register_builtin_mappers();
    INesHeader h{};
    h.mapper_number = 221U;
    h.is_nes2 = true;
    h.chr_ram_size = 8192U;
    h.mirror_mode = MirrorMode::Horizontal;
    std::vector<Byte> prg(2097152U);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = static_cast<Byte>(i / 16384U);
    auto m = MapperRegistry::instance().create(h, std::move(prg), {});
    REQUIRE(m);
    (*m)->write_prg(0xC005U, 0xFFU);
    REQUIRE((*m)->read_prg(0x8000U) == 5U);
    REQUIRE((*m)->read_prg(0xC000U) == 5U);
    (*m)->write_prg(0x8002U, 0U);
    REQUIRE((*m)->read_prg(0x8000U) == 4U);
    REQUIRE((*m)->read_prg(0xC000U) == 5U);
    (*m)->write_prg(0x831EU, 0xA7U);
    REQUIRE((*m)->read_prg(0x8000U) == 125U);
    REQUIRE((*m)->read_prg(0xC000U) == 127U);
}

TEST_CASE("Mapper 221 routes all outer pins, inner banks, modes and CIRAM selections",
          "[mappers][mapper221]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        for (unsigned block = 0U; block < 16U; ++block)
            for (unsigned mode : {0U, 0x100U, 2U, 0x102U})
                for (unsigned inner = 0U; inner < 16U; ++inner)
                    for (bool horizontal : {false, true}) {
                        m->write_prg(control(block, mode, horizontal), 0U);
                        m->write_prg(static_cast<Address>(0xC000U + inner), 0xFFU);
                        const unsigned low =
                            block * 8U + (mode == 2U ? kEven[inner % 8U] : inner % 8U);
                        const unsigned high = block * 8U + (mode == 2U       ? kOdd[inner % 8U]
                                                            : mode == 0x102U ? 7U
                                                                             : inner % 8U);
                        REQUIRE(m->read_prg(0x8000U) == low);
                        REQUIRE(m->read_prg(0xBFFFU) == low);
                        REQUIRE(m->read_prg(0xC000U) == high);
                        REQUIRE(m->read_prg(0xFFFFU) == high);
                        REQUIRE(m->mirror_mode() ==
                                (horizontal ? MirrorMode::Horizontal : MirrorMode::Vertical));
                    }
    }
}

TEST_CASE("Mapper 221 ignores all write data and unconnected address pins",
          "[mappers][mapper221]") {
    auto m = board();
    for (Address a : {0x8000U, 0x8002U, 0x8102U, 0x8200U, 0x831FU, 0xBFFFU}) {
        m->write_prg(a, 0U);
        m->write_prg(0xC005U, 0U);
        const auto saved = state(*m);
        const Byte low = m->read_prg(0x8000U);
        const Byte high = m->read_prg(0xC000U);
        const auto mirror = m->mirror_mode();
        for (unsigned data = 0U; data < 256U; ++data) {
            m->write_prg(a, static_cast<Byte>(data));
            REQUIRE(m->read_prg(0x8000U) == low);
            REQUIRE(m->read_prg(0xC000U) == high);
            REQUIRE(m->mirror_mode() == mirror);
        }
        REQUIRE(state(*m) == saved);
    }
    for (unsigned inner = 0U; inner < 16U; ++inner) {
        m->write_prg(0x831EU, 0U);
        const auto a = static_cast<Address>(0xC000U + inner);
        m->write_prg(a, 0U);
        for (unsigned data = 0U; data < 256U; ++data) {
            m->write_prg(a, static_cast<Byte>(data));
            REQUIRE(m->read_prg(0x8000U) == 120U + inner % 8U);
            const auto value = static_cast<Byte>(data);
            m->write_chr(0x123U, value);
            REQUIRE(m->read_chr(0x123U) == (inner < 8U ? value : 0xFFU));
        }
    }
    // Sweep every CPU register address; decode aliases independently by
    // constructing a canonical command on a second board.
    auto expected = board();
    m->reset();
    m->write_chr(0x123U, 0U);
    expected->write_chr(0x123U, 0U);
    for (unsigned a = 0x8000U; a <= 0xFFFFU; ++a) {
        const auto canonical = static_cast<Address>(
            a < 0xC000U ? 0x8000U + (a % 32U) + (a / 256U % 4U) * 256U : 0xC000U + a % 16U);
        m->write_prg(static_cast<Address>(a), static_cast<Byte>(a));
        expected->write_prg(canonical, 0U);
        REQUIRE(m->read_prg(0x8000U) == expected->read_prg(0x8000U));
        REQUIRE(m->read_prg(0xC000U) == expected->read_prg(0xC000U));
        REQUIRE(m->mirror_mode() == expected->mirror_mode());
        m->write_chr(0x123U, static_cast<Byte>(a));
        expected->write_chr(0x123U, static_cast<Byte>(a));
        REQUIRE(m->read_chr(0x123U) == expected->read_chr(0x123U));
    }
}

TEST_CASE("Mapper 221 bounds decoder outputs before wrapping smaller physical ROMs",
          "[mappers][mapper221]") {
    for (bool legacy : {false, true})
        for (std::size_t pages : {1UZ, 3UZ, 8UZ, 17UZ, 128UZ}) {
            auto m = board(legacy, pages * 16384U);
            for (unsigned block = 0U; block < 16U; ++block)
                for (unsigned mode : {0U, 0x100U, 2U, 0x102U})
                    for (unsigned inner = 0U; inner < 8U; ++inner) {
                        m->write_prg(control(block, mode), 0U);
                        m->write_prg(static_cast<Address>(0xC000U + inner), 0U);
                        const unsigned low = block * 8U + (mode == 2U ? kEven[inner] : inner);
                        const unsigned high = block * 8U + (mode == 2U       ? kOdd[inner]
                                                            : mode == 0x102U ? 7U
                                                                             : inner);
                        REQUIRE(m->read_prg(0x8123U) == low % pages);
                        REQUIRE(m->read_prg(0xC123U) == high % pages);
                    }
        }
}

TEST_CASE("Mapper 221 CHR protection covers every RAM cell and retains bytes across bank changes "
          "and reset",
          "[mappers][mapper221]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        for (unsigned a = 0U; a < 8192U; ++a) {
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
            m->write_chr(static_cast<Address>(a), static_cast<Byte>(a ^ (a / 256U)));
        }
        for (unsigned inner : {8U, 15U}) {
            m->write_prg(static_cast<Address>(0xC000U + inner), 0U);
            for (unsigned a = 0U; a < 8192U; ++a) {
                const auto value = static_cast<Byte>(a ^ (a / 256U));
                m->write_chr(static_cast<Address>(a), value ^ 0xFFU);
                REQUIRE(m->read_chr(static_cast<Address>(a)) == value);
            }
        }
        m->write_prg(0x831FU, 0U);
        m->reset();
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(m->read_prg(0xC000U) == 0U);
        for (unsigned a = 0U; a < 8192U; ++a) {
            REQUIRE(m->read_chr(static_cast<Address>(a)) == static_cast<Byte>(a ^ (a / 256U)));
            m->write_chr(static_cast<Address>(a), 0x5AU);
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 0x5AU);
        }
    }
}

TEST_CASE("Mapper 221 snapshots restore protected RAM and validate canonical address latches",
          "[mappers][mapper221][state]") {
    auto m = board();
    for (unsigned a = 0U; a < 8192U; ++a)
        m->write_chr(static_cast<Address>(a), static_cast<Byte>(a ^ (a / 256U)));
    const auto reset_ram = state(*m);
    m->write_prg(0x831FU, 0U);
    m->write_prg(0xC00DU, 0U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 8197U);
    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    copy->write_chr(0x123U, 0U);
    REQUIRE(copy->read_chr(0x123U) == static_cast<Byte>(0x123U ^ 1U));
    for (std::size_t n : {0UZ, 1UZ, 2UZ, 3UZ, 4UZ, 5UZ, 4096UZ, 8196UZ}) {
        StateReader truncated(std::span(saved).first(n));
        m->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*m) == reset_ram);
    }
    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 5>{
             {{0U, 1U}, {1U, 0U}, {2U, 0x20U}, {3U, 4U}, {4U, 16U}}}) {
        auto bad = saved;
        bad[5U + 0x123U] ^= 0xFFU;
        bad[offset] = value;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == reset_ram);
    }
    StateReader restore(saved);
    m->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*m) == saved);
}

TEST_CASE("Mapper 221 rejects unsupported profiles, RAM populations and ROM geometry",
          "[mappers][mapper221]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header();
        std::size_t p = 32768U;
        std::size_t c = 0U;
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
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 16384U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 9U:
            p = 8192U;
            break;
        case 10U:
            p = 2113536U;
            break;
        case 11U:
            --p;
            break;
        case 12U:
            c = 8192U;
            break;
        case 13U:
            p = 0U;
            break;
        case 14U:
            h = header(true);
            h.has_battery = true;
            break;
        case 15U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 16U:
            h.chr_ram_size = 4096U;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
    auto m = board();
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->has_expansion_audio());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
}

std::vector<Byte> image(bool legacy = false, bool vertical = true) {
    auto p = rom(2097152U);
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
    const auto pwrite = [&store, &paddr](Address a, Byte value) {
        paddr(a);
        store(0x2007U, value);
    };
    const auto pread = [&code, &paddr, &expect](Address a, Byte value) {
        paddr(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, value);
    };
    store(0x4017U, 0x40U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    for (unsigned block : {0U, 3U, 8U, 15U})
        for (unsigned mode : {0U, 0x100U, 2U, 0x102U})
            for (Byte inner : {1U, 5U}) {
                store(control(block, mode), 0xA7U);
                store(static_cast<Address>(0xC000U + inner), 0U);
                const auto low =
                    static_cast<Byte>(block * 8U + (mode == 2U ? kEven[inner] : inner));
                const auto high = static_cast<Byte>(block * 8U + (mode == 2U       ? kOdd[inner]
                                                                  : mode == 0x102U ? 7U
                                                                                   : inner));
                expect(0x9A00U, low);
                expect(0xBA00U, low);
                expect(0xDA00U, high);
                expect(0xFA00U, high);
                pwrite(0x123U, 0x39U);
                store(static_cast<Address>(0xC008U + inner), 0xFFU);
                pwrite(0x123U, 0xA7U);
                pread(0x123U, 0x39U);
                store(static_cast<Address>(0xC000U + inner), 0U);
                pwrite(0x123U, 0xA7U);
                pread(0x123U, 0xA7U);
            }
    store(0x8000U, 0xFFU);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0x8001U, 0U);
    pread(0x2400U, 0x19U);
    store(0x8000U, 0U);
    pread(0x2400U, 0x27U);
    pwrite(0x1FFFU, 0x5AU);
    store(0xC00DU, 0U);
    pwrite(0x1FFFU, 0xC3U);
    pread(0x1FFFU, 0x5AU);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    // All three modes move the upper CPU window. Replicate original firmware
    // within each 16 KiB page to keep opcode fetches stable through switches.
    for (std::size_t base = 0U; base < p.size(); base += 16384U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
        for (std::size_t v = base + 0x3FFAU; v < base + 16384U; v += 2U) {
            p[v] = 0U;
            p[v + 1U] = 0xE0U;
        }
    }
    std::vector<Byte> result(16U);
    result[0] = 'N';
    result[1] = 'E';
    result[2] = 'S';
    result[3] = 0x1AU;
    result[4] = 128U;
    result[6] = static_cast<Byte>(0xD0U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0xD0U : 0xD8U;
    result[11] = legacy ? 0U : 7U;
    result.insert(result.end(), p.begin(), p.end());
    return result;
}
Cartridge cartridge(bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(legacy, vertical));
    REQUIRE(c);
    return std::move(*c);
}
void paddr(Ppu& p, Address a) {
    (void)p.read_register(2U);
    p.write_register(6U, static_cast<Byte>(a >> 8U));
    p.write_register(6U, static_cast<Byte>(a));
}
void pwrite(Ppu& p, Address a, Byte value) {
    paddr(p, a);
    p.write_register(7U, value);
}
Byte pread(Ppu& p, Address a) {
    paddr(p, a);
    (void)p.read_register(7U);
    return p.read_register(7U);
}

TEST_CASE("Mapper 221 PPUDATA honors write protection while CIRAM mirroring remains independent",
          "[mappers][mapper221][ppu]") {
    auto c = cartridge();
    Ppu p;
    p.connect_cartridge(&c);
    p.reset();
    pwrite(p, 0x123U, 0x39U);
    c.write_prg(0xC008U, 0U);
    pwrite(p, 0x123U, 0xA7U);
    REQUIRE(pread(p, 0x123U) == 0x39U);
    pwrite(p, 0x2000U, 0x19U);
    pwrite(p, 0x2400U, 0x27U);
    c.write_prg(0x8001U, 0U);
    REQUIRE(pread(p, 0x2400U) == 0x19U);
    c.write_prg(0x8000U, 0U);
    REQUIRE(pread(p, 0x2400U) == 0x27U);
    pwrite(p, 0x123U, 0xA7U);
    REQUIRE(pread(p, 0x123U) == 0x39U);
    c.reset();
    REQUIRE(pread(p, 0x123U) == 0x39U);
    pwrite(p, 0x123U, 0xA7U);
    REQUIRE(pread(p, 0x123U) == 0xA7U);
}

TEST_CASE("Mapper 221 ignores low writes and reads leave every CPU open-bus bit intact",
          "[mappers][mapper221]") {
    auto m = board();
    m->write_prg(0x831FU, 0U);
    m->write_prg(0xC00DU, 0U);
    const auto saved = state(*m);
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        const auto addr = static_cast<Address>(a);
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE_FALSE(m->maps_expansion(addr));
        m->write_prg(addr, 0xFFU);
        m->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*m) == saved);
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (unsigned value = 0U; value < 256U; ++value)
        for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
            bus.write(a, 0xFFU);
            bus.write(0x10U, static_cast<Byte>(value));
            REQUIRE(bus.read(a) == value);
        }
}

TEST_CASE("Mapper 221 CPU fixtures verify address latches, protected RAM, reset and atomic replay",
          "[mappers][mapper221][state]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            Emulator e;
            REQUIRE(e.load_cartridge(cartridge(legacy, vertical), ""));
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            const auto early = e.save_state();
            e.step_frame();
            const auto future = e.save_state();
            REQUIRE(e.load_state(early));
            e.step_frame();
            REQUIRE(e.save_state() == future);
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            const auto start = early.size() - tail.data().size() - 8197U;
            for (std::size_t offset : {0UZ, 1UZ, 2UZ, 3UZ, 4UZ}) {
                auto bad = early;
                bad[start + 5U + 0x123U] = 0U;
                bad[start + offset] ^= 0xFFU;
                const auto before = e.save_state();
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == before);
            }
            auto truncated = early;
            truncated.resize(start + 8196U);
            const auto before = e.save_state();
            REQUIRE_FALSE(e.load_state(truncated));
            REQUIRE(e.save_state() == before);
            e.reset();
            REQUIRE(e.read_cpu(0xFA00U) == 0U);
            const auto reset = e.save_state();
            REQUIRE(reset[start + 5U + 0x123U] == 0xA7U);
            REQUIRE(reset[start + 5U + 0x1FFFU] == 0x5AU);
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        }
}
} // namespace
