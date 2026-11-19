#include <array>
#include <catch2/catch_test_macros.hpp>
#include <string_view>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper045.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

enum class Wram { None, Volatile, Persistent };

INesHeader header(Wram wram = Wram::None, bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 45U;
    h.is_nes2 = !legacy;
    h.has_battery = wram == Wram::Persistent;
    h.prg_ram_size = legacy ? 8192U : (wram == Wram::Volatile ? 8192U : 0U);
    h.prg_nvram_size = !legacy && wram == Wram::Persistent ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset * 7U + (offset >> 8U));
}

std::vector<Byte> rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = cell(static_cast<unsigned>(i / bank_size), static_cast<unsigned>(i % bank_size));
    return data;
}

void install_test_factory() {
    static const bool kInstalled = [] {
        MapperRegistry::instance().register_mapper(
            45U,
            [](const INesHeader& h,
               std::vector<Byte> prg,
               std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
                if (!Mapper045::validate_geometry(h, prg.size(), chr.size()))
                    return nullptr;
                return std::make_unique<Mapper045>(h, std::move(prg), std::move(chr));
            });
        return true;
    }();
    static_cast<void>(kInstalled);
}

std::unique_ptr<Mapper> board(std::size_t prg_size = 524288U,
                              std::size_t chr_size = 524288U,
                              Wram wram = Wram::None,
                              bool legacy = false,
                              bool vertical = true) {
    install_test_factory();
    auto result = MapperRegistry::instance().create(
        header(wram, legacy, vertical), rom(prg_size, 8192U), rom(chr_size, 1024U));
    REQUIRE(result);
    REQUIRE(*result);
    return std::move(*result);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void outer(Mapper& mapper, Byte r0, Byte r1, Byte r2, Byte r3) {
    mapper.write_prg(0x6001U, 0U);
    for (Byte value : {r0, r1, r2, r3})
        mapper.write_prg(0x6000U, value);
}

void bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 045 validates ROM geometry and native memory populations", "[mapper045]") {
    install_test_factory();
    const auto reject = [](INesHeader h, std::size_t prg, std::size_t chr) {
        INFO("prg=" << prg << " chr=" << chr << " sub=" << +h.submapper << " battery="
                    << h.has_battery << " ram=" << h.prg_ram_size << " nvram=" << h.prg_nvram_size
                    << " chr-ram=" << h.chr_ram_size << " chr-nvram=" << h.chr_nvram_size
                    << " mirror=" << +static_cast<Byte>(h.mirror_mode));
        REQUIRE_FALSE(Mapper045::validate_geometry(h, prg, chr));
    };
    for (auto size : {0U, 16384U, 32767U, 32769U, 8388608U + 8192U})
        reject(header(), size, 8192U);
    for (auto size : {0U, 1024U, 8193U, 4194304U + 1024U})
        reject(header(), 32768U, size);
    REQUIRE(Mapper045::validate_geometry(header(), 8388608U, 4194304U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, 32768U, 8192U);
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        reject(h, 32768U, 8192U);
    }
    for (unsigned kind = 0U; kind < 8U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.chr_ram_size = 8192U;
            break;
        case 2U:
            h.chr_nvram_size = 8192U;
            break;
        case 3U:
            h.prg_ram_size = 4096U;
            break;
        case 4U:
            h.prg_nvram_size = 4096U;
            break;
        case 5U:
            h.prg_ram_size = h.prg_nvram_size = 8192U;
            break;
        case 6U:
            h.has_battery = true;
            break;
        default:
            h.prg_nvram_size = 8192U;
            break;
        }
        reject(h, 32768U, 8192U);
    }
    for (bool legacy : {false, true})
        for (auto wram : {Wram::None, Wram::Volatile, Wram::Persistent}) {
            auto m = board(32768U, 8192U, wram, legacy);
            const bool populated = wram != Wram::None && (wram == Wram::Persistent || !legacy);
            REQUIRE(m->maps_prg(0x6000U) == populated);
            REQUIRE(m->battery_ram().size() == (wram == Wram::Persistent ? 8192U : 0U));
            REQUIRE(state(*m).size() == (populated ? 8224U : 32U));
            REQUIRE_FALSE(m->has_expansion_audio());
            REQUIRE_FALSE(m->configure_board_jumpers(8U));
            REQUIRE_FALSE(m->configure_board_profile("menu-0"));
        }
}

