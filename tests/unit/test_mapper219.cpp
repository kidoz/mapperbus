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
    h.mapper_number = 219U;
    h.is_nes2 = !legacy;
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
std::unique_ptr<Mapper> board(bool legacy = false,
                              std::size_t p = 524288U,
                              std::size_t c = 524288U) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(header(legacy), rom(p, 8192U), rom(c, 1024U));
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
void outer(Mapper& m, Byte value) {
    m.write_expansion(0x5002U, value & 1U);
    m.write_expansion(0x5003U, static_cast<Byte>((value & 2U) << 4U));
}
void bank(Mapper& m, Byte index, Byte value, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(index | mode));
    m.write_prg(0x8001U, value);
}
unsigned chr_marker(Mapper& m, Address a) {
    return m.read_chr(a) | (static_cast<unsigned>(m.read_chr(a + 1U)) << 8U);
}
constexpr std::array<Byte, 16> kReverse = {
    0U, 8U, 4U, 12U, 2U, 10U, 6U, 14U, 1U, 9U, 5U, 13U, 3U, 11U, 7U, 15U};
TEST_CASE("Mapper 219 starts in outer bank three for its multicart menu", "[mappers][mapper219]") {
    register_builtin_mappers();
    INesHeader h{};
    h.mapper_number = 219U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    std::vector<Byte> prg(524288U);
    std::vector<Byte> chr(524288U);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = static_cast<Byte>(i / 8192U);
    for (std::size_t i = 0U; i < chr.size(); ++i)
        chr[i] = static_cast<Byte>(i / 1024U);
    auto m = MapperRegistry::instance().create(h, std::move(prg), std::move(chr));
    REQUIRE(m);
    REQUIRE((*m)->read_prg(0x8000U) == 48U);
    REQUIRE((*m)->read_prg(0xE000U) == 63U);
    REQUIRE((*m)->read_chr(0U) == 128U);
}

TEST_CASE("Mapper 219 normal mode masks both ROMs before outer pins and physical wrapping",
          "[mappers][mapper219]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        for (Byte block = 0U; block < 4U; ++block) {
            outer(*m, block);
            for (Byte mode : {0U, 0x40U, 0x80U, 0xC0U})
                for (unsigned data = 0U; data < 256U; ++data) {
                    std::array<unsigned, 6> values{};
                    for (Byte r = 0U; r < 6U; ++r) {
                        values[r] = (data + r * 19U) % 128U;
                        bank(*m, r, static_cast<Byte>(data + r * 19U), mode);
                    }
                    bank(*m, 6U, static_cast<Byte>(data), mode);
                    bank(*m, 7U, static_cast<Byte>(data ^ 0x35U), mode);
                    const bool swapped = (mode & 0x40U) != 0U;
                    const std::array<unsigned, 4> p = {swapped ? 14U : data % 16U,
                                                       (data ^ 0x35U) % 16U,
                                                       swapped ? data % 16U : 14U,
                                                       15U};
                    for (unsigned slot = 0U; slot < 4U; ++slot)
                        REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                                p[slot] + block * 16U);
                    const std::array<unsigned, 8> c = {values[0] / 2U * 2U,
                                                       values[0] | 1U,
                                                       values[1] / 2U * 2U,
                                                       values[1] | 1U,
                                                       values[2],
                                                       values[3],
                                                       values[4],
                                                       values[5]};
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const unsigned index = (slot + ((mode & 0x80U) != 0U ? 4U : 0U)) % 8U;
                        REQUIRE(chr_marker(*m, static_cast<Address>(slot * 1024U)) ==
                                c[index] + block * 128U);
                    }
                }
        }
    }
    for (std::size_t p : {32768UZ, 98304UZ, 270336UZ})
        for (std::size_t c : {8192UZ, 24576UZ, 263168UZ}) {
            auto m = board(false, p, c);
            for (Byte block = 0U; block < 4U; ++block) {
                outer(*m, block);
                bank(*m, 6U, 0xE5U);
                bank(*m, 2U, 0xD5U);
                REQUIRE(m->read_prg(0x8000U) == (block * 16U + 5U) % (p / 8192U));
                REQUIRE(m->read_prg(0xC000U) == (block * 16U + 14U) % (p / 8192U));
                REQUIRE(m->read_prg(0xE000U) == (block * 16U + 15U) % (p / 8192U));
                REQUIRE(chr_marker(*m, 0x1000U) == (block * 128U + 0x55U) % (c / 1024U));
            }
        }
}

