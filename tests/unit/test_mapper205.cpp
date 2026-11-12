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
    h.mapper_number = 205U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}
std::vector<Byte> rom(std::size_t size, std::size_t page_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>((i / page_size) >> (i % page_size == 1U ? 8U : 0U));
    return data;
}
std::unique_ptr<Mapper> board(bool force = false,
                              bool legacy = false,
                              std::size_t p = 524288U,
                              std::size_t c = 524288U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(header(legacy), rom(p, 8192U), rom(c, 1024U));
    REQUIRE(r);
    REQUIRE((*r)->configure_board_jumpers(force ? 1U : 0U));
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
TEST_CASE("Mapper 205 applies each documented PRG block to switchable and fixed banks",
          "[mappers][mapper205]") {
    constexpr std::array<unsigned, 4> kAnd = {0x1FU, 0x1FU, 0x0FU, 0x0FU};
    constexpr std::array<unsigned, 4> kOr = {0U, 0x10U, 0x20U, 0x30U};
    for (bool force : {false, true})
        for (bool legacy : {false, true}) {
            auto m = board(force, legacy);
            for (unsigned raw = 0U; raw < 4U; ++raw) {
                const unsigned block = force && (raw & 3U) == 1U ? 3U : raw & 3U;
                m->write_prg(static_cast<Address>(0x6000U + raw), static_cast<Byte>(raw));
                for (Byte mode : {0U, 0x40U})
                    for (unsigned value = 0U; value < 256U; ++value) {
                        bank(*m, 6U, static_cast<Byte>(value), mode);
                        bank(*m, 7U, static_cast<Byte>(value ^ 0x35U), mode);
                        const std::array<unsigned, 4> inner = {mode == 0U ? value : 0x3EU,
                                                               value ^ 0x35U,
                                                               mode == 0U ? 0x3EU : value,
                                                               0x3FU};
                        for (unsigned slot = 0U; slot < 4U; ++slot)
                            REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                                    ((inner[slot] & kAnd[block]) | kOr[block]));
                    }
            }
        }
}
TEST_CASE("Mapper 205 block register remains writable through every A001 gate",
          "[mappers][mapper205]") {
    auto m = board();
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        m->write_prg(0xA001U, static_cast<Byte>(gate));
        m->write_prg(0x7FFFU, 3U);
        REQUIRE(m->read_prg(0xE000U) == 63U);
        m->write_prg(0x6000U, 0U);
        REQUIRE(m->read_prg(0xE000U) == 31U);
    }
    REQUIRE(m->battery_ram().empty());
}
TEST_CASE("Mapper 205 restores block and IRQ state and rejects a different solder pad",
          "[mappers][mapper205][state]") {
    auto m = board();
    m->write_prg(0x6000U, 1U);
    bank(*m, 6U, 0x17U, 0xC0U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    m->clock_irq_counter();
    const auto saved = state(*m);
    REQUIRE(saved.size() == 18U);
    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    copy->write_prg(0x6000U, 3U); // A game switch must preserve the in-flight counter.
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    auto alternate = board(true);
    StateReader wrong(saved);
    alternate->load_state(wrong);
    REQUIRE_FALSE(wrong.ok());
}

TEST_CASE("Mapper 205 applies CHR block pins after pair alignment and inversion",
          "[mappers][mapper205]") {
    constexpr std::array<unsigned, 4> kAnd = {0xFFU, 0xFFU, 0x7FU, 0x7FU};
    constexpr std::array<unsigned, 4> kOr = {0U, 0x80U, 0x100U, 0x180U};
    for (bool force : {false, true}) {
        auto m = board(force);
        for (unsigned raw = 0U; raw < 4U; ++raw) {
            const unsigned block = force && raw == 1U ? 3U : raw;
            m->write_prg(0x6000U, static_cast<Byte>(raw));
            for (Byte mode : {0U, 0x80U})
                for (Byte reg = 0U; reg < 6U; ++reg)
                    for (unsigned value = 0U; value < 256U; ++value) {
                        bank(*m, reg, static_cast<Byte>(value), mode);
                        const unsigned start = reg < 2U ? reg * 2U + (mode == 0U ? 0U : 4U)
                                                        : reg - 2U + (mode == 0U ? 4U : 0U);
                        for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                            const unsigned inner = reg < 2U ? (value & 0xFEU) | half : value;
                            const unsigned expected = (inner & kAnd[block]) | kOr[block];
                            const auto a = static_cast<Address>((start + half) * 1024U);
                            m->write_chr(a, 0xA7U);
                            REQUIRE(m->read_chr(a) == static_cast<Byte>(expected));
                            REQUIRE(m->read_chr(a + 1U) == (expected >> 8U));
                        }
                    }
        }
    }
}