TEST_CASE("Mapper 045 sequences every outer alias, locks, resets and overlays WRAM",
          "[mapper045]") {
    auto m = board(524288U, 524288U, Wram::Persistent);
    m->write_prg(0xA001U, 0x80U);
    for (unsigned addr = 0x6000U; addr < 0x7000U; ++addr) {
        outer(*m, 0U, 0U, 0U, (addr & 1U) != 0U ? 0x40U : 0U);
        m->write_prg(static_cast<Address>(addr), 1U);
        if ((addr & 1U) != 0U)
            m->write_prg(0x6000U, 1U);
        REQUIRE(m->read_chr(0U) == cell(1U, 0U));
        REQUIRE(m->read_prg(static_cast<Address>(addr)) == 1U);
        m->write_prg(0x6001U, 0U);
    }

    outer(*m, 0x12U, 0x34U, 0x56U, 0U);
    for (unsigned addr = 0x7000U; addr < 0x8000U; ++addr) {
        const auto port = static_cast<Address>(addr);
        m->write_prg(port, 0xA5U);
        REQUIRE(m->read_prg(port) == 0xA5U);
        REQUIRE(m->read_chr(0U) == cell(0x12U, 0U));
    }

    outer(*m, 0x12U, 0x34U, 0x56U, 0xD0U);
    const auto locked = state(*m);
    for (unsigned addr = 0x6000U; addr < 0x8000U; addr += 2U)
        m->write_prg(static_cast<Address>(addr), 0xFFU);
    REQUIRE(state(*m) != locked); // WRAM changes; outer registers do not.
    REQUIRE(m->read_chr(0U) == cell(0x12U, 0U));
    m->write_prg(0x6001U, 0U);
    m->write_prg(0x6000U, 1U);
    REQUIRE(m->read_chr(0U) == cell(1U, 0U));

    m->write_prg(0x6001U, 0U);
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0x6122U, 0xA7U);
    m->write_prg(0xA001U, 0U);
    m->write_prg(0x6122U, 0x5AU);
    REQUIRE(m->read_prg(0x6122U) == 0U);
    REQUIRE_FALSE(m->maps_prg(0x6122U));
    m->write_prg(0xA001U, 0x80U);
    REQUIRE(m->read_prg(0x6122U) == 0xA7U);
    m->write_prg(0xA001U, 0xC0U);
    m->write_prg(0x6122U, 0x5AU);
    REQUIRE(m->read_prg(0x6122U) == 0xA7U);
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0x6122U, 0x5AU);
    REQUIRE(m->read_prg(0x6122U) == 0x5AU);
    m->write_prg(0x6001U, 0U);
    m->write_prg(0x6000U, 0x5AU);
    REQUIRE(m->read_chr(0U) == cell(0x5AU, 0U));
}

TEST_CASE("Mapper 045 applies PRG outer pins after inner and fixed bank selection", "[mapper045]") {
    auto m = board();
    for (Byte control : {0x00U, 0x15U, 0x20U, 0x3FU, 0x55U}) {
        outer(*m, 0U, 0xA5U, 0xC0U, control);
        const unsigned mask = 0x3FU ^ static_cast<unsigned>(control);
        const unsigned outer_bank = 0xA5U | 0x300U;
        for (Byte mode : {0U, 0x40U})
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                bank(*m, 6U, static_cast<Byte>(raw), mode);
                bank(*m, 7U, static_cast<Byte>(raw ^ 0x35U), mode);
                const std::array<unsigned, 4> inner = {
                    mode == 0U ? raw & 63U : 62U,
                    (raw ^ 0x35U) & 63U,
                    mode == 0U ? 62U : raw & 63U,
                    63U,
                };
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto addr = static_cast<Address>(0x8000U + slot * 8192U + 0x123U);
                    const unsigned expected = ((inner[slot] & mask) | outer_bank) % 64U;
                    REQUIRE(m->read_prg(addr) == cell(expected, 0x123U));
                }
            }
    }
}

