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
    h.mapper_number = 215U;
    h.is_nes2 = !legacy;
    h.submapper = profile;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}
std::vector<Byte> rom(std::size_t size, std::size_t page) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>((i / page) >> (i % page == 1U ? 8U : 0U));
    return data;
}
std::unique_ptr<Mapper> board(Byte profile = 0U,
                              bool legacy = false,
                              std::size_t p = 0U,
                              std::size_t c = 0U) {
    register_builtin_mappers();
    const auto max_rom = profile == 0U ? 0x100000U : 0x200000U;
    auto m = MapperRegistry::instance().create(header(profile, legacy),
                                               rom(p == 0U ? max_rom : p, 8192U),
                                               rom(c == 0U ? max_rom : c, 1024U));
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
// Inverse of the published r5704 tables: physical write address/index needed
// to reach a logical MMC3 port/register. Production uses the forward mapping.
constexpr std::array<std::array<Address, 8>, 8> kWritePorts = {
    {{0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U},
     {0xA000U, 0xC000U, 0x8001U, 0x8000U, 0xA001U, 0xC001U, 0xE000U, 0xE001U},
     {0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U},
     {0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xE001U, 0x8000U, 0xE000U, 0xC001U},
     {0xA000U, 0x8001U, 0xC000U, 0x8000U, 0xA001U, 0xC001U, 0xE000U, 0xE001U},
     {0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U},
     {0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U},
     {0x8000U, 0x8001U, 0xA000U, 0xA001U, 0xC000U, 0xC001U, 0xE000U, 0xE001U}}};
constexpr std::array<std::array<Byte, 8>, 8> kWriteIndices = {{{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U},
                                                               {0U, 3U, 1U, 5U, 6U, 7U, 2U, 4U},
                                                               {0U, 3U, 5U, 7U, 2U, 1U, 6U, 4U},
                                                               {0U, 7U, 5U, 2U, 6U, 4U, 1U, 3U},
                                                               {0U, 5U, 1U, 3U, 7U, 2U, 4U, 6U},
                                                               {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U},
                                                               {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U},
                                                               {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}}};
void logical_write(Mapper& m, unsigned pattern, unsigned port, Byte value) {
    if (port == 0U)
        value = static_cast<Byte>((value & 0xF8U) | kWriteIndices[pattern][value & 7U]);
    m.write_prg(kWritePorts[pattern][port], value);
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U, unsigned pattern = 0U) {
    logical_write(m, pattern, 0U, static_cast<Byte>(reg | mode));
    logical_write(m, pattern, 1U, value);
}
// Independent pin-by-pin oracle in bank units (PRG 8 KiB, CHR 1 KiB).
unsigned routed_bank(unsigned inner, bool chr, Byte profile, Byte mode, Byte outer) {
    const unsigned width = chr ? 8U : 5U;
    unsigned result = inner % (1U << width);
    if ((mode & 0x40U) != 0U) {
        result %= 1U << (width - 1U);
        result += ((outer >> (chr ? 5U : 4U)) % 2U) << (width - 1U);
    }
    const std::array<unsigned, 3> pins =
        chr ? (profile == 0U ? std::array{2U, 3U, 0U} : std::array{1U, 2U, 3U})
            : std::array{0U, 1U, 3U};
    for (unsigned bit = 0U; bit < (profile == 0U ? 2U : 3U); ++bit)
        result += ((outer >> pins[bit]) % 2U) << (width + bit);
    return result;
}
unsigned chr_marker(Mapper& m, Address addr) {
    return m.read_chr(addr) | (static_cast<unsigned>(m.read_chr(addr + 1U)) << 8U);
}
TEST_CASE("Mapper 215 starts in the documented outer bank for both boards",
          "[mappers][mapper215]") {
    register_builtin_mappers();
    for (Byte profile : {0U, 1U}) {
        INesHeader h{};
        h.mapper_number = 215U;
        h.is_nes2 = true;
        h.submapper = profile;
        h.mirror_mode = MirrorMode::Vertical;
        h.prg_ram_size = 0U;
        std::vector<Byte> prg(profile == 0U ? 0x100000U : 0x200000U);
        for (std::size_t i = 0U; i < prg.size(); ++i)
            prg[i] = static_cast<Byte>(i / 8192U);
        auto m = MapperRegistry::instance().create(h, std::move(prg), std::vector<Byte>(0x2000U));
        REQUIRE(m);
        REQUIRE((*m)->read_prg(0x8000U) == (profile == 0U ? 96U : 224U));
        REQUIRE((*m)->read_prg(0xE000U) == (profile == 0U ? 127U : 255U));
    }
}

TEST_CASE("Mapper 215 routes every mode byte and outer pin in MMC3 and NROM modes",
          "[mappers][mapper215]") {
    for (Byte profile : {0U, 1U}) {
        auto m = board(profile);
        for (Byte outer = 0U; outer < 64U; ++outer) {
            m->write_expansion(0x5001U, outer);
            for (unsigned control = 0U; control < 256U; ++control) {
                const auto mode = static_cast<Byte>(control);
                const bool swapped = (control & 1U) != 0U;
                bank(*m, 6U, 0x35U, swapped ? 0x40U : 0U);
                bank(*m, 7U, 0x2BU, swapped ? 0x40U : 0U);
                m->write_expansion(0x5000U, mode);
                const std::array<unsigned, 4> inner = {
                    swapped ? 62U : 53U, 43U, swapped ? 53U : 62U, 63U};
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    unsigned raw = inner[slot];
                    if ((control & 0x80U) != 0U) {
                        unsigned page16 = control % 16U;
                        if ((control & 0x20U) != 0U)
                            page16 = page16 / 2U * 2U + slot / 2U;
                        raw = page16 * 2U + slot % 2U;
                    }
                    const auto a = static_cast<Address>(0x8000U + slot * 8192U);
                    REQUIRE(m->read_prg(a) == routed_bank(raw, false, profile, mode, outer));
                }
            }
        }
    }
}

