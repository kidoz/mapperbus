#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper042.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool legacy = false, bool chr_ram = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 42U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.chr_ram_size = chr_ram ? 8192U : 0U;
    return h;
}

Byte cell(std::size_t offset) {
    const auto bank = offset / 8192U;
    const auto within = offset & 8191U;
    return static_cast<Byte>(bank * 29U + within + (within >> 8U));
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t i = 0U; i < size; ++i)
        bytes[i] = cell(i);
    return bytes;
}

std::unique_ptr<Mapper042> board(bool legacy = false, bool chr_ram = false, bool vertical = true) {
    const auto h = header(legacy, chr_ram, vertical);
    REQUIRE(Mapper042::validate_geometry(h, 131072U, chr_ram ? 0U : 131072U));
    return std::make_unique<Mapper042>(h, rom(131072U), rom(chr_ram ? 0U : 131072U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void install_test_factory() {
    register_builtin_mappers();
    MapperRegistry::instance().register_mapper(
        42U,
        [](const INesHeader& h,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            if (!Mapper042::validate_geometry(h, prg.size(), chr.size()))
                return nullptr;
            return std::make_unique<Mapper042>(h, std::move(prg), std::move(chr));
        });
}

TEST_CASE("Mapper 042 maps every fixed and switchable PRG cell without CPU RAM", "[mapper042]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
            REQUIRE(m->read_prg(static_cast<Address>(addr)) == cell(0x18000U + (addr & 0x7FFFU)));
        for (unsigned value = 0U; value < 256U; ++value) {
            m->write_prg(0xE000U, static_cast<Byte>(value));
            const auto bank = value & 15U;
            for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
                REQUIRE(m->read_prg(static_cast<Address>(addr)) ==
                        cell(bank * 8192U + (addr & 0x1FFFU)));
            REQUIRE(m->read_prg(0x8000U) == cell(0x18000U));
            REQUIRE(m->read_prg(0xFFFFU) == cell(0x1FFFFU));
        }
        REQUIRE(m->battery_ram().empty());
        REQUIRE_FALSE(m->has_expansion_audio());
        for (unsigned addr = 0U; addr < 0x6000U; ++addr) {
            REQUIRE_FALSE(m->maps_prg(static_cast<Address>(addr)));
            REQUIRE_FALSE(m->maps_expansion(static_cast<Address>(addr)));
        }
        for (unsigned addr = 0x6000U; addr <= 0xFFFFU; ++addr)
            REQUIRE(m->maps_prg(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 042 decodes the complete E003 register mask and ignores E003", "[mapper042]") {
    auto m = board();
    const auto initial = state(*m);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        m->power_on();
        m->write_prg(static_cast<Address>(addr), 0xAFU);
        switch (addr & 0xE003U) {
        case 0x8000U:
            REQUIRE(m->read_prg(0x6000U) == cell(0U));
            REQUIRE(m->read_chr(0U) == cell(15U * 8192U));
            REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(state(*m)[5] == 0U);
            break;
        case 0xE000U:
            REQUIRE(m->read_prg(0x6000U) == cell(15U * 8192U));
            REQUIRE(m->read_chr(0U) == cell(0U));
            REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(state(*m)[5] == 0U);
            break;
        case 0xE001U:
            REQUIRE(m->read_prg(0x6000U) == cell(0U));
            REQUIRE(m->read_chr(0U) == cell(0U));
            REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
            REQUIRE(state(*m)[5] == 0U);
            break;
        case 0xE002U:
            REQUIRE(m->read_prg(0x6000U) == cell(0U));
            REQUIRE(m->read_chr(0U) == cell(0U));
            REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(state(*m)[5] == 1U);
            break;
        default:
            REQUIRE(state(*m) == initial);
            break;
        }
    }
}

TEST_CASE("Mapper 042 register data uses only documented bits and has no bus conflicts",
          "[mapper042]") {
    auto m = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        m->write_prg(0xE000U, static_cast<Byte>(value));
        REQUIRE(m->read_prg(0x6123U) == cell((value & 15U) * 8192U + 0x123U));
        m->write_prg(0x8000U, static_cast<Byte>(value));
        REQUIRE(m->read_chr(0x123U) == cell((value & 15U) * 8192U + 0x123U));
        m->write_prg(0xE001U, static_cast<Byte>(value));
        REQUIRE(m->mirror_mode() ==
                ((value & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        m->write_prg(0xE002U, static_cast<Byte>(value));
        REQUIRE(state(*m)[5] == ((value & 2U) != 0U ? 1U : 0U));
    }
}

TEST_CASE("Mapper 042 selects all CHR ROM banks and protects ROM from writes", "[mapper042]") {
    auto m = board();
    for (unsigned bank = 0U; bank < 16U; ++bank) {
        m->write_prg(0x8000U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            m->write_chr(static_cast<Address>(addr), 0xFFU);
            REQUIRE(m->read_chr(static_cast<Address>(addr)) == cell(bank * 8192U + addr));
        }
    }
}

TEST_CASE("Mapper 042 CHR RAM is fixed writable volatile and ignores CHR selection",
          "[mapper042]") {
    for (bool legacy : {false, true}) {
        auto m = board(legacy, true);
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            m->write_chr(static_cast<Address>(addr), cell(addr ^ 0x1555U));
        const auto populated = state(*m);
        REQUIRE(populated.size() == 8200U);
        for (unsigned value = 0U; value < 256U; ++value) {
            m->write_prg(0x8000U, static_cast<Byte>(value));
            REQUIRE(state(*m) == populated);
        }
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            REQUIRE(m->read_chr(static_cast<Address>(addr)) == cell(addr ^ 0x1555U));
        m->reset();
        REQUIRE(state(*m).size() == 8200U);
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            REQUIRE(m->read_chr(static_cast<Address>(addr)) == cell(addr ^ 0x1555U));
        m->power_on();
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            REQUIRE(m->read_chr(static_cast<Address>(addr)) == 0U);
    }
}

TEST_CASE("Mapper 042 IRQ is a free-running fifteen-bit M2 counter", "[mapper042]") {
    auto m = board();
    for (unsigned n = 0U; n < 40000U; ++n) {
        m->clock_cpu();
        m->clock_irq_counter();
        m->clock_audio();
        REQUIRE_FALSE(m->irq_pending());
    }
    m->write_prg(0xE002U, 0xFEU);
    for (unsigned n = 0U; n < 30000U; ++n) {
        m->clock_irq_counter();
        m->clock_audio();
    }
    REQUIRE_FALSE(m->irq_pending());
    for (unsigned n = 1U; n <= 24576U; ++n) {
        m->clock_cpu();
        REQUIRE(m->irq_pending() == (n == 24576U));
    }
    m->acknowledge_irq();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE002U, 2U);
    REQUIRE(m->irq_pending());
    for (unsigned n = 1U; n <= 8192U; ++n) {
        m->clock_cpu();
        REQUIRE(m->irq_pending() == (n < 8192U));
    }
    for (unsigned n = 1U; n <= 24576U; ++n) {
        m->clock_cpu();
        REQUIRE(m->irq_pending() == (n == 24576U));
    }
    m->write_prg(0xE002U, 0xFDU);
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE(state(*m)[6] == 0U);
    REQUIRE(state(*m)[7] == 0U);
    for (unsigned n = 0U; n < 40000U; ++n) {
        m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
    }
    m->write_prg(0xE002U, 2U);
    for (unsigned n = 0U; n < 24575U; ++n)
        m->clock_cpu();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_cpu();
    REQUIRE(m->irq_pending());
}

TEST_CASE("Mapper 042 reset restores deterministic defaults and cold power clears CHR RAM",
          "[mapper042]") {
    for (bool chr_ram : {false, true}) {
        auto m = board(false, chr_ram, false);
        m->write_prg(0xE000U, 11U);
        m->write_prg(0x8000U, 9U);
        m->write_prg(0xE001U, 8U);
        m->write_prg(0xE002U, 2U);
        for (unsigned n = 0U; n < 24576U; ++n)
            m->clock_cpu();
        m->write_chr(0x321U, 0xA7U);
        REQUIRE(m->irq_pending());
        m->reset();
        REQUIRE(m->read_prg(0x6000U) == cell(0U));
        REQUIRE(m->read_chr(0U) == (chr_ram ? 0U : cell(0U)));
        REQUIRE(m->read_chr(0x321U) == (chr_ram ? 0xA7U : cell(0x321U)));
        REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        REQUIRE_FALSE(m->irq_pending());
        REQUIRE(state(*m)[5] == 0U);
        m->power_on();
        REQUIRE(m->read_chr(0x321U) == (chr_ram ? 0U : cell(0x321U)));
        m->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
        REQUIRE(m->battery_ram().empty());
    }
}

TEST_CASE("Mapper 042 accepts exactly the two documented hardware populations", "[mapper042]") {
    for (bool legacy : {false, true})
        for (bool chr_ram : {false, true})
            for (bool vertical : {false, true})
                REQUIRE(Mapper042::validate_geometry(
                    header(legacy, chr_ram, vertical), 131072U, chr_ram ? 0U : 131072U));
    for (auto size : {0U, 1U, 8192U, 131071U, 131073U, 262144U})
        REQUIRE_FALSE(Mapper042::validate_geometry(header(), size, 131072U));
    for (auto size : {1U, 8192U, 131071U, 131073U, 262144U})
        REQUIRE_FALSE(Mapper042::validate_geometry(header(), 131072U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper042::validate_geometry(h, 131072U, 131072U));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        REQUIRE_FALSE(Mapper042::validate_geometry(h, 131072U, 131072U));
    }
    for (bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = true;
        h.prg_ram_size = 0U;
        h.prg_nvram_size = 8192U;
        REQUIRE_FALSE(Mapper042::validate_geometry(h, 131072U, 131072U));
        h = header(legacy, true);
        h.chr_nvram_size = 8192U;
        REQUIRE_FALSE(Mapper042::validate_geometry(h, 131072U, 0U));
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header(false, field >= 4U);
        switch (field) {
        case 0U:
            h.prg_ram_size = 8192U;
            break;
        case 1U:
            h.prg_nvram_size = 8192U;
            break;
        case 2U:
            h.chr_ram_size = 8192U;
            break;
        case 3U:
            h.chr_nvram_size = 8192U;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 4096U;
            break;
        default:
            break;
        }
        INFO(field);
        REQUIRE_FALSE(Mapper042::validate_geometry(h, 131072U, field >= 4U ? 0U : 131072U));
    }
}

TEST_CASE("Mapper 042 state replays registers partial IRQs and CHR RAM without ROM",
          "[mapper042]") {
    for (bool chr_ram : {false, true}) {
        auto m = board(false, chr_ram);
        m->write_prg(0xE000U, 13U);
        m->write_prg(0x8000U, 7U);
        m->write_prg(0xE001U, 8U);
        m->write_prg(0xE002U, 2U);
        for (unsigned n = 0U; n < 24583U; ++n)
            m->clock_cpu();
        m->write_chr(0x789U, 0xA6U);
        const auto saved = state(*m);
        REQUIRE(saved.size() == (chr_ram ? 8200U : 8U));
        m->power_on();
        for (unsigned n = 0U; n < 1000U; ++n)
            m->clock_cpu();
        StateReader reader(saved);
        m->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*m) == saved);
        REQUIRE(m->read_prg(0x6000U) == cell(13U * 8192U));
        REQUIRE(m->read_chr(0U) == (chr_ram ? 0U : cell(7U * 8192U)));
        REQUIRE(m->read_chr(0x789U) == (chr_ram ? 0xA6U : cell(7U * 8192U + 0x789U)));
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(m->irq_pending());
        for (unsigned n = 0U; n < 8185U; ++n)
            m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
    }
}

TEST_CASE("Mapper 042 bounded malformed and cross-population states reject atomically",
          "[mapper042]") {
    for (bool chr_ram : {false, true}) {
        auto m = board(false, chr_ram);
        m->write_prg(0xE000U, 12U);
        m->write_prg(0x8000U, 6U);
        m->write_prg(0xE001U, 8U);
        m->write_prg(0xE002U, 2U);
        for (unsigned n = 0U; n < 24580U; ++n)
            m->clock_cpu();
        m->write_chr(0x111U, 0xA5U);
        const auto saved = state(*m);
        const auto reject = [&](std::span<const Byte> bytes) {
            StateReader reader(bytes);
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        };
        for (std::size_t size = 0U; size < saved.size(); ++size)
            reject(std::span(saved).first(size));
        for (unsigned bit = 0U; bit < 16U; ++bit) {
            auto bad = saved;
            bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
            reject(bad);
        }
        for (unsigned field = 0U; field < 6U; ++field) {
            auto bad = saved;
            switch (field) {
            case 0U:
                bad[2] = 16U;
                break;
            case 1U:
                bad[3] = 16U;
                break;
            case 2U:
                bad[4] = 2U;
                break;
            case 3U:
                bad[5] = 2U;
                break;
            case 4U:
                bad[6] = 0U;
                bad[7] = 0x80U;
                break;
            case 5U:
                bad[5] = 0U;
                break;
            default:
                break;
            }
            INFO(field);
            reject(bad);
        }
        auto other = board(false, !chr_ram);
        const auto before = state(*other);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(*other) == before);
    }
}

std::vector<Byte> image(bool legacy = false, bool chr_ram = false, bool vertical = true) {
    auto prg = rom(131072U);
    const auto chr = rom(chr_ram ? 0U : 131072U);
    std::vector<Byte> code{0x78U, 0xD8U, 0xA2U, 0xFFU, 0x9AU};
    const auto store = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xA9U, value, 0x8DU, static_cast<Byte>(addr), static_cast<Byte>(addr >> 8U)});
    };
    const auto expect = [&code](Address addr, Byte value) {
        code.insert(code.end(),
                    {0xADU,
                     static_cast<Byte>(addr),
                     static_cast<Byte>(addr >> 8U),
                     0xC9U,
                     value,
                     0xD0U,
                     0xFEU});
    };
    const auto ppu_address = [&store](Address addr) {
        store(0x2006U, static_cast<Byte>(addr >> 8U));
        store(0x2006U, static_cast<Byte>(addr));
    };
    const auto ppu_write = [&store, &ppu_address](Address addr, Byte value) {
        ppu_address(addr);
        store(0x2007U, value);
    };
    const auto ppu_read = [&code, &expect, &ppu_address](Address addr, Byte value) {
        ppu_address(addr);
        code.insert(code.end(), {0xADU, 0x07U, 0x20U});
        expect(0x2007U, value);
    };
    store(0x4017U, 0x40U);
    store(0x20U, 0U);
    store(0x21U, 0U);
    store(0xE000U, 5U);
    expect(0x6000U, cell(5U * 8192U));
    store(0x8000U, 3U);
    ppu_read(0U, cell(3U * 8192U));
    store(0xE001U, 8U);
    ppu_write(0x2000U, 0x19U);
    ppu_read(0x2400U, 0x19U);
    store(0xE001U, 0U);
    ppu_write(0x2400U, 0x27U);
    ppu_read(0x2000U, 0x19U);
    ppu_read(0x2C00U, 0x27U);
    store(0xE002U, 2U);
    code.push_back(0x58U);
    code.insert(code.end(), {0xA5U, 0x20U, 0xF0U, 0xFCU, 0xA9U, 0xC7U, 0x85U, 0x21U});
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x22U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1000U);
    std::ranges::copy(code, prg.begin() + 0x1E000U);
    const std::array<Byte, 8> irq_handler{0xE6U, 0x20U, 0xA9U, 0x00U, 0x8DU, 0x02U, 0xE0U, 0x40U};
    std::ranges::copy(irq_handler, prg.begin() + 0x1F000U);
    prg[0x1FFFAU] = 0x00U;
    prg[0x1FFFBU] = 0xF0U;
    prg[0x1FFFCU] = 0x00U;
    prg[0x1FFFDU] = 0xE0U;
    prg[0x1FFFEU] = 0x00U;
    prg[0x1FFFFU] = 0xF0U;

    std::vector<Byte> result(16U, 0U);
    result[0] = 'N';
    result[1] = 'E';
    result[2] = 'S';
    result[3] = 0x1AU;
    result[4] = 8U;
    result[5] = chr_ram ? 0U : 16U;
    result[6] = static_cast<Byte>(0xA0U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0x20U : 0x28U;
    result[8] = legacy ? 1U : 0U;
    result[11] = !legacy && chr_ram ? 7U : 0U;
    result.insert(result.end(), prg.begin(), prg.end());
    result.insert(result.end(), chr.begin(), chr.end());
    return result;
}

