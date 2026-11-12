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
    h.mapper_number = 202U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}
std::vector<Byte> rom(std::size_t size, std::size_t page_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / page_size);
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(),
                              std::size_t p = 131072U,
                              std::size_t c = 65536U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, rom(p, 16384U), rom(c, 8192U));
    REQUIRE(r);
    return std::move(*r);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("Mapper 202 decodes every latch and ignores write data", "[mappers][mapper202]") {
    for (bool legacy : {false, true}) {
        auto m = board(header(legacy));
        for (unsigned latch = 0U; latch < 16U; ++latch) {
            const auto page = static_cast<Byte>(latch >> 1U);
            // Explicit compatibility table: consecutive, unaligned 16 KiB pages.
            constexpr std::array<Byte, 16> kUpper = {
                0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U, 4U, 5U, 5U, 6U, 6U, 7U, 7U, 0U};
            for (unsigned value = 0U; value < 256U; ++value) {
                m->write_prg(static_cast<Address>(0x8000U | latch), static_cast<Byte>(value));
                REQUIRE(m->read_prg(0x8000U) == page);
                REQUIRE(m->read_prg(0xBFFFU) == page);
                REQUIRE(m->read_prg(0xC000U) == kUpper[latch]);
                REQUIRE(m->read_prg(0xFFFFU) == kUpper[latch]);
                REQUIRE(m->read_chr(0U) == page);
                REQUIRE(m->read_chr(0x1FFFU) == page);
                REQUIRE(m->mirror_mode() ==
                        ((latch & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            }
        }
    }
}
TEST_CASE("Mapper 202 normalizes reset to bank zero and vertical CIRAM", "[mappers][mapper202]") {
    auto m = board();
    const auto initial = state(*m);
    REQUIRE(m->read_prg(0xC000U) == 0U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    m->write_prg(0xFFFFU, 0U);
    REQUIRE(m->read_prg(0x8000U) == 7U);
    REQUIRE(m->read_prg(0xC000U) == 0U);
    m->reset();
    REQUIRE(state(*m) == initial);
    REQUIRE(m->read_prg(0x8000U) == 0U);
    REQUIRE(m->read_prg(0xC000U) == 0U);
    REQUIRE(m->read_chr(0U) == 0U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("Mapper 202 ignores upper address pins and reads never relatch", "[mappers][mapper202]") {
    auto m = board();
    for (unsigned a = 0x8000U; a <= 0xFFFFU; ++a) {
        const auto page = static_cast<Byte>((a & 15U) >> 1U);
        m->write_prg(static_cast<Address>(a), static_cast<Byte>(a >> 5U));
        const auto saved = state(*m);
        REQUIRE(m->read_prg(0xBFFFU) == page);
        REQUIRE(m->read_chr(0x1FFFU) == page);
        REQUIRE(m->mirror_mode() ==
                ((a & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        m->read_prg(static_cast<Address>(a ^ 15U));
        REQUIRE(state(*m) == saved);
    }
}

TEST_CASE("Mapper 202 bounds the latch before wrapping smaller physical ROMs",
          "[mappers][mapper202]") {
    constexpr std::array<Byte, 16> kUpper = {
        0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U, 4U, 5U, 5U, 6U, 6U, 7U, 7U, 0U};
    for (bool legacy : {false, true})
        for (std::size_t p = 1U; p <= 8U; ++p)
            for (std::size_t c = 1U; c <= 8U; ++c) {
                auto m = board(header(legacy), p * 16384U, c * 8192U);
                for (unsigned latch = 0U; latch < 16U; ++latch) {
                    m->write_prg(static_cast<Address>(0xFFF0U | latch), 0U);
                    REQUIRE(m->read_prg(0x8123U) == (latch >> 1U) % p);
                    REQUIRE(m->read_prg(0xC123U) == kUpper[latch] % p);
                    m->write_chr(0x123U, 0xA7U);
                    REQUIRE(m->read_chr(0x123U) == (latch >> 1U) % c);
                }
            }
}

TEST_CASE("Mapper 202 exposes only ROM, with no low registers, RAM, IRQ or audio",
          "[mappers][mapper202]") {
    auto m = board();
    m->write_prg(0x800BU, 0U);
    const auto saved = state(*m);
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        const auto addr = static_cast<Address>(a);
        REQUIRE_FALSE(m->maps_prg(addr));
        REQUIRE_FALSE(m->maps_expansion(addr));
        m->write_prg(addr, 0xFFU);
        m->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*m) == saved);
    m->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->has_expansion_audio());
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        m->clock_cpu();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE(m->audio_output() == 0.0F);
}

TEST_CASE("Mapper 202 snapshots contain only a tagged canonical latch",
          "[mappers][mapper202][state]") {
    for (unsigned latch = 0U; latch < 16U; ++latch) {
        auto m = board();
        m->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        const auto saved = state(*m);
        REQUIRE(saved.size() == 3U);
        m->write_prg(static_cast<Address>(0xFFF0U | latch), 0xFFU);
        REQUIRE(state(*m) == saved);
        auto restored = board(header(true));
        StateReader reader(saved);
        restored->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*restored) == saved);
        REQUIRE(restored->read_prg(0x8000U) == m->read_prg(0x8000U));
        REQUIRE(restored->read_prg(0xFFFFU) == m->read_prg(0xFFFFU));
        REQUIRE(restored->read_chr(0x1FFFU) == m->read_chr(0x1FFFU));
        REQUIRE(restored->mirror_mode() == m->mirror_mode());
    }
    auto m = board();
    const auto initial = state(*m);
    for (unsigned kind = 0U; kind < 245U; ++kind) {
        auto bad = initial;
        if (kind < 3U)
            bad.resize(kind);
        else if (kind < 5U)
            bad[kind - 3U] ^= 0xFFU;
        else
            bad[2U] = static_cast<Byte>(kind + 11U);
        m->write_prg(0xFFFFU, 0U);
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == initial);
    }
}

TEST_CASE("Mapper 202 rejects unsupported memory and metadata", "[mappers][mapper202]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 16U; ++kind) {
        auto h = header();
        auto p = rom(131072U, 16384U);
        auto c = rom(65536U, 8192U);
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
            p.clear();
            break;
        case 9U:
            p.resize(147456U);
            break;
        case 10U:
            p.pop_back();
            break;
        case 11U:
            c.clear();
            break;
        case 12U:
            c.resize(73728U);
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

std::vector<Byte> image(bool legacy = false, bool vertical = true) {
    auto p = rom(131072U, 16384U);
    const auto c = rom(65536U, 8192U);
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
    store(0x12U, 0U);
    store(0x13U, 0U);
    expect(0x9A00U, 0U);
    expect(0xDA00U, 0U);
    for (unsigned latch = 0U; latch < 16U; ++latch) {
        constexpr std::array<Byte, 16> kUpper = {
            0U, 0U, 1U, 1U, 2U, 2U, 3U, 3U, 4U, 5U, 5U, 6U, 6U, 7U, 7U, 0U};
        store(static_cast<Address>(0xFFF0U | latch), static_cast<Byte>(latch ^ 0xA7U));
        expect(0x9A00U, static_cast<Byte>(latch >> 1U));
        expect(0xDA00U, kUpper[latch]);
        pread(0x123U, static_cast<Byte>(latch >> 1U));
        pwrite(0x123U, 0xA7U);
        pread(0x123U, static_cast<Byte>(latch >> 1U));
    }
    store(0x8000U, 0xFFU);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    pread(0x2800U, 0x19U);
    pread(0x2C00U, 0x27U);
    store(0x8001U, 0U);
    pread(0x2400U, 0x19U);
    pwrite(0x2800U, 0x35U);
    pread(0x2C00U, 0x35U);
    store(0xFFFDU, 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    // Identical firmware in every 16 KiB page keeps execution valid through
    // unaligned pairs and the final-page-to-zero transition.
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
    result[4] = 8U;
    result[5] = 8U;
    result[6] = static_cast<Byte>(0xA0U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0xC0U : 0xC8U;
    result.insert(result.end(), p.begin(), p.end());
    result.insert(result.end(), c.begin(), c.end());
    return result;
}
Cartridge cartridge(bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(legacy, vertical));
    REQUIRE(r);
    return std::move(*r);
}

TEST_CASE("Mapper 202 leaves unmapped CPU reads on the full open bus", "[mappers][mapper202]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (unsigned value = 0U; value < 256U; ++value)
        for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
            bus.write(0x10U, static_cast<Byte>(value));
            REQUIRE(bus.read(a) == value);
        }
}

TEST_CASE("Mapper 202 CPU fixtures exercise banks, CHR and CIRAM with atomic replay",
          "[mappers][mapper202][state]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            Emulator e;
            REQUIRE(e.load_cartridge(cartridge(legacy, vertical), ""));
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x9A00U) == 6U);
            REQUIRE(e.read_cpu(0xDA00U) == 7U);
            const auto early = e.save_state();
            e.step_frame();
            const auto future = e.save_state();
            REQUIRE(e.load_state(early));
            e.step_frame();
            REQUIRE(e.save_state() == future);
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            const auto start = early.size() - tail.data().size() - 3U;
            for (std::size_t offset : {0UZ, 1UZ, 2UZ}) {
                auto bad = early;
                bad[start + offset] ^= 0xFFU;
                const auto before = e.save_state();
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == before);
            }
            e.reset();
            REQUIRE(e.read_cpu(0xDA00U) == 0U);
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        }
}
} // namespace