TEST_CASE("Mapper 219 extended PRG reverses exactly four data pins and switches all CPU slots",
          "[mappers][mapper219]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        m->write_prg(0x8002U, 0x20U);
        std::array<unsigned, 4> expected = {0U, 0U, 14U, 15U};
        for (Byte block = 0U; block < 4U; ++block) {
            outer(*m, block);
            for (unsigned slot = 0U; slot < 4U; ++slot)
                for (Byte alias : {0U, 8U})
                    for (unsigned data = 0U; data < 256U; ++data) {
                        bank(*m, static_cast<Byte>(0x26U - slot + alias), static_cast<Byte>(data));
                        expected[slot] = kReverse[(data / 4U) % 16U];
                        for (unsigned window = 0U; window < 4U; ++window)
                            REQUIRE(m->read_prg(static_cast<Address>(0x8000U + window * 8192U)) ==
                                    expected[window] + block * 16U);
                    }
        }
    }
}

TEST_CASE("Mapper 219 extended CHR composes every latch and data byte without changing neighbors",
          "[mappers][mapper219]") {
    constexpr std::array<Byte, 12> kIndices = {
        9U, 11U, 13U, 15U, 17U, 19U, 21U, 23U, 25U, 27U, 29U, 31U};
    constexpr std::array<unsigned, 12> kSlots = {0U, 1U, 2U, 3U, 4U, 4U, 5U, 5U, 6U, 6U, 7U, 7U};
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        m->write_prg(0x8002U, 0xE0U);
        std::array<unsigned, 8> expected = {0U, 1U, 0U, 1U, 0U, 0U, 0U, 0U};
        for (Byte block : {0U, 3U}) {
            outer(*m, block);
            for (Byte latch = 0U; latch < 8U; ++latch)
                for (unsigned i = 0U; i < kIndices.size(); ++i) {
                    // Every documented even selector reaches the same latch.
                    const auto latch_index = static_cast<Byte>(8U + i * 2U);
                    bank(*m, latch_index, static_cast<Byte>(latch | 0xF8U));
                    for (unsigned slot = 0U; slot < 8U; ++slot)
                        REQUIRE(chr_marker(*m, static_cast<Address>(slot * 1024U)) ==
                                expected[slot] + block * 128U);
                    for (unsigned data = 0U; data < 256U; ++data) {
                        const unsigned slot = kSlots[i];
                        const unsigned neighbor = (slot + 1U) % 8U;
                        bank(*m, kIndices[i], static_cast<Byte>(data));
                        expected[slot] = (data / 2U) | (latch * 16U) | (i < 4U ? i % 2U : 0U);
                        REQUIRE(chr_marker(*m, static_cast<Address>(slot * 1024U)) ==
                                expected[slot] + block * 128U);
                        REQUIRE(chr_marker(*m, static_cast<Address>(neighbor * 1024U)) ==
                                expected[neighbor] + block * 128U);
                    }
                    // Latch changes affect subsequent commits, not stored banks.
                    const auto committed = state(*m);
                    bank(*m, latch_index, static_cast<Byte>(latch ^ 7U));
                    const auto after = state(*m);
                    REQUIRE(
                        std::equal(committed.begin() + 25, committed.end(), after.begin() + 25));
                    for (unsigned slot = 0U; slot < 8U; ++slot)
                        REQUIRE(chr_marker(*m, static_cast<Address>(slot * 1024U)) ==
                                expected[slot] + block * 128U);
                }
        }
    }
}

