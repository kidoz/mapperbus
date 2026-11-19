#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper183.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgSize = 262144U;
constexpr std::size_t kChrSize = 524288U;
constexpr std::size_t kStateSize = 8249U;

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 183U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 13U + offset + (offset >> 8U));
}

std::vector<Byte> prg_image(std::size_t size = kPrgSize) {
    std::vector<Byte> prg(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        prg[offset] = prg_cell(offset / 8192U, offset % 8192U);
    return prg;
}

std::vector<Byte> chr_image(std::size_t size = kChrSize) {
    std::vector<Byte> chr(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        chr[offset] = chr_cell(offset / 1024U, offset % 1024U);
    return chr;
}

std::unique_ptr<Mapper183> board(bool legacy = false,
                                 bool vertical = true,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    return std::make_unique<Mapper183>(
        header(legacy, vertical), prg_image(prg_size), chr_image(chr_size));
}

void chr_bank(Mapper& mapper, unsigned slot, unsigned bank) {
    const auto reg = static_cast<Address>(0xB000U + slot / 2U * 4096U + (slot & 1U) * 8U);
    mapper.write_prg(reg, static_cast<Byte>(bank & 15U));
    mapper.write_prg(reg + 4U, static_cast<Byte>(bank >> 4U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> make_rom(bool firmware = false) {
    std::vector<Byte> rom{
        0x4EU, 0x45U, 0x53U, 0x1AU, 16U, 64U, 0x71U, 0xB8U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const auto prg = prg_image();
    const auto chr = chr_image();
    rom.insert(rom.end(), prg.begin(), prg.end());
    rom.insert(rom.end(), chr.begin(), chr.end());
    if (!firmware)
        return rom;

    constexpr unsigned fixed_bank = 31U;
    Address pc = 0xE000U;
    auto emit = [&](std::initializer_list<Byte> bytes) {
        for (const auto byte : bytes) {
            rom[16U + fixed_bank * 8192U + (pc - 0xE000U)] = byte;
            ++pc;
        }
    };
    emit({0x78U,                             // SEI
          0xA9U, 0x00U, 0x85U, 0x17U,        // Initialize the IRQ counter.
          0xA9U, 0x40U, 0x8DU, 0x17U, 0x40U, // Inhibit APU frame IRQ.
          0xA9U, 0x5AU, 0x8DU, 0x0BU, 0x60U, // Low PRG bank 11; data is ignored.
          0xADU, 0x23U, 0x61U, 0x85U, 0x10U, 0xA9U, 0x05U, 0x8DU, 0x00U, 0x80U, 0xADU, 0x23U,
          0x81U, 0x85U, 0x11U, 0xA9U, 0x07U, 0x8DU, 0x00U, 0xA0U, 0xADU, 0x23U, 0xA1U, 0x85U,
          0x12U, 0xA9U, 0x02U, 0x8DU, 0x08U, 0x90U, 0xADU, 0x23U, 0xC1U, 0x85U, 0x13U, 0xADU,
          0x23U, 0x81U, 0x85U, 0x14U, 0xA9U, 0x03U, 0x8DU, 0x00U, 0x90U, // One-screen upper CIRAM.
          0xA9U, 0x09U, 0x8DU, 0x00U, 0xB0U, 0xA9U, 0x00U, 0x8DU, 0x04U, 0xB0U, 0x8DU, 0x06U,
          0x20U, 0x8DU, 0x06U, 0x20U, 0xADU, 0x07U, 0x20U, 0xADU, 0x07U, 0x20U, 0x85U, 0x15U,
          0xA9U, 0x20U, 0x8DU, 0x06U, 0x20U, 0xA9U, 0x00U, 0x8DU, 0x06U, 0x20U, 0xA9U, 0xA5U,
          0x8DU, 0x07U, 0x20U, 0xA9U, 0x24U, 0x8DU, 0x06U, 0x20U, 0xA9U, 0x00U, 0x8DU, 0x06U,
          0x20U, 0xADU, 0x07U, 0x20U, 0xADU, 0x07U, 0x20U, 0x85U, 0x16U, 0xA9U, 0x0CU, 0x8DU,
          0x00U, 0xF0U, 0xA9U, 0x0FU, 0x8DU, 0x04U, 0xF0U, 0xA9U, 0x06U, 0x8DU, 0x08U, 0xF0U,
          0x58U,                      // CLI
          0xA5U, 0x17U, 0xF0U, 0xFCU, // Wait for the cycle-mode IRQ handler.
          0xA9U, 0xC7U, 0x85U, 0x18U});
    emit({0x4CU, static_cast<Byte>(pc), static_cast<Byte>(pc >> 8U)});

    pc = 0xF100U;
    emit({0xE6U,
          0x17U, // INC IRQ count
          0xA9U,
          0x00U,
          0x8DU,
          0x0CU,
          0xF0U,
          0x40U}); // RTI
    const auto vector = [&](Address address, Address target) {
        rom[16U + fixed_bank * 8192U + (address - 0xE000U)] = static_cast<Byte>(target);
        rom[17U + fixed_bank * 8192U + (address - 0xE000U)] = static_cast<Byte>(target >> 8U);
    };
    vector(0xFFFAU, 0xE000U);
    vector(0xFFFCU, 0xE000U);
    vector(0xFFFEU, 0xF100U);
    return rom;
}

void register_test_factory() {
    register_builtin_mappers();
    MapperRegistry::instance().register_mapper(
        183U, [](const INesHeader& h, std::vector<Byte> prg, std::vector<Byte> chr) {
            return std::make_unique<Mapper183>(h, std::move(prg), std::move(chr));
        });
}

TEST_CASE("Mapper 183 powers on with VRC4e banks and a ROM-only low window", "[mapper183]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto mapper = board(legacy, vertical);
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->read_prg(0x6000U) == prg_cell(0U, 0U));
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
            REQUIRE(mapper->read_prg(0xA000U) == prg_cell(1U, 0U));
            REQUIRE(mapper->read_prg(0xC000U) == prg_cell(30U, 0U));
            REQUIRE(mapper->read_prg(0xE000U) == prg_cell(31U, 0U));
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
            REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
            for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU})
                REQUIRE(mapper->maps_prg(addr) == (addr >= 0x6000U));
            REQUIRE_FALSE(mapper->maps_prg(0x5000U));
            REQUIRE_FALSE(mapper->maps_expansion(0x6000U));
        }
}

TEST_CASE("Mapper 183 low PRG latch decodes every address and ignores data", "[mapper183]") {
    auto mapper = board();
    for (unsigned value : {0U, 0xA7U, 0xFFU})
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(value));
            INFO("addr=" << addr << " value=" << value);
            REQUIRE(mapper->read_prg(0x6000U) == prg_cell(addr & 15U, 0U));
            REQUIRE(mapper->read_prg(0x7FFFU) == prg_cell(addr & 15U, 8191U));
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
            REQUIRE(mapper->read_prg(0xA000U) == prg_cell(1U, 0U));
        }
    for (unsigned bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(static_cast<Address>(0x6000U | bank), 0U);
        for (unsigned offset = 0U; offset < 8192U; ++offset)
            REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                    prg_cell(bank, offset));
    }
}

