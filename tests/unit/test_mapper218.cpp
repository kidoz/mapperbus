#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
std::vector<Byte> image(Byte wiring, bool legacy = false) {
    std::vector<Byte> data(16U + 32768U, 0xEAU);
    std::fill_n(data.begin(), 16U, 0U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = 2U;
    data[6] = static_cast<Byte>(0xA0U | wiring);
    data[7] = legacy ? 0xD0U : 0xD8U;
    return data;
}
Cartridge cartridge(Byte wiring, bool legacy = false) {
    register_builtin_mappers();
    auto c = Cartridge::from_data(image(wiring, legacy));
    REQUIRE(c);
    return std::move(*c);
}
void address(Ppu& p, Address a) {
    (void)p.read_register(2U);
    p.write_register(6U, static_cast<Byte>(a >> 8U));
    p.write_register(6U, static_cast<Byte>(a));
}
void write(Ppu& p, Address a, Byte v) {
    address(p, a);
    p.write_register(7U, v);
}
Byte read(Ppu& p, Address a) {
    address(p, a);
    (void)p.read_register(7U);
    address(p, a);
    return p.read_register(7U);
}
unsigned physical(Byte wiring, Address addr) {
    const unsigned select = wiring == 0U   ? 0x800U
                            : wiring == 1U ? 0x400U
                            : wiring == 8U ? 0x1000U
                                           : 0x2000U;
    return (addr & 0x3FFU) + ((addr & select) != 0U ? 1024U : 0U);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
INesHeader header(Byte wiring, bool legacy = false) {
    auto h = parse_ines_header(image(wiring, legacy));
    REQUIRE(h);
    return *h;
}
std::unique_ptr<Mapper> board(Byte wiring, bool legacy = false, std::size_t size = 32768U) {
    register_builtin_mappers();
    std::vector<Byte> prg(size);
    for (std::size_t i = 0U; i < size; ++i)
        prg[i] = static_cast<Byte>((i / 256U) ^ i);
    auto m = MapperRegistry::instance().create(header(wiring, legacy), std::move(prg), {});
    REQUIRE(m);
    return std::move(*m);
}
TEST_CASE("Mapper 218 shares console CIRAM across all four pattern wirings",
          "[mappers][mapper218]") {
    constexpr std::array<Byte, 4> kWiring = {0U, 1U, 8U, 9U};
    constexpr std::array<Address, 4> kPattern = {0x0800U, 0x0400U, 0x1000U, 0x0000U};
    constexpr std::array<Address, 4> kName = {0x2800U, 0x2400U, 0x2000U, 0x2000U};
    for (unsigned i = 0U; i < kWiring.size(); ++i)
        for (bool legacy : {false, true}) {
            auto c = cartridge(kWiring[i], legacy);
            Ppu p;
            p.connect_cartridge(&c);
            p.reset();
            write(p, kPattern[i] + 0x123U, 0xA7U);
            const auto alias = static_cast<Address>((i == 2U ? 0x3000U : kName[i]) + 0x123U);
            REQUIRE(read(p, alias) == (i == 3U ? 0U : 0xA7U));
            if (i == 3U) {
                write(p, 0x2123U, 0x5AU);
                REQUIRE(read(p, 0x0123U) == 0xA7U);
                REQUIRE(read(p, 0x3123U) == 0x5AU);
            } else {
                write(p, alias, 0x5AU);
                REQUIRE(read(p, kPattern[i] + 0x123U) == 0x5AU);
            }
        }
}

TEST_CASE("Mapper 218 decodes every external PPU address into the same console RAM",
          "[mappers][mapper218]") {
    for (Byte wiring : {0U, 1U, 8U, 9U})
        for (bool legacy : {false, true}) {
            auto c = cartridge(wiring, legacy);
            Ppu p;
            p.connect_cartridge(&c);
            p.reset();
            std::array<Byte, 2048> expected{};
            for (unsigned a = 0U; a < 0x3F00U; ++a) {
                const auto addr = static_cast<Address>(a);
                const auto index = physical(wiring, addr);
                REQUIRE(c.ciram_address(addr) == index);
                const auto value = static_cast<Byte>((a / 256U) ^ (a * 13U));
                write(p, addr, value);
                expected[index] = value;
                REQUIRE(p.vram()[index] == value);
            }
            for (unsigned a = 0U; a < 0x3F00U; ++a)
                REQUIRE(read(p, static_cast<Address>(a)) ==
                        expected[physical(wiring, static_cast<Address>(a))]);
            REQUIRE(std::equal(expected.begin(), expected.end(), p.vram().begin()));
            REQUIRE(std::all_of(
                p.vram().begin() + 2048, p.vram().end(), [](Byte v) { return v == 0U; }));
            const auto before = p.vram();
            write(p, 0x3F10U, 0x29U);
            REQUIRE(p.vram() == before);
            REQUIRE_FALSE(c.ciram_address(0x3F10U));
            address(p, 0x3F10U);
            REQUIRE(p.read_register(7U) == 0x29U);
            // Palette reads refill the PPUDATA buffer from external $2F10.
            address(p, 0U);
            REQUIRE(p.read_register(7U) == expected[physical(wiring, 0x2F10U)]);
        }
}

TEST_CASE("Mapper 218 mirrors ROM by fitted pins and has no cartridge RAM or registers",
          "[mappers][mapper218]") {
    for (Byte wiring : {0U, 1U, 8U, 9U})
        for (bool legacy : {false, true})
            for (std::size_t size : {1UZ, 4096UZ, 8192UZ, 16384UZ, 32768UZ}) {
                auto m = board(wiring, legacy, size);
                const auto saved = state(*m);
                REQUIRE(saved.size() == 2U);
                for (unsigned a = 0x4020U; a <= 0xFFFFU; a += 257U) {
                    const auto addr = static_cast<Address>(a);
                    REQUIRE(m->maps_prg(addr) == (a >= 0x8000U));
                    REQUIRE_FALSE(m->maps_expansion(addr));
                    m->write_prg(addr, static_cast<Byte>(a));
                    if (a >= 0x8000U) {
                        const auto offset = (a - 0x8000U) % size;
                        REQUIRE(m->read_prg(addr) == static_cast<Byte>((offset / 256U) ^ offset));
                    }
                }
                REQUIRE(state(*m) == saved);
                m->reset();
                REQUIRE(state(*m) == saved);
                REQUIRE(m->battery_ram().empty());
                REQUIRE_FALSE(m->has_expansion_audio());
                REQUIRE_FALSE(m->irq_pending());
            }
}

TEST_CASE("Mapper 218 validates real memory population and raw header wiring",
          "[mappers][mapper218]") {
    register_builtin_mappers();
    for (bool legacy : {false, true})
        for (Byte wiring : {0U, 1U, 8U, 9U})
            for (unsigned kind = 0U; kind < (legacy ? 6U : 11U); ++kind) {
                auto h = header(wiring, legacy);
                std::size_t p = 32768U;
                std::size_t c = 0U;
                switch (kind) {
                case 0U:
                    p = 0U;
                    break;
                case 1U:
                    p = 65536U;
                    break;
                case 2U:
                    p = 24576U;
                    break;
                case 3U:
                    c = 8192U;
                    break;
                case 4U:
                    h.has_battery = true;
                    break;
                case 5U:
                    h.nametable_bits ^= 2U;
                    break;
                case 6U:
                    h.submapper = 1U;
                    break;
                case 7U:
                    h.prg_ram_size = 8192U;
                    break;
                case 8U:
                    h.prg_nvram_size = 8192U;
                    break;
                case 9U:
                    h.chr_ram_size = 8192U;
                    break;
                case 10U:
                    h.chr_nvram_size = 8192U;
                    break;
                default:
                    break;
                }
                REQUIRE_FALSE(MapperRegistry::instance().create(
                    h, std::vector<Byte>(p), std::vector<Byte>(c)));
            }
    auto inconsistent = header(8U);
    inconsistent.mirror_mode = MirrorMode::SingleLower;
    REQUIRE_FALSE(MapperRegistry::instance().create(inconsistent, std::vector<Byte>(32768U), {}));
}

TEST_CASE("Mapper 218 component snapshots store only immutable wiring and reject mismatches",
          "[mappers][mapper218][state]") {
    for (Byte wiring : {0U, 1U, 8U, 9U}) {
        auto m = board(wiring);
        const auto saved = state(*m);
        StateReader valid(saved);
        m->load_state(valid);
        REQUIRE(valid.ok());
        REQUIRE(valid.remaining() == 0U);
        for (std::size_t size : {0UZ, 1UZ}) {
            StateReader truncated(std::span(saved).first(size));
            m->load_state(truncated);
            REQUIRE_FALSE(truncated.ok());
            REQUIRE(state(*m) == saved);
        }
        for (std::size_t offset : {0UZ, 1UZ}) {
            auto bad = saved;
            bad[offset] ^= 0xFFU;
            StateReader invalid(bad);
            m->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*m) == saved);
        }
        for (Byte other : {0U, 1U, 8U, 9U}) {
            auto copy = board(other);
            StateReader reader(saved);
            copy->load_state(reader);
            REQUIRE(reader.ok() == (wiring == other));
        }
    }
}