TEST_CASE("Mapper 219 mode selection and independent register banks survive protocol switches",
          "[mappers][mapper219]") {
    auto m = board();
    outer(*m, 0U);
    bank(*m, 6U, 5U, 0xC0U);
    bank(*m, 7U, 10U, 0xC0U);
    bank(*m, 2U, 0x55U, 0xC0U);
    m->write_prg(0x8002U, 0x26U);
    m->write_prg(0x8001U, 4U); // $8002 also selects extended PRG slot zero.
    bank(*m, 0x10U, 3U);
    bank(*m, 0x11U, 0x85U);
    REQUIRE(m->read_prg(0x8000U) == 8U);
    REQUIRE(chr_marker(*m, 0x1000U) == 0x72U);
    for (Byte high : {0U, 0x40U, 0x80U, 0xC0U}) {
        bank(*m, 0x26U, 4U, high);
        REQUIRE(m->read_prg(0x8000U) == 8U);
        REQUIRE(m->read_prg(0xC000U) == 14U);
        REQUIRE(chr_marker(*m, 0x1000U) == 0x72U);
        REQUIRE(state(*m)[18U] == 1U);
    }
    m->write_prg(0x8002U, 0xC6U);
    REQUIRE(m->read_prg(0x8000U) == 14U);
    REQUIRE(m->read_prg(0xA000U) == 10U);
    REQUIRE(m->read_prg(0xC000U) == 5U);
    REQUIRE(chr_marker(*m, 0U) == 0x55U);
    bank(*m, 0x26U, 7U, 0xC0U); // D5 in $8000 cannot enter extended mode.
    REQUIRE(state(*m)[18U] == 0U);
    REQUIRE(m->read_prg(0xC000U) == 7U);
    m->write_prg(0x8002U, 0x20U);
    REQUIRE(m->read_prg(0x8000U) == 8U);
    REQUIRE(chr_marker(*m, 0x1000U) == 0x72U);
    const auto saved = state(*m);
    for (Byte index : {0U, 7U, 0x20U, 0x21U, 0x22U, 0x27U, 0x28U, 0x2FU, 0x3FU}) {
        bank(*m, index, 0xFFU);
        const auto after = state(*m);
        REQUIRE(std::equal(saved.begin() + 19, saved.end(), after.begin() + 19));
    }
}