TEST_CASE("Mapper 183 decodes every VRC4e register alias", "[mapper183]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), 0x1BU);
        const unsigned group = addr >> 12U;
        const unsigned port = (addr >> 2U) & 3U;
        const bool swap = group == 9U && port == 2U;
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(swap ? 30U : group == 8U ? 27U : 0U, 0U));
        REQUIRE(mapper->read_prg(0xA000U) == prg_cell(group == 10U ? 27U : 1U, 0U));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(swap ? (group == 8U ? 27U : 0U) : 30U, 0U));
        REQUIRE(mapper->read_prg(0xE000U) == prg_cell(31U, 0U));
        REQUIRE(mapper->mirror_mode() ==
                (group == 9U && port == 0U ? MirrorMode::SingleUpper : MirrorMode::Vertical));
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const bool selected =
                group >= 11U && group <= 14U && slot == (group - 11U) * 2U + port / 2U;
            const unsigned bank = selected ? ((port & 1U) == 0U ? 11U : 432U) : 0U;
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) == chr_cell(bank, 0U));
        }
    }
}

TEST_CASE("Mapper 183 maps all VRC PRG and CHR cells without ROM writes or conflicts",
          "[mapper183]") {
    auto mapper = board();
    mapper->write_prg(0x600FU, 0U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        mapper->write_prg(0xA000U, static_cast<Byte>(value));
        mapper->write_prg(0x9008U, 0U);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(value & 31U, 0U));
        REQUIRE(mapper->read_prg(0xA000U) == prg_cell(value & 31U, 0U));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(30U, 0U));
        REQUIRE(mapper->read_prg(0x6000U) == prg_cell(15U, 0U));
    }
    for (unsigned bank = 0U; bank < 32U; ++bank)
        for (Byte mode : {0U, 2U}) {
            mapper->write_prg(0x8000U, static_cast<Byte>(bank));
            mapper->write_prg(0xA000U, static_cast<Byte>(bank));
            mapper->write_prg(0x9008U, mode);
            const std::array pages{mode == 0U ? bank : 30U, bank, mode == 0U ? 30U : bank, 31U};
            for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
                REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                        prg_cell(pages[(addr - 0x8000U) / 8192U], addr & 0x1FFFU));
        }
    for (unsigned bank = 0U; bank < 512U; ++bank) {
        for (unsigned slot = 0U; slot < 8U; ++slot)
            chr_bank(*mapper, slot, bank);
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xFFU));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr_cell(bank, addr & 0x3FFU));
        }
    }
}