TEST_CASE("Mapper 045 applies every CHR AND width and OR pin after pairing and inversion",
          "[mapper045]") {
    auto m = board();
    for (unsigned width = 0U; width < 16U; ++width) {
        outer(*m, 0xA5U, 0U, static_cast<Byte>(0xD0U | width), 0U);
        const unsigned and_mask = width < 8U ? 0U : (1U << (width - 7U)) - 1U;
        constexpr unsigned kOr = 0xA5U | 0xD00U;
        for (Byte mode : {0U, 0x80U}) {
            std::array<Byte, 6> values{};
            for (Byte index = 0U; index < 6U; ++index) {
                values[index] = static_cast<Byte>(index * 37U + 0x83U);
                bank(*m, index, values[index], mode);
            }
            const std::array<unsigned, 8> inner = {values[0] & 0xFEU,
                                                   values[0] | 1U,
                                                   values[1] & 0xFEU,
                                                   values[1] | 1U,
                                                   values[2],
                                                   values[3],
                                                   values[4],
                                                   values[5]};
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const auto slot = ((addr / 1024U) ^ (mode == 0U ? 0U : 4U));
                const unsigned expected = ((inner[slot] & and_mask) | kOr) % 512U;
                m->write_chr(static_cast<Address>(addr), 0U);
                REQUIRE(m->read_chr(static_cast<Address>(addr)) == cell(expected, addr & 1023U));
            }
        }
    }
}

TEST_CASE("Mapper 045 wraps physical ROMs only after outer decoding", "[mapper045]") {
    for (std::size_t prg_size : {32768U, 98304U})
        for (std::size_t chr_size : {8192U, 24576U}) {
            auto m = board(prg_size, chr_size);
            outer(*m, 0xF3U, 0xD7U, 0xFFU, 0x08U);
            bank(*m, 6U, 0x2AU);
            bank(*m, 2U, 0xB4U);
            const unsigned prg_bank = ((0x2AU & 0x37U) | 0xD7U | 0x300U) % (prg_size / 8192U);
            const unsigned chr_bank = ((0xB4U & 0xFFU) | 0xF3U | 0xF00U) % (chr_size / 1024U);
            REQUIRE(m->read_prg(0x8123U) == cell(prg_bank, 0x123U));
            REQUIRE(m->read_prg(0xF123U) ==
                    cell(((63U & 0x37U) | 0xD7U | 0x300U) % (prg_size / 8192U), 0x1123U));
            REQUIRE(m->read_chr(0x1123U) == cell(chr_bank, 0x123U));
        }
}

TEST_CASE("Mapper 045 reads the selected DIP pin and preserves other open-bus bits",
          "[mapper045]") {
    auto m = board();
    constexpr std::array<Address, 8> kPins = {
        0x10U, 0x20U, 0x40U, 0x80U, 0x100U, 0x200U, 0x400U, 0x800U};
    for (Byte dip = 0U; dip < 8U; ++dip) {
        REQUIRE(m->configure_board_jumpers(dip));
        for (unsigned addr = 0x5000U; addr < 0x6000U; ++addr) {
            REQUIRE(m->maps_expansion(static_cast<Address>(addr)));
            REQUIRE(m->expansion_read_mask(static_cast<Address>(addr)) == 1U);
            REQUIRE(m->read_expansion(static_cast<Address>(addr)) ==
                    ((addr & kPins[dip]) != 0U ? 1U : 0U));
            m->write_expansion(static_cast<Address>(addr), 0xFFU);
        }
    }
    REQUIRE_FALSE(m->maps_expansion(0x4FFFU));
    REQUIRE_FALSE(m->maps_expansion(0x6000U));
}

