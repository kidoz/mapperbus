#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper222_ctc31.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 131072U;
constexpr std::size_t kChrSize = 131072U;

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 222U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 17U + offset + (offset >> 8U));
}

std::vector<Byte> prg_rom() {
    std::vector<Byte> data(kPrgSize);
    for (unsigned i = 0U; i < data.size(); ++i)
        data[i] = prg_cell(i / 8192U, i % 8192U);
    return data;
}

std::vector<Byte> chr_rom() {
    std::vector<Byte> data(kChrSize);
    for (unsigned i = 0U; i < data.size(); ++i)
        data[i] = chr_cell(i / 1024U, i % 1024U);
    return data;
}

std::unique_ptr<Ctc31> board(bool legacy = false, bool vertical = true) {
    return std::make_unique<Ctc31>(header(legacy, vertical), prg_rom(), chr_rom());
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void clocks(Mapper& mapper, unsigned count) {
    for (unsigned i = 0U; i < count; ++i)
        mapper.clock_cpu();
}

void load_irq(Mapper& mapper, Byte value) {
    mapper.write_prg(0xF000U, 0U);
    mapper.write_prg(0xF001U, value);
    mapper.write_prg(0xF002U, 0U);
}

TEST_CASE("Mapper 222 CTC-31 validates only the documented board population", "[mapper222]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            const auto h = header(legacy, vertical);
            REQUIRE(Ctc31::validate_geometry(h, kPrgSize, kChrSize));
            auto mapper = board(legacy, vertical);
            REQUIRE(state(*mapper).size() == 20U);
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
            REQUIRE(mapper->read_prg(0xA000U) == prg_cell(0U, 0U));
            REQUIRE(mapper->read_prg(0xC000U) == prg_cell(14U, 0U));
            REQUIRE(mapper->read_prg(0xE000U) == prg_cell(15U, 0U));
            REQUIRE(mapper->read_chr(0U) == chr_cell(0U, 0U));
            REQUIRE(mapper->mirror_mode() == h.mirror_mode);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->configure_board_profile("ctc-31"));
            REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
        }

    const auto reject =
        [](const INesHeader& h, std::size_t p = kPrgSize, std::size_t c = kChrSize) {
            REQUIRE_FALSE(Ctc31::validate_geometry(h, p, c));
        };
    for (std::size_t size : {0UZ, 8192UZ, 65536UZ, 131071UZ, 131073UZ, 262144UZ}) {
        reject(header(), size);
        reject(header(), kPrgSize, size);
    }
    auto ctc = header();
    ctc.submapper = 1U;
    REQUIRE(Ctc31::validate_geometry(ctc, kPrgSize, kChrSize));
    for (Byte submapper = 2U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h);
    }
    for (bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = true;
        reject(h);
        h = header(legacy);
        h.chr_ram_size = 8192U;
        reject(h);
        h = header(legacy);
        h.chr_nvram_size = 8192U;
        reject(h);
        for (MirrorMode mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            h = header(legacy);
            h.mirror_mode = mirror;
            reject(h);
        }
    }
    for (unsigned kind = 0U; kind < 3U; ++kind) {
        auto h = header();
        if (kind == 0U)
            h.mapper_number = 22U;
        else if (kind == 1U)
            h.prg_ram_size = 8192U;
        else
            h.prg_nvram_size = 8192U;
        reject(h);
    }
}

TEST_CASE("Mapper 222 CTC-31 maps VRC2 PRG windows and physical ROM pins", "[mapper222]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Address alias : {0x8000U, 0x8001U, 0x8002U, 0x8003U})
            mapper->write_prg(alias, static_cast<Byte>(value));
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(value & 15U, 0U));
        for (Address alias : {0xA000U, 0xA001U, 0xA002U, 0xA003U})
            mapper->write_prg(alias, static_cast<Byte>(value ^ 0xA5U));
        REQUIRE(mapper->read_prg(0xA000U) == prg_cell((value ^ 0xA5U) & 15U, 0U));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(14U, 0U));
        REQUIRE(mapper->read_prg(0xE000U) == prg_cell(15U, 0U));
    }
    for (unsigned bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(0x8000U, static_cast<Byte>(bank));
        mapper->write_prg(0xA000U, static_cast<Byte>(15U - bank));
        for (unsigned offset = 0U; offset < 8192U; ++offset) {
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    prg_cell(bank, offset));
            REQUIRE(mapper->read_prg(static_cast<Address>(0xA000U + offset)) ==
                    prg_cell(15U - bank, offset));
            REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) ==
                    prg_cell(14U, offset));
            REQUIRE(mapper->read_prg(static_cast<Address>(0xE000U + offset)) ==
                    prg_cell(15U, offset));
        }
    }
}