TEST_CASE("Mapper 205 every low address immediately updates both ROMs and ignores unused data",
          "[mappers][mapper205]") {
    auto m = board();
    bank(*m, 6U, 0x23U);
    bank(*m, 2U, 0xBCU);
    constexpr std::array<Byte, 4> kPrg = {3U, 19U, 35U, 51U};
    constexpr std::array<unsigned, 4> kChr = {0xBCU, 0xBCU, 0x13CU, 0x1BCU};
    for (unsigned a = 0x6000U; a < 0x8000U; ++a) {
        const auto value = static_cast<Byte>(a ^ (a >> 8U));
        m->write_prg(static_cast<Address>(a), value);
        REQUIRE(m->read_prg(0x8000U) == kPrg[value & 3U]);
        REQUIRE(m->read_chr(0x1000U) == static_cast<Byte>(kChr[value & 3U]));
        REQUIRE(m->read_chr(0x1001U) == (kChr[value & 3U] >> 8U));
    }
    const auto saved = state(*m);
    for (unsigned a = 0x4020U; a < 0x6000U; ++a) {
        REQUIRE_FALSE(m->maps_expansion(static_cast<Address>(a)));
        m->write_expansion(static_cast<Address>(a), 0xFFU);
        m->write_prg(static_cast<Address>(a), 0xFFU);
    }
    REQUIRE(state(*m) == saved);
}

TEST_CASE("Mapper 205 wraps physical ROM sizes after applying the block pins",
          "[mappers][mapper205]") {
    for (bool legacy : {false, true})
        for (std::size_t p : {32768UZ, 98304UZ, 524288UZ})
            for (std::size_t c : {8192UZ, 24576UZ, 524288UZ}) {
                auto m = board(false, legacy, p, c);
                for (Byte block = 0U; block < 4U; ++block) {
                    m->write_prg(0x6000U, block);
                    const unsigned pmask = block < 2U ? 0x1FU : 0x0FU;
                    const unsigned cmask = block < 2U ? 0xFFU : 0x7FU;
                    for (unsigned value = 0U; value < 256U; ++value) {
                        bank(*m, 6U, static_cast<Byte>(value));
                        bank(*m, 2U, static_cast<Byte>(value));
                        const auto expected = ((value & cmask) | (block * 128U)) % (c / 1024U);
                        REQUIRE(m->read_prg(0x8000U) ==
                                ((value & pmask) | (block * 16U)) % (p / 8192U));
                        REQUIRE(m->read_prg(0xE000U) ==
                                ((63U & pmask) | (block * 16U)) % (p / 8192U));
                        REQUIRE(m->read_chr(0x1000U) == static_cast<Byte>(expected));
                        REQUIRE(m->read_chr(0x1001U) == (expected >> 8U));
                    }
                }
            }
}

TEST_CASE("Mapper 205 validates compact state and retains its physical pad across reset",
          "[mappers][mapper205][state]") {
    for (bool force : {false, true}) {
        auto m = board(force);
        const auto initial = state(*m);
        for (unsigned size = 0U; size < initial.size(); ++size) {
            m->write_prg(0x6000U, 3U);
            StateReader short_reader(std::span(initial).first(size));
            m->load_state(short_reader);
            REQUIRE_FALSE(short_reader.ok());
            REQUIRE(state(*m) == initial);
        }
        for (std::size_t offset : {0UZ, 1UZ, 8UZ, 9UZ, 10UZ, 13UZ, 14UZ, 15UZ, 16UZ, 17UZ}) {
            auto bad = initial;
            bad[offset] = 0xFFU;
            StateReader reader(bad);
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == initial);
        }
        m->write_prg(0x6000U, 3U);
        m->write_prg(0xA000U, 1U);
        m->write_prg(0xC000U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->reset();
        REQUIRE(state(*m) == initial);
        m->write_prg(0x6000U, 1U);
        REQUIRE(m->read_prg(0xE000U) == (force ? 63U : 31U));
        const auto before = state(*m);
        REQUIRE_FALSE(m->configure_board_jumpers(2U));
        REQUIRE_FALSE(m->configure_board_profile("nitra-l401"));
        REQUIRE(state(*m) == before);
        REQUIRE(m->configure_board_profile("jc-016-2-force-block3"));
        REQUIRE(m->read_prg(0xE000U) == 63U);
        REQUIRE(m->configure_board_profile("jc-016-2"));
        REQUIRE(m->read_prg(0xE000U) == 31U);
    }
}

TEST_CASE("Mapper 205 rejects undocumented memory populations and submappers",
          "[mappers][mapper205]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 16U; ++kind) {
        auto h = header();
        auto p = rom(524288U, 8192U);
        auto c = rom(524288U, 1024U);
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
            h.chr_ram_size = 8192U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 8U:
            p.resize(16384U);
            break;
        case 9U:
            p.resize(532480U);
            break;
        case 10U:
            p.pop_back();
            break;
        case 11U:
            c.clear();
            break;
        case 12U:
            c.resize(525312U);
            break;
        case 13U:
            c.pop_back();
            break;
        case 14U:
            h = header(true);
            h.has_battery = true;
            break;
        case 15U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(MapperRegistry::instance().create(h, std::move(p), std::move(c)));
    }
}