TEST_CASE("Mapper 183 low expansion floats and the low CPU window is driven", "[mapper183]") {
    register_test_factory();
    auto cartridge = Cartridge::from_data(make_rom());
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU}) {
        bus.write(addr, 0xA7U);
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(addr) == 0x39U);
    }
    bus.write(0x600AU, 0xFFU);
    REQUIRE(bus.read(0x6000U) == prg_cell(10U, 0U));
    REQUIRE(bus.read(0x7FFFU) == prg_cell(10U, 8191U));
}

TEST_CASE("Mapper 183 reset clears bank and IRQ state", "[mapper183]") {
    auto mapper = board(false, false);
    mapper->write_prg(0x600CU, 0U);
    mapper->write_prg(0x8000U, 9U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF004U, 15U);
    mapper->write_prg(0xF008U, 6U);
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    for (bool cold : {false, true}) {
        if (cold)
            mapper->power_on();
        else
            mapper->reset();
        REQUIRE(mapper->read_prg(0x6000U) == prg_cell(0U, 0U));
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper).size() == kStateSize);
        mapper->write_prg(0x600CU, 0U);
        mapper->write_prg(0x8000U, 9U);
        mapper->write_prg(0x9000U, 3U);
        mapper->write_prg(0xF000U, 15U);
        mapper->write_prg(0xF004U, 15U);
        mapper->write_prg(0xF008U, 6U);
        mapper->clock_cpu();
    }
}

