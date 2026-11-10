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
INesHeader header(bool aux = false, bool battery = false) {
    INesHeader h{};
    h.mapper_number = 195U;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    h.chr_ram_size = 8192U;
    h.prg_ram_size = aux ? 4096U : 0U;
    h.has_battery = battery;
    h.prg_nvram_size = battery ? 8192U : 0U;
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
void command(Mapper& m, Byte value) {
    bank(m, 2U, value);
    m.write_chr(0x1000U, 0U);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("FS303 power-on RAM window moves only after a PPU write", "[mappers][fs303]") {
    auto m = board();
    bank(*m, 2U, 0x28U);
    m->write_chr(0x1000U, 0xA7U);
    REQUIRE(m->read_chr(0x1000U) == 0xA7U);
    bank(*m, 2U, 0x82U);
    REQUIRE(m->read_chr(0x1000U) == 0x82U);
    bank(*m, 2U, 0U);
    REQUIRE(m->read_chr(0x1000U) == 0U);
    command(*m, 0x82U);
    bank(*m, 2U, 0U);
    REQUIRE(m->read_chr(0x1000U) == 0xA7U);
}
TEST_CASE("FS303 optional RAM occupies the complete 5000 window", "[mappers][fs303]") {
    auto m = board(header(true));
    REQUIRE(m->maps_expansion(0x5000U));
    REQUIRE(m->maps_expansion(0x5FFFU));
    m->write_expansion(0x5000U, 0x39U);
    m->write_expansion(0x5FFFU, 0x57U);
    REQUIRE(m->read_expansion(0x5000U) == 0x39U);
    REQUIRE(m->read_expansion(0x5FFFU) == 0x57U);
}

// Measured FS303 truth table, independent of the implementation's bit decoder.
bool selected(Byte mode, unsigned page) {
    switch (mode & 0xDAU) {
    case 0x80U:
        return page >= 0x28U && page <= 0x2BU;
    case 0x82U:
        return page <= 3U;
    case 0x88U:
        return page >= 0x4CU && page <= 0x4FU;
    case 0x8AU:
        return page >= 0x64U && page <= 0x67U;
    case 0xC0U:
        return page >= 0x46U && page <= 0x47U;
    case 0xC2U:
        return page >= 0x7CU && page <= 0x7DU;
    case 0xC8U:
        return page >= 0x0AU && page <= 0x0BU;
    default:
        return false;
    }
}
void seed_ram(Mapper& m) {
    for (Byte mode : {0x82U, 0x88U}) {
        command(m, mode);
        for (unsigned i = 0U; i < 4U; ++i) {
            const auto page = static_cast<Byte>((mode == 0x82U ? 0U : 0x4CU) + i);
            bank(m, 3U, page);
            m.write_chr(0x1400U, static_cast<Byte>(0xD0U + (page & 7U)));
            m.write_chr(0x17FFU, static_cast<Byte>(0xE0U + (page & 7U)));
        }
    }
}
TEST_CASE("FS303 all control-bank aliases select the measured physical RAM pages",
          "[mappers][fs303]") {
    // Small ROM also proves that GAL decoding precedes ROM bank wrapping.
    for (std::size_t chr_size : {8192UZ, 0x40000UZ}) {
        auto m = board(header(), 98304U, chr_size);
        seed_ram(*m);
        for (unsigned control = 0x80U; control < 256U; ++control) {
            INFO(control);
            command(*m, static_cast<Byte>(control));
            for (unsigned page = 0U; page < 0x80U; ++page) {
                bank(*m, 3U, static_cast<Byte>(page));
                const unsigned expected = selected(static_cast<Byte>(control), page)
                                              ? 0xD0U + (page & 7U)
                                              : page % (chr_size / 1024U);
                REQUIRE(m->read_chr(0x1400U) == expected);
                REQUIRE(m->read_chr(0x17FFU) == (selected(static_cast<Byte>(control), page)
                                                     ? 0xE0U + (page & 7U)
                                                     : page % (chr_size / 1024U)));
            }
        }
    }
}
TEST_CASE("FS303 reads and MMC3 writes do not select a window and PPU data is ignored",
          "[mappers][fs303]") {
    auto m = board();
    seed_ram(*m);
    command(*m, 0x82U);
    for (unsigned value = 0U; value < 256U; ++value) {
        bank(*m, 2U, static_cast<Byte>(value));
        m->read_chr(0x1000U);
        bank(*m, 3U, 0U);
        REQUIRE(m->read_chr(0x1400U) == 0xD0U);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Byte cmd : {0xC8U, 0x82U}) {
            bank(*m, 2U, cmd);
            m->write_chr(0x1000U, static_cast<Byte>(value));
            bank(*m, 3U, cmd == 0xC8U ? 0xAU : 2U);
            REQUIRE(m->read_chr(0x1400U) == 0xD2U);
        }
    }
}
TEST_CASE("FS303 paired and inverted slots decode raw banks before ROM wrapping",
          "[mappers][fs303]") {
    constexpr std::array<Address, 6> kSlots = {0U, 0x800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
    auto m = board(header(), 32768U, 16384U);
    Byte control = 0x80U;
    std::array<Byte, 8> ram{};
    for (Byte mode : {0U, 0x80U})
        for (Byte reg = 0U; reg < 6U; ++reg)
            for (unsigned value = 0U; value < 256U; ++value) {
                bank(*m, reg, static_cast<Byte>(value), mode);
                const auto base = kSlots[reg] ^ (mode == 0U ? 0U : 0x1000U);
                for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                    const auto page = reg < 2U ? (value & 0xFEU) + half : value;
                    const auto addr = static_cast<Address>(base + half * 1024U + 0x3FFU);
                    REQUIRE(m->read_chr(addr) ==
                            (selected(control, page) ? ram[page & 7U] : page % 16U));
                    const auto data = static_cast<Byte>(value ^ 0xB9U);
                    m->write_chr(addr, data);
                    if (page >= 0x80U)
                        control = static_cast<Byte>(page);
                    else if (selected(control, page))
                        ram[page & 7U] = data;
                    REQUIRE(m->read_chr(addr) ==
                            (selected(control, page) ? ram[page & 7U] : page % 16U));
                }
            }
}
TEST_CASE("FS303 all RAM bytes survive mode changes, reset, and WRAM gates", "[mappers][fs303]") {
    auto m = board(header(true, true));
    for (Byte cmd : {0x82U, 0x88U}) {
        command(*m, cmd);
        for (unsigned page = 0U; page < 4U; ++page) {
            bank(*m, 3U, static_cast<Byte>((cmd == 0x82U ? 0U : 0x4CU) + page));
            for (unsigned i = 0U; i < 1024U; ++i)
                m->write_chr(static_cast<Address>(0x1400U + i), static_cast<Byte>(page * 37U + i));
        }
    }
    for (Byte gate : {0U, 0x80U, 0xC0U}) {
        m->write_prg(0xA001U, gate);
        for (Byte cmd : {0x80U, 0x8AU}) {
            command(*m, cmd);
            for (unsigned page = 0U; page < 4U; ++page) {
                bank(*m, 3U, static_cast<Byte>((cmd == 0x80U ? 0x28U : 0x64U) + page));
                for (unsigned i = 0U; i < 1024U; ++i)
                    REQUIRE(m->read_chr(static_cast<Address>(0x1400U + i)) ==
                            static_cast<Byte>(page * 37U + i));
            }
        }
    }
    command(*m, 0xCAU);
    m->reset();
    bank(*m, 3U, 0x28U);
    REQUIRE(m->read_chr(0x17FFU) == 0xFFU);
    bank(*m, 3U, 0U);
    m->write_chr(0x17FFU, 0x31U); // ROM write with bit 7 clear cannot select a new window.
    REQUIRE(m->read_chr(0x17FFU) == 0U);
    command(*m, 0x82U);
    REQUIRE(m->read_chr(0x17FFU) == 0xFFU);
}
TEST_CASE("FS303 auxiliary RAM is independent of MMC3 WRAM gates and battery memory",
          "[mappers][fs303]") {
    for (bool persistent : {false, true}) {
        auto m = board(header(true, persistent));
        for (unsigned addr = 0x4000U; addr < 0x8000U; ++addr)
            REQUIRE(m->maps_expansion(static_cast<Address>(addr)) ==
                    (addr >= 0x5000U && addr < 0x6000U));
        for (unsigned i = 0U; i < 4096U; ++i)
            m->write_expansion(static_cast<Address>(0x5000U + i), static_cast<Byte>(i ^ (i >> 8U)));
        for (Byte gate : {0U, 0xC0U, 0x80U}) {
            m->write_prg(0xA001U, gate);
            for (unsigned i = 0U; i < 4096U; ++i) {
                const auto addr = static_cast<Address>(0x5000U + i);
                REQUIRE(m->expansion_read_mask(addr) == 0xFFU);
                REQUIRE(m->read_expansion(addr) == static_cast<Byte>(i ^ (i >> 8U)));
            }
            m->write_expansion(0x5000U, 0x39U);
            REQUIRE(m->read_expansion(0x5000U) == 0x39U);
            m->write_expansion(0x5000U, 0U);
        }
        m->write_expansion(0x5FFFU, 0x73U);
        m->write_prg(0x6000U, 0x59U);
        m->set_battery_ram(std::vector<Byte>(8192U, 0xA7U));
        REQUIRE(m->battery_ram().size() == (persistent ? 8192U : 0U));
        REQUIRE(m->read_expansion(0x5FFFU) == 0x73U);
        REQUIRE(m->maps_prg(0x6000U) == persistent);
        if (persistent) {
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
            m->write_prg(0xA001U, 0xC0U);
            m->write_prg(0x6000U, 0U);
            REQUIRE(m->read_prg(0x6000U) == 0xA7U);
            m->write_prg(0xA001U, 0U);
            REQUIRE_FALSE(m->maps_prg(0x6000U));
        }
        m->reset();
        REQUIRE(m->read_expansion(0x5FFFU) == 0x73U);
    }
}
TEST_CASE("FS303 snapshots retain modes, hidden RAM, IRQs and immutable RAM population",
          "[mappers][fs303][state]") {
    for (bool aux : {false, true})
        for (bool persistent : {false, true}) {
            const auto h = header(aux, persistent);
            auto m = board(h);
            seed_ram(*m);
            command(*m, 0xC8U);
            bank(*m, 3U, 0xAU, 0xC0U);
            m->write_expansion(0x5FFFU, 0x73U);
            m->write_prg(0x6000U, 0x59U);
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
            REQUIRE(copy->read_chr(0x400U) == 0xD2U);
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
            if (aux)
                REQUIRE(copy->read_expansion(0x5FFFU) == 0x73U);
            if (persistent)
                REQUIRE(copy->read_prg(0x6000U) == 0x59U);
            for (Byte cmd : {0x82U, 0x88U}) {
                command(*copy, cmd);
                for (unsigned i = 0U; i < 4U; ++i) {
                    const auto page = static_cast<Byte>((cmd == 0x82U ? 0U : 0x4CU) + i);
                    bank(*copy, 3U, page);
                    REQUIRE(copy->read_chr(0x17FFU) == 0xE0U + (page & 7U));
                }
            }
            for (auto incompatible : {header(!aux, persistent), header(aux, !persistent)}) {
                auto other = board(incompatible);
                StateReader cross(saved);
                other->load_state(cross);
                REQUIRE_FALSE(cross.ok());
            }
            constexpr std::size_t kTag = 16399U;
            for (const auto size : {0UZ, kTag, saved.size() - 1U}) {
                StateReader truncated(std::span(saved).first(size));
                copy->load_state(truncated);
                REQUIRE_FALSE(truncated.ok());
            }
            for (std::size_t offset : {kTag, kTag + 1U, kTag + 2U, kTag + 3U}) {
                auto bad = saved;
                bad[offset] ^= 0xFFU;
                StateReader malformed(bad);
                copy->load_state(malformed);
                REQUIRE_FALSE(malformed.ok());
                copy->reset();
                if (aux) {
                    copy->write_expansion(0x5FFFU, 0x39U);
                    REQUIRE(copy->read_expansion(0x5FFFU) == 0x39U);
                }
            }
        }
}
TEST_CASE("FS303 validates memory metadata and bounded ROM geometry", "[mappers][fs303]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 19U; ++kind) {
        auto h = header();
        std::size_t p = 32768U, c = 8192U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.chr_ram_size = 0U;
            break;
        case 2U:
            h.chr_ram_size = 32768U;
            break;
        case 3U:
            h.chr_nvram_size = 8192U;
            break;
        case 4U:
            h.prg_ram_size = 2048U;
            break;
        case 5U:
            h.prg_ram_size = 16384U;
            break;
        case 6U:
            h.prg_nvram_size = 4096U;
            h.has_battery = true;
            break;
        case 7U:
            h.prg_nvram_size = 8192U;
            break;
        case 8U:
            h.has_battery = true;
            break;
        case 9U:
            h.prg_ram_size = h.prg_nvram_size = 8192U;
            h.has_battery = true;
            break;
        case 10U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 11U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 12U:
            p = 16384U;
            break;
        case 13U:
            p = 0x82000U;
            break;
        case 14U:
            p = 32769U;
            break;
        case 15U:
            c = 0U;
            break;
        case 16U:
            c = 0x40400U;
            break;
        case 17U:
            c = 4096U;
            break;
        case 18U:
            c = 8193U;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(MapperRegistry::instance().create(h, banks(p, 8192U), banks(c, 1024U)));
    }
    for (bool legacy : {false, true})
        for (bool persistent : {false, true}) {
            auto h = header(false, persistent);
            h.is_nes2 = !legacy;
            if (legacy) {
                h.chr_ram_size = 0U;
                h.prg_ram_size = 8192U; // Ignore iNES's implied default allocation.
            }
            auto m = board(h, 98304U, 196608U);
            REQUIRE(m->maps_prg(0x6000U) == persistent);
            REQUIRE_FALSE(m->maps_expansion(0x5000U));
        }
    auto h = header();
    h.prg_ram_size = 8192U;
    auto m = board(h);
    m->write_prg(0x6000U, 0x73U);
    REQUIRE(m->read_prg(0x6000U) == 0x73U);
    REQUIRE(m->battery_ram().empty());
    REQUIRE_FALSE(m->maps_expansion(0x5000U));
}