TEST_CASE("Mapper 222 CTC-31 uses unshifted VRC2 CHR registers and H/V CIRAM", "[mapper222]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const Address low =
            static_cast<Address>(0xB000U + (slot / 2U) * 0x1000U + (slot % 2U) * 2U);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(low, static_cast<Byte>(value));
            mapper->write_prg(static_cast<Address>(low + 1U), static_cast<Byte>(value ^ 0x5AU));
            const unsigned bank = ((value & 15U) | ((value ^ 0x5AU) << 4U)) & 127U;
            const Address address = static_cast<Address>(slot * 1024U);
            REQUIRE(mapper->read_chr(address) == chr_cell(bank, 0U));
            mapper->write_chr(address, static_cast<Byte>(value));
            REQUIRE(mapper->read_chr(address) == chr_cell(bank, 0U));
        }
    }
    for (unsigned bank = 0U; bank < 128U; ++bank) {
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const Address reg =
                static_cast<Address>(0xB000U + (slot / 2U) * 0x1000U + (slot % 2U) * 2U);
            mapper->write_prg(reg, static_cast<Byte>(bank));
            mapper->write_prg(static_cast<Address>(reg + 1U), static_cast<Byte>(bank >> 4U));
        }
        for (unsigned offset = 0U; offset < 8192U; ++offset)
            REQUIRE(mapper->read_chr(static_cast<Address>(offset)) ==
                    chr_cell(bank, offset & 1023U));
    }
    for (unsigned value = 0U; value < 256U; ++value)
        for (Address alias : {0x9000U, 0x9001U, 0x9002U, 0x9003U}) {
            mapper->write_prg(alias, static_cast<Byte>(value));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
}

TEST_CASE("Mapper 222 CTC-31 decodes every VRC2 and discrete IRQ alias", "[mapper222]") {
    auto actual = board();
    auto expected = board();
    for (unsigned address = 0x6000U; address <= 0xFFFFU; ++address) {
        actual->power_on();
        expected->power_on();
        const auto value = static_cast<Byte>(address * 37U);
        unsigned canonical = address;
        if (address < 0x7000U) {
            canonical = 0x6000U;
        } else if (address < 0x8000U) {
            actual->write_prg(static_cast<Address>(address), value);
            expected->write_prg(static_cast<Address>(address), value);
            REQUIRE(state(*actual) == state(*expected));
            continue;
        } else {
            const unsigned group = address >> 12U;
            canonical = group << 12U;
            if (group >= 11U && group <= 14U)
                canonical |= address & 3U;
            else if (group == 15U) {
                if ((address & 1U) != 0U)
                    canonical = 0xF001U;
                else
                    canonical |= address & 2U;
            }
        }
        actual->write_prg(static_cast<Address>(address), value);
        expected->write_prg(static_cast<Address>(canonical), value);
        REQUIRE(state(*actual) == state(*expected));
    }
}

TEST_CASE("Mapper 222 CTC-31 exposes only the VRC2 microwire data bit", "[mapper222]") {
    auto mapper = board();
    for (unsigned address = 0U; address <= 0xFFFFU; ++address) {
        const bool rom_or_latch = address >= 0x8000U || (address >= 0x6000U && address < 0x7000U);
        REQUIRE(mapper->maps_prg(static_cast<Address>(address)) == rom_or_latch);
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(address)) ==
                (address >= 0x6000U && address < 0x7000U ? 1U : 0xFFU));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(address)));
    }
    for (unsigned address = 0x6000U; address < 0x7000U; ++address)
        for (Byte value : {0U, 1U, 2U, 0xFFU}) {
            mapper->write_prg(static_cast<Address>(address), value);
            REQUIRE(mapper->read_prg(static_cast<Address>(address)) == (value & 1U));
        }
}

TEST_CASE("Mapper 222 CTC-31 IRQ prescaler has a 64-cycle first phase", "[mapper222]") {
    auto mapper = board();
    mapper->write_prg(0xF000U, 0U);
    mapper->write_prg(0xF001U, 0U);
    clocks(*mapper, 1000U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[15U] == 0U);
    mapper->write_prg(0xF002U, 0U);
    clocks(*mapper, 63U);
    REQUIRE(state(*mapper)[15U] == 0U);
    mapper->clock_cpu();
    REQUIRE(state(*mapper)[15U] == 1U);
    clocks(*mapper, 127U);
    REQUIRE(state(*mapper)[15U] == 1U);
    mapper->clock_cpu();
    REQUIRE(state(*mapper)[15U] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 222 CTC-31 reproduces the cascaded 74161 counting sequence", "[mapper222]") {
    auto mapper = board();
    load_irq(*mapper, 0x0EU);
    clocks(*mapper, 64U);
    REQUIRE(state(*mapper)[15U] == 0x1FU);
    REQUIRE_FALSE(mapper->irq_pending());
    clocks(*mapper, 128U);
    REQUIRE(state(*mapper)[15U] == 0x10U);
    load_irq(*mapper, 0xEEU);
    clocks(*mapper, 64U);
    REQUIRE(state(*mapper)[15U] == 0xFFU);
    REQUIRE(mapper->irq_pending());
    const auto pending = state(*mapper);
    clocks(*mapper, 1000U);
    REQUIRE(state(*mapper) == pending);
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF001U, 0x55U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[15U] == 0xFFU);
    clocks(*mapper, 63U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[15U] == 0xF0U);
    mapper->write_prg(0xF000U, 0U);
    mapper->write_prg(0xF001U, 0xF5U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF003U, 0xEEU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[15U] == 0xEEU);
}