Cartridge cartridge(bool legacy = false, bool chr_ram = false, bool vertical = true) {
    install_test_factory();
    auto result = Cartridge::from_data(image(legacy, chr_ram, vertical));
    REQUIRE(result);
    return std::move(*result);
}

TEST_CASE("Mapper 042 low CPU reads float while 6000 is driven through the real bus",
          "[mapper042]") {
    auto c = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0xE000U, 7U);
    REQUIRE(bus.read(0x6000U) == cell(7U * 8192U));
    bus.write(0U, 0x55U);
    REQUIRE(bus.read(0x5000U) == 0x55U);
}

TEST_CASE("Mapper 042 original firmware exercises PRG CHR CIRAM IRQ and atomic replay",
          "[mapper042]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto c = cartridge(legacy, false, vertical);
            Emulator emulator;
            REQUIRE(emulator.load_cartridge(std::move(c), ""));
            emulator.power_on();
            REQUIRE(emulator.cpu().pc() == 0xE000U);
            emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x20U) == 1U);
            REQUIRE(emulator.read_cpu(0x21U) == 0xC7U);
            const auto saved = emulator.save_state();
            emulator.step_frame();
            const auto expected = emulator.save_state();
            REQUIRE(emulator.load_state(saved));
            emulator.step_frame();
            REQUIRE(emulator.save_state() == expected);

            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            auto bad = saved;
            const auto offset = saved.size() - tail.data().size() - 8U;
            bad[offset] ^= 1U;
            REQUIRE_FALSE(emulator.load_state(bad));
            REQUIRE(emulator.save_state() == expected);
            bad = saved;
            bad.resize(bad.size() - 10U);
            REQUIRE_FALSE(emulator.load_state(bad));
            REQUIRE(emulator.save_state() == expected);
        }
}

} // namespace