TEST_CASE("FS303 MMC3 aliases retain six PRG bits, fixed banks and H/V control",
          "[mappers][fs303]") {
    for (std::size_t size : {32768UZ, 98304UZ, 0x80000UZ}) {
        auto m = board(header(), size);
        const auto count = size / 8192U;
        for (Byte mode : {0U, 0x40U})
            for (unsigned value = 0U; value < 256U; ++value) {
                m->write_prg(0x9FFEU, static_cast<Byte>(6U | mode));
                m->write_prg(0x9FFFU, static_cast<Byte>(value));
                bank(*m, 7U, static_cast<Byte>(value ^ 0x35U), mode);
                const auto r6 = (value & 63U) % count;
                REQUIRE(m->read_prg(0x8000U) == (mode == 0U ? r6 : count - 2U));
                REQUIRE(m->read_prg(0xA000U) == ((value ^ 0x35U) & 63U) % count);
                REQUIRE(m->read_prg(0xC000U) == (mode == 0U ? count - 2U : r6));
                REQUIRE(m->read_prg(0xE000U) == count - 1U);
            }
        m->write_prg(0xBFFEU, 1U);
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        m->write_prg(0xBFFEU, 0U);
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        m->reset();
        REQUIRE(m->read_prg(0x8000U) == 0U);
        REQUIRE(m->read_prg(0xC000U) == count - 2U);
    }
}

