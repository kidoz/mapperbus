#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper237_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 237U;
    h.is_nes2 = !legacy;
    h.chr_ram_size = 8192U;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}
Byte pattern(unsigned page, unsigned offset) {
    return static_cast<Byte>(page * 7U + offset / 4U * 5U + offset % 4U);
}
std::vector<Byte> rom() {
    std::vector<Byte> bytes(1048576U);
    for (unsigned a = 0U; a < bytes.size(); ++a)
        bytes[a] = pattern(a / 16384U, a % 16384U);
    return bytes;
}
std::unique_ptr<Mapper> board(bool legacy = false) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(header(legacy), rom(), {});
    REQUIRE(m);
    return std::move(*m);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}
unsigned page(unsigned address, unsigned data, bool high) {
    const unsigned outer = address / 4U % 2U * 32U + data / 8U % 4U * 8U;
    const unsigned inner = data % 8U;
    switch (data / 64U) {
    case 0U:
        return outer + (high ? 7U : inner);
    case 1U:
        return outer + (high ? 7U : inner / 2U * 2U);
    case 2U:
        return outer + inner;
    default:
        return outer + inner / 2U * 2U + static_cast<unsigned>(high);
    }
}
Byte expected(Address cpu, unsigned address, unsigned data) {
    unsigned offset = cpu % 16384U;
    if (address % 2U != 0U)
        offset = offset / 4U * 4U + 2U + offset % 2U;
    return pattern(page(address, data, cpu >= 0xC000U), offset);
}
TEST_CASE("Mapper 237 registers and initializes the documented 420-in-1", "[mapper237]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        REQUIRE(m->read_prg(0x8000U) == pattern(0U, 0U));
        REQUIRE(m->read_prg(0xC000U) == pattern(7U, 0U));
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(state(*m).size() == 8196U);
        REQUIRE(m->battery_ram().empty());
        REQUIRE_FALSE(m->has_expansion_audio());
        m->clock_cpu();
        m->clock_audio();
        m->clock_irq_counter();
        REQUIRE_FALSE(m->irq_pending());
        REQUIRE_FALSE(m->configure_board_profile("menu-0"));
        REQUIRE_FALSE(m->configure_board_jumpers(0U));
    }
}
TEST_CASE("Mapper 237 maps all data and address latch combinations including forced ROM A1",
          "[mapper237]") {
    auto m = board();
    for (unsigned address = 0U; address < 8U; ++address)
        for (unsigned data = 0U; data < 256U; ++data) {
            m->reset();
            m->write_prg(static_cast<Address>(0x8000U + address), static_cast<Byte>(data));
            for (Address a : {0x8000U,
                              0x8001U,
                              0x8002U,
                              0x8003U,
                              0x9A80U,
                              0xBFFFU,
                              0xC000U,
                              0xC001U,
                              0xC002U,
                              0xC003U,
                              0xFA80U,
                              0xFFFFU}) {
                REQUIRE(m->read_prg(a) == expected(a, address, data));
                REQUIRE(m->prg_read_mask(a) == 0xFFU);
            }
            REQUIRE(m->mirror_mode() ==
                    (data / 32U % 2U != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }
}
TEST_CASE(
    "Mapper 237 lock freezes address mode outer bank mirroring and type but permits inner writes",
    "[mapper237]") {
    auto m = board();
    for (unsigned address : {2U, 3U, 6U, 7U})
        for (unsigned data = 0U; data < 256U; ++data) {
            m->reset();
            m->write_prg(static_cast<Address>(0x8000U + address), static_cast<Byte>(data));
            for (unsigned next_address = 0U; next_address < 8U; ++next_address)
                for (unsigned inner = 0U; inner < 8U; ++inner) {
                    const unsigned upper = ((data / 8U) ^ next_address ^ 31U) * 8U;
                    m->write_prg(static_cast<Address>(0xFFF8U + next_address),
                                 static_cast<Byte>(upper + inner));
                    const unsigned retained = data / 8U * 8U + inner;
                    REQUIRE(m->read_prg(0x9A80U) == expected(0x9A80U, address, retained));
                    REQUIRE(m->read_prg(0xFA81U) == expected(0xFA81U, address, retained));
                    REQUIRE(m->mirror_mode() == (data / 32U % 2U != 0U ? MirrorMode::Vertical
                                                                       : MirrorMode::Horizontal));
                }
            const auto saved = state(*m);
            REQUIRE(saved[2U] == address);
            REQUIRE(saved[3U] == (data / 8U * 8U + 7U));
        }
}
TEST_CASE("Mapper 237 decodes every CPU write address and uses data without bus conflicts",
          "[mapper237]") {
    auto m = board();
    for (bool locked : {false, true})
        for (unsigned a = 0U; a <= 0xFFFFU; ++a) {
            m->reset();
            if (locked)
                m->write_prg(0x8007U, 0xE9U);
            const auto value = static_cast<Byte>(a / 256U);
            m->write_prg(static_cast<Address>(a), value);
            const unsigned address = locked ? 7U : (a < 0x8000U ? 0U : a % 8U);
            const unsigned data =
                a < 0x8000U ? (locked ? 0xE9U : 0U) : (locked ? 0xE8U + value % 8U : value);
            REQUIRE(m->read_prg(0x9A80U) == expected(0x9A80U, address, data));
            REQUIRE(m->read_prg(0xFA81U) == expected(0xFA81U, address, data));
            REQUIRE(m->mirror_mode() ==
                    (data / 32U % 2U != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }
}
TEST_CASE(
    "Mapper 237 CHR RAM stays writable in every bank type and lock state and survives soft reset",
    "[mapper237]") {
    auto m = board();
    for (unsigned a = 0U; a < 8192U; ++a) {
        REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
        m->write_chr(static_cast<Address>(a), static_cast<Byte>(a ^ (a / 256U)));
    }
    for (unsigned control = 0U; control < 32U; ++control) {
        m->reset();
        m->write_prg(static_cast<Address>(0x8000U + control % 8U),
                     static_cast<Byte>(control * 8U + 5U));
        for (unsigned a = 0U; a < 8192U; ++a) {
            const auto value = static_cast<Byte>((a ^ (a / 256U)) + control);
            REQUIRE(m->read_chr(static_cast<Address>(a)) == value);
            m->write_chr(static_cast<Address>(a), static_cast<Byte>(value + 1U));
        }
    }
    m->reset();
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(m->read_prg(0x8000U) == pattern(0U, 0U));
    REQUIRE(m->read_prg(0xC000U) == pattern(7U, 0U));
    for (unsigned a = 0U; a < 8192U; ++a)
        REQUIRE(m->read_chr(static_cast<Address>(a)) == static_cast<Byte>((a ^ (a / 256U)) + 32U));
    m->power_on();
    for (unsigned a = 0U; a < 8192U; ++a)
        REQUIRE(m->read_chr(static_cast<Address>(a)) == 0U);
}
TEST_CASE("Mapper 237 snapshots retain latent inner bits type and lock and validate before commit",
          "[mapper237][state]") {
    auto m = board();
    auto copy = board();
    for (unsigned address = 0U; address < 8U; ++address)
        for (Byte data : {0U, 7U, 0x41U, 0x7FU, 0x81U, 0xC1U, 0xFFU}) {
            m->reset();
            m->write_prg(static_cast<Address>(0x8000U + address), data);
            m->write_chr(0x123U, 0xA7U);
            const auto saved = state(*m);
            REQUIRE(saved[2U] == address);
            REQUIRE(saved[3U] == data);
            StateReader reader(saved);
            copy->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(reader.remaining() == 0U);
            REQUIRE(state(*copy) == saved);
            for (std::size_t n : {0UZ, 1UZ, 2UZ, 3UZ, 4UZ, 4096UZ, 8195UZ}) {
                StateReader truncated(std::span(saved).first(n));
                m->load_state(truncated);
                REQUIRE_FALSE(truncated.ok());
                REQUIRE(state(*m) == saved);
            }
            for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 5>{
                     {{0U, 1U}, {1U, 0U}, {2U, 8U}, {2U, 0x80U}, {2U, 0xFFU}}}) {
                auto bad = saved;
                bad[offset] = value;
                bad[4U + 0x123U] = 0U;
                StateReader invalid(bad);
                m->load_state(invalid);
                REQUIRE_FALSE(invalid.ok());
                REQUIRE(state(*m) == saved);
            }
            m->reset();
            REQUIRE(state(*m)[2U] == 0U);
            REQUIRE(state(*m)[3U] == 0U);
            REQUIRE(m->read_chr(0x123U) == 0xA7U);
        }
}
TEST_CASE("Mapper 237 rejects undocumented geometry memory and submappers", "[mapper237]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 20U; ++kind) {
        auto h = header();
        std::size_t p = 1048576U, c = 0U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
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
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 4096U;
            break;
        case 7U:
            h.chr_ram_size = 16384U;
            break;
        case 8U:
            h.chr_nvram_size = 8192U;
            break;
        case 9U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 10U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 11U:
            p = 0U;
            break;
        case 12U:
            p = 524288U;
            break;
        case 13U:
            p += 16384U;
            break;
        case 14U:
            p = 2097152U;
            break;
        case 15U:
            --p;
            break;
        case 16U:
            c = 8192U;
            break;
        case 17U:
            h = header(true);
            h.has_battery = true;
            break;
        case 18U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 19U:
            h = header(true);
            p = 524288U;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
}

Cartridge cartridge(unsigned mode = 0U,
                    bool type = false,
                    unsigned outer = 7U,
                    bool legacy = false) {
    register_builtin_mappers();
    REQUIRE(mapperbus::tests::mapper237_program(mode, type, outer).size() < 1536U);
    REQUIRE(mapperbus::tests::mapper237_boot(mode, type, outer).size() < 0x1800U);
    auto c = Cartridge::from_data(
        mapperbus::tests::make_mapper237_rom(mode, type, outer, legacy, mode % 2U != 0U));
    REQUIRE(c);
    return std::move(*c);
}
std::size_t payload_start(const std::vector<Byte>& bytes) {
    StateWriter tail;
    Controller{}.save_state(tail);
    Controller{}.save_state(tail);
    return bytes.size() - tail.data().size() - 8196U;
}
TEST_CASE("Mapper 237 RAM firmware proves banks type probes lock reset and full machine replay",
          "[mapper237][state]") {
    for (unsigned mode = 0U; mode < 4U; ++mode)
        for (bool type : {false, true})
            for (unsigned outer : {0U, 3U, 4U, 7U})
                for (bool legacy : {false, true}) {
                    CAPTURE(mode, type, outer, legacy);
                    Emulator e;
                    REQUIRE(e.load_cartridge(cartridge(mode, type, outer, legacy), ""));
                    e.power_on();
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == 0U);
                    const auto early = e.save_state();
                    const auto start = payload_start(early);
                    const unsigned address = outer / 4U * 4U + (type ? 1U : 0U) + 2U;
                    const unsigned data = mode * 64U + 32U + outer % 4U * 8U + 6U;
                    REQUIRE(early[start] == 0U);
                    REQUIRE(early[start + 1U] == 0xEDU);
                    REQUIRE(early[start + 2U] == address);
                    REQUIRE(early[start + 3U] == data);
                    REQUIRE(early[start + 4U + 0x123U] == 0x5AU);
                    REQUIRE(e.read_cpu(0x9A80U) == expected(0x9A80U, address, data));
                    REQUIRE(e.load_state(early)); // The external debug read changes CPU open bus.
                    e.step_frame();
                    const auto future = e.save_state();
                    REQUIRE(e.load_state(early));
                    e.step_frame();
                    REQUIRE(e.save_state() == future);
                    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 4>{
                             {{0U, 1U}, {1U, 0U}, {2U, 8U}, {2U, 0x80U}}}) {
                        auto bad = early;
                        bad[start + offset] = value;
                        bad[start + 4U + 0x123U] = 0U;
                        REQUIRE_FALSE(e.load_state(bad));
                        REQUIRE(e.save_state() == future);
                    }
                    auto truncated = early;
                    truncated.resize(start + 8195U);
                    REQUIRE_FALSE(e.load_state(truncated));
                    REQUIRE(e.save_state() == future);
                    e.reset();
                    REQUIRE(e.read_cpu(0x9A80U) == pattern(0U, 0x1A80U));
                    REQUIRE(e.read_cpu(0xFA80U) == pattern(7U, 0x3A80U));
                    const auto reset = e.save_state();
                    REQUIRE(reset[start + 2U] == 0U);
                    REQUIRE(reset[start + 3U] == 0U);
                    REQUIRE(reset[start + 4U + 0x123U] == 0x5AU);
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == 0x5AU);
                    e.power_on();
                    REQUIRE(e.save_state()[start + 4U + 0x123U] == 0U);
                    e.step_frame();
                    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                    REQUIRE(e.read_cpu(0x14U) == 0U);
                }
}
void paddr(Ppu& p, Address a) {
    (void)p.read_register(2U);
    p.write_register(6U, static_cast<Byte>(a >> 8U));
    p.write_register(6U, static_cast<Byte>(a));
}
void pwrite(Ppu& p, Address a, Byte v) {
    paddr(p, a);
    p.write_register(7U, v);
}
Byte pread(Ppu& p, Address a) {
    paddr(p, a);
    (void)p.read_register(7U);
    paddr(p, a);
    return p.read_register(7U);
}
TEST_CASE("Mapper 237 bus and PPU retain real ROM data CHR RAM and CIRAM while locked in type mode",
          "[mapper237][bus][ppu]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    Ppu p;
    p.connect_cartridge(&c);
    p.reset();
    for (unsigned value = 0U; value < 256U; ++value)
        for (Address a : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
            REQUIRE_FALSE(c.maps_prg(a));
            bus.write(0x10U, static_cast<Byte>(value));
            REQUIRE(bus.read(a) == value);
        }
    bus.write(0x8000U, 0x20U);
    pwrite(p, 0x2000U, 0x19U);
    pwrite(p, 0x2400U, 0x27U);
    bus.write(0x8000U, 0U);
    REQUIRE(pread(p, 0x2400U) == 0x19U);
    REQUIRE(pread(p, 0x2800U) == 0x27U);
    bus.write(0x8001U, 0x20U);
    REQUIRE(bus.read(0xC000U) == 2U); // Forced A1 returns a ROM byte, not the open bus.
    REQUIRE(bus.read(0xC001U) == 3U);
    REQUIRE(pread(p, 0x2400U) == 0x27U);
    REQUIRE(pread(p, 0x2800U) == 0x19U);
    bus.write(0x8007U, 0xE8U);
    bus.write(0x8000U, 6U); // Cannot release lock or type, change mirror, or switch ROM half.
    for (unsigned value = 0U; value < 256U; ++value) {
        bus.write(0x10U, static_cast<Byte>(value));
        REQUIRE(bus.read(0x9A80U) == expected(0x9A80U, 7U, 0xEEU));
    }
    pwrite(p, 0x123U, 0xA7U);
    REQUIRE(pread(p, 0x123U) == 0xA7U);
    REQUIRE(pread(p, 0x2400U) == 0x27U);
    REQUIRE(pread(p, 0x2800U) == 0x19U);
    c.reset();
    REQUIRE(pread(p, 0x123U) == 0xA7U);
    REQUIRE(pread(p, 0x2400U) == 0x19U);
}
TEST_CASE("Mapper 237 type wiring applies to opcode and operand fetches", "[mapper237][cpu]") {
    register_builtin_mappers();
    auto bytes = mapperbus::tests::make_mapper237_rom();
    // A1-high reads pairs 2/3, 6/7, A/B twice. Execute LDA #C7; STA $12;
    // BNE -2 through these aliased pairs, proving the CPU fetch path is rewired.
    const std::array<Byte, 12> probe{
        0U, 0U, 0xA9U, 0xC7U, 0U, 0U, 0x85U, 0x12U, 0U, 0U, 0xD0U, 0xFEU};
    std::ranges::copy(probe, bytes.begin() + 16U + 0x1A80U);
    const std::vector<Byte> ram{0x78U, 0xA9U, 0U, 0x8DU, 1U, 0x80U, 0x4CU, 0x80U, 0x9AU};
    std::vector<Byte> boot;
    for (unsigned i = 0U; i < ram.size(); ++i)
        boot.insert(boot.end(), {0xA9U, ram[i], 0x8DU, static_cast<Byte>(i), 2U});
    boot.insert(boot.end(), {0x4CU, 0U, 2U});
    for (unsigned page = 0U; page < 64U; ++page)
        std::ranges::copy(
            boot, bytes.begin() + static_cast<std::ptrdiff_t>(16U + page * 16384U + 0x2000U));
    auto c = Cartridge::from_data(bytes);
    if (!c)
        FAIL(c.error());
    REQUIRE(c);
    Emulator e;
    REQUIRE(e.load_cartridge(std::move(*c), ""));
    e.power_on();
    e.step_frame();
    REQUIRE(e.read_cpu(0x12U) == 0xC7U);
    REQUIRE(e.read_cpu(0x9A80U) == 0xA9U);
    REQUIRE(e.read_cpu(0x9A81U) == 0xC7U);
}
} // namespace
