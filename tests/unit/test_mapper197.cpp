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
INesHeader header(Byte profile = 0U, bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 197U;
    h.is_nes2 = !legacy;
    h.submapper = profile;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
std::vector<Byte> prg_rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 8192U);
    return data;
}
std::vector<Byte> chr_rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>((i / 2048U) ^ ((i & 1024U) != 0U ? 0xA5U : 0U));
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h = header(),
                              std::size_t p = 0U,
                              std::size_t c = 0x80000U) {
    register_builtin_mappers();
    if (p == 0U)
        p = h.is_nes2 && h.submapper == 3U ? 0x40000U : 0x80000U;
    auto r = MapperRegistry::instance().create(h, prg_rom(p), chr_rom(c));
    REQUIRE(r);
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
// Published CHR window table. Entries 0..3 mean R0-even/odd and R1-even/odd;
// entries 4..7 mean R2..R5. Each row lists four physical 2 KiB PPU regions.
constexpr std::array<std::array<Byte, 4>, 8> kChoices = {{{0U, 1U, 4U, 5U},
                                                          {4U, 5U, 0U, 1U},
                                                          {2U, 3U, 6U, 7U},
                                                          {6U, 7U, 2U, 3U},
                                                          {0U, 3U, 4U, 7U},
                                                          {4U, 7U, 0U, 3U},
                                                          {0U, 1U, 4U, 5U},
                                                          {4U, 5U, 0U, 1U}}};
unsigned expected_bank(const std::array<Byte, 6>& regs,
                       Byte profile,
                       bool inverted,
                       unsigned region) {
    const unsigned choice = kChoices[profile * 2U + (inverted ? 1U : 0U)][region];
    return choice < 4U ? (regs[choice / 2U] & 0xFEU) | (choice & 1U) : regs[choice - 2U];
}
TEST_CASE("Mapper 197 doubles CHR output granularity while preserving PPU A10",
          "[mappers][mapper197]") {
    auto m = board();
    bank(*m, 0U, 0x81U);
    REQUIRE(m->read_chr(0U) == 0x80U);
    REQUIRE(m->read_chr(0x400U) == 0x25U);
    REQUIRE(m->read_chr(0x800U) == 0x81U);
}
TEST_CASE("Mapper 197 submapper three gates its PRG mux through A001", "[mappers][mapper197]") {
    auto m = board(header(3U), 0x40000U);
    m->write_prg(0xA001U, 0U);
    m->write_prg(0x6000U, 8U);
    REQUIRE(m->read_prg(0xE000U) == 31U);
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0x6000U, 8U);
    REQUIRE(m->read_prg(0xE000U) == 15U);
}