TEST_CASE("Mapper 215 scrambling preserves mode bits, all data bytes and every MMC3 register",
          "[mappers][mapper215]") {
    constexpr std::array<Byte, 6> kChr = {0x51U, 0xA3U, 0x35U, 0xC9U, 0x72U, 0xEDU};
    for (Byte profile : {0U, 1U}) {
        auto m = board(profile);
        m->write_expansion(0x5001U, 0U);
        for (unsigned pattern = 0U; pattern < 8U; ++pattern) {
            m->write_expansion(0x5007U, static_cast<Byte>(pattern));
            for (unsigned data = 0U; data < 256U; ++data) {
                const auto modes = static_cast<Byte>(data & 0xC0U);
                // All 256 select bytes, including ignored bits 3-5, must route
                // to the intended register and retain PRG/CHR inversion bits.
                const auto reg = static_cast<Byte>(data & 7U);
                logical_write(*m, pattern, 0U, static_cast<Byte>(data));
                logical_write(*m, pattern, 1U, static_cast<Byte>(0xA7U ^ reg));
                const auto selected = state(*m);
                REQUIRE(selected[0U] == reg);
                REQUIRE(selected[1U] == modes);
                REQUIRE(selected[2U + reg] == ((0xA7U ^ reg) & (reg >= 6U ? 0x3FU : 0xFFU)));
                for (Byte r = 0U; r < 6U; ++r)
                    bank(*m, r, kChr[r], modes, pattern);
                bank(*m, 6U, static_cast<Byte>(data), modes, pattern);
                bank(*m, 7U, static_cast<Byte>(data ^ 0x2BU), modes, pattern);
                const bool inv = (data & 0x80U) != 0U;
                const std::array<unsigned, 8> normal = {
                    0x50U, 0x51U, 0xA2U, 0xA3U, 0x35U, 0xC9U, 0x72U, 0xEDU};
                for (unsigned slot = 0U; slot < 8U; ++slot)
                    REQUIRE(chr_marker(*m, static_cast<Address>(slot * 1024U)) ==
                            normal[(slot + (inv ? 4U : 0U)) % 8U]);
                REQUIRE(m->read_prg(0x8000U) == ((data & 0x40U) != 0U ? 30U : data % 32U));
                REQUIRE(m->read_prg(0xA000U) == ((data ^ 0x2BU) % 32U));
                REQUIRE(m->read_prg(0xC000U) == ((data & 0x40U) != 0U ? data % 32U : 30U));
                REQUIRE(m->read_prg(0xE000U) == 31U);
            }
        }
    }
}