std::vector<Byte> image(bool force = false, bool legacy = false, bool vertical = true) {
    auto p = rom(524288U, 8192U);
    const auto c = rom(524288U, 1024U);
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
    store(0x13U, 0U);
    for (Byte raw = 0U; raw < 4U; ++raw) {
        const unsigned block = force && raw == 1U ? 3U : raw;
        const unsigned pmask = block < 2U ? 0x1FU : 0x0FU;
        const unsigned cmask = block < 2U ? 0xFFU : 0x7FU;
        for (Byte mode : {0U, 0x40U}) {
            set_bank(6U, 0x35U, mode);
            set_bank(7U, 0x2AU, mode);
            set_bank(2U, 0xD5U, mode);
            store(static_cast<Address>(0x6000U + raw * 0x555U), static_cast<Byte>(0xFCU | raw));
            const auto mapped = [pmask, block](unsigned value) {
                return static_cast<Byte>((value & pmask) | (block * 16U));
            };
            expect(0x9A00U, mapped(mode == 0U ? 0x35U : 0x3EU));
            expect(0xBA00U, mapped(0x2AU));
            expect(0xDA00U, mapped(mode == 0U ? 0x3EU : 0x35U));
            expect(0xFA00U, mapped(0x3FU));
            const auto chr = (0xD5U & cmask) | (block * 128U);
            pread(0x1000U, static_cast<Byte>(chr));
            pread(0x1001U, static_cast<Byte>(chr >> 8U));
            pwrite(0x1000U, 0U);
            pread(0x1000U, static_cast<Byte>(chr));
            set_bank(2U, 0xD5U, static_cast<Byte>(mode | 0x80U));
            pread(0U, static_cast<Byte>(chr));
            pread(1U, static_cast<Byte>(chr >> 8U));
        }
    }
    for (Byte gate : {0U, 0xC0U, 0x80U}) {
        store(0xA001U, gate);
        store(0x6000U, 2U);
        expect(0xFA00U, 47U);
        store(0x7FFFU, 1U);
        expect(0xFA00U, force ? 63U : 31U);
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
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    constexpr std::array<Byte, 13> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0xE6U, 0x11U, 0x68U, 0x40U};
    // Fixed banks move with the block latch. Replicate original firmware in
    // every 8 KiB page so instruction fetches remain stable across switches.
    for (std::size_t base = 0U; base < p.size(); base += 8192U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(base));
        std::ranges::copy(kIrq, p.begin() + static_cast<std::ptrdiff_t>(base + 0x1800U));
        for (std::size_t v = base + 0x1FFAU; v < base + 8192U; v += 2U) {
            p[v] = 0U;
            p[v + 1U] = v == base + 0x1FFCU ? 0xE0U : 0xF8U;
        }
    }
    std::vector<Byte> result(16U);
    result[0] = 'N';
    result[1] = 'E';
    result[2] = 'S';
    result[3] = 0x1AU;
    result[4] = 32U;
    result[5] = 64U;
    result[6] = static_cast<Byte>(0xD0U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0xC0U : 0xC8U;
    result.insert(result.end(), p.begin(), p.end());
    result.insert(result.end(), c.begin(), c.end());
    return result;
}
Cartridge cartridge(bool force = false, bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(force, legacy, vertical));
    REQUIRE(r);
    REQUIRE(r->configure_board_profile(force ? "jc-016-2-force-block3" : "jc-016-2"));
    return std::move(*r);
}
TEST_CASE("Mapper 205 write-only block reads preserve every CPU open-bus bit",
          "[mappers][mapper205]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (unsigned value = 0U; value < 256U; ++value)
        for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
            bus.write(0xA001U, 0U);
            bus.write(a, 3U);
            bus.write(0x10U, static_cast<Byte>(value));
            REQUIRE(bus.read(a) == value);
            if (a >= 0x6000U)
                REQUIRE(bus.read(0xFA00U) == 63U);
        }
}
TEST_CASE("Mapper 205 original CPU fixtures verify block pins, CIRAM, IRQs and atomic replay",
          "[mappers][mapper205][state]") {
    for (bool force : {false, true})
        for (bool legacy : {false, true})
            for (bool vertical : {false, true}) {
                Emulator e;
                REQUIRE(e.load_cartridge(cartridge(force, legacy, vertical), ""));
                e.reset();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x11U) >= 3U);
                REQUIRE(e.read_cpu(0xFA00U) == (force ? 63U : 31U));
                const auto early = e.save_state();
                e.step_frame();
                const auto future = e.save_state();
                REQUIRE(e.load_state(early));
                e.step_frame();
                REQUIRE(e.save_state() == future);
                StateWriter tail;
                Controller{}.save_state(tail);
                Controller{}.save_state(tail);
                const auto start = early.size() - tail.data().size() - 18U;
                for (std::size_t offset : {0UZ, 15UZ, 16UZ, 17UZ}) {
                    auto bad = early;
                    bad[start + offset] ^= 0xFFU;
                    const auto before = e.save_state();
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                }
                e.reset();
                REQUIRE(e.read_cpu(0xFA00U) == 31U);
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            }
}
} // namespace