void scene(Ppu& p, bool bg_high, bool sprite_high) {
    for (unsigned row = 0U; row < 8U; ++row) {
        write(p, static_cast<Address>((bg_high ? 0x1000U : 0U) + 0x30U + row), 0x80U);
        write(p, static_cast<Address>((sprite_high ? 0x1000U : 0U) + 0x40U + row), 0x80U);
    }
    write(p, 0x2000U, 3U);
    write(p, 0x3F00U, 0x0FU);
    write(p, 0x3F01U, 1U);
    write(p, 0x3F11U, 0x21U);
    p.write_register(3U, 0U);
    for (unsigned i = 0U; i < 256U; ++i)
        p.write_oam_dma(0xFFU);
    p.write_register(3U, 0U);
    for (Byte v : {0U, 4U, 0U, 16U})
        p.write_oam_dma(v);
    p.write_register(0U, static_cast<Byte>((bg_high ? 0x10U : 0U) | (sprite_high ? 8U : 0U)));
    address(p, 0x2000U);
    p.write_register(5U, 0U);
    p.write_register(5U, 0U);
    p.write_register(1U, 0x1EU);
}
TEST_CASE("Mapper 218 background and sprite renderers fetch console pattern RAM",
          "[mappers][mapper218][ppu]") {
    for (Byte wiring : {0U, 1U, 8U, 9U})
        for (bool bg_high : {false, true})
            for (bool sprite_high : {false, true}) {
                auto c = cartridge(wiring);
                Ppu p;
                p.connect_cartridge(&c);
                p.reset();
                scene(p, bg_high, sprite_high);
                p.step_dots(341U * 3U);
                REQUIRE(p.frame_buffer().pixels[2U * 256U] == 0xFF002A88U);
                REQUIRE(p.frame_buffer().pixels[2U * 256U + 16U] == 0xFF64B0FFU);
            }
}