TEST_CASE("Mapper 197 all CHR values respect each wiring profile, inversion and physical ROM size",
          "[mappers][mapper197]") {
    for (Byte profile : {0U, 1U, 2U, 3U})
        for (std::size_t size : {16384UZ, 196608UZ, 0x80000UZ}) {
            auto m = board(header(profile), 0U, size);
            const auto rom = chr_rom(size);
            std::array<Byte, 6> regs = {0x81U, 0x35U, 0x93U, 0xC7U, 0x55U, 0x6BU};
            for (Byte reg = 0U; reg < 6U; ++reg)
                bank(*m, reg, regs[reg]);
            for (bool inverted : {false, true})
                for (Byte reg = 0U; reg < 6U; ++reg)
                    for (unsigned value = 0U; value < 256U; ++value) {
                        regs[reg] = static_cast<Byte>(value);
                        bank(*m, reg, regs[reg], inverted ? 0x80U : 0U);
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            const auto a = static_cast<Address>(slot * 1024U + 0x3FFU);
                            const auto selected = expected_bank(regs, profile, inverted, slot / 2U);
                            const auto expected = rom[(selected * 2048U + (a & 0x7FFU)) % size];
                            REQUIRE(m->read_chr(a) == expected);
                            m->write_chr(a, static_cast<Byte>(expected ^ 0xFFU));
                            REQUIRE(m->read_chr(a) == expected);
                        }
                    }
        }
}
TEST_CASE("Mapper 197 ordinary profiles retain six PRG bits, aliases and both MMC3 modes",
          "[mappers][mapper197]") {
    for (Byte profile : {0U, 1U, 2U})
        for (std::size_t size : {32768UZ, 98304UZ, 0x80000UZ}) {
            auto m = board(header(profile), size);
            const auto count = size / 8192U;
            for (Byte mode : {0U, 0x40U})
                for (unsigned value = 0U; value < 256U; ++value) {
                    m->write_prg(0x9FFEU, static_cast<Byte>(6U | mode));
                    m->write_prg(0x9FFFU, static_cast<Byte>(value));
                    bank(*m, 7U, static_cast<Byte>(value ^ 0x35U), mode);
                    const std::array<std::size_t, 4> expected = {
                        mode == 0U ? (value & 63U) % count : count - 2U,
                        ((value ^ 0x35U) & 63U) % count,
                        mode == 0U ? count - 2U : (value & 63U) % count,
                        count - 1U};
                    m->write_prg(0x6000U, 9U);
                    for (unsigned slot = 0U; slot < 4U; ++slot)
                        REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                                expected[slot]);
                }
        }
}
TEST_CASE("Mapper 197 submapper three applies A17 selection before wrapping every PRG window",
          "[mappers][mapper197]") {
    for (std::size_t size : {32768UZ, 98304UZ, 0x40000UZ}) {
        auto m = board(header(3U), size);
        const auto count = size / 8192U;
        for (Byte mode : {0U, 0x40U})
            for (Byte control : {0U, 1U, 8U, 9U}) {
                m->write_prg(0x6000U, control);
                for (unsigned value = 0U; value < 256U; ++value) {
                    bank(*m, 6U, static_cast<Byte>(value), mode);
                    bank(*m, 7U, static_cast<Byte>(value ^ 0x35U), mode);
                    const std::array<unsigned, 4> raw = {mode == 0U ? value & 31U : 30U,
                                                         (value ^ 0x35U) & 31U,
                                                         mode == 0U ? 30U : value & 31U,
                                                         31U};
                    for (unsigned slot = 0U; slot < 4U; ++slot) {
                        const auto selected = control < 8U    ? raw[slot]
                                              : control == 8U ? raw[slot] % 16U
                                                              : raw[slot] % 16U + 16U;
                        REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U +
                                                                 0x3FFU)) == selected % count);
                    }
                }
            }
    }
}
TEST_CASE("Mapper 197 all A001 gate and PRG control bytes preserve the documented pins",
          "[mappers][mapper197]") {
    auto m = board(header(3U));
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        m->write_prg(0xA001U, 0x80U);
        m->write_prg(0x6000U, 8U);
        m->write_prg(0xBFFFU, static_cast<Byte>(gate));
        m->write_prg(0x7FFFU, 9U);
        REQUIRE(m->read_prg(0xE000U) == ((gate & 0xC0U) == 0x80U ? 31U : 15U));
    }
    m->write_prg(0xA001U, 0x80U);
    for (unsigned value = 0U; value < 256U; ++value) {
        m->write_prg(0x6523U, static_cast<Byte>(value));
        REQUIRE(state(*m).back() == (value & 9U));
        REQUIRE(m->read_prg(0xE000U) == ((value & 9U) == 8U ? 15U : 31U));
    }
}
TEST_CASE("Mapper 197 low register aliases are write-only and restricted to submapper three",
          "[mappers][mapper197]") {
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto m = board(header(profile));
        for (unsigned a = 0x6000U; a < 0x8000U; ++a) {
            m->write_prg(static_cast<Address>(a), static_cast<Byte>(8U | (a & 1U)));
            REQUIRE(m->read_prg(0xE000U) == (profile == 3U ? ((a & 1U) != 0U ? 31U : 15U) : 63U));
            REQUIRE_FALSE(m->maps_prg(static_cast<Address>(a)));
        }
        const auto before = state(*m);
        for (Address a : {0x4020U, 0x4100U, 0x5000U, 0x5FFFU}) {
            m->write_prg(a, 0xFFU);
            m->write_expansion(a, 0xFFU);
            REQUIRE_FALSE(m->maps_expansion(a));
        }
        m->set_battery_ram(std::vector<Byte>(8192U, 0x73U));
        REQUIRE(m->battery_ram().empty());
        REQUIRE((state(*m) == before));
    }
}
TEST_CASE("Mapper 197 snapshots retain wiring, CHR inversion, PRG gates, and IRQ progress",
          "[mappers][mapper197][state]") {
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto m = board(header(profile));
        const std::array<Byte, 6> regs = {0x81U, 0x35U, 0x93U, 0xC7U, 0x55U, 0x6BU};
        for (Byte reg = 0U; reg < 6U; ++reg)
            bank(*m, reg, regs[reg], 0xC0U);
        bank(*m, 6U, 35U, 0xC0U);
        m->write_prg(0x6000U, 9U);
        m->write_prg(0xA001U, 0xC0U);
        m->write_prg(0xA000U, 1U);
        m->write_prg(0xC000U, 2U);
        m->write_prg(0xC001U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        const auto saved = state(*m);
        auto copy = board(header(profile));
        StateReader r(saved);
        copy->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(r.remaining() == 0U);
        REQUIRE((state(*copy) == saved));
        REQUIRE(copy->read_chr(0U) == expected_bank(regs, profile, true, 0U));
        REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(copy->read_prg(0xE000U) == (profile == 3U ? 31U : 63U));
        copy->write_prg(0x6000U, 8U);
        REQUIRE(copy->read_prg(0xE000U) == (profile == 3U ? 31U : 63U));
        copy->write_prg(0xA001U, 0x80U);
        copy->write_prg(0x6000U, 8U);
        REQUIRE(copy->read_prg(0xE000U) == (profile == 3U ? 15U : 63U));
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
        for (Byte other_profile : {0U, 1U, 2U, 3U}) {
            if (other_profile == profile)
                continue;
            auto other = board(header(other_profile));
            StateReader cross(saved);
            other->load_state(cross);
            REQUIRE_FALSE(cross.ok());
        }
        for (std::size_t length : {0UZ, 16399UZ, saved.size() - 1U}) {
            StateReader short_state(std::span(saved).first(length));
            copy->load_state(short_state);
            REQUIRE_FALSE(short_state.ok());
        }
        for (std::size_t offset : {16399UZ, 16400UZ, 16401UZ}) {
            auto bad = saved;
            bad[offset] ^= 0xFFU;
            StateReader malformed(bad);
            copy->load_state(malformed);
            REQUIRE_FALSE(malformed.ok());
        }
        copy->reset();
        REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
        REQUIRE_FALSE(copy->irq_pending());
        REQUIRE(copy->read_prg(0xE000U) == (profile == 3U ? 31U : 63U));
        REQUIRE(copy->read_chr(0U) == 0U);
    }
}
TEST_CASE("Mapper 197 snapshot mux bytes reject unused bits and non-mux profiles",
          "[mappers][mapper197][state]") {
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto m = board(header(profile));
        const auto saved = state(*m);
        for (unsigned value = 0U; value < 256U; ++value) {
            auto candidate = saved;
            candidate.back() = static_cast<Byte>(value);
            StateReader r(candidate);
            m->load_state(r);
            const bool valid = profile == 3U
                                   ? value == 0U || value == 1U || value == 8U || value == 9U
                                   : value == 0U;
            REQUIRE(r.ok() == valid);
            if (valid)
                REQUIRE((state(*m) == candidate));
        }
    }
}
TEST_CASE("Mapper 197 validates submapper ROM bounds and rejects unverified RAM populations",
          "[mappers][mapper197]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header();
        std::size_t p = 32768U, c = 8192U;
        switch (kind) {
        case 0U:
            h.submapper = 4U;
            break;
        case 1U:
            h.submapper = 15U;
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
        case 7U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 9U:
            p = 16384U;
            break;
        case 10U:
            p = 0x82000U;
            break;
        case 11U:
            p = 32769U;
            break;
        case 12U:
            c = 0U;
            break;
        case 13U:
            c = 4096U;
            break;
        case 14U:
            c = 0x80800U;
            break;
        case 15U:
            c = 0x2400U;
            break;
        case 16U:
            h.submapper = 3U;
            p = 0x42000U;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(MapperRegistry::instance().create(h, prg_rom(p), chr_rom(c)));
    }
    for (Byte profile : {0U, 1U, 2U, 3U})
        REQUIRE(board(header(profile), 98304U, 196608U));
    auto h = header(15U, true);
    h.prg_ram_size = 8192U;
    auto legacy = board(h);
    bank(*legacy, 0U, 0x81U);
    REQUIRE(legacy->read_chr(0U) == 0x80U);
    REQUIRE_FALSE(legacy->maps_prg(0x6000U));
}