TEST_CASE("Mapper 222 CTC-31 reset restores deterministic startup state", "[mapper222]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    mapper->write_prg(0x8000U, 9U);
    mapper->write_prg(0xA000U, 7U);
    mapper->write_prg(0xB000U, 3U);
    mapper->write_prg(0xB001U, 4U);
    mapper->write_prg(0x9000U, 1U);
    mapper->write_prg(0x6000U, 1U);
    load_irq(*mapper, 0xEEU);
    clocks(*mapper, 64U);
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    load_irq(*mapper, 0xEEU);
    clocks(*mapper, 64U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 222 CTC-31 snapshots replay and reject corruption atomically",
          "[mapper222][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 5U);
    mapper->write_prg(0xA000U, 11U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const Address reg =
            static_cast<Address>(0xB000U + (slot / 2U) * 0x1000U + (slot % 2U) * 2U);
        mapper->write_prg(reg, static_cast<Byte>(slot + 3U));
        mapper->write_prg(static_cast<Address>(reg + 1U), static_cast<Byte>(slot + 1U));
    }
    mapper->write_prg(0x9000U, 1U);
    mapper->write_prg(0x6000U, 1U);
    mapper->write_prg(0xF000U, 0U);
    mapper->write_prg(0xF001U, 0x42U);
    mapper->write_prg(0xF002U, 0U);
    clocks(*mapper, 37U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 20U);
    auto copy = board();
    StateReader restore(saved);
    copy->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    clocks(*mapper, 1000U);
    clocks(*copy, 1000U);
    REQUIRE(state(*copy) == state(*mapper));

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader truncated(std::span(saved).first(length));
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*copy) == state(*mapper));
    }
    const auto reject = [&](std::size_t offset, Byte value) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == state(*mapper));
    };
    reject(0U, 0U);
    reject(1U, 0U);
    reject(2U, 32U);
    reject(3U, 0xFFU);
    reject(12U, 2U);
    reject(13U, 2U);
    reject(16U, 128U);
    reject(17U, 1U);
    reject(18U, 2U);
    reject(19U, 2U);
    auto inconsistent = saved;
    inconsistent[19U] = 1U;
    StateReader bad_pending(inconsistent);
    copy->load_state(bad_pending);
    REQUIRE_FALSE(bad_pending.ok());
    REQUIRE(state(*copy) == state(*mapper));
    auto other = board(false, false);
    const auto before = state(*other);
    StateReader wrong_mirror(saved);
    other->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other) == before);
}