TEST_CASE("Mapper 219 decodes outer-register aliases independently of MMC3 RAM gates",
          "[mappers][mapper219]") {
    auto m = board();
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        const auto addr = static_cast<Address>(a);
        const bool decoded = (a & 0x5002U) == 0x5002U;
        REQUIRE(m->maps_expansion(addr) == decoded);
        REQUIRE(m->maps_prg(addr) == (a >= 0x6000U && decoded));
        outer(*m, 3U);
        m->write_prg(0xA001U, static_cast<Byte>(a));
        const auto before = state(*m);
        const auto value = static_cast<Byte>(a >> 2U);
        if (a < 0x6000U)
            m->write_expansion(addr, value);
        else
            m->write_prg(addr, value);
        const unsigned expected = !decoded       ? 3U
                                  : a % 2U == 0U ? 2U + value % 2U
                                                 : 1U + ((value / 32U) % 2U) * 2U;
        REQUIRE(m->read_prg(0xE000U) == expected * 16U + 15U);
        REQUIRE(chr_marker(*m, 0U) == expected * 128U);
        if (!decoded)
            REQUIRE(state(*m) == before);
        else {
            REQUIRE(m->expansion_read_mask(addr) == 0U);
            REQUIRE(m->prg_read_mask(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 219 preserves standard mirroring and IRQ progress in either banking mode",
          "[mappers][mapper219]") {
    for (Byte extended : {0U, 0x20U}) {
        auto m = board();
        m->write_prg(0x8002U, extended);
        for (unsigned data = 0U; data < 256U; ++data) {
            m->write_prg(0xA000U, static_cast<Byte>(data));
            REQUIRE(m->mirror_mode() ==
                    (data % 2U == 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }
        m->write_prg(0xC000U, 2U);
        m->write_prg(0xC001U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        m->write_prg(0x8002U, extended ^ 0x20U);
        outer(*m, 1U);
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(m->irq_pending());
        m->write_prg(0xC000U, 0U);
        m->write_prg(0xC001U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->acknowledge_irq();
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
    }
}

TEST_CASE("Mapper 219 decodes every mode and data address alias with E003 priority",
          "[mappers][mapper219]") {
    auto actual = board();
    auto expected = board();
    for (Byte initial : {0U, 0x20U})
        for (unsigned a = 0x8000U; a < 0xA000U; ++a) {
            for (auto* m : {actual.get(), expected.get()}) {
                m->reset();
                m->write_prg(0x8002U, initial);
            }
            const auto value = static_cast<Byte>(a >> 3U);
            actual->write_prg(static_cast<Address>(a), value);
            expected->write_prg(static_cast<Address>(0x8000U + a % 4U), value);
            REQUIRE(state(*actual) == state(*expected));
        }
}

TEST_CASE("Mapper 219 extended outputs wrap smaller ROMs after pin decoding",
          "[mappers][mapper219]") {
    constexpr std::array<Byte, 4> kPages = {3U, 5U, 7U, 11U};
    for (std::size_t p : {32768UZ, 98304UZ, 270336UZ})
        for (std::size_t c : {8192UZ, 24576UZ, 263168UZ}) {
            auto m = board(false, p, c);
            m->write_prg(0x8002U, 0x20U);
            for (Byte block = 0U; block < 4U; ++block) {
                outer(*m, block);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    bank(*m,
                         static_cast<Byte>(0x26U - slot),
                         static_cast<Byte>(kReverse[kPages[slot]] * 4U));
                    REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                            (block * 16U + kPages[slot]) % (p / 8192U));
                }
                bank(*m, 8U, 7U);
                bank(*m, 9U, 0xFFU);
                bank(*m, 0x1DU, 0xFFU);
                m->write_chr(0U, 0U);
                REQUIRE(chr_marker(*m, 0U) == (block * 128U + 127U) % (c / 1024U));
                REQUIRE(chr_marker(*m, 0x1C00U) == (block * 128U + 127U) % (c / 1024U));
            }
        }
}

TEST_CASE("Mapper 219 snapshots retain both register files and validate every canonical field",
          "[mappers][mapper219][state]") {
    for (bool extended : {false, true}) {
        auto m = board();
        const auto initial = state(*m);
        bank(*m, 6U, 5U, 0xC0U);
        bank(*m, 2U, 0x55U, 0xC0U);
        m->write_prg(0x8002U, 0x26U);
        m->write_prg(0x8001U, 4U);
        bank(*m, 0x10U, 3U);
        bank(*m, 0x11U, 0x85U);
        if (!extended)
            m->write_prg(0x8002U, 0xC6U);
        outer(*m, 1U);
        m->write_prg(0xA000U, 1U);
        m->write_prg(0xC000U, 2U);
        m->write_prg(0xC001U, 0U);
        m->write_prg(0xE001U, 0U);
        m->clock_irq_counter();
        m->clock_irq_counter();
        const auto saved = state(*m);
        REQUIRE(saved.size() == 33U);
        auto copy = board();
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
        for (std::size_t n = 0U; n < saved.size(); ++n) {
            StateReader truncated(std::span(saved).first(n));
            m->load_state(truncated);
            REQUIRE_FALSE(truncated.ok());
            REQUIRE(state(*m) == initial);
        }
        for (const auto [offset, value] :
             std::array<std::pair<std::size_t, Byte>, 18>{{{0U, 8U},
                                                           {1U, 1U},
                                                           {2U, 128U},
                                                           {8U, 16U},
                                                           {9U, 16U},
                                                           {10U, 2U},
                                                           {13U, 4U},
                                                           {14U, 1U},
                                                           {15U, 0xFFU},
                                                           {16U, 0xFFU},
                                                           {17U, 64U},
                                                           {18U, 2U},
                                                           {19U, 8U},
                                                           {20U, 4U},
                                                           {21U, 16U},
                                                           {24U, 16U},
                                                           {25U, 128U},
                                                           {32U, 128U}}}) {
            auto bad = saved;
            bad[offset] = value;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == initial);
        }
        if (!extended) {
            auto bad = saved;
            bad[17U] ^= 1U; // Normal-mode selectors must agree.
            StateReader inconsistent(bad);
            m->load_state(inconsistent);
            REQUIRE_FALSE(inconsistent.ok());
        }
        StateReader restore(saved);
        m->load_state(restore);
        REQUIRE(restore.ok());
        m->reset();
        REQUIRE(state(*m) == initial);
        REQUIRE(m->battery_ram().empty());
        REQUIRE_FALSE(m->has_expansion_audio());
    }
}

TEST_CASE("Mapper 219 rejects unknown profiles and unsupported memory geometry",
          "[mappers][mapper219]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 16U; ++kind) {
        auto h = header();
        std::size_t p = 32768U;
        std::size_t c = 8192U;
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
            p = 16384U;
            break;
        case 9U:
            p = 532480U;
            break;
        case 10U:
            --p;
            break;
        case 11U:
            c = 0U;
            break;
        case 12U:
            c = 525312U;
            break;
        case 13U:
            --c;
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
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
}

std::vector<Byte> image(bool legacy = false, bool vertical = true) {
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
    const auto set_bank = [&store](Byte index, Byte value, Byte mode = 0U) {
        store(0x8000U, static_cast<Byte>(index | mode));
        store(0x8001U, value);
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
    store(0x11U, 0U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    for (Byte block = 0U; block < 4U; ++block) {
        store(0x5002U, block & 1U);
        store(0x7003U, static_cast<Byte>((block & 2U) << 4U));
        store(0x8002U, 0U);
        for (Byte mode : {0U, 0xC0U}) {
            set_bank(6U, 0x35U, mode);
            set_bank(7U, 0x2AU, mode);
            set_bank(2U, 0xD5U, mode);
            const std::array<unsigned, 4> pages = {
                mode == 0U ? 5U : 14U, 10U, mode == 0U ? 14U : 5U, 15U};
            for (unsigned slot = 0U; slot < 4U; ++slot)
                expect(static_cast<Address>(0x9A00U + slot * 8192U),
                       static_cast<Byte>(block * 16U + pages[slot]));
            const unsigned chr = block * 128U + 0x55U;
            const auto a = static_cast<Address>(mode == 0U ? 0x1000U : 0U);
            pread(a, static_cast<Byte>(chr));
            pread(a + 1U, static_cast<Byte>(chr >> 8U));
        }
        store(0x8002U, 0xE0U);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto page = static_cast<Byte>((slot * 3U + block + 1U) % 16U);
            set_bank(static_cast<Byte>(0x26U - slot), static_cast<Byte>(kReverse[page] * 4U));
        }
        for (unsigned slot = 0U; slot < 4U; ++slot)
            expect(static_cast<Address>(0x9A00U + slot * 8192U),
                   static_cast<Byte>(block * 16U + (slot * 3U + block + 1U) % 16U));
        set_bank(0x10U, 3U);
        constexpr std::array<Byte, 8> kChrIndices = {9U, 11U, 13U, 15U, 17U, 21U, 25U, 29U};
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            set_bank(kChrIndices[slot], 0x85U);
            const unsigned chr = block * 128U + ((slot == 1U || slot == 3U) ? 0x73U : 0x72U);
            const auto a = static_cast<Address>(slot * 1024U);
            pread(a, static_cast<Byte>(chr));
            pread(a + 1U, static_cast<Byte>(chr >> 8U));
        }
        // Hidden normal registers retain their values under this compatibility model.
        store(0x8002U, 0xC6U);
        expect(0x9A00U, static_cast<Byte>(block * 16U + 14U));
        expect(0xDA00U, static_cast<Byte>(block * 16U + 5U));
        store(0xA000U, 0U);
        pwrite(0x2000U, 0x19U);
        pwrite(0x2400U, 0x27U);
        store(0xA000U, 1U);
        pread(0x2400U, 0x19U);
        store(0xA000U, 0U);
        pread(0x2400U, 0x27U);
    }
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
    // Even the final CPU window is switchable. Repeat original firmware in
    // every 8 KiB page so opcode fetches remain stable across both protocols.
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
    result[6] = static_cast<Byte>(0xB0U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0xD0U : 0xD8U;
    result.insert(result.end(), p.begin(), p.end());
    result.insert(result.end(), c.begin(), c.end());
    return result;
}
Cartridge cartridge(bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(legacy, vertical));
    REQUIRE(c);
    return std::move(*c);
}

TEST_CASE("Mapper 219 write-only outer ports preserve every CPU open-bus bit",
          "[mappers][mapper219]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (unsigned data = 0U; data < 256U; ++data)
        for (Address a : {0x4020U, 0x5000U, 0x5002U, 0x5003U, 0x6000U, 0x7002U, 0x7003U, 0x7FFFU}) {
            bus.write(a, 0xFFU);
            bus.write(0x10U, static_cast<Byte>(data));
            REQUIRE(bus.read(a) == data);
        }
}

TEST_CASE("Mapper 219 CPU fixtures verify bank protocols, CIRAM, IRQs and atomic replay",
          "[mappers][mapper219][state]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            Emulator e;
            REQUIRE(e.load_cartridge(cartridge(legacy, vertical), ""));
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
            const auto start = early.size() - tail.data().size() - 33U;
            for (std::size_t offset : {0UZ, 15UZ, 16UZ, 17UZ, 18UZ, 19UZ, 20UZ, 21UZ, 32UZ}) {
                auto bad = early;
                bad[start + 20U] = 1U; // Decode a changed valid outer bank before rejection.
                bad[start + offset] ^= 0xFFU;
                const auto before = e.save_state();
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == before);
            }
            e.reset();
            REQUIRE(e.read_cpu(0xFA00U) == 63U);
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        }
}
} // namespace