TEST_CASE("Mapper 218 nametable aliases invalidate cached background pattern bytes",
          "[mappers][mapper218][ppu]") {
    for (Byte wiring : {0U, 1U, 8U}) {
        auto c = cartridge(wiring);
        Ppu p;
        p.connect_cartridge(&c);
        p.reset();
        scene(p, false, false);
        write(p, 0x0032U, 0xF0U);
        address(p, 0x2000U); // Fine Y = 2, representable by PPUADDR.
        p.step_dots(20U);    // Allow the delayed PPUMASK write to become visible.
        REQUIRE(p.frame_buffer().pixels[19U] == 0xFF002A88U);
        const auto v = p.loopy_v();
        CAPTURE(wiring, v);
        write(p, 0x2032U, 0x08U);
        address(p, v);
        p.step_dots(1U);
        REQUIRE(p.frame_buffer().pixels[20U] == 0xFF002A88U);
    }
}

std::vector<Byte> cpu_image(Byte wiring, bool legacy, Region region) {
    auto data = image(wiring, legacy);
    data[legacy ? 9U : 12U] = region == Region::PAL ? 1U : region == Region::Dendy ? 3U : 0U;
    for (std::size_t i = 16U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i - 16U) / 8192U);
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
        paddr(a);
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U);
    store(0x12U, 0U);
    store(0x13U, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        const auto a = static_cast<Address>(0x9A00U + slot * 8192U);
        store(a, 0xFFU);
        expect(a, static_cast<Byte>(slot));
    }
    const unsigned bank_pin = wiring == 0U   ? 0x800U
                              : wiring == 1U ? 0x400U
                              : wiring == 8U ? 0x1000U
                                             : 0x2000U;
    for (unsigned offset : {0U, 0x123U, 0x3FEU}) {
        pwrite(static_cast<Address>(offset), 0x39U);
        pwrite(static_cast<Address>(bank_pin + offset), 0xA7U);
        for (unsigned page = 0U; page < 16U; ++page) {
            const unsigned a = page * 1024U + offset;
            if (a < 0x3F00U)
                pread(static_cast<Address>(a), (a & bank_pin) != 0U ? 0xA7U : 0x39U);
        }
    }
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    std::ranges::copy(code, data.begin() + 16U + 0x6000U);
    for (std::size_t offset = data.size() - 6U; offset < data.size(); offset += 2U) {
        data[offset] = 0U;
        data[offset + 1U] = 0xE0U;
    }
    return data;
}