TEST_CASE("Mapper 215 applies CHR outer pins after pairing and inversion and before ROM wrap",
          "[mappers][mapper215]") {
    for (Byte profile : {0U, 1U}) {
        auto m = board(profile);
        for (Byte outer : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 63U}) {
            m->write_expansion(0x5001U, outer);
            for (Byte control : {0U, 0x40U, 0x80U, 0xC0U}) {
                m->write_expansion(0x5000U, control);
                for (unsigned data = 0U; data < 256U; ++data)
                    for (Byte inversion : {0U, 0x80U}) {
                        bank(*m, 0U, static_cast<Byte>(data), inversion);
                        bank(*m, 2U, static_cast<Byte>(data), inversion);
                        const auto pair = static_cast<Address>(inversion == 0U ? 0U : 0x1000U);
                        const auto single = static_cast<Address>(pair ^ 0x1000U);
                        REQUIRE(chr_marker(*m, pair) ==
                                routed_bank(data & 0xFEU, true, profile, control, outer));
                        REQUIRE(chr_marker(*m, pair + 1024U) ==
                                routed_bank(data | 1U, true, profile, control, outer));
                        const auto expected = routed_bank(data, true, profile, control, outer);
                        m->write_chr(single, 0x5AU);
                        REQUIRE(chr_marker(*m, single) == expected);
                    }
            }
        }
    }
    for (Byte profile : {0U, 1U})
        for (std::size_t p : {32768UZ, 98304UZ, 270336UZ})
            for (std::size_t c : {8192UZ, 24576UZ, 263168UZ}) {
                auto m = board(profile, false, p, c);
                for (Byte outer = 0U; outer < 64U; ++outer) {
                    m->write_expansion(0x5001U, outer);
                    for (Byte control : {0U, 0x40U, 0x80U, 0xAFU, 0xE7U}) {
                        m->write_expansion(0x5000U, control);
                        bank(*m, 6U, 0x25U);
                        bank(*m, 2U, 0xD5U);
                        const std::array<unsigned, 4> mmc3 = {0x25U, 0U, 62U, 63U};
                        for (unsigned slot = 0U; slot < 4U; ++slot) {
                            unsigned raw = mmc3[slot];
                            if ((control & 0x80U) != 0U) {
                                unsigned page16 = control % 16U;
                                if ((control & 0x20U) != 0U)
                                    page16 = page16 / 2U * 2U + slot / 2U;
                                raw = page16 * 2U + slot % 2U;
                            }
                            REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                                    routed_bank(raw, false, profile, control, outer) % (p / 8192U));
                        }
                        REQUIRE(chr_marker(*m, 0x1000U) ==
                                routed_bank(0xD5U, true, profile, control, outer) % (c / 1024U));
                    }
                }
            }
}

TEST_CASE("Mapper 215 low-port aliases ignore unused pins and A001 gates", "[mappers][mapper215]") {
    auto m = board();
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        const auto addr = static_cast<Address>(a);
        const bool mapped = a >= 0x5000U && a < 0x6000U && (a % 8U < 2U || a % 8U == 7U);
        REQUIRE(m->maps_expansion(addr) == mapped);
        REQUIRE_FALSE(m->maps_prg(addr));
        m->write_prg(0xA001U, static_cast<Byte>(a));
        m->write_expansion(0x5007U, 0U);
        m->write_expansion(0x5000U, 0U);
        m->write_expansion(0x5001U, 0U);
        const auto before = state(*m);
        m->write_prg(addr, 0xFFU);
        m->write_expansion(addr, 0xFFU);
        const auto after = state(*m);
        if (!mapped)
            REQUIRE(after == before);
        else {
            REQUIRE(m->expansion_read_mask(addr) == 0U);
            REQUIRE(after[17U] == (a % 8U == 0U ? 0xEFU : 0U));
            REQUIRE(after[18U] == (a % 8U == 1U ? 0x3FU : 0U));
            REQUIRE(after[19U] == (a % 8U == 7U ? 7U : 0U));
        }
    }
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->has_expansion_audio());
}

