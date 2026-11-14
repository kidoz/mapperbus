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
TEST_CASE("Mapper 228 registers the Action 52 board", "[mappers][mapper228]") {
    register_builtin_mappers();
    INesHeader h{};
    h.mapper_number = 228U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Horizontal;
    auto m = MapperRegistry::instance().create(
        h, std::vector<Byte>(1572864U), std::vector<Byte>(524288U));
    REQUIRE(m);
}

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 228U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}
std::vector<Byte> rom(std::size_t pages, std::size_t page_size) {
    std::vector<Byte> data(pages * page_size);
    for (std::size_t a = 0U; a < data.size(); ++a)
        data[a] = static_cast<Byte>(a / page_size);
    return data;
}
std::unique_ptr<Mapper> board(bool packed = true,
                              bool legacy = false,
                              std::size_t pages = 32U,
                              std::size_t chr_pages = 64U) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(
        header(legacy), rom(packed ? 96U : pages, 16384U), rom(chr_pages, 8192U));
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
Address command(
    unsigned chip, unsigned page, bool small, unsigned chr = 0U, bool horizontal = false) {
    return static_cast<Address>(0x8000U + chip * 2048U + page * 64U + (small ? 32U : 0U) +
                                chr / 4U + (horizontal ? 8192U : 0U));
}
constexpr std::array<unsigned, 4> kPackedBase{0U, 32U, 0U, 64U};
TEST_CASE("Mapper 228 separates chip enables from aligned and mirrored PRG pages",
          "[mappers][mapper228]") {
    for (bool legacy : {false, true})
        for (bool packed : {false, true}) {
            auto m = board(packed, legacy);
            for (unsigned chip = 0U; chip < 4U; ++chip)
                for (unsigned page = 0U; page < 32U; ++page)
                    for (bool small : {false, true})
                        for (bool horizontal : {false, true}) {
                            m->write_prg(command(chip, page, small, 0U, horizontal), 0U);
                            const unsigned base = packed ? kPackedBase[chip] : 0U;
                            const unsigned low = base + (small ? page : (page / 2U) * 2U);
                            const unsigned high = base + (small ? page : (page / 2U) * 2U + 1U);
                            const bool hole = packed && chip == 2U;
                            REQUIRE(m->prg_read_mask(0x8000U) == (hole ? 0U : 0xFFU));
                            REQUIRE(m->prg_read_mask(0xFFFFU) == (hole ? 0U : 0xFFU));
                            REQUIRE(m->read_prg(0x8000U) == (hole ? 0U : low));
                            REQUIRE(m->read_prg(0xBFFFU) == (hole ? 0U : low));
                            REQUIRE(m->read_prg(0xC000U) == (hole ? 0U : high));
                            REQUIRE(m->read_prg(0xFFFFU) == (hole ? 0U : high));
                            REQUIRE(m->mirror_mode() ==
                                    (horizontal ? MirrorMode::Horizontal : MirrorMode::Vertical));
                        }
        }
}
TEST_CASE("Mapper 228 latches every connected pin and ignores A4 A14 and upper data bits",
          "[mappers][mapper228]") {
    for (bool packed : {false, true}) {
        auto m = board(packed);
        for (unsigned a = 0x8000U; a <= 0xFFFFU; ++a) {
            const unsigned chip = a / 2048U % 4U;
            const unsigned page = a / 64U % 32U;
            const bool small = a / 32U % 2U != 0U;
            const unsigned base = packed ? kPackedBase[chip] : 0U;
            const unsigned low = base + (small ? page : (page / 2U) * 2U);
            const unsigned high = base + (small ? page : (page / 2U) * 2U + 1U);
            const bool hole = packed && chip == 2U;
            m->write_prg(static_cast<Address>(a), static_cast<Byte>(a));
            REQUIRE(m->prg_read_mask(0x8123U) == (hole ? 0U : 0xFFU));
            REQUIRE(m->read_prg(0x8123U) == (hole ? 0U : low));
            REQUIRE(m->read_prg(0xC123U) == (hole ? 0U : high));
            REQUIRE(m->read_chr(0x123U) == (a % 16U) * 4U + a % 4U);
            REQUIRE(m->mirror_mode() ==
                    (a / 8192U % 2U != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        for (Address a : {0x8000U, 0x87EFU, 0x9000U, 0x9FEFU, 0xBFEFU})
            for (unsigned value = 0U; value < 256U; ++value) {
                m->write_prg(a, static_cast<Byte>(value % 4U));
                const auto saved = state(*m);
                m->write_prg(static_cast<Address>(a | 0x4010U), static_cast<Byte>(value));
                REQUIRE(state(*m) == saved);
            }
    }
}
TEST_CASE("Mapper 228 CHR pins are independent of PRG chip holes and ROM is read-only",
          "[mappers][mapper228]") {
    for (bool packed : {false, true}) {
        auto m = board(packed);
        for (unsigned chip = 0U; chip < 4U; ++chip)
            for (unsigned chr = 0U; chr < 64U; ++chr) {
                m->write_prg(command(chip, 17U, true, chr, true), static_cast<Byte>(chr % 4U));
                REQUIRE(m->read_chr(0U) == chr);
                REQUIRE(m->read_chr(0x1FFFU) == chr);
            }
        for (unsigned a = 0U; a < 8192U; ++a) {
            m->write_chr(static_cast<Address>(a), 0xA7U);
            REQUIRE(m->read_chr(static_cast<Address>(a)) == 63U);
        }
        m->reset();
        REQUIRE(m->read_chr(0U) == 0U);
        REQUIRE(m->read_prg(0x8000U) == 0U);
        REQUIRE(m->read_prg(0xFFFFU) == 1U);
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    }
}
TEST_CASE("Mapper 228 wraps smaller single chips without inventing additional chip enables",
          "[mappers][mapper228]") {
    for (std::size_t pages : {1UZ, 3UZ, 16UZ, 32UZ})
        for (std::size_t chr_pages : {1UZ, 3UZ, 64UZ}) {
            auto m = board(false, false, pages, chr_pages);
            for (unsigned chip = 0U; chip < 4U; ++chip)
                for (unsigned page = 0U; page < 32U; ++page)
                    for (bool small : {false, true}) {
                        const unsigned chr = page * 2U + (small ? 1U : 0U);
                        m->write_prg(command(chip, page, small, chr), static_cast<Byte>(chr % 4U));
                        const unsigned low = small ? page : (page / 2U) * 2U;
                        const unsigned high = small ? page : (page / 2U) * 2U + 1U;
                        REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
                        REQUIRE(m->read_prg(0x8123U) == low % pages);
                        REQUIRE(m->read_prg(0xC123U) == high % pages);
                        REQUIRE(m->read_chr(0x123U) == chr % chr_pages);
                    }
        }
}
TEST_CASE("Mapper 228 snapshots reject other populations, noncanonical latches and truncation",
          "[mappers][mapper228][state]") {
    for (bool packed : {false, true}) {
        auto m = board(packed);
        const auto reset = state(*m);
        m->write_prg(0xBFEFU, 0xFFU);
        const auto saved = state(*m);
        REQUIRE(saved.size() == 5U);
        auto copy = board(packed);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        for (std::size_t n = 0U; n < saved.size(); ++n) {
            StateReader truncated(std::span(saved).first(n));
            m->load_state(truncated);
            REQUIRE_FALSE(truncated.ok());
            REQUIRE(state(*m) == reset);
        }
        for (const auto [offset, value] :
             std::array<std::pair<std::size_t, Byte>, 6>{{{0U, static_cast<Byte>(packed ? 0U : 1U)},
                                                          {1U, 0U},
                                                          {2U, 0xFFU},
                                                          {3U, 0x7FU},
                                                          {3U, 0xBFU},
                                                          {4U, 4U}}}) {
            auto bad = saved;
            bad[offset] = value;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == reset);
        }
        StateReader restore(saved);
        m->load_state(restore);
        REQUIRE(restore.ok());
        REQUIRE(state(*m) == saved);
        auto other = board(!packed);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
    }
}
TEST_CASE("Mapper 228 rejects unknown populations, RAM, battery and alternate CIRAM",
          "[mappers][mapper228]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 18U; ++kind) {
        auto h = header();
        std::size_t p = 524288U, c = 524288U;
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
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 8U:
            p = 0U;
            break;
        case 9U:
            p = 8192U;
            break;
        case 10U:
            --p;
            break;
        case 11U:
            p = 1048576U;
            break;
        case 12U:
            p = 2097152U;
            break;
        case 13U:
            c = 0U;
            break;
        case 14U:
            c = 4096U;
            break;
        case 15U:
            --c;
            break;
        case 16U:
            c = 532480U;
            break;
        case 17U:
            h = header(true);
            h.has_battery = true;
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
    m->clock_cpu();
    m->clock_irq_counter();
    m->clock_audio();
    REQUIRE_FALSE(m->irq_pending());
}

std::vector<Byte> image(bool packed = true, bool legacy = false, bool vertical = true) {
    auto p = rom(packed ? 96U : 32U, 16384U);
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
    for (unsigned chip : {0U, 1U, 2U, 3U}) {
        if (packed && chip == 2U)
            continue;
        for (unsigned page : {0U, 1U, 17U, 31U})
            for (bool small : {false, true}) {
                store(command(chip, page, small), 0xA7U);
                const unsigned base = packed ? kPackedBase[chip] : 0U;
                const unsigned low = base + (small ? page : page / 2U * 2U);
                const unsigned high = base + (small ? page : page / 2U * 2U + 1U);
                expect(0x9A00U, static_cast<Byte>(low));
                expect(0xFA00U, static_cast<Byte>(high));
            }
    }
    for (unsigned chr = 0U; chr < 64U; ++chr) {
        store(command(3U, 31U, true, chr), static_cast<Byte>(chr % 4U));
        pread(0x123U, static_cast<Byte>(chr));
        pwrite(0x123U, 0xA7U);
        pread(0x123U, static_cast<Byte>(chr));
    }
    store(0x8000U, 0U);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0xA000U, 0U);
    pread(0x2400U, 0x19U);
    store(0x8000U, 0U);
    pread(0x2400U, 0x27U);
    if (packed) {
        // Keep opcode fetches on CPU RAM while no cartridge PRG chip drives
        // the bus. Absolute LDA's last operand byte supplies the retained bus.
        const auto continuation = static_cast<Address>(0xE000U + code.size() + 138U);
        const std::array<Byte, 27> routine{0xA9U,
                                           0U,
                                           0x8DU,
                                           0U,
                                           0x90U,
                                           0xADU,
                                           0U,
                                           0x9AU,
                                           0xC9U,
                                           0x9AU,
                                           0xD0U,
                                           0xFEU,
                                           0xADU,
                                           0U,
                                           0xFAU,
                                           0xC9U,
                                           0xFAU,
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
    store(command(3U, 31U, true, 63U, true), 3U);
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    for (std::size_t base = 0U; base < p.size(); base += 16384U) {
        std::ranges::copy(code, p.begin() + static_cast<std::ptrdiff_t>(base + 0x2000U));
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
    data[4] = packed ? 96U : 32U;
    data[5] = 64U;
    data[6] = static_cast<Byte>(0x40U | (vertical ? 1U : 0U));
    data[7] = legacy ? 0xE0U : 0xE8U;
    data.insert(data.end(), p.begin(), p.end());
    const auto chr = rom(64U, 8192U);
    data.insert(data.end(), chr.begin(), chr.end());
    return data;
}
Cartridge cartridge(bool packed = true, bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(packed, legacy, vertical));
    REQUIRE(c);
    return std::move(*c);
}
TEST_CASE("Mapper 228 CPU bus floats at absent chips and has no rumored four-nibble RAM",
          "[mappers][mapper228]") {
    for (bool packed : {false, true}) {
        auto c = cartridge(packed);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        bus.write(0x9000U, 0U);
        for (unsigned value = 0U; value < 256U; ++value) {
            for (Address a : {0x4020U, 0x4023U, 0x5FF0U, 0x5FF1U, 0x5FF2U, 0x6000U, 0x7FFFU}) {
                bus.write(a, 0xA7U);
                bus.write(0x10U, static_cast<Byte>(value));
                REQUIRE(bus.read(a) == value);
            }
            for (Address a : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
                bus.write(0x10U, static_cast<Byte>(value));
                const Byte expected = a < 0xC000U ? 0U : 1U;
                // Avoid vector/firmware locations for mapped single-chip data.
                const auto probe = static_cast<Address>((a & 0xC000U) | 0x1A00U);
                REQUIRE(bus.read(probe) == (packed ? value : expected));
            }
        }
        // A write must reconnect PRG even when reads are currently undriven.
        bus.write(0x9800U, 0U);
        REQUIRE(bus.read(0x9A00U) == (packed ? 64U : 0U));
        auto m = board(packed);
        const auto saved = state(*m);
        for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
            const auto addr = static_cast<Address>(a);
            REQUIRE_FALSE(m->maps_prg(addr));
            REQUIRE_FALSE(m->maps_expansion(addr));
            m->write_prg(addr, 0xFFU);
            m->write_expansion(addr, 0xFFU);
        }
        REQUIRE(state(*m) == saved);
    }
}
TEST_CASE("Mapper 228 CPU fixtures verify banked fetches, PPUDATA, chip holes, reset and replay",
          "[mappers][mapper228][state]") {
    for (bool packed : {false, true})
        for (bool legacy : {false, true})
            for (bool vertical : {false, true}) {
                Emulator e;
                REQUIRE(e.load_cartridge(cartridge(packed, legacy, vertical), ""));
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
                const auto start = early.size() - tail.data().size() - 5U;
                for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 5>{
                         {{0U, static_cast<Byte>(packed ? 0U : 1U)},
                          {1U, 0U},
                          {2U, 0xFFU},
                          {3U, 0x7FU},
                          {4U, 4U}}}) {
                    auto bad = early;
                    bad[start + offset] = value;
                    const auto before = e.save_state();
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                }
                auto truncated = early;
                truncated.resize(start + 4U);
                const auto before = e.save_state();
                REQUIRE_FALSE(e.load_state(truncated));
                REQUIRE(e.save_state() == before);
                e.reset();
                REQUIRE(e.read_cpu(0x9A00U) == 0U);
                REQUIRE(e.read_cpu(0xFA00U) == 1U);
                const auto reset = e.save_state();
                REQUIRE(reset[start + 2U] == 0U);
                REQUIRE(reset[start + 3U] == 0U);
                REQUIRE(reset[start + 4U] == 0U);
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            }
}
} // namespace
