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
    h.mapper_number = 196U;
    h.is_nes2 = !legacy;
    h.submapper = profile;
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
Address port(Byte profile, Address canonical) {
    return static_cast<Address>((canonical & 0xE000U) |
                                ((canonical & 1U) != 0U ? (profile == 2U ? 4U : 2U) : 0U));
}
void write(Mapper& m, Byte profile, Address canonical, Byte value) {
    m.write_prg(port(profile, canonical), value);
}
void bank(Mapper& m, Byte profile, Byte reg, Byte value, Byte mode = 0U) {
    write(m, profile, 0x8000U, static_cast<Byte>(reg | mode));
    write(m, profile, 0x8001U, value);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("MRCM submapper one uses CPU A1 for the MMC3 register select", "[mappers][mrcm196]") {
    auto m = board(header(1U));
    m->write_prg(0x8001U, 6U); // A0 is ignored; this selects R6.
    m->write_prg(0x8002U, 5U);
    REQUIRE(m->read_prg(0x8000U) == 5U);
    REQUIRE(m->read_prg(0xE000U) == 63U);
    bank(*m, 1U, 6U, 7U);
    REQUIRE(m->read_prg(0x8000U) == 7U);
}
TEST_CASE("MRCM submapper three starts in 32 KiB mode and folds both data nibbles",
          "[mappers][mrcm196]") {
    auto m = board(header(3U));
    REQUIRE(m->read_prg(0xE000U) == 3U);
    m->write_prg(0x6000U, 0x23U);
    REQUIRE(m->read_prg(0x8000U) == 12U);
    REQUIRE(m->read_prg(0xE000U) == 15U);
}

TEST_CASE("MRCM all register data bytes preserve ordinary MMC3 behavior", "[mappers][mrcm196]") {
    constexpr std::array<Address, 8> kPorts = {
        0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U};
    auto h = header();
    h.mapper_number = 119U; // Existing compact MMC3 state, without extra board latches.
    auto ref = board(h, 0x80000U, 0x20000U);
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto m = board(header(profile));
        for (const auto a : kPorts)
            for (unsigned value = 0U; value < 256U; ++value) {
                m->reset();
                ref->reset();
                bank(*m, profile, 2U, 9U);
                ref->write_prg(0x8000U, 2U);
                ref->write_prg(0x8001U, 9U);
                write(*m, profile, a, static_cast<Byte>(value));
                ref->write_prg(a, static_cast<Byte>(value));
                auto expected = state(*ref);
                StateWriter tail;
                tail.write<uint16_t>(static_cast<uint16_t>(0xC400U | profile));
                tail.write<Byte>(profile == 3U ? 0x80U : 0U);
                expected.insert(expected.end(), tail.data().begin(), tail.data().end());
                REQUIRE((state(*m) == expected));
            }
    }
}
TEST_CASE("MRCM low-address truth tables and every ignored address pin select the right port",
          "[mappers][mrcm196]") {
    constexpr std::array<std::array<bool, 16>, 4> kData = {{{false,
                                                             false,
                                                             true,
                                                             false,
                                                             true,
                                                             false,
                                                             true,
                                                             false,
                                                             true,
                                                             false,
                                                             true,
                                                             false,
                                                             true,
                                                             false,
                                                             true,
                                                             false},
                                                            {false,
                                                             false,
                                                             true,
                                                             true,
                                                             false,
                                                             false,
                                                             true,
                                                             true,
                                                             false,
                                                             false,
                                                             true,
                                                             true,
                                                             false,
                                                             false,
                                                             true,
                                                             true},
                                                            {false,
                                                             false,
                                                             false,
                                                             false,
                                                             true,
                                                             true,
                                                             true,
                                                             true,
                                                             false,
                                                             false,
                                                             false,
                                                             false,
                                                             true,
                                                             true,
                                                             true,
                                                             true},
                                                            {false,
                                                             false,
                                                             true,
                                                             true,
                                                             false,
                                                             false,
                                                             true,
                                                             true,
                                                             false,
                                                             false,
                                                             true,
                                                             true,
                                                             false,
                                                             false,
                                                             true,
                                                             true}}};
    constexpr std::array<Address, 11> kAliases = {
        0U, 0x10U, 0x20U, 0x40U, 0x80U, 0x100U, 0x200U, 0x400U, 0x800U, 0x1000U, 0x1FF0U};
    auto h = header();
    h.mapper_number = 119U;
    auto ref = board(h, 0x80000U, 0x20000U);
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto m = board(header(profile));
        for (Address high : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
            for (Address alias : kAliases)
                for (unsigned low = 0U; low < 16U; ++low) {
                    m->reset();
                    ref->reset();
                    bank(*m, profile, 6U, 5U);
                    ref->write_prg(0x8000U, 6U);
                    ref->write_prg(0x8001U, 5U);
                    m->write_prg(static_cast<Address>(high | alias | low), 0xC7U);
                    ref->write_prg(static_cast<Address>(high | (kData[profile][low] ? 1U : 0U)),
                                   0xC7U);
                    const auto actual = state(*m), expected = state(*ref);
                    REQUIRE(actual.size() == expected.size() + 3U);
                    REQUIRE(std::equal(expected.begin(), expected.end(), actual.begin()));
                }
    }
}
TEST_CASE("MRCM all override data values fold nibbles and bypass both MMC3 PRG modes",
          "[mappers][mrcm196]") {
    for (std::size_t size : {32768UZ, 98304UZ, 0x80000UZ})
        for (Byte profile : {0U, 1U, 2U, 3U}) {
            auto m = board(header(profile), size);
            const auto count = size / 8192U;
            for (Byte mode : {0U, 0x40U}) {
                m->reset();
                bank(*m, profile, 6U, 35U, mode);
                bank(*m, profile, 7U, 37U, mode);
                REQUIRE(m->read_prg(0xE000U) == (profile == 3U ? 3U : count - 1U));
                for (unsigned data = 0U; data < 256U; ++data) {
                    m->write_prg(0x6523U, static_cast<Byte>(data));
                    const auto first = ((data / 16U) | (data % 16U)) * 4U;
                    const std::array<std::size_t, 4> normal = {
                        mode == 0U ? 35U % count : count - 2U,
                        37U % count,
                        mode == 0U ? count - 2U : 35U % count,
                        count - 1U};
                    for (unsigned slot = 0U; slot < 4U; ++slot)
                        REQUIRE(
                            m->read_prg(static_cast<Address>(0x8000U + slot * 8192U + 0x321U)) ==
                            (profile == 0U || profile == 3U ? (first + slot) % count
                                                            : normal[slot]));
                }
            }
        }
}
TEST_CASE("MRCM low write decoder is limited to 6000-7FFF and independent of A001",
          "[mappers][mrcm196]") {
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto m = board(header(profile));
        for (Byte gate : {0U, 0x80U, 0xC0U}) {
            write(*m, profile, 0xA001U, gate);
            for (unsigned a = 0x6000U; a < 0x8000U; ++a) {
                const auto value = static_cast<Byte>(a ^ (a >> 5U));
                m->write_prg(static_cast<Address>(a), value);
                const unsigned first = ((value / 16U) | (value % 16U)) * 4U;
                REQUIRE(m->read_prg(0xE000U) ==
                        (profile == 0U || profile == 3U ? first + 3U : 63U));
                REQUIRE_FALSE(m->maps_prg(static_cast<Address>(a)));
            }
        }
        m->reset();
        for (Address a : {0x4020U, 0x4100U, 0x5000U, 0x5FFFU}) {
            m->write_prg(a, 0x77U);
            m->write_expansion(a, 0x77U);
            REQUIRE_FALSE(m->maps_expansion(a));
            REQUIRE(m->read_prg(0xE000U) == (profile == 3U ? 3U : 63U));
        }
        REQUIRE(m->battery_ram().empty());
        m->set_battery_ram(std::vector<Byte>(8192U, 0x73U));
        REQUIRE(m->battery_ram().empty());
    }
}
TEST_CASE("MRCM all paired and inverted CHR values wrap ROM and ignore PPU writes",
          "[mappers][mrcm196]") {
    constexpr std::array<Address, 6> kSlots = {0U, 0x800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto m = board(header(profile), 98304U, 16384U);
        for (Byte mode : {0U, 0x80U})
            for (Byte reg = 0U; reg < 6U; ++reg)
                for (unsigned value = 0U; value < 256U; ++value) {
                    bank(*m, profile, reg, static_cast<Byte>(value), mode);
                    const auto base = kSlots[reg] ^ (mode == 0U ? 0U : 0x1000U);
                    for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                        const auto page = reg < 2U ? (value & 0xFEU) + half : value;
                        const auto a = static_cast<Address>(base + half * 1024U + 0x3FFU);
                        REQUIRE(m->read_chr(a) == page % 16U);
                        m->write_chr(a, static_cast<Byte>(value ^ 0xA7U));
                        REQUIRE(m->read_chr(a) == page % 16U);
                    }
                }
    }
}
TEST_CASE("MRCM snapshots retain override state, hidden MMC3 registers, IRQs and wiring",
          "[mappers][mrcm196][state]") {
    for (Byte profile : {0U, 1U, 2U, 3U})
        for (bool override_write : {false, true}) {
            auto m = board(header(profile));
            if (override_write)
                m->write_prg(0x7FFFU, 0x23U);
            bank(*m, profile, 6U, 35U, 0xC0U);
            bank(*m, profile, 2U, 19U, 0xC0U);
            write(*m, profile, 0xA000U, 1U);
            write(*m, profile, 0xC000U, 2U);
            write(*m, profile, 0xC001U, 0U);
            write(*m, profile, 0xE001U, 0U);
            m->clock_irq_counter();
            const auto saved = state(*m);
            auto copy = board(header(profile));
            StateReader r(saved);
            copy->load_state(r);
            REQUIRE(r.ok());
            REQUIRE(r.remaining() == 0U);
            REQUIRE((state(*copy) == saved));
            REQUIRE(copy->read_chr(0U) == 19U);
            REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
            const bool forced = profile == 3U || (profile == 0U && override_write);
            REQUIRE(copy->read_prg(0xC000U) == (forced ? (override_write ? 14U : 2U) : 35U));
            copy->clock_irq_counter();
            REQUIRE_FALSE(copy->irq_pending());
            copy->clock_irq_counter();
            REQUIRE(copy->irq_pending());
            write(*copy, profile, 0xE000U, 0U);
            REQUIRE_FALSE(copy->irq_pending());
            write(*copy, profile, 0xC000U, 0U);
            write(*copy, profile, 0xE001U, 0U);
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
            REQUIRE(copy->read_prg(0xE000U) == (profile == 3U ? 3U : 63U));
        }
}
TEST_CASE("MRCM snapshot override encodings reject impossible profile states",
          "[mappers][mrcm196][state]") {
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto m = board(header(profile));
        const auto saved = state(*m);
        for (unsigned value = 0U; value < 256U; ++value) {
            auto candidate = saved;
            candidate.back() = static_cast<Byte>(value);
            StateReader r(candidate);
            m->load_state(r);
            const bool override_bank = value >= 0x80U && value <= 0x8FU;
            const bool valid = profile == 0U   ? value == 0U || override_bank
                               : profile == 3U ? override_bank
                                               : value == 0U;
            REQUIRE(r.ok() == valid);
            if (valid)
                REQUIRE((state(*m) == candidate));
        }
    }
}
TEST_CASE("MRCM factory validates submappers, ROM bounds and unsupported RAM populations",
          "[mappers][mrcm196]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 16U; ++kind) {
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
            c = 0x40400U;
            break;
        case 15U:
            c = 8193U;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(MapperRegistry::instance().create(h, banks(p, 8192U), banks(c, 1024U)));
    }
    for (Byte profile : {0U, 1U, 2U, 3U})
        for (std::size_t p : {32768UZ, 98304UZ, 0x80000UZ})
            for (std::size_t c : {8192UZ, 196608UZ, 0x40000UZ})
                REQUIRE(board(header(profile), p, c));
    auto h = header(15U, true);
    h.prg_ram_size = 8192U; // Legacy iNES default does not populate a chip.
    auto m = board(h);
    REQUIRE_FALSE(m->maps_prg(0x6000U));
    m->write_prg(0x8001U, 6U); // Legacy follows profile zero, regardless of ignored submapper.
    m->write_prg(0x8008U, 5U);
    REQUIRE(m->read_prg(0x8000U) == 5U);
}