TEST_CASE("Mapper 218 CPU fixtures verify CIRAM aliases, regions, reset and atomic snapshots",
          "[mappers][mapper218][state]") {
    register_builtin_mappers();
    for (Byte wiring : {0U, 1U, 8U, 9U})
        for (bool legacy : {false, true})
            for (Region region : {Region::NTSC, Region::PAL, Region::Dendy}) {
                if (legacy && region == Region::Dendy)
                    continue;
                auto c = Cartridge::from_data(cpu_image(wiring, legacy, region));
                REQUIRE(c);
                Emulator e;
                REQUIRE(e.load_cartridge(std::move(*c), ""));
                e.reset();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.ppu().vram()[0x123U] == 0x39U);
                REQUIRE(e.ppu().vram()[0x523U] == 0xA7U);
                const auto early = e.save_state();
                e.step_frame();
                const auto future = e.save_state();
                REQUIRE(e.load_state(early));
                e.step_frame();
                REQUIRE(e.save_state() == future);
                StateWriter tail;
                Controller{}.save_state(tail);
                Controller{}.save_state(tail);
                const auto mapper_start = early.size() - tail.data().size() - 2U;
                StateWriter cpu;
                e.cpu().save_state(cpu);
                const auto ppu_start = 4U + sizeof(uint32_t) + sizeof(uint16_t) + sizeof(Region) +
                                       e.content_hash().size() + cpu.data().size();
                const auto ram_start = ppu_start + 2U * sizeof(uint16_t) + sizeof(bool) + 256U;
                for (std::size_t offset : {0UZ, 1UZ}) {
                    auto bad = early;
                    bad[ram_start + 0x123U] ^= 0xFFU;
                    bad[mapper_start + offset] ^= 0xFFU;
                    const auto before = e.save_state();
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == before);
                    REQUIRE(e.ppu().vram()[0x123U] == 0x39U);
                }
                e.reset();
                REQUIRE(e.ppu().vram()[0x123U] == 0U);
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            }
}

TEST_CASE("Mapper 218 absent CPU RAM preserves open bus independently of CIRAM",
          "[mappers][mapper218]") {
    auto c = cartridge(9U, true);
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (unsigned data = 0U; data < 256U; ++data)
        for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
            bus.write(a, 0xA7U);
            bus.write(0x10U, static_cast<Byte>(data));
            REQUIRE(bus.read(a) == data);
        }
}
} // namespace