TEST_CASE("Mapper 045 models each explicit PRG chip-enable menu profile", "[mapper045]") {
    constexpr std::array<std::string_view, 5> kProfiles = {"menu-selection-0",
                                                           "menu-selection-1",
                                                           "menu-selection-2",
                                                           "menu-selection-3",
                                                           "menu-selection-4"};
    for (Byte selection = 0U; selection < 5U; ++selection) {
        auto m = board();
        REQUIRE(m->configure_board_profile(kProfiles[selection]));
        REQUIRE(m->observes_ppu_address() == (selection == 2U || selection == 4U));
        outer(*m,
              0U,
              selection == 1U ? 0x80U : (selection == 3U ? 0x40U : 0U),
              selection == 2U ? 0x40U : (selection == 4U ? 0x20U : 0U),
              0U);
        m->observe_ppu_address(0U);
        REQUIRE(m->prg_read_mask(0x8000U) == (selection == 0U ? 0xFFU : 0U));
        REQUIRE(m->read_prg(0x8000U) == (selection == 0U ? cell(0U, 0U) : 0U));
        outer(*m, 0U, 0U, 0U, 0U);
        m->observe_ppu_address(0U);
        REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
        REQUIRE(m->read_prg(0x8000U) == cell(0U, 0U));
    }
    auto large = board(1048576U, 524288U);
    const auto before = state(*large);
    REQUIRE_FALSE(large->configure_board_profile("menu-selection-1"));
    REQUIRE(state(*large) == before);
    REQUIRE(large->configure_board_profile("menu-selection-0"));
}

TEST_CASE("Mapper 045 decodes every MMC3 register alias", "[mapper045]") {
    auto m = board(524288U, 524288U, Wram::Persistent);
    constexpr std::size_t kBase = 8192U + 10U;
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        m->reset();
        const auto port = static_cast<Address>(addr);
        switch ((static_cast<unsigned>(addr) >> 13U) & 3U) {
        case 0U:
            if ((addr & 1U) == 0U) {
                m->write_prg(port, 0xC6U);
                m->write_prg(0x8001U, 0xFFU);
                const auto saved = state(*m);
                REQUIRE(saved[kBase] == 6U);
                REQUIRE(saved[kBase + 1U] == 0xC0U);
                REQUIRE(saved[kBase + 8U] == 63U);
            } else {
                m->write_prg(0x8000U, 6U);
                m->write_prg(port, 0xFFU);
                REQUIRE(state(*m)[kBase + 8U] == 63U);
            }
            break;
        case 1U:
            if ((addr & 1U) == 0U) {
                m->write_prg(port, 0U);
                REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
                m->write_prg(port, 1U);
                REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
            } else {
                m->write_prg(port, 0xC0U);
                REQUIRE(state(*m)[kBase + 14U] == 0xC0U);
            }
            break;
        case 2U:
            if ((addr & 1U) == 0U) {
                m->write_prg(port, 0xA5U);
                REQUIRE(state(*m)[kBase + 12U] == 0xA5U);
            } else {
                m->write_prg(0xC000U, 2U);
                m->write_prg(port, 0U);
                const auto saved = state(*m);
                REQUIRE(saved[kBase + 11U] == 0U);
                REQUIRE((saved[kBase + 13U] & 1U) == 1U);
            }
            break;
        default:
            if ((addr & 1U) == 0U) {
                m->write_prg(0xE001U, 0U);
                m->write_prg(0xC000U, 0U);
                m->write_prg(0xC001U, 0U);
                m->clock_irq_counter();
                REQUIRE(m->irq_pending());
                m->write_prg(port, 0U);
                REQUIRE_FALSE(m->irq_pending());
                REQUIRE((state(*m)[kBase + 13U] & 6U) == 0U);
            } else {
                m->write_prg(port, 0U);
                m->write_prg(0xC000U, 0U);
                m->write_prg(0xC001U, 0U);
                m->clock_irq_counter();
                REQUIRE(m->irq_pending());
            }
            break;
        }
    }
}

