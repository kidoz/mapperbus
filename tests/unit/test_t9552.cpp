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
INesHeader header(bool canonical = false) {
    INesHeader h{};
    h.mapper_number = canonical ? 4U : 249U;
    h.submapper = canonical ? 5U : 0U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / unit);
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(),
                              std::size_t p = 0x80000U,
                              std::size_t c = 0x40000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, banks(p, 8192U), banks(c, 1024U));
    REQUIRE(r);
    return std::move(*r);
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(reg | mode));
    m.write_prg(0x8001U, value);
}
TEST_CASE("T9552 mapper 249 uses the documented legacy dump-order example", "[mappers][t9552]") {
    auto m = board();
    m->write_expansion(0x5000U, 2U);
    bank(*m, 6U, 2U);
    REQUIRE(m->read_prg(0x8000U) == 8U);
}
TEST_CASE("T9552 mapper 4.5 boots in pattern zero before selecting canonical order",
          "[mappers][t9552]") {
    auto m = board(header(true));
    bank(*m, 6U, 2U);
    REQUIRE(m->read_prg(0x8000U) == 16U);
    m->write_expansion(0x5000U, 2U);
    REQUIRE(m->read_prg(0x8000U) == 2U);
}
// Independent signal destinations, transposed from the measured pin tables:
// each entry gives the physical bank bit driven by one MMC3 bank bit.
unsigned physical_bank(unsigned value, unsigned pattern, bool chr) {
    constexpr std::array<std::array<unsigned, 4>, 4> kPrgDest = {
        {{4U, 3U, 1U, 2U}, {3U, 4U, 2U, 1U}, {1U, 2U, 3U, 4U}, {2U, 1U, 4U, 3U}}};
    constexpr std::array<std::array<unsigned, 6>, 8> kChrDest = {{{3U, 7U, 6U, 2U, 4U, 5U},
                                                                  {5U, 4U, 2U, 3U, 7U, 6U},
                                                                  {2U, 3U, 4U, 5U, 6U, 7U},
                                                                  {4U, 5U, 3U, 7U, 2U, 6U},
                                                                  {6U, 3U, 7U, 2U, 5U, 4U},
                                                                  {3U, 7U, 2U, 4U, 5U, 6U},
                                                                  {6U, 2U, 4U, 5U, 3U, 7U},
                                                                  {2U, 6U, 7U, 3U, 4U, 5U}}};
    unsigned out = value & (chr ? 3U : 0x21U);
    if (chr) {
        for (unsigned bit = 0U; bit < 6U; ++bit)
            out |= ((value >> (bit + 2U)) & 1U) << kChrDest[pattern][bit];
    } else {
        for (unsigned bit = 0U; bit < 4U; ++bit)
            out |= ((value >> (bit + 1U)) & 1U) << kPrgDest[pattern & 3U][bit];
    }
    return out;
}
unsigned file_bank(unsigned value, unsigned pattern, bool chr, bool canonical) {
    const auto physical = physical_bank(value, pattern, chr);
    if (canonical)
        return physical;
    // Invert pattern zero, the legacy file's address convention. Basis-bit
    // probes avoid reusing the production row-to-row address permutation.
    unsigned out = physical & (chr ? 3U : 0x21U);
    const unsigned first = chr ? 2U : 1U, last = chr ? 7U : 4U;
    for (unsigned bit = first; bit <= last; ++bit)
        if ((physical & physical_bank(1U << bit, 0U, chr)) != 0U)
            out |= 1U << bit;
    return out;
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("T9552 all PRG bytes preserve both modes and permute before small-ROM wrapping",
          "[mappers][t9552]") {
    for (bool canonical : {false, true})
        for (std::size_t size : {0x80000UZ, 0x20000UZ, 0x18000UZ}) {
            auto m = board(header(canonical), size);
            for (unsigned pattern = 0U; pattern < 8U; ++pattern) {
                m->write_expansion(0x5000U, static_cast<Byte>(pattern));
                for (Byte swap : {0U, 0x40U})
                    for (unsigned value = 0U; value < 256U; ++value) {
                        bank(*m, 6U, static_cast<Byte>(value), swap);
                        bank(*m, 7U, static_cast<Byte>(value ^ 0x15U), swap);
                        const std::array<unsigned, 4> pages =
                            swap == 0U ? std::array<unsigned, 4>{value & 63U,
                                                                 (value ^ 0x15U) & 63U,
                                                                 62U,
                                                                 63U}
                                       : std::array<unsigned, 4>{
                                             62U, (value ^ 0x15U) & 63U, value & 63U, 63U};
                        for (unsigned slot = 0U; slot < 4U; ++slot)
                            REQUIRE(
                                m->read_prg(static_cast<Address>(0x8000U + slot * 8192U + 17U)) ==
                                file_bank(pages[slot], pattern, false, canonical) % (size / 8192U));
                    }
            }
        }
}
TEST_CASE("T9552 all CHR bytes pair and invert before all eight physical permutations",
          "[mappers][t9552]") {
    for (bool canonical : {false, true}) {
        auto m = board(header(canonical));
        for (unsigned pattern = 0U; pattern < 8U; ++pattern) {
            m->write_expansion(0x5000U, static_cast<Byte>(pattern));
            for (Byte inversion : {0U, 0x80U})
                for (unsigned value = 0U; value < 256U; ++value) {
                    bank(*m, 0U, static_cast<Byte>(value), inversion);
                    bank(*m, 2U, static_cast<Byte>(value), inversion);
                    const Address pair = inversion == 0U ? 0U : 0x1000U;
                    REQUIRE(m->read_chr(pair) ==
                            file_bank(value & 0xFEU, pattern, true, canonical));
                    REQUIRE(m->read_chr(static_cast<Address>(pair + 1024U)) ==
                            file_bank(value | 1U, pattern, true, canonical));
                    const Address single = static_cast<Address>(pair ^ 0x1000U);
                    REQUIRE(m->read_chr(single) == file_bank(value, pattern, true, canonical));
                    m->write_chr(single, 0U);
                    REQUIRE(m->read_chr(single) == file_bank(value, pattern, true, canonical));
                }
        }
        auto small = board(header(canonical), 0x20000U, 0x6000U);
        bank(*small, 5U, 0xFCU);
        small->write_expansion(0x5000U, 7U);
        REQUIRE(small->read_chr(0x1C00U) == file_bank(0xFCU, 7U, true, canonical) % 24U);
    }
}
TEST_CASE("T9552 control aliases mask unused data bits and bypass WRAM gating",
          "[mappers][t9552]") {
    for (bool canonical : {false, true}) {
        auto m = board(header(canonical));
        bank(*m, 6U, 2U);
        bank(*m, 2U, 0xA4U);
        for (Byte gate : {0U, 0x80U, 0xC0U}) {
            m->write_prg(0xA001U, gate);
            for (unsigned a = 0x5000U; a <= 0x5FFFU; ++a) {
                const auto value = static_cast<Byte>(a);
                m->write_expansion(static_cast<Address>(a), value);
                REQUIRE(m->read_prg(0x8000U) == file_bank(2U, value & 7U, false, canonical));
                REQUIRE(m->read_chr(0x1000U) == file_bank(0xA4U, value & 7U, true, canonical));
            }
        }
        m->write_expansion(0x5000U, 2U);
        for (Address a : {0x4FFFU, 0x6000U, 0x6001U, 0x7FFFU}) {
            REQUIRE_FALSE(m->maps_expansion(a));
            m->write_expansion(a, 0U);
            m->write_prg(a, 0U);
            REQUIRE(m->read_prg(0x8000U) == file_bank(2U, 2U, false, canonical));
        }
        REQUIRE_FALSE(m->maps_prg(0x6000U));
        REQUIRE(m->battery_ram().empty());
        m->write_prg(0xA000U, 1U);
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        m->reset();
        bank(*m, 6U, 2U);
        REQUIRE(m->read_prg(0x8000U) == file_bank(2U, 0U, false, canonical));
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    }
}
TEST_CASE("T9552 snapshots restore wiring, live registers, WRAM and an in-flight IRQ",
          "[mappers][t9552]") {
    for (bool canonical : {false, true}) {
        auto h = header(canonical);
        h.has_battery = true;
        h.prg_nvram_size = 8192U;
        auto m = board(h);
        bank(*m, 6U, 0xE2U, 0x40U);
        bank(*m, 5U, 0xE5U, 0xC0U);
        m->write_expansion(0x5000U, 7U);
        m->write_prg(0x6000U, 0xA7U);
        m->write_prg(0xC000U, 2U);
        m->write_prg(0xC001U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        const auto saved = state(*m);
        auto copy = board(h);
        StateReader r(saved);
        copy->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(r.remaining() == 0U);
        REQUIRE((state(*copy) == saved));
        REQUIRE(copy->read_prg(0xC000U) == file_bank(0x22U, 7U, false, canonical));
        REQUIRE(copy->read_chr(0xC00U) == file_bank(0xE5U, 7U, true, canonical));
        REQUIRE(copy->read_prg(0x6000U) == 0xA7U);
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
        auto other = board(header(!canonical));
        StateReader cross_order(saved);
        other->load_state(cross_order);
        REQUIRE_FALSE(cross_order.ok());
        for (std::size_t offset :
             {16384UZ, 16385UZ, 16392UZ, 16394UZ, saved.size() - 2U, saved.size() - 1U}) {
            auto bad = saved;
            bad[offset] = 0xFFU;
            StateReader invalid(bad);
            copy->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
        }
        StateReader truncated(std::span{saved}.first(saved.size() - 1U));
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
    }
}
TEST_CASE("T9552 optional WRAM obeys gates and persists only its declared NVRAM",
          "[mappers][t9552]") {
    for (bool canonical : {false, true})
        for (bool persistent : {false, true}) {
            auto h = header(canonical);
            h.has_battery = persistent;
            h.prg_ram_size = persistent ? 0U : 8192U;
            h.prg_nvram_size = persistent ? 8192U : 0U;
            auto m = board(h);
            m->write_prg(0x6000U, 0x71U);
            m->write_prg(0xA001U, 0xC0U);
            m->write_prg(0x6000U, 0x29U);
            REQUIRE(m->read_prg(0x6000U) == 0x71U);
            m->write_prg(0xA001U, 0U);
            REQUIRE_FALSE(m->maps_prg(0x6000U));
            m->write_prg(0xA001U, 0x80U);
            REQUIRE(m->read_prg(0x6000U) == 0x71U);
            REQUIRE(m->battery_ram().size() == (persistent ? 8192U : 0U));
            m->set_battery_ram(std::vector<Byte>(8192U, 0xB9U));
            REQUIRE(m->read_prg(0x6000U) == (persistent ? 0xB9U : 0x71U));
        }
    for (bool persistent : {false, true}) {
        auto h = header();
        h.is_nes2 = false;
        h.has_battery = persistent;
        h.prg_ram_size = 8192U; // Legacy default must not create a chip without a battery.
        auto m = board(h);
        REQUIRE(m->maps_prg(0x6000U) == persistent);
        REQUIRE(m->battery_ram().size() == (persistent ? 8192U : 0U));
    }
}
TEST_CASE("T9552 headers reject unsupported geometry and contradictory memory metadata",
          "[mappers][t9552]") {
    register_builtin_mappers();
    for (bool canonical : {false, true})
        for (unsigned kind = 0U; kind < 14U; ++kind) {
            auto h = header(canonical);
            std::size_t p = 0x80000U, c = 0x40000U;
            switch (kind) {
            case 0U:
                h.chr_ram_size = 8192U;
                break;
            case 1U:
                h.chr_nvram_size = 8192U;
                break;
            case 2U:
                h.has_battery = true;
                break;
            case 3U:
                h.prg_nvram_size = 8192U;
                break;
            case 4U:
                h.prg_ram_size = 4096U;
                break;
            case 5U:
                h.prg_ram_size = 8192U;
                h.prg_nvram_size = 8192U;
                h.has_battery = true;
                break;
            case 6U:
                h.prg_ram_size = 16384U;
                break;
            case 7U:
                p = 16384U;
                break;
            case 8U:
                p = 0x82000U;
                break;
            case 9U:
                p = 32769U;
                break;
            case 10U:
                c = 0U;
                break;
            case 11U:
                c = 0x40400U;
                break;
            case 12U:
                c = 8193U;
                break;
            default:
                h.mirror_mode = MirrorMode::FourScreen;
                break;
            }
            REQUIRE_FALSE(
                MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
        }
    auto h = header();
    h.submapper = 1U;
    REQUIRE_FALSE(
        MapperRegistry::instance().create(h, banks(0x40000U, 8192U), banks(0x40000U, 1024U)));
}
std::vector<Byte> image(bool canonical, bool legacy = false, bool persistent = false) {
    auto p = banks(0x80000U, 8192U);
    auto c = banks(0x40000U, 1024U);
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
    const auto pwrite = [&address, &store](Address a, Byte v) {
        address(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &address, &expect](Address a, Byte v) {
        address(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    set_bank(6U, 2U);
    expect(0x9A00U, 16U); // Power-on pattern 0 in both file formats.
    set_bank(7U, 5U);
    set_bank(0U, 0x83U);
    set_bank(2U, 0xE5U);
    store(0xA001U, 0U);
    for (unsigned pattern = 0U; pattern < 8U; ++pattern) {
        store(static_cast<Address>(0x5000U + pattern * 0x101U), static_cast<Byte>(0xF8U | pattern));
        expect(0x9A00U, static_cast<Byte>(physical_bank(2U, pattern, false)));
        expect(0xBA00U, static_cast<Byte>(physical_bank(5U, pattern, false)));
        expect(0xDA00U, 62U);
        expect(0xFA00U, 63U);
        pread(0U, static_cast<Byte>(physical_bank(0x82U, pattern, true)));
        pread(0x400U, static_cast<Byte>(physical_bank(0x83U, pattern, true)));
        pread(0x1000U, static_cast<Byte>(physical_bank(0xE5U, pattern, true)));
    }
    store(0x5FFFU, 2U);
    set_bank(6U, 0xC7U, 0xC0U);
    expect(0x9A00U, 62U);
    expect(0xDA00U, 7U);
    pread(0U, 0xE5U);
    pread(0x1000U, 0x82U);
    pwrite(0U, 0U);
    pread(0U, 0xE5U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0xA000U, 1U);
    pread(0x2400U, 0x19U);
    store(0xA000U, 0U);
    pread(0x2400U, 0x27U);
    if (persistent) {
        store(0xA001U, 0x80U);
        store(0x6000U, 0xA7U);
        store(0xA001U, 0xC0U);
        store(0x6000U, 0U);
        expect(0x6000U, 0xA7U);
    }
    store(0xC000U, 0U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 2U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    constexpr std::array<Byte, 13> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0xE6U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(code, p.begin() + 0x7E000U);
    std::ranges::copy(kIrq, p.begin() + 0x7F800U);
    for (std::size_t v = 0x7FFFAU; v < 0x80000U; v += 2U) {
        p[v] = 0U;
        p[v + 1U] = v == 0x7FFFCU ? 0xE0U : 0xF8U;
    }
    // Construct equivalent physical cartridges. Legacy files store the same
    // physical data in the order observed with the T9552 latch at zero.
    if (!canonical) {
        const auto physical_prg = p, physical_chr = c;
        for (unsigned b = 0U; b < 64U; ++b)
            std::copy_n(physical_prg.begin() +
                            static_cast<std::ptrdiff_t>(physical_bank(b, 0U, false) * 8192U),
                        8192U,
                        p.begin() + b * 8192U);
        for (unsigned b = 0U; b < 256U; ++b)
            std::copy_n(physical_chr.begin() +
                            static_cast<std::ptrdiff_t>(physical_bank(b, 0U, true) * 1024U),
                        1024U,
                        c.begin() + b * 1024U);
    }
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 32U;
    rom[5] = 32U;
    rom[6] = static_cast<Byte>((canonical ? 0x40U : 0x90U) | 1U | (persistent ? 2U : 0U));
    rom[7] = canonical ? 8U : legacy ? 0xF0U : 0xF8U;
    rom[8] = canonical ? 0x50U : 0U;
    rom[10] = persistent && !legacy ? 0x70U : 0U;
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(bool canonical, bool legacy = false, bool persistent = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(canonical, legacy, persistent));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("T9552 controls are write-only and unpopulated WRAM leaves all bus bits floating",
          "[mappers][t9552]") {
    for (bool canonical : {false, true}) {
        auto c = cartridge(canonical);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        for (Address a : {0x4FFFU, 0x5000U, 0x5800U, 0x5FFFU, 0x6000U, 0x7FFFU})
            for (Byte value : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 255U}) {
                bus.write(0x10U, value);
                REQUIRE(bus.read(a) == value);
            }
    }
}
TEST_CASE("T9552 original CPU program checks every pattern and both file orders before replay",
          "[mappers][t9552]") {
    for (unsigned profile = 0U; profile < 5U; ++profile) {
        Emulator e;
        REQUIRE(e.load_cartridge(
            cartridge(profile == 0U || profile == 3U, profile == 2U, profile >= 3U), ""));
        e.reset();
        e.step_frame();
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        REQUIRE(e.read_cpu(0x11U) >= 2U);
        if (profile >= 3U)
            REQUIRE(e.read_cpu(0x6000U) == 0xA7U);
        const auto early = e.save_state();
        e.step_frame();
        const auto future = e.save_state();
        REQUIRE(e.load_state(early));
        e.step_frame();
        REQUIRE((e.save_state() == future));
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        auto bad = early;
        bad[bad.size() - tail.data().size() - 2U] = 8U;
        const auto before = e.save_state();
        REQUIRE_FALSE(e.load_state(bad));
        REQUIRE((e.save_state() == before));
    }
}
TEST_CASE("T9552 mapper 4.5 rejects the historical ordinary MMC3 payload layout",
          "[mappers][t9552]") {
    auto h = header(true);
    h.submapper = 0U;
    auto ordinary = board(h), t9552 = board(header(true));
    const auto ordinary_state = state(*ordinary), t9552_state = state(*t9552);
    StateReader old_payload(ordinary_state);
    t9552->load_state(old_payload);
    REQUIRE_FALSE(old_payload.ok());
    StateReader new_payload(t9552_state);
    ordinary->load_state(new_payload);
    REQUIRE_FALSE(new_payload.ok());
}
} // namespace
