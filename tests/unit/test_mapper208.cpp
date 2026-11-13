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
    h.mapper_number = 208U;
    h.is_nes2 = !legacy;
    h.submapper = profile;
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
std::unique_ptr<Mapper> board(Byte profile = 0U,
                              bool legacy = false,
                              std::size_t p = 131072U,
                              std::size_t c = 262144U) {
    register_builtin_mappers();
    auto r =
        MapperRegistry::instance().create(header(profile, legacy), rom(p, 8192U), rom(c, 1024U));
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
// Functional truth-table data published in NESdev r5614, independent of the
// production Boolean reduction. No external emulator implementation is used.
constexpr std::array<Byte, 256> kMasks = {
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x49, 0x19, 0x09, 0x59, 0x49, 0x19, 0x09,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x51, 0x41, 0x11, 0x01, 0x51, 0x41, 0x11, 0x01,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x49, 0x19, 0x09, 0x59, 0x49, 0x19, 0x09,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x51, 0x41, 0x11, 0x01, 0x51, 0x41, 0x11, 0x01,
    0x00, 0x10, 0x40, 0x50, 0x00, 0x10, 0x40, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x18, 0x48, 0x58, 0x08, 0x18, 0x48, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x10, 0x40, 0x50, 0x00, 0x10, 0x40, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x08, 0x18, 0x48, 0x58, 0x08, 0x18, 0x48, 0x58, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x58, 0x48, 0x18, 0x08, 0x58, 0x48, 0x18, 0x08,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x50, 0x40, 0x10, 0x00, 0x50, 0x40, 0x10, 0x00,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x58, 0x48, 0x18, 0x08, 0x58, 0x48, 0x18, 0x08,
    0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x59, 0x50, 0x40, 0x10, 0x00, 0x50, 0x40, 0x10, 0x00,
    0x01, 0x11, 0x41, 0x51, 0x01, 0x11, 0x41, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x19, 0x49, 0x59, 0x09, 0x19, 0x49, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x11, 0x41, 0x51, 0x01, 0x11, 0x41, 0x51, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x09, 0x19, 0x49, 0x59, 0x09, 0x19, 0x49, 0x59, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
TEST_CASE("Mapper 208 original board latches address aliases and ignores MMC3 PRG/mirroring",
          "[mappers][mapper208]") {
    for (bool legacy : {false, true}) {
        auto m = board(0U, legacy);
        REQUIRE(m->read_prg(0x8000U) == 12U);
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        for (unsigned value = 0U; value < 256U; ++value) {
            const unsigned page = (value & 1U) | ((value >> 3U) & 2U);
            for (Address a : {0x4800U, 0x4FFFU, 0x6800U, 0x6FFFU}) {
                if (a < 0x6000U)
                    m->write_expansion(a, static_cast<Byte>(value));
                else
                    m->write_prg(a, static_cast<Byte>(value));
                bank(*m, 6U, 0x35U, 0x40U);
                bank(*m, 7U, 0x2AU);
                m->write_prg(0xA000U, static_cast<Byte>(value ^ 1U));
                for (unsigned slot = 0U; slot < 4U; ++slot)
                    REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                            page * 4U + slot);
                REQUIRE(m->mirror_mode() ==
                        ((value & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            }
        }
    }
}
TEST_CASE("Mapper 208 submapper 1 uses R6 shifted right by two for the whole PRG window",
          "[mappers][mapper208]") {
    auto m = board(1U, false, 2097152U);
    for (Byte mode : {0U, 0x40U, 0x80U, 0xC0U})
        for (unsigned value = 0U; value < 256U; ++value) {
            bank(*m, 6U, static_cast<Byte>(value), mode);
            bank(*m, 7U, static_cast<Byte>(value ^ 0x35U), mode);
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                        (value & 0xFCU) + slot);
            m->write_expansion(0x4800U, 0U);
            m->write_prg(0x6800U, 0U);
            REQUIRE(m->read_prg(0x8000U) == (value & 0xFCU));
        }
}
TEST_CASE("Mapper 208 XOR protection matches every published key and every data byte",
          "[mappers][mapper208]") {
    auto m = board();
    for (unsigned key = 0U; key < 256U; ++key) {
        m->write_expansion(static_cast<Address>(0x5000U + key * 7U), static_cast<Byte>(key));
        for (unsigned value = 0U; value < 256U; ++value)
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                m->write_expansion(static_cast<Address>(0x5800U + slot), static_cast<Byte>(value));
                const auto expected = static_cast<Byte>(value ^ kMasks[key]);
                REQUIRE(m->read_expansion(static_cast<Address>(0x5800U + slot)) == expected);
                REQUIRE(m->read_expansion(static_cast<Address>(0x5FFCU + slot)) == expected);
            }
    }
    m->write_expansion(0x5000U, 0x40U);
    for (Byte slot = 0U; slot < 4U; ++slot)
        m->write_expansion(0x5800U + slot, static_cast<Byte>(slot + 0x31U));
    for (unsigned key = 0U; key < 256U; ++key) {
        m->write_expansion(0x57FFU, static_cast<Byte>(key));
        for (Byte slot = 0U; slot < 4U; ++slot)
            REQUIRE(m->read_expansion(0x5800U + slot) == slot + 0x31U);
    }
    REQUIRE(m->read_prg(0x8000U) == 12U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
}
TEST_CASE("Mapper 208 decodes every low-register alias without exposing RAM",
          "[mappers][mapper208]") {
    auto m = board();
    for (unsigned a = 0x4800U; a < 0x7000U; ++a) {
        if (a >= 0x5000U && a < 0x6800U)
            continue;
        const auto value = static_cast<Byte>(a ^ (a >> 8U));
        if (a < 0x6000U)
            m->write_expansion(static_cast<Address>(a), value);
        else
            m->write_prg(static_cast<Address>(a), value);
        REQUIRE(m->read_prg(0x8000U) == (((value & 1U) | ((value >> 3U) & 2U)) * 4U));
        REQUIRE(m->mirror_mode() ==
                ((value & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    m->write_expansion(0x5000U, 0x40U);
    for (unsigned a = 0x5800U; a < 0x6000U; ++a) {
        m->write_expansion(static_cast<Address>(a), static_cast<Byte>(a));
        REQUIRE(m->read_expansion(static_cast<Address>(0x5800U | (a & 3U))) ==
                static_cast<Byte>(a));
    }
    for (Byte gate : {0U, 0x80U, 0xC0U}) {
        m->write_prg(0xA001U, gate);
        m->write_prg(0x6800U, 0U);
        REQUIRE(m->read_prg(0x8000U) == 0U);
        m->write_prg(0x6FFFU, 0x11U);
        REQUIRE(m->read_prg(0x8000U) == 12U);
        m->write_expansion(0x5000U, 0x40U);
        m->write_expansion(0x5800U, gate);
        REQUIRE(m->read_expansion(0x5800U) == gate);
    }
    const auto before = state(*m);
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        const bool expansion = a >= 0x4800U && a < 0x6000U;
        const bool low_prg = a >= 0x6800U && a < 0x7000U;
        REQUIRE(m->maps_expansion(static_cast<Address>(a)) == expansion);
        REQUIRE(m->maps_prg(static_cast<Address>(a)) == low_prg);
        if (!expansion && !low_prg) {
            m->write_expansion(static_cast<Address>(a), 0xFFU);
            m->write_prg(static_cast<Address>(a), 0xFFU);
        }
    }
    REQUIRE(state(*m) == before);
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->has_expansion_audio());
    auto alternate = board(1U);
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        REQUIRE_FALSE(alternate->maps_expansion(static_cast<Address>(a)));
        REQUIRE_FALSE(alternate->maps_prg(static_cast<Address>(a)));
    }
}
TEST_CASE("Mapper 208 preserves MMC3 CHR pairing and inversion across physical ROM sizes",
          "[mappers][mapper208]") {
    for (Byte profile : {0U, 1U})
        for (std::size_t p : {32768UZ, 98304UZ, 131072UZ})
            for (std::size_t c : {8192UZ, 24576UZ, 262144UZ}) {
                auto m = board(profile, false, p, c);
                for (Byte mode : {0U, 0x80U})
                    for (Byte reg = 0U; reg < 6U; ++reg)
                        for (unsigned value = 0U; value < 256U; ++value) {
                            bank(*m, reg, static_cast<Byte>(value), mode);
                            const unsigned start = reg < 2U ? reg * 2U + (mode == 0U ? 0U : 4U)
                                                            : reg - 2U + (mode == 0U ? 4U : 0U);
                            for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                                const auto raw = reg < 2U ? (value & 0xFEU) | half : value;
                                const auto a =
                                    static_cast<Address>((start + half) * 1024U + 0x123U);
                                m->write_chr(a, 0xA7U);
                                REQUIRE(m->read_chr(a) == raw % (c / 1024U));
                            }
                        }
                for (unsigned value = 0U; value < 256U; ++value) {
                    if (profile == 0U)
                        m->write_expansion(0x4800U, static_cast<Byte>(value));
                    else
                        bank(*m, 6U, static_cast<Byte>(value));
                    const unsigned page =
                        profile == 0U ? (value & 1U) | ((value >> 3U) & 2U) : value >> 2U;
                    for (unsigned slot = 0U; slot < 4U; ++slot)
                        REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                                (page * 4U + slot) % (p / 8192U));
                }
            }
}
TEST_CASE("Mapper 208 snapshots retain the key, protection bytes, bank latch and IRQ progress",
          "[mappers][mapper208][state]") {
    for (Byte profile : {0U, 1U}) {
        auto m = board(profile);
        const auto initial = state(*m);
        m->write_expansion(0x5000U, 0xFFU);
        for (Byte slot = 0U; slot < 4U; ++slot)
            m->write_expansion(0x5800U + slot, static_cast<Byte>(0xA7U ^ slot));
        m->write_expansion(0x4800U, 0x30U);
        bank(*m, 6U, 0xFFU, 0xC0U);
        m->write_prg(0xC000U, 2U);
        m->write_prg(0xC001U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        m->clock_irq_counter();
        const auto saved = state(*m);
        REQUIRE(saved.size() == (profile == 0U ? 23U : 17U));
        auto copy = board(profile);
        StateReader r(saved);
        copy->load_state(r);
        REQUIRE(r.ok());
        REQUIRE(r.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        copy->clock_irq_counter();
        REQUIRE(copy->irq_pending());
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        if (profile == 0U) {
            for (Byte slot = 0U; slot < 4U; ++slot)
                REQUIRE(copy->read_expansion(0x5800U + slot) == (0xA7U ^ slot));
            copy->write_expansion(0x5FFFU, 0x73U);
            REQUIRE(copy->read_expansion(0x5803U) == 0x73U);
        }
        auto other = board(profile ^ 1U);
        StateReader wrong(saved);
        other->load_state(wrong);
        REQUIRE_FALSE(wrong.ok());
        for (std::size_t length = 0U; length < saved.size(); ++length) {
            StateReader short_reader(std::span(saved).first(length));
            m->load_state(short_reader);
            REQUIRE_FALSE(short_reader.ok());
            REQUIRE(state(*m) == initial);
        }
        for (std::size_t offset : {0UZ, 1UZ, 9UZ, 10UZ, 13UZ, 14UZ, 15UZ, 16UZ}) {
            auto bad = saved;
            bad[offset] = 0xFFU;
            StateReader bad_reader(bad);
            m->load_state(bad_reader);
            REQUIRE_FALSE(bad_reader.ok());
            REQUIRE(state(*m) == initial);
        }
        if (profile == 0U) {
            for (std::size_t offset : {10UZ, 17UZ}) {
                auto bad = saved;
                bad[offset] ^= offset == 10U ? 1U : 0x20U;
                StateReader bad_reader(bad);
                m->load_state(bad_reader);
                REQUIRE_FALSE(bad_reader.ok());
                REQUIRE(state(*m) == initial);
            }
        }
        m->reset();
        REQUIRE(state(*m) == initial);
    }
}
TEST_CASE("Mapper 208 validates profile-specific ROM and memory metadata", "[mappers][mapper208]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 16U; ++kind) {
        auto h = header();
        auto p = rom(131072U, 8192U);
        auto c = rom(262144U, 1024U);
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
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 8U:
            p.resize(16384U);
            break;
        case 9U:
            p.resize(163840U);
            break;
        case 10U:
            p.pop_back();
            break;
        case 11U:
            c.clear();
            break;
        case 12U:
            c.resize(263168U);
            break;
        case 13U:
            c.pop_back();
            break;
        case 14U:
            h = header(0U, true);
            h.has_battery = true;
            break;
        case 15U:
            h = header(1U);
            p.resize(2129920U);
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(MapperRegistry::instance().create(h, std::move(p), std::move(c)));
    }
}

std::vector<Byte> image(Byte profile = 0U, bool legacy = false, bool vertical = true) {
    auto p = rom(131072U, 8192U);
    const auto c = rom(262144U, 1024U);
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
    store(0x14U, 0U);
    if (profile == 0U) {
        expect(0x9A00U, 12U);
        store(0x5000U, 9U);
        const auto resume = static_cast<Address>(0xE000U + code.size() + 18U);
        store(0x5800U, 0x4CU ^ 0x49U);
        store(0x5801U, static_cast<Byte>(resume) ^ 0x49U);
        store(0x5802U, static_cast<Byte>(resume >> 8U) ^ 0x49U);
        code.insert(code.end(),
                    {0x4CU, 0U, 0x58U}); // Execute the decoded JMP from cartridge registers.
    } else {
        store(0x5000U, 9U);
        store(0x5800U, 0U);
        store(0x6800U, 0x11U);
        expect(0x5800U, 0x58U); // Undriven: last absolute-address operand byte.
        expect(0x6800U, 0x68U);
    }
    store(0x14U, 0x42U);
    for (unsigned page = 0U; page < 4U; ++page) {
        if (profile == 0U) {
            store(0x4800U, static_cast<Byte>((page & 1U) | ((page & 2U) << 3U)));
            set_bank(6U, 0x39U, 0x40U);
            set_bank(7U, 0x17U, 0x40U);
        } else {
            set_bank(6U, static_cast<Byte>((page << 2U) | page | 0x80U), 0x40U);
            set_bank(7U, 0x17U, 0x40U);
        }
        for (unsigned slot = 0U; slot < 4U; ++slot)
            expect(static_cast<Address>(0x9A00U + slot * 8192U),
                   static_cast<Byte>(page * 4U + slot));
        if (profile == 0U) {
            store(0xA001U, 0U);
            store(0x6FFFU, 0x31U);
            expect(0x9A00U, 12U);
        }
    }
    constexpr std::array<Byte, 6> kChr = {0x7DU, 0xCBU, 0x35U, 0xA6U, 0xE7U, 9U};
    constexpr std::array<Byte, 8> kChrPages = {0x7CU, 0x7DU, 0xCAU, 0xCBU, 0x35U, 0xA6U, 0xE7U, 9U};
    for (Byte mode : {0U, 0x80U}) {
        for (Byte reg = 0U; reg < 6U; ++reg)
            set_bank(reg, kChr[reg], mode);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto a = static_cast<Address>(slot * 1024U + 0x123U);
            const Byte expected = kChrPages[slot ^ (mode != 0U ? 4U : 0U)];
            pread(a, expected);
            pwrite(a, 0U);
            pread(a, expected);
        }
    }
    if (profile == 0U)
        store(0x4800U, 0x11U);
    else
        store(0xA000U, 0U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    if (profile == 0U) {
        store(0xA000U, 1U);
        pread(0x2400U, 0x27U);
        store(0x6800U, 0x31U);
    } else
        store(0xA000U, 1U);
    pread(0x2400U, 0x19U);
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
    for (std::size_t base = 0U; base < p.size(); base += 32768U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(base + 0x6000U));
        std::ranges::copy(kIrq, p.begin() + static_cast<std::ptrdiff_t>(base + 0x7800U));
        for (std::size_t v = base + 0x7FFAU; v < base + 32768U; v += 2U) {
            p[v] = 0U;
            p[v + 1U] = v == base + 0x7FFCU ? 0xE0U : 0xF8U;
        }
    }
    std::vector<Byte> result(16U);
    result[0] = 'N';
    result[1] = 'E';
    result[2] = 'S';
    result[3] = 0x1AU;
    result[4] = 8U;
    result[5] = 32U;
    result[6] = vertical ? 1U : 0U;
    result[7] = legacy ? 0xD0U : 0xD8U;
    result[8] = legacy ? 0U : static_cast<Byte>(profile << 4U);
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
TEST_CASE("Mapper 208 expansion and low PRG reads merge only the documented bus drivers",
          "[mappers][mapper208]") {
    for (Byte profile : {0U, 1U}) {
        auto c = cartridge(profile);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        bus.write(0x5000U, 0x40U);
        bus.write(0x5800U, 0xA7U);
        for (unsigned value = 0U; value < 256U; ++value) {
            for (Address a : {0x4020U,
                              0x47FFU,
                              0x4800U,
                              0x4FFFU,
                              0x5000U,
                              0x57FFU,
                              0x6000U,
                              0x67FFU,
                              0x6800U,
                              0x6FFFU,
                              0x7000U,
                              0x7FFFU}) {
                bus.write(0x10U, static_cast<Byte>(value));
                REQUIRE(bus.read(a) == value);
            }
            bus.write(0x10U, static_cast<Byte>(value));
            REQUIRE(bus.read(0x5800U) == (profile == 0U ? 0xA7U : value));
        }
    }
}
TEST_CASE("Mapper 208 CPU fixtures execute protection RAM and preserve IRQ/snapshot behavior",
          "[mappers][mapper208][state]") {
    for (Byte profile : {0U, 1U})
        for (bool legacy : {false, true}) {
            if (profile == 1U && legacy)
                continue;
            for (bool vertical : {false, true}) {
                INFO(profile);
                INFO(legacy);
                INFO(vertical);
                Emulator e;
                REQUIRE(e.load_cartridge(cartridge(profile, legacy, vertical), ""));
                e.reset();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0x42U);
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
                const auto start = early.size() - tail.data().size() - (profile == 0U ? 23U : 17U);
                for (std::size_t offset : {0UZ, 15UZ, 16UZ}) {
                    auto bad = early;
                    bad[start + offset] ^= 0xFFU;
                    if (profile == 0U)
                        bad[start + 19U] ^= 0xA7U;
                    const auto before = e.save_state();
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                }
                e.reset();
                REQUIRE(e.read_cpu(0x9A00U) == (profile == 0U ? 12U : 0U));
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            }
        }
}
} // namespace