TEST_CASE("Mapper 045 retains MMC3 IRQ behavior across outer writes and acknowledgement",
          "[mapper045]") {
    auto m = board();
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    const auto partial = state(*m);
    outer(*m, 1U, 2U, 3U, 4U);
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->acknowledge_irq();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(m->irq_pending());
    StateReader reader(partial);
    m->load_state(reader);
    REQUIRE(reader.ok());
    m->clock_irq_counter();
    REQUIRE_FALSE(m->irq_pending());
    m->clock_irq_counter();
    REQUIRE(m->irq_pending());
    m->write_prg(0xE000U, 0U);
    m->write_prg(0xC000U, 0U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    for (unsigned n = 0U; n < 3U; ++n) {
        m->clock_irq_counter();
        REQUIRE(m->irq_pending());
        m->write_prg(0xE000U, 0U);
        m->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 045 reset and cold power preserve only documented RAM and configuration",
          "[mapper045]") {
    for (auto wram : {Wram::Volatile, Wram::Persistent}) {
        auto m = board(524288U, 524288U, wram, false, false);
        INFO("wram=" << static_cast<int>(wram));
        REQUIRE(m->configure_board_jumpers(6U));
        REQUIRE(m->configure_board_profile("menu-selection-1"));
        m->write_prg(0xA001U, 0x80U);
        m->write_prg(0x6123U, 0xA7U);
        outer(*m, 1U, 2U, 3U, 4U);
        bank(*m, 6U, 9U, 0xC0U);
        m->write_prg(0xA000U, 0U);
        m->reset();
        REQUIRE(m->read_prg(0x6123U) == 0xA7U);
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(m->read_chr(0U) == cell(0U, 0U));
        REQUIRE(m->read_prg(0x8000U) == cell(0U, 0U));
        REQUIRE(m->read_expansion(0x5400U) == 1U);
        REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
        m->power_on();
        REQUIRE(m->read_prg(0x6123U) == (wram == Wram::Persistent ? 0xA7U : 0U));
        REQUIRE(m->read_expansion(0x5400U) == 1U);
    }
}

TEST_CASE("Mapper 045 state replay is bounded and all rejection paths are atomic", "[mapper045]") {
    auto m = board(524288U, 524288U, Wram::Persistent);
    REQUIRE(m->configure_board_jumpers(5U));
    REQUIRE(m->configure_board_profile("menu-selection-2"));
    m->write_prg(0xA001U, 0x80U);
    m->write_prg(0x6123U, 0xA7U);
    outer(*m, 1U, 2U, 0x4FU, 3U);
    bank(*m, 6U, 9U, 0xC0U);
    m->write_prg(0xC000U, 2U);
    m->write_prg(0xC001U, 0U);
    m->write_prg(0xE001U, 0U);
    m->clock_irq_counter();
    m->observe_ppu_address(0x1000U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == 8224U);
    for (std::size_t size : {0U, 1U, 9U, 10U, 8191U, 8192U, 8223U}) {
        StateReader reader(std::span(saved).first(size));
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    const std::size_t base = 8192U + 10U;
    const std::array<std::pair<std::size_t, Byte>, 12> bad_fields = {{
        {0U, 0U},
        {4U, 0U},
        {8U, 0U},
        {base + 0U, 8U},
        {base + 1U, 1U},
        {base + 8U, 64U},
        {base + 10U, 2U},
        {base + 13U, 8U},
        {base + 14U, 0xFFU},
        {base + 18U, 0x80U},
        {base + 19U, 4U},
        {base + 21U, 0x20U},
    }};
    for (const auto [offset, value] : bad_fields) {
        auto bad = saved;
        bad[offset] = value;
        INFO("offset=" << offset << " value=" << +value);
        StateReader reader(bad);
        m->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*m) == saved);
    }
    auto copy = board(524288U, 524288U, Wram::Persistent);
    REQUIRE(copy->configure_board_jumpers(5U));
    REQUIRE(copy->configure_board_profile("menu-selection-2"));
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6123U) == 0xA7U);
    REQUIRE(copy->read_prg(0x8000U) == m->read_prg(0x8000U));
    REQUIRE(copy->read_chr(0U) == m->read_chr(0U));

    auto mismatch = board();
    const auto before = state(*mismatch);
    StateReader wrong(saved);
    mismatch->load_state(wrong);
    REQUIRE_FALSE(wrong.ok());
    REQUIRE(state(*mismatch) == before);
}