TEST_CASE("Mapper 183 VRC IRQ modes and aliases match the cycle specification", "[mapper183]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xF0F0U, static_cast<Byte>(latch & 15U));
        mapper->write_prg(0xF0F4U, static_cast<Byte>(latch >> 4U));
        mapper->write_prg(0xF0F8U, 6U);
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF0FCU, 0U);
        for (unsigned cycle = 0U; cycle < 300U; ++cycle)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->reset();
    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF004U, 15U);
    mapper->write_prg(0xF008U, 3U);
    for (unsigned period : {114U, 114U, 113U, 114U}) {
        for (unsigned cycle = 1U; cycle < period; ++cycle)
            mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->write_prg(0xF008U, 3U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle)
        mapper->clock_cpu();
    const auto partial = state(*mapper);
    mapper->write_prg(0xF008U, 0U);
    const auto disabled = state(*mapper);
    REQUIRE(disabled[13U] == 0x55U);
    REQUIRE(disabled[14U] == 0x01U);
    REQUIRE(disabled[12U] == partial[12U]);
    mapper->reset();
    StateReader reader(partial);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    for (unsigned cycle = 0U; cycle < 13U; ++cycle)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 183 validates native and legacy geometry", "[mapper183]") {
    for (bool legacy : {false, true}) {
        for (unsigned prg : {32768U, 65536U, 131072U, 262144U})
            for (unsigned chr : {8192U, 16384U, 32768U, 65536U, 131072U, 262144U, 524288U})
                REQUIRE(Mapper183::validate_geometry(header(legacy), prg, chr));
        for (unsigned prg : {0U, 16384U, 24576U, 49152U, 262145U, 524288U})
            REQUIRE_FALSE(Mapper183::validate_geometry(header(legacy), prg, kChrSize));
        for (unsigned chr : {0U, 4096U, 12288U, 24576U, 524289U, 1048576U})
            REQUIRE_FALSE(Mapper183::validate_geometry(header(legacy), kPrgSize, chr));
        auto battery = header(legacy);
        battery.has_battery = true;
        REQUIRE_FALSE(Mapper183::validate_geometry(battery, kPrgSize, kChrSize));
        auto four_screen = header(legacy);
        four_screen.mirror_mode = MirrorMode::FourScreen;
        REQUIRE_FALSE(Mapper183::validate_geometry(four_screen, kPrgSize, kChrSize));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper183::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header();
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
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper183::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 183 snapshots are bounded canonical and atomic on rejection", "[mapper183]") {
    auto mapper = board();
    mapper->write_prg(0x600BU, 0U);
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0x9000U, 3U);
    chr_bank(*mapper, 0U, 443U);
    mapper->write_prg(0xF000U, 8U);
    mapper->write_prg(0xF004U, 12U);
    mapper->write_prg(0xF008U, 3U);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle)
        mapper->clock_cpu();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    for (unsigned length = 0U; length < saved.size(); ++length) {
        StateReader reader(std::span(saved).first(length));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    constexpr std::array<std::pair<unsigned, Byte>, 23> bad_fields{{
        {10U, 16U}, {13U, 0U}, {14U, 2U}, {15U, 8U}, {16U, 2U}, {17U, 2U}, {18U, 32U}, {19U, 32U},
        {20U, 1U},  {22U, 2U}, {37U, 2U}, {37U, 5U}, {38U, 4U}, {39U, 1U}, {40U, 1U},  {42U, 1U},
        {44U, 0U},  {45U, 0U}, {46U, 1U}, {47U, 1U}, {48U, 1U}, {49U, 1U}, {57U, 1U},
    }};
    for (const auto [offset, value] : bad_fields) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto inconsistent_irq = saved;
    inconsistent_irq[16U] = 0U;
    inconsistent_irq[17U] = 1U;
    StateReader inconsistent(inconsistent_irq);
    mapper->load_state(inconsistent);
    REQUIRE_FALSE(inconsistent.ok());
    REQUIRE(state(*mapper) == saved);

    auto other = board(false, false);
    const auto before = state(*other);
    StateReader wrong_mirror(saved);
    other->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other) == before);
    auto smaller = board(false, true, kPrgSize / 2U, kChrSize);
    StateReader wrong_geometry(saved);
    smaller->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());

    mapper->power_on();
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
}

TEST_CASE("Mapper 183 original firmware exercises CPU PPU CIRAM IRQ and machine replay",
          "[mapper183]") {
    register_test_factory();
    auto cartridge = Cartridge::from_data(make_rom(true));
    REQUIRE(cartridge);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(std::move(*cartridge), ""));
    emulator.power_on();
    emulator.step_frame();
    INFO("pc=" << emulator.cpu().pc() << " irq_count=" << +emulator.read_cpu(0x17U));
    REQUIRE(emulator.read_cpu(0x10U) == prg_cell(11U, 0x123U));
    REQUIRE(emulator.read_cpu(0x11U) == prg_cell(5U, 0x123U));
    REQUIRE(emulator.read_cpu(0x12U) == prg_cell(7U, 0x123U));
    REQUIRE(emulator.read_cpu(0x13U) == prg_cell(5U, 0x123U));
    REQUIRE(emulator.read_cpu(0x14U) == prg_cell(30U, 0x123U));
    REQUIRE(emulator.read_cpu(0x15U) == chr_cell(9U, 0U));
    REQUIRE(emulator.read_cpu(0x16U) == 0xA5U);
    REQUIRE(emulator.read_cpu(0x17U) == 1U);
    REQUIRE(emulator.read_cpu(0x18U) == 0xC7U);

    const auto saved = emulator.save_state();
    emulator.step_frame();
    const auto expected = emulator.save_state();
    REQUIRE(emulator.load_state(saved));
    emulator.step_frame();
    REQUIRE(emulator.save_state() == expected);
    const auto before_bad_load = emulator.save_state();
    auto truncated = saved;
    truncated.resize(truncated.size() - 10U);
    REQUIRE_FALSE(emulator.load_state(truncated));
    REQUIRE(emulator.save_state() == before_bad_load);

    emulator.reset();
    emulator.step_frame();
    REQUIRE(emulator.read_cpu(0x18U) == 0xC7U);
    REQUIRE(emulator.read_cpu(0x17U) == 1U);
    emulator.power_on();
    emulator.step_frame();
    REQUIRE(emulator.read_cpu(0x18U) == 0xC7U);
    REQUIRE(emulator.read_cpu(0x17U) == 1U);
}

} // namespace
