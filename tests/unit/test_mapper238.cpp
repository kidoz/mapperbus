#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "../support/mapper238_rom.hpp"
#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::array<Byte, 4> kResponses{0U, 2U, 2U, 3U};
INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 238U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}
std::vector<Byte> pages(std::size_t size, std::size_t page_size) {
    std::vector<Byte> bytes(size);
    for (std::size_t i = 0U; i < size; ++i)
        bytes[i] = static_cast<Byte>(i / page_size);
    return bytes;
}
std::unique_ptr<Mapper> board(bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto m = MapperRegistry::instance().create(
        header(legacy, vertical), pages(131072U, 8192U), pages(262144U, 1024U));
    REQUIRE(m);
    return std::move(*m);
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(reg | mode));
    m.write_prg(0x8001U, value);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}
TEST_CASE("Mapper 238 registers the ROM-only Contra Fighter population", "[mapper238]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto m = board(legacy, vertical);
            REQUIRE(m->read_prg(0x8000U) == 0U);
            REQUIRE(m->read_prg(0xC000U) == 14U);
            REQUIRE(m->read_prg(0xE000U) == 15U);
            REQUIRE(m->mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(m->read_expansion(0x4020U) == 0U);
            REQUIRE(m->read_prg(0x7FFFU) == 0U);
            REQUIRE(m->battery_ram().empty());
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE(state(*m).size() == 18U);
        }
}
TEST_CASE("Mapper 238 protection aliases every low cartridge address and ignores upper data bits",
          "[mapper238]") {
    auto m = board();
    bank(*m, 6U, 3U);
    bank(*m, 7U, 5U);
    bank(*m, 2U, 47U);
    for (Address w : {0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU, 0x6000U, 0x6FFFU, 0x7000U, 0x7FFFU})
        for (unsigned data = 0U; data < 256U; ++data) {
            if (w < 0x6000U)
                m->write_expansion(w, static_cast<Byte>(data));
            else
                m->write_prg(w, static_cast<Byte>(data));
            for (Address r :
                 {0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU, 0x6000U, 0x6FFFU, 0x7000U, 0x7FFFU}) {
                REQUIRE(m->maps_expansion(r));
                REQUIRE(m->expansion_read_mask(r) == 0xFFU);
                REQUIRE(m->read_expansion(r) == kResponses[data % 4U]);
                if (r >= 0x6000U) {
                    REQUIRE(m->maps_prg(r));
                    REQUIRE(m->prg_read_mask(r) == 0xFFU);
                    REQUIRE(m->read_prg(r) == kResponses[data % 4U]);
                }
            }
            REQUIRE(m->read_prg(0x8000U) == 3U);
            REQUIRE(m->read_prg(0xA000U) == 5U);
            REQUIRE(m->read_chr(0x1000U) == 47U);
        }
    for (unsigned a = 0x4020U; a < 0x8000U; ++a) {
        m->write_expansion(static_cast<Address>(a), static_cast<Byte>(a / 256U));
        REQUIRE(m->read_expansion(0x5000U) == kResponses[a / 256U % 4U]);
        REQUIRE(m->read_prg(0x7FFFU) == kResponses[a / 256U % 4U]);
    }
}
TEST_CASE(
    "Mapper 238 distinguishes every register address and protection bypasses absent WRAM gates",
    "[mapper238]") {
    auto m = board();
    for (unsigned a = 0U; a <= 0xFFFFU; ++a) {
        m->reset();
        m->write_expansion(0x4020U, 3U);
        m->write_prg(static_cast<Address>(a), static_cast<Byte>(a / 256U));
        REQUIRE(m->read_expansion(0x4020U) ==
                (a >= 0x4020U && a < 0x8000U ? kResponses[a / 256U % 4U] : 3U));
        REQUIRE(m->maps_expansion(static_cast<Address>(a)) == (a >= 0x4020U && a < 0x8000U));
        REQUIRE(m->maps_prg(0x6000U));
    }
    for (Byte gate : {0U, 0x40U, 0x80U, 0xC0U}) {
        m->write_prg(0xA001U, gate);
        m->write_prg(0x6000U, 1U);
        REQUIRE(m->read_prg(0x6000U) == 2U);
        REQUIRE(m->read_expansion(0x5000U) == 2U);
        REQUIRE(m->maps_prg(0x7FFFU));
    }
    for (unsigned a = 0U; a < 0x4020U; ++a) {
        m->write_expansion(static_cast<Address>(a), 3U);
        REQUIRE_FALSE(m->maps_expansion(static_cast<Address>(a)));
        REQUIRE(m->read_expansion(0x5000U) == 2U);
    }
}
TEST_CASE("Mapper 238 retains standard MMC3 PRG CHR and CIRAM controls", "[mapper238]") {
    auto m = board();
    for (Byte mode : {0U, 0x40U})
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            bank(*m, 6U, static_cast<Byte>(raw), mode);
            bank(*m, 7U, static_cast<Byte>(raw ^ 0x35U), mode);
            m->write_expansion(0x5FFFU, 1U);
            const std::array<unsigned, 4> expected{mode == 0U ? raw % 16U : 14U,
                                                   (raw ^ 0x35U) % 16U,
                                                   mode == 0U ? 14U : raw % 16U,
                                                   15U};
            for (unsigned i = 0U; i < 4U; ++i)
                REQUIRE(m->read_prg(static_cast<Address>(0x8000U + i * 8192U)) == expected[i]);
        }
    constexpr std::array<Byte, 6> initial{8U, 12U, 47U, 63U, 100U, 191U};
    for (Byte inversion : {0U, 0x80U})
        for (unsigned reg = 0U; reg < 6U; ++reg)
            for (unsigned value = 0U; value < 256U; ++value) {
                auto regs = initial;
                regs[reg] = static_cast<Byte>(value);
                for (unsigned r = 0U; r < 6U; ++r)
                    bank(*m, static_cast<Byte>(r), regs[r], inversion);
                const std::array<unsigned, 8> expected{regs[0] & 0xFEU,
                                                       (regs[0] & 0xFEU) + 1U,
                                                       regs[1] & 0xFEU,
                                                       (regs[1] & 0xFEU) + 1U,
                                                       regs[2],
                                                       regs[3],
                                                       regs[4],
                                                       regs[5]};
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * 1024U + 0x123U);
                    const auto value_at_slot = expected[slot ^ (inversion != 0U ? 4U : 0U)];
                    REQUIRE(m->read_chr(addr) == value_at_slot);
                    m->write_chr(addr, static_cast<Byte>(value_at_slot ^ 255U));
                    REQUIRE(m->read_chr(addr) == value_at_slot);
                }
            }
    m->write_prg(0xA000U, 1U);
    REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
    m->write_prg(0xA000U, 0U);
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
}
TEST_CASE("Mapper 238 protection survives soft reset and does not clock or acknowledge MMC3 IRQs",
          "[mapper238][irq]") {
    auto m = board();
    m->write_prg(0xC000U, 1U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->write_expansion(0x4020U, 3U);
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0x7000U, 2U);
    REQUIRE(m->irq_pending());
    m->reset();
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE(m->read_expansion(0x4020U) == 2U);
    REQUIRE(m->read_prg(0xC000U) == 14U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    REQUIRE(m->irq_pending()); // Existing MMC3B zero-latch policy.
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    m->power_on();
    REQUIRE(m->read_prg(0x6000U) == 0U);
}
TEST_CASE("Mapper 238 compact state restores protection and pending IRQ and rejects atomically",
          "[mapper238][state]") {
    auto m = board();
    auto copy = board();
    bank(*m, 6U, 9U, 0xC0U);
    bank(*m, 2U, 47U, 0xC0U);
    m->write_expansion(0x4020U, 3U);
    m->write_prg(0xA000U, 1U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto saved = state(*m);
    REQUIRE(saved.size() == 18U);
    REQUIRE(saved[15U] == 0U);
    REQUIRE(saved[16U] == 0xEEU);
    REQUIRE(saved[17U] == 3U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->irq_pending());
    REQUIRE(copy->read_prg(0x7FFFU) == 3U);
    for (std::size_t n = 0U; n < saved.size(); ++n) {
        StateReader short_state{std::span(saved).first(n)};
        m->load_state(short_state);
        REQUIRE_FALSE(short_state.ok());
        REQUIRE(state(*m) == saved);
    }
    for (const auto& [offset, value] : std::array<std::pair<std::size_t, Byte>, 10>{{{0U, 8U},
                                                                                     {1U, 1U},
                                                                                     {8U, 64U},
                                                                                     {10U, 255U},
                                                                                     {13U, 8U},
                                                                                     {14U, 1U},
                                                                                     {15U, 1U},
                                                                                     {16U, 0U},
                                                                                     {17U, 1U},
                                                                                     {17U, 4U}}}) {
        auto bad = saved;
        bad[offset] = value;
        StateReader invalid(bad);
        m->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*m) == saved);
    }
}
TEST_CASE("Mapper 238 rejects unverified ROM pairs RAM and submappers", "[mapper238]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 18U; ++kind) {
        auto h = header();
        std::size_t p = 131072U, c = 262144U;
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
            h.chr_ram_size = 8192U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 9U:
            p = 0U;
            break;
        case 10U:
            p = 262144U;
            c = 131072U;
            break;
        case 11U:
            p += 16384U;
            break;
        case 12U:
            c = 0U;
            break;
        case 13U:
            c = 131072U;
            break;
        case 14U:
            ++c;
            break;
        case 15U:
            h = header(true);
            h.has_battery = true;
            break;
        case 16U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 17U:
            --p;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
}