std::vector<Byte> cpu_image() {
    auto prg = rom(32768U, 8192U);
    // SEI; result=$45; $FFE5: INC $11; JMP $FFE5.
    prg[0x7FE0U] = 0x78U;
    prg[0x7FE1U] = 0xA9U;
    prg[0x7FE2U] = 0x45U;
    prg[0x7FE3U] = 0x85U;
    prg[0x7FE4U] = 0x10U;
    prg[0x7FE5U] = 0xE6U;
    prg[0x7FE6U] = 0x11U;
    prg[0x7FE7U] = 0x4CU;
    prg[0x7FE8U] = 0xE5U;
    prg[0x7FE9U] = 0xFFU;
    for (Address vector : {0xFFFAU, 0xFFFCU, 0xFFFEU}) {
        prg[vector - 0x8000U] = 0xE0U;
        prg[vector - 0x8000U + 1U] = 0xFFU;
    }
    std::vector<Byte> image = {
        'N', 'E', 'S', 0x1AU, 2U, 1U, 0xD1U, 0x20U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    image.insert(image.end(), prg.begin(), prg.end());
    const auto chr = rom(8192U, 1024U);
    image.insert(image.end(), chr.begin(), chr.end());
    return image;
}

TEST_CASE("Mapper 045 CPU bus merges DIP and disabled-ROM open-bus bits", "[mapper045]") {
    install_test_factory();
    auto cartridge = Cartridge::from_data(cpu_image());
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    bus.write(0U, 0xA6U);
    REQUIRE(bus.read(0x5010U) == 0xA7U);
    REQUIRE(bus.read(0x5020U) == 0xA6U);
    REQUIRE(bus.read(0x4FFFU) == 0xA6U);
    REQUIRE(cartridge->configure_board_profile("menu-selection-1"));
    bus.write(0x6000U, 0U);
    bus.write(0x6000U, 0x80U);
    bus.write(0x6000U, 0U);
    bus.write(0x6000U, 0U);
    bus.write(0U, 0x39U);
    REQUIRE(bus.read(0x8000U) == 0x39U);
    bus.write(0x6001U, 0U);
    REQUIRE(bus.read(0x8000U) == cell(0U, 0U));
}

TEST_CASE("Mapper 045 test-local registration boots and replays original CPU firmware",
          "[mapper045]") {
    install_test_factory();
    auto cartridge = Cartridge::from_data(cpu_image());
    REQUIRE(cartridge);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(std::move(*cartridge), ""));
    emulator.power_on();
    emulator.step_frame();
    REQUIRE(emulator.read_cpu(0x10U) == 0x45U);
    const auto saved = emulator.save_state();
    emulator.step_frame();
    const auto future = emulator.save_state();
    REQUIRE(emulator.load_state(saved));
    emulator.step_frame();
    REQUIRE(emulator.save_state() == future);
}

} // namespace