std::vector<Byte> image(bool legacy, bool aux, bool persistent, bool volatile_base = false) {
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
    const auto set_bank = [&store](Byte reg, Byte v, Byte mode = 0U) {
        store(0x8000U, static_cast<Byte>(reg | mode));
        store(0x8001U, v);
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
    const auto select = [&set_bank, &pwrite](Byte v) {
        set_bank(2U, v);
        pwrite(0x1000U, 0xFFU);
    };
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    pwrite(0U, 0U);
    pread(0U, 0x5DU);
    set_bank(6U, 35U);
    set_bank(7U, 37U);
    expect(0x9A00U, 35U);
    expect(0xBA00U, 37U);
    expect(0xDA00U, 62U);
    expect(0xFA00U, 63U);
    set_bank(6U, 35U, 0x40U);
    expect(0x9A00U, 62U);
    expect(0xDA00U, 35U);
    set_bank(3U, 0x28U);
    pwrite(0x1400U, 0xD0U);
    set_bank(2U, 0x82U);
    pread(0x1000U, 0x82U); // Reading the control bank must leave the power-on window.
    pread(0x1400U, 0xD0U);
    pwrite(0x1000U, 0U);
    set_bank(3U, 0U);
    pread(0x1400U, 0xD0U);
    for (Byte cmd : {0x82U, 0x88U}) {
        select(cmd);
        for (unsigned i = 0U; i < 4U; ++i) {
            const auto page = static_cast<Byte>((cmd == 0x82U ? 0U : 0x4CU) + i);
            set_bank(3U, page);
            pwrite(0x1400U, static_cast<Byte>(0xD0U + (page & 7U)));
        }
    }
    for (Byte cmd : {0x80U, 0x82U, 0x88U, 0x8AU, 0xC0U, 0xC2U, 0xC8U}) {
        select(cmd);
        const Byte first = cmd == 0x80U   ? 0x28U
                           : cmd == 0x82U ? 0U
                           : cmd == 0x88U ? 0x4CU
                           : cmd == 0x8AU ? 0x64U
                           : cmd == 0xC0U ? 0x46U
                           : cmd == 0xC2U ? 0x7CU
                                          : 0xAU;
        set_bank(0U, static_cast<Byte>(first | 1U), 0x80U);
        pread(0x1000U, static_cast<Byte>(0xD0U + (first & 7U)));
        pread(0x1400U, static_cast<Byte>(0xD1U + (first & 7U)));
        set_bank(3U, first, 0x80U);
        pread(0x400U, static_cast<Byte>(0xD0U + (first & 7U)));
    }
    for (Byte cmd : {0xCAU, 0x90U, 0xD0U}) {
        select(cmd);
        set_bank(3U, 0U);
        pwrite(0x1400U, 0U);
        pread(0x1400U, 0x5DU);
    }
    select(0x82U);
    set_bank(3U, 0U);
    pread(0x1400U, 0xD0U);
    for (Byte gate : {0U, 0xC0U, 0x80U}) {
        store(0xA001U, gate);
        pwrite(0x1400U, gate);
        pread(0x1400U, gate);
        if (aux) {
            store(0x5000U, gate);
            store(0x5FFFU, static_cast<Byte>(gate ^ 0x73U));
            expect(0x5000U, gate);
            expect(0x5FFFU, static_cast<Byte>(gate ^ 0x73U));
        }
    }
    if (persistent || volatile_base) {
        store(0x6000U, 0x59U);
        expect(0x6000U, 0x59U);
        store(0xA001U, 0xC0U);
        store(0x6000U, 0U);
        expect(0x6000U, 0x59U);
        store(0xA001U, 0x80U);
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
    if (persistent)
        store(0x6000U, 0xA7U);
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
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 32U;
    rom[5] = 32U;
    rom[6] = static_cast<Byte>(0x31U | (persistent ? 2U : 0U));
    rom[7] = legacy ? 0xC0U : 0xC8U;
    rom[10] = legacy ? 0U
                     : static_cast<Byte>((persistent ? 0x70U : 0U) | (aux             ? 6U
                                                                      : volatile_base ? 7U
                                                                                      : 0U));
    rom[11] = legacy ? 0U : 7U;
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(bool legacy, bool aux, bool persistent, bool volatile_base = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(legacy, aux, persistent, volatile_base));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("FS303 CPU open bus and auxiliary RAM drive the expected CPU bits", "[mappers][fs303]") {
    for (bool aux : {false, true}) {
        auto c = cartridge(false, aux, false);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        for (Address a : {0x5000U, 0x5800U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            const bool mapped = aux && a < 0x6000U;
            if (mapped)
                bus.write(a, 0x73U);
            for (Byte value : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 255U}) {
                bus.write(0x10U, value);
                REQUIRE(bus.read(a) == (mapped ? 0x73U : value));
            }
        }
    }
}
TEST_CASE("FS303 original CPU programs check banking, RAM, IRQs, replay and atomic rejection",
          "[mappers][fs303][state]") {
    for (unsigned profile = 0U; profile < 7U; ++profile) {
        const bool legacy = profile < 2U;
        const bool aux = profile == 4U || profile == 5U;
        const bool persistent = profile == 1U || profile == 3U || profile == 5U;
        const bool volatile_base = profile == 6U;
        INFO(profile);
        auto h = header(aux, persistent);
        if (volatile_base)
            h.prg_ram_size = 8192U;
        const auto payload_size = state(*board(h)).size();
        Emulator e;
        REQUIRE(e.load_cartridge(cartridge(legacy, aux, persistent, volatile_base), ""));
        e.reset();
        e.step_frame();
        REQUIRE(e.read_cpu(0x12U) == 0xC7U);
        REQUIRE(e.read_cpu(0x11U) >= 3U);
        if (persistent)
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
        const auto mapper_start = early.size() - tail.data().size() - payload_size;
        for (std::size_t offset : {16399UZ, 16401UZ, 16402UZ}) {
            auto bad = early;
            bad[mapper_start + 8192U + 13U] ^= 0xA5U;
            bad[mapper_start + offset] ^= 0xFFU;
            const auto before = e.save_state();
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE((e.save_state() == before));
        }
    }
}
} // namespace