std::vector<Byte> image(Byte profile, bool legacy = false) {
    auto p = prg_rom(profile == 3U ? 0x40000U : 0x80000U), c = chr_rom(0x80000U);
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
    pwrite(0U, 0xFFU);
    pread(0U, 0U);
    set_bank(6U, 35U);
    set_bank(7U, 37U);
    expect(0x9A00U, profile == 3U ? 3U : 35U);
    expect(0xBA00U, profile == 3U ? 5U : 37U);
    expect(0xDA00U, profile == 3U ? 30U : 62U);
    expect(0xFA00U, profile == 3U ? 31U : 63U);
    set_bank(6U, 35U, 0x40U);
    expect(0x9A00U, profile == 3U ? 30U : 62U);
    expect(0xDA00U, profile == 3U ? 3U : 35U);
    store(0xA001U, 0U);
    store(0x6000U, 8U);
    expect(0xFA00U, profile == 3U ? 31U : 63U);
    store(0xA001U, 0x80U);
    store(0x7FFFU, 8U);
    expect(0xFA00U, profile == 3U ? 15U : 63U);
    expect(0x9A00U, profile == 3U ? 14U : 62U);
    for (Byte gate : {0U, 0x40U, 0xC0U}) {
        store(0xA001U, gate);
        store(0x6523U, 9U);
        expect(0xFA00U, profile == 3U ? 15U : 63U);
    }
    store(0xA001U, 0x80U);
    store(0x6000U, 9U);
    expect(0xFA00U, profile == 3U ? 31U : 63U);
    expect(0xDA00U, profile == 3U ? 19U : 35U);
    store(0x6000U, 0U);
    expect(0xDA00U, profile == 3U ? 3U : 35U);
    const std::array<Byte, 6> regs = {0x81U, 0x35U, 0x93U, 0xC7U, 0x55U, 0x6BU};
    for (bool inverted : {false, true}) {
        for (Byte reg = 0U; reg < 6U; ++reg)
            set_bank(reg, regs[reg], inverted ? 0x80U : 0U);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto a = static_cast<Address>(slot * 1024U + 0x3FFU);
            const unsigned page = expected_bank(regs, profile, inverted, slot / 2U);
            const auto expected = c[page * 2048U + (a & 0x7FFU)];
            pread(a, expected);
            pwrite(a, static_cast<Byte>(expected ^ 0xFFU));
            pread(a, expected);
        }
    }
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
    for (std::size_t page = 0U; page < p.size(); page += 8192U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(page));
        std::ranges::copy(kIrq, p.begin() + static_cast<std::ptrdiff_t>(page + 0x1800U));
        for (std::size_t v = page + 0x1FFAU; v < page + 0x2000U; v += 2U) {
            p[v] = 0U;
            p[v + 1U] = v == page + 0x1FFCU ? 0xE0U : 0xF8U;
        }
    }
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = static_cast<Byte>(p.size() / 16384U);
    rom[5] = 64U;
    rom[6] = 0x51U;
    rom[7] = legacy ? 0xC0U : 0xC8U;
    rom[8] = legacy ? 0U : static_cast<Byte>(profile << 4U);
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(Byte profile, bool legacy = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(profile, legacy));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("Mapper 197 low reads preserve all CPU open-bus bits", "[mappers][mapper197]") {
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto c = cartridge(profile);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        for (Address a : {0x4020U, 0x4100U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU})
            for (Byte value : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 255U}) {
                bus.write(0x10U, value);
                REQUIRE(bus.read(a) == value);
            }
    }
}
TEST_CASE("Mapper 197 original CPU fixtures verify all profiles, gated mux, IRQs and state replay",
          "[mappers][mapper197][state]") {
    for (unsigned fixture = 0U; fixture < 5U; ++fixture) {
        const bool legacy = fixture == 0U;
        const auto profile = static_cast<Byte>(legacy ? 0U : fixture - 1U);
        INFO(fixture);
        Emulator e;
        REQUIRE(e.load_cartridge(cartridge(profile, legacy), ""));
        e.reset();
        e.step_frame();
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        REQUIRE(e.read_cpu(0x11U) >= 3U);
        const auto early = e.save_state();
        e.step_frame();
        const auto future = e.save_state();
        REQUIRE(e.load_state(early));
        e.step_frame();
        REQUIRE((e.save_state() == future));
        StateWriter tail;
        Controller{}.save_state(tail);
        Controller{}.save_state(tail);
        const auto start =
            early.size() - tail.data().size() - state(*board(header(profile))).size();
        for (std::size_t offset : {16399UZ, 16400UZ, 16401UZ}) {
            auto bad = early;
            const unsigned active_reg = profile == 1U ? 4U : 2U;
            bad[start + 16386U + active_reg] ^= 0xA5U;
            bad[start + offset] ^= 0xFFU;
            const auto before = e.save_state();
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE((e.save_state() == before));
        }
    }
}
} // namespace