std::vector<Byte> image(Byte profile, bool legacy = false) {
    auto p = banks(0x80000U, 8192U), c = banks(0x40000U, 1024U);
    std::fill_n(c.begin(), 1024U, 0x5DU);
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
    const auto control = [&store, profile](Address canonical, Byte value) {
        store(port(profile, canonical), value);
    };
    const auto set_bank = [&control](Byte reg, Byte value, Byte mode = 0U) {
        control(0x8000U, static_cast<Byte>(reg | mode));
        control(0x8001U, value);
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
    pwrite(0U, 0U);
    pread(0U, 0x5DU);
    set_bank(6U, 35U);
    set_bank(7U, 37U);
    expect(0x9A00U, profile == 3U ? 0U : 35U);
    expect(0xBA00U, profile == 3U ? 1U : 37U);
    expect(0xDA00U, profile == 3U ? 2U : 62U);
    expect(0xFA00U, profile == 3U ? 3U : 63U);
    set_bank(6U, 35U, 0x40U);
    expect(0x9A00U, profile == 3U ? 0U : 62U);
    expect(0xDA00U, profile == 3U ? 2U : 35U);
    for (Byte gate : {0U, 0xC0U, 0x80U}) {
        control(0xA001U, gate);
        for (Byte data : {0x23U, 0x80U, 0x11U, 0xFFU}) {
            store(0x7FFFU, data);
            const unsigned first = ((data / 16U) | (data % 16U)) * 4U;
            expect(0x9A00U, static_cast<Byte>(profile == 0U || profile == 3U ? first : 62U));
            expect(0xBA00U, static_cast<Byte>(profile == 0U || profile == 3U ? first + 1U : 37U));
            expect(0xDA00U, static_cast<Byte>(profile == 0U || profile == 3U ? first + 2U : 35U));
            expect(0xFA00U, static_cast<Byte>(profile == 0U || profile == 3U ? first + 3U : 63U));
        }
    }
    // Registers still receive writes behind the override, including CHR inversion.
    set_bank(6U, 41U, 0xC0U);
    set_bank(0U, 9U, 0x80U);
    pread(0x1000U, 8U);
    pread(0x1400U, 9U);
    set_bank(1U, 15U, 0x80U);
    pread(0x1800U, 14U);
    pread(0x1C00U, 15U);
    set_bank(2U, 19U, 0x80U);
    pread(0U, 19U);
    pwrite(0U, 0U);
    pread(0U, 19U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    control(0xA000U, 1U);
    pread(0x2400U, 0x19U);
    control(0xA000U, 0U);
    pread(0x2400U, 0x27U);
    control(0xC000U, 2U);
    control(0xC001U, 0U);
    control(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 1U, 0x90U, 0xFAU});
    control(0xE000U, 0U);
    control(0xC000U, 0U);
    control(0xE001U, 0U);
    code.insert(code.end(), {0xA5U, 0x11U, 0xC9U, 3U, 0x90U, 0xFAU});
    control(0xE000U, 0U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    const std::array<Byte, 13> irq = {0x48U,
                                      0xA9U,
                                      0U,
                                      0x8DU,
                                      0U,
                                      0xE0U,
                                      0x8DU,
                                      static_cast<Byte>(port(profile, 0xE001U)),
                                      0xE0U,
                                      0xE6U,
                                      0x11U,
                                      0x68U,
                                      0x40U};
    for (std::size_t group = 0U; group < p.size(); group += 32768U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(group + 0x6000U));
        std::ranges::copy(irq, p.begin() + static_cast<std::ptrdiff_t>(group + 0x7800U));
        for (std::size_t v = group + 0x7FFAU; v < group + 0x8000U; v += 2U) {
            p[v] = 0U;
            p[v + 1U] = v == group + 0x7FFCU ? 0xE0U : 0xF8U;
        }
    }
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 32U;
    rom[5] = 32U;
    rom[6] = 0x41U;
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
TEST_CASE("MRCM low reads preserve CPU open bus without enabling the override",
          "[mappers][mrcm196]") {
    for (Byte profile : {0U, 1U, 2U, 3U}) {
        auto c = cartridge(profile);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        for (Address a : {0x4020U, 0x4100U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU})
            for (Byte value : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 255U}) {
                bus.write(0x10U, value);
                REQUIRE(bus.read(a) == value);
                REQUIRE(bus.read(0xFA00U) == (profile == 3U ? 3U : 63U));
            }
    }
}
TEST_CASE("MRCM original CPU fixtures verify all profiles, IRQs, replay and atomic rollback",
          "[mappers][mrcm196][state]") {
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
            bad[start + 16388U] ^= 0xA5U; // Corrupt the live CHR register before the invalid tag.
            bad[start + offset] ^= 0xFFU;
            const auto before = e.save_state();
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE((e.save_state() == before));
        }
    }
}
} // namespace