TEST_CASE("Mapper 215 scrambling includes mirroring and every IRQ port without resetting progress",
          "[mappers][mapper215]") {
    for (unsigned pattern = 0U; pattern < 8U; ++pattern) {
        auto m = board();
        m->write_expansion(0x5007U, static_cast<Byte>(pattern));
        for (unsigned data = 0U; data < 256U; ++data) {
            logical_write(*m, pattern, 2U, static_cast<Byte>(data));
            REQUIRE(m->mirror_mode() ==
                    (data % 2U == 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }
        logical_write(*m, pattern, 4U, 2U);
        logical_write(*m, pattern, 5U, 0U);
        logical_write(*m, pattern, 7U, 0U);
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        m->write_expansion(0x5000U, 0xEFU);
        m->write_expansion(0x5001U, 0x36U);
        m->write_expansion(0x5007U, static_cast<Byte>(pattern ^ 7U));
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        logical_write(*m, pattern ^ 7U, 6U, 0U);
        REQUIRE_FALSE(m->irq_pending());
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        logical_write(*m, pattern ^ 7U, 4U, 0U);
        logical_write(*m, pattern ^ 7U, 5U, 0U);
        logical_write(*m, pattern ^ 7U, 7U, 0U);
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->acknowledge_irq();
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
    }
}

TEST_CASE("Mapper 215 snapshots retain scrambling and pending IRQs and reject malformed wiring",
          "[mappers][mapper215][state]") {
    for (Byte profile : {0U, 1U}) {
        auto m = board(profile);
        const auto initial = state(*m);
        m->write_expansion(0x5000U, 0xEFU);
        m->write_expansion(0x5001U, 0x36U);
        m->write_expansion(0x5007U, 3U);
        bank(*m, 6U, 0x35U, 0xC0U, 3U);
        bank(*m, 0U, 0xA7U, 0xC0U, 3U);
        logical_write(*m, 3U, 2U, 1U);
        logical_write(*m, 3U, 4U, 2U);
        logical_write(*m, 3U, 5U, 0U);
        logical_write(*m, 3U, 7U, 0U);
        m->clock_irq_counter();
        m->clock_irq_counter();
        const auto saved = state(*m);
        REQUIRE(saved.size() == 20U);
        auto copy = board(profile);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->read_prg(0x8000U) == m->read_prg(0x8000U));
        REQUIRE(chr_marker(*copy, 0x1000U) == chr_marker(*m, 0x1000U));
        copy->clock_irq_counter();
        REQUIRE(copy->irq_pending());
        const auto pending = state(*copy);
        StateReader pending_reader(pending);
        m->load_state(pending_reader);
        REQUIRE(pending_reader.ok());
        REQUIRE(m->irq_pending());
        logical_write(*m, 3U, 6U, 0U);
        REQUIRE_FALSE(m->irq_pending());
        auto other = board(profile ^ 1U);
        StateReader wrong(saved);
        other->load_state(wrong);
        REQUIRE_FALSE(wrong.ok());
        for (std::size_t n = 0U; n < saved.size(); ++n) {
            StateReader short_reader(std::span(saved).first(n));
            m->load_state(short_reader);
            REQUIRE_FALSE(short_reader.ok());
            REQUIRE(state(*m) == initial);
        }
        for (const auto [offset, value] :
             std::array<std::pair<std::size_t, Byte>, 11>{{{0U, 8U},
                                                           {1U, 1U},
                                                           {8U, 64U},
                                                           {9U, 64U},
                                                           {10U, 2U},
                                                           {13U, 4U},
                                                           {14U, 1U},
                                                           {15U, 0xFFU},
                                                           {17U, 0x10U},
                                                           {18U, 0x40U},
                                                           {19U, 8U}}}) {
            auto bad = saved;
            bad[offset] = value;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == initial);
        }
        StateReader restore(saved);
        m->load_state(restore);
        REQUIRE(restore.ok());
        m->reset();
        REQUIRE(state(*m) == initial);
    }
}