Cartridge cartridge(bool legacy = false, bool vertical = true, Byte last = 3U) {
    register_builtin_mappers();
    REQUIRE(mapperbus::tests::mapper238_program(vertical, last).size() < 0x1800U);
    auto c = Cartridge::from_data(mapperbus::tests::make_mapper238_rom(legacy, vertical, last));
    REQUIRE(c);
    return std::move(*c);
}
std::size_t payload_start(const std::vector<Byte>& bytes) {
    StateWriter tail;
    Controller{}.save_state(tail);
    Controller{}.save_state(tail);
    return bytes.size() - tail.data().size() - 18U;
}
TEST_CASE("Mapper 238 CPU firmware verifies protection bank modes CIRAM IRQs and state replay",
          "[mapper238][state]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true})
            for (Byte last : {0U, 1U, 2U, 3U}) {
                CAPTURE(legacy, vertical, last);
                Emulator e;
                REQUIRE(e.load_cartridge(cartridge(legacy, vertical, last), ""));
                e.power_on();
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x11U) >= 3U);
                REQUIRE(e.read_cpu(0x14U) == 0U);
                const auto early = e.save_state();
                const auto start = payload_start(early);
                REQUIRE(early[start + 17U] == kResponses[last]);
                e.step_frame();
                const auto future = e.save_state();
                REQUIRE(e.load_state(early));
                e.step_frame();
                REQUIRE(e.save_state() == future);
                for (const auto& [offset, value] : std::array<std::pair<std::size_t, Byte>, 6>{
                         {{0U, 8U}, {1U, 1U}, {13U, 8U}, {15U, 1U}, {16U, 0U}, {17U, 1U}}}) {
                    auto bad = early;
                    bad[start + offset] = value;
                    REQUIRE_FALSE(e.load_state(bad));
                    REQUIRE(e.save_state() == future);
                }
                auto bad = early;
                bad.resize(start + 17U);
                REQUIRE_FALSE(e.load_state(bad));
                REQUIRE(e.save_state() == future);
                e.reset();
                REQUIRE(e.read_cpu(0x4020U) == kResponses[last]);
                REQUIRE(e.read_cpu(0xDA00U) == 14U);
                REQUIRE(e.read_cpu(0xFA00U) == 15U);
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == kResponses[last]);
                e.power_on();
                REQUIRE(e.read_cpu(0x7FFFU) == 0U);
                e.step_frame();
                REQUIRE(e.read_cpu(0x12U) == 0xC7U);
                REQUIRE(e.read_cpu(0x14U) == 0U);
            }
}
TEST_CASE("Mapper 238 protection is fully driven through bus and DMA and cannot alias console I/O",
          "[mapper238][bus][ppu]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    Ppu p;
    p.connect_cartridge(&c);
    p.reset();
    bus.connect_ppu(&p);
    for (Byte data : {0U, 1U, 2U, 3U}) {
        bus.write(0x4020U, data);
        for (unsigned seed = 0U; seed < 256U; ++seed)
            for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
                bus.write(0x10U, static_cast<Byte>(seed));
                REQUIRE(bus.read(addr) == kResponses[data]);
            }
        bus.write(0x4014U, 0x50U);
        for (unsigned index = 0U; index < 256U; ++index) {
            p.write_register(3U, static_cast<Byte>(index));
            REQUIRE(p.read_register(4U) == kResponses[data]);
        }
        REQUIRE(bus.read(0x7FFFU) == kResponses[data]);
        bus.write(0x401FU, 0xFFU);
        REQUIRE(bus.read(0x4020U) == kResponses[data]);
    }
}
} // namespace