std::vector<Byte> image(bool legacy = false, bool vertical = true) {
    auto prg = prg_rom();
    const auto chr = chr_rom();
    std::vector<Byte> code;
    const auto store = [&code](Address address, Byte value) {
        code.insert(
            code.end(),
            {0xA9U, value, 0x8DU, static_cast<Byte>(address), static_cast<Byte>(address >> 8U)});
    };
    const auto expect = [&code](Address address, Byte value) {
        code.insert(code.end(),
                    {0xADU,
                     static_cast<Byte>(address),
                     static_cast<Byte>(address >> 8U),
                     0xC9U,
                     value,
                     0xD0U,
                     0xFEU});
    };
    const auto ppu_address = [&store](Address address) {
        store(0x2006U, static_cast<Byte>(address >> 8U));
        store(0x2006U, static_cast<Byte>(address));
    };
    const auto ppu_write = [&store, &ppu_address](Address address, Byte value) {
        ppu_address(address);
        store(0x2007U, value);
    };
    const auto ppu_read = [&code, &ppu_address, &expect](Address address, Byte value) {
        ppu_address(address);
        code.insert(code.end(), {0xADU, 0x07U, 0x20U});
        expect(0x2007U, value);
    };
    store(0x4017U, 0x40U);
    store(0x8000U, 3U);
    store(0xA000U, 5U);
    expect(0x9000U, prg_cell(3U, 0x1000U));
    expect(0xB123U, prg_cell(5U, 0x1123U));
    expect(0xC000U, prg_cell(14U, 0U));
    store(0xB000U, 0x0DU);
    store(0xB001U, 0x05U);
    ppu_read(0U, chr_cell(0x5DU, 0U));
    store(0x9000U, 0U);
    ppu_write(0x2000U, 0x19U);
    ppu_write(0x2400U, 0x27U);
    store(0x9000U, 1U);
    ppu_read(0x2400U, 0x19U);
    store(0x9000U, 0U);
    ppu_read(0x2400U, 0x27U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    store(0xF000U, 0U);
    store(0xF001U, 0xEEU);
    store(0xF002U, 0U);
    code.insert(code.end(), {0x58U, 0xA5U, 0x11U, 0xC9U, 1U, 0x90U, 0xFAU});
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(), {0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    std::ranges::copy(code, prg.begin() + static_cast<std::ptrdiff_t>(15U * 8192U));
    constexpr std::array<Byte, 23> kHandler = {0xE6U, 0x11U, 0xA9U, 0U,    0x8DU, 0U, 0xF0U, 0xA9U,
                                               0xEEU, 0x8DU, 1U,    0xF0U, 0xA9U, 0U, 0x8DU, 2U,
                                               0xF0U, 0x40U, 0U,    0U,    0U,    0U, 0U};
    std::ranges::copy(kHandler, prg.begin() + static_cast<std::ptrdiff_t>(15U * 8192U + 0x1800U));
    prg[15 * 8192 + 0x1FFAU] = 0x00U;
    prg[15 * 8192 + 0x1FFBU] = 0xE0U;
    prg[15 * 8192 + 0x1FFCU] = 0x00U;
    prg[15 * 8192 + 0x1FFDU] = 0xE0U;
    prg[15 * 8192 + 0x1FFEU] = 0x00U;
    prg[15 * 8192 + 0x1FFFU] = 0xF8U;
    std::vector<Byte> result(16U);
    result[0] = 'N';
    result[1] = 'E';
    result[2] = 'S';
    result[3] = 0x1AU;
    result[4] = 8U;
    result[5] = 16U;
    result[6] = static_cast<Byte>(0xE0U | (vertical ? 1U : 0U));
    result[7] = legacy ? 0xD0U : 0xD8U;
    result[8] = legacy ? 0U : 0x10U;
    result.insert(result.end(), prg.begin(), prg.end());
    result.insert(result.end(), chr.begin(), chr.end());
    return result;
}

Cartridge cartridge(bool legacy = false, bool vertical = true) {
    register_builtin_mappers();
    auto result = Cartridge::from_data(image(legacy, vertical));
    REQUIRE(result);
    return std::move(*result);
}

TEST_CASE("Mapper 222 CTC-31 CPU bus preserves open bus around the microwire latch",
          "[mapper222]") {
    auto cart = cartridge();
    MemoryBus bus;
    bus.connect_cartridge(&cart);
    for (Address address : {0x4020U, 0x5000U, 0x7000U, 0x7FFFU}) {
        bus.write(address, 0xA7U);
        bus.write(0U, 0x39U);
        REQUIRE(bus.read(address) == 0x39U);
    }
    for (Address address : {0x6000U, 0x6123U, 0x6FFFU}) {
        bus.write(address, 1U);
        bus.write(0U, 0x38U);
        REQUIRE(bus.read(address) == 0x39U);
        bus.write(address, 0U);
        bus.write(0U, 0x38U);
        REQUIRE(bus.read(address) == 0x38U);
    }
}

TEST_CASE("Mapper 222 CTC-31 original firmware verifies banking CIRAM IRQ and replay",
          "[mapper222][state]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            Emulator emulator;
            REQUIRE(emulator.load_cartridge(cartridge(legacy, vertical), ""));
            emulator.power_on();
            emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x12U) == 0xC7U);
            REQUIRE(emulator.read_cpu(0x11U) >= 1U);
            const auto early = emulator.save_state();
            emulator.step_frame();
            const auto future = emulator.save_state();
            REQUIRE(emulator.load_state(early));
            emulator.step_frame();
            REQUIRE(emulator.save_state() == future);
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            const auto start = early.size() - tail.data().size() - 20U;
            auto bad = early;
            bad[start] ^= 1U;
            const auto before = emulator.save_state();
            REQUIRE_FALSE(emulator.load_state(bad));
            REQUIRE(emulator.save_state() == before);
            emulator.reset();
            emulator.step_frame();
            REQUIRE(emulator.read_cpu(0x12U) == 0xC7U);
        }
}

} // namespace