TEST_CASE("Mapper 215 validates submappers, ROM capacity and unsupported memory metadata",
          "[mappers][mapper215]") {
    register_builtin_mappers();
    for (Byte profile : {0U, 1U}) {
        const auto maximum = profile == 0U ? 0x100000U : 0x200000U;
        for (unsigned kind = 0U; kind < 16U; ++kind) {
            auto h = header(profile);
            std::size_t p = 32768U;
            std::size_t c = 8192U;
            switch (kind) {
            case 0U:
                h.submapper = 2U;
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
                h.mirror_mode = MirrorMode::SingleUpper;
                break;
            case 8U:
                p = 16384U;
                break;
            case 9U:
                p = maximum + 8192U;
                break;
            case 10U:
                --p;
                break;
            case 11U:
                c = 0U;
                break;
            case 12U:
                c = maximum + 1024U;
                break;
            case 13U:
                --c;
                break;
            case 14U:
                h = header(0U, true);
                h.has_battery = true;
                break;
            case 15U:
                h = header(0U, true);
                h.mirror_mode = MirrorMode::FourScreen;
                break;
            default:
                break;
            }
            INFO(profile << ':' << kind);
            REQUIRE_FALSE(
                MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
        }
    }
    auto legacy = board(0U, true);
    REQUIRE(legacy->read_prg(0x8000U) == 96U);
    REQUIRE(legacy->read_prg(0xE000U) == 127U);
}

TEST_CASE("Mapper 215 MMC3 aliases ignore only the documented address pins",
          "[mappers][mapper215]") {
    auto actual = board();
    auto expected = board();
    for (unsigned pattern = 0U; pattern < 8U; ++pattern)
        for (unsigned port = 0U; port < 8U; ++port)
            for (Address alias : {0U, 2U, 0x20U, 0x200U, 0x1FFEU})
                for (Byte value : {0U, 0x3FU, 0x80U, 0xFFU}) {
                    for (auto* m : {actual.get(), expected.get()}) {
                        m->reset();
                        m->write_expansion(0x5007U, static_cast<Byte>(pattern));
                        logical_write(*m, pattern, 4U, 2U);
                        logical_write(*m, pattern, 5U, 0U);
                        logical_write(*m, pattern, 7U, 0U);
                        m->clock_irq_counter();
                    }
                    logical_write(*expected, pattern, port, value);
                    const auto encoded = port == 0U
                                             ? static_cast<Byte>((value & 0xF8U) |
                                                                 kWriteIndices[pattern][value & 7U])
                                             : value;
                    actual->write_prg(kWritePorts[pattern][port] | alias, encoded);
                    REQUIRE(state(*actual) == state(*expected));
                }
}

std::vector<Byte> image(Byte profile = 0U, bool legacy = false, bool vertical = true) {
    const auto size = profile == 0U ? 0x100000U : 0x200000U;
    auto p = rom(size, 8192U);
    const auto c = rom(size, 1024U);
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
    const auto write = [&store](unsigned pattern, unsigned port, Byte value) {
        if (port == 0U)
            value = static_cast<Byte>((value & 0xF8U) | kWriteIndices[pattern][value & 7U]);
        store(kWritePorts[pattern][port], value);
    };
    const auto set_bank = [&write](unsigned pattern, Byte reg, Byte v, Byte mode = 0U) {
        write(pattern, 0U, static_cast<Byte>(reg | mode));
        write(pattern, 1U, v);
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
    for (unsigned pattern = 0U; pattern < 8U; ++pattern) {
        store(0x5007U, static_cast<Byte>(pattern));
        const auto outer = static_cast<Byte>(0x30U | pattern);
        store(0x5001U, outer);
        for (Byte control : {0U, 0x4FU, 0x8BU, 0xEFU}) {
            store(0x5000U, control);
            const auto mode = static_cast<Byte>(pattern % 2U == 0U ? 0U : 0xC0U);
            set_bank(pattern, 6U, 0x35U, mode);
            set_bank(pattern, 7U, 0x2BU, mode);
            set_bank(pattern, 2U, 0xD5U, mode);
            std::array<unsigned, 4> inner = {
                mode == 0U ? 53U : 62U, 43U, mode == 0U ? 62U : 53U, 63U};
            if ((control & 0x80U) != 0U) {
                // Published NROM examples: $8B mirrors page 11; $EF aligns 14/15.
                inner = control == 0x8BU ? std::array{22U, 23U, 22U, 23U}
                                         : std::array{28U, 29U, 30U, 31U};
            }
            for (unsigned slot = 0U; slot < 4U; ++slot)
                expect(static_cast<Address>(0x9A00U + slot * 8192U),
                       static_cast<Byte>(routed_bank(inner[slot], false, profile, control, outer)));
            const auto chr = routed_bank(0xD5U, true, profile, control, outer);
            const auto ca = static_cast<Address>(mode == 0U ? 0x1000U : 0U);
            pread(ca, static_cast<Byte>(chr));
            pread(ca + 1U, static_cast<Byte>(chr >> 8U));
            pwrite(ca, 0U);
            pread(ca, static_cast<Byte>(chr));
        }
        write(pattern, 2U, 0U);
        pwrite(0x2000U, 0x19U);
        pwrite(0x2400U, 0x27U);
        write(pattern, 2U, 1U);
        pread(0x2400U, 0x19U);
        write(pattern, 2U, 0U);
        pread(0x2400U, 0x27U);
    }
    // Return to identity scrambling for a shared IRQ handler; enable actual
    // PPU rendering so the CPU observes the production MMC3 IRQ clock path.
    store(0x5007U, 0U);
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 1U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    store(0xC000U, 0U);
    store(0xC001U, 0U);
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
    for (std::size_t base = 0U; base < p.size(); base += 8192U) {
        // Identical original firmware in every switchable page keeps code
        // fetches stable while all outer and NROM pins are exercised.
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
    result[4] = static_cast<Byte>(size / 16384U);
    result[5] = static_cast<Byte>((size / 8192U) & 0xFFU);
    result[6] = static_cast<Byte>(0x70U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0xD0U : 0xD8U;
    result[8] = legacy ? 0U : static_cast<Byte>(profile << 4U);
    result[9] = legacy ? 0U : static_cast<Byte>((size / 8192U) >> 8U << 4U);
    result.insert(result.end(), p.begin(), p.end());
    result.insert(result.end(), c.begin(), c.end());
    return result;
}
Cartridge cartridge(Byte profile = 0U, bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(profile, legacy, vertical));
    REQUIRE(r);
    return std::move(*r);
}

TEST_CASE("Mapper 215 write-only reads preserve every CPU open-bus bit", "[mappers][mapper215]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (unsigned data = 0U; data < 256U; ++data)
        for (Address a : {0x4020U, 0x5000U, 0x5001U, 0x5002U, 0x5007U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            bus.write(0x5007U, 0U);
            bus.write(0xA001U, static_cast<Byte>(data));
            bus.write(0x10U, static_cast<Byte>(data));
            REQUIRE(bus.read(a) == data);
        }
}

TEST_CASE("Mapper 215 original CPU fixtures verify every scrambling mode and atomic replay",
          "[mappers][mapper215][state]") {
    for (Byte profile : {0U, 1U})
        for (bool vertical : {false, true})
            for (bool legacy : {false, true}) {
                if (profile == 1U && legacy)
                    continue;
                Emulator e;
                REQUIRE(e.load_cartridge(cartridge(profile, legacy, vertical), ""));
                e.reset();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x11U) >= 3U);
                const auto early = e.save_state();
                e.step_frame();
                const auto future = e.save_state();
                REQUIRE(e.load_state(early));
                e.step_frame();
                REQUIRE(e.save_state() == future);
                StateWriter tail;
                Controller{}.save_state(tail);
                Controller{}.save_state(tail);
                const auto start = early.size() - tail.data().size() - 20U;
                for (std::size_t offset : {0UZ, 15UZ, 16UZ, 17UZ, 18UZ, 19UZ}) {
                    auto bad = early;
                    bad[start + 18U] = 1U; // Change a valid decoded field before failing later.
                    bad[start + offset] ^= 0xFFU;
                    const auto before = e.save_state();
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                }
                e.reset();
                REQUIRE(e.read_cpu(0xFA00U) == (profile == 0U ? 127U : 255U));
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            }
}
} // namespace
