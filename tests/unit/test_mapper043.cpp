#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/mapper043.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x14000U;
constexpr std::size_t kChrSize = 0x2000U;
constexpr std::array kBanks = {4U, 3U, 4U, 4U, 4U, 7U, 5U, 6U};

Byte prg_byte(std::size_t offset) {
    return static_cast<Byte>((offset * 37U) ^ (offset >> 8U) ^ 0xA5U);
}

Byte chr_byte(std::size_t offset) {
    return static_cast<Byte>((offset * 11U) ^ (offset >> 7U) ^ 0x3CU);
}

std::vector<Byte> make_prg() {
    std::vector<Byte> prg(kPrgSize);
    for (std::size_t i = 0; i < prg.size(); ++i)
        prg[i] = prg_byte(i);
    return prg;
}

std::vector<Byte> make_chr() {
    std::vector<Byte> chr(kChrSize);
    for (std::size_t i = 0; i < chr.size(); ++i)
        chr[i] = chr_byte(i);
    return chr;
}

INesHeader header(bool nes2 = false, bool vertical = true) {
    INesHeader h{};
    h.prg_rom_banks = 5U;
    h.chr_rom_banks = 1U;
    h.mapper_number = 43U;
    h.is_nes2 = nes2;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_rom_size = kPrgSize;
    h.chr_rom_size = kChrSize;
    h.prg_ram_size = nes2 ? 0U : 8192U;
    return h;
}

std::unique_ptr<Mapper043> board(bool vertical = true) {
    return std::make_unique<Mapper043>(header(false, vertical), make_prg(), make_chr());
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void register_test_mapper() {
    static const bool registered = [] {
        register_builtin_mappers();
        MapperRegistry::instance().register_mapper(
            43U,
            [](const INesHeader& h,
               std::vector<Byte> prg,
               std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
                if (!Mapper043::validate_geometry(h, prg.size(), chr.size()))
                    return nullptr;
                return std::make_unique<Mapper043>(h, std::move(prg), std::move(chr));
            });
        return true;
    }();
    (void)registered;
}

std::vector<Byte> make_rom(bool nes2 = false, bool vertical = true) {
    std::vector<Byte> rom(16U + kPrgSize + kChrSize, 0U);
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 5U;
    rom[5] = 1U;
    rom[6] = static_cast<Byte>(0xB0U | (vertical ? 1U : 0U));
    rom[7] = static_cast<Byte>(0x20U | (nes2 ? 0x08U : 0U));
    for (std::size_t i = 0; i < kPrgSize; ++i)
        rom[16U + i] = prg_byte(i);
    for (std::size_t i = 0; i < kChrSize; ++i)
        rom[16U + kPrgSize + i] = chr_byte(i);
    return rom;
}

void write_register(Mapper043& mapper, Address addr, Byte value) {
    if (addr < 0x6000U)
        mapper.write_expansion(addr, value);
    else
        mapper.write_prg(addr, value);
}

TEST_CASE("Mapper 043 maps every fixed and switchable ROM cell and protects CHR", "[mapper043]") {
    auto mapper = board();
    REQUIRE(state(*mapper).size() == 6U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);

    for (unsigned addr = 0x5000U; addr < 0x6000U; ++addr)
        REQUIRE(mapper->read_expansion(static_cast<Address>(addr)) ==
                prg_byte(0x10000U + (addr & 0x07FFU)));
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                prg_byte(0x4000U + (addr & 0x1FFFU)));
    for (unsigned addr = 0x8000U; addr < 0xA000U; ++addr)
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                prg_byte(0x2000U + (addr & 0x1FFFU)));
    for (unsigned addr = 0xA000U; addr < 0xC000U; ++addr)
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == prg_byte(addr & 0x1FFFU));
    for (unsigned addr = 0xE000U; addr <= 0xFFFFU; ++addr)
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                prg_byte(0x12000U + (addr & 0x1FFFU)));

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x4022U, static_cast<Byte>(value));
        const auto bank = kBanks[value & 7U];
        for (unsigned addr = 0xC000U; addr < 0xE000U; ++addr)
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                    prg_byte(bank * 0x2000U + (addr & 0x1FFFU)));
    }

    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(~chr_byte(addr)));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr_byte(addr));
    }
}

TEST_CASE("Mapper 043 register masks cover every alias without unrelated writes", "[mapper043]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        const bool bank_register = (addr & 0x71FFU) == 0x4022U;
        const bool irq_register = (addr & 0x71FFU) == 0x4122U || (addr & 0x71FFU) == 0x0122U;
        mapper->reset();
        write_register(*mapper, static_cast<Address>(addr), 5U);
        REQUIRE(state(*mapper)[2] == (bank_register ? 5U : 0U));
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
        const bool expansion = addr >= 0x4020U && addr < 0x6000U;
        REQUIRE(mapper->maps_expansion(static_cast<Address>(addr)) ==
                (expansion && (addr >= 0x5000U || bank_register || irq_register)));

        mapper->reset();
        write_register(*mapper, static_cast<Address>(addr), 1U);
        REQUIRE((state(*mapper)[5] & 1U) == (irq_register ? 1U : 0U));
    }
}

TEST_CASE("Mapper 043 cartridge bus routes ROM registers and floating reads", "[mapper043]") {
    register_test_mapper();
    for (bool nes2 : {false, true}) {
        auto cartridge = Cartridge::from_data(make_rom(nes2));
        REQUIRE(cartridge);
        MemoryBus bus;
        bus.connect_cartridge(&*cartridge);

        REQUIRE(bus.read(0x5000U) == prg_byte(0x10000U));
        REQUIRE(bus.read(0x5800U) == prg_byte(0x10000U));
        REQUIRE(bus.read(0x5FFFU) == prg_byte(0x107FFU));
        REQUIRE(bus.read(0x6000U) == prg_byte(0x4000U));
        REQUIRE(bus.read(0xE000U) == prg_byte(0x12000U));
        for (Address addr : {0x4020U, 0x4022U, 0x4122U, 0x4FFFU}) {
            bus.write(0U, 0x39U);
            REQUIRE(bus.read(addr) == 0x39U);
        }

        bus.write(0x4022U, 5U);
        REQUIRE(bus.read(0xC000U) == prg_byte(7U * 0x2000U));
        bus.write(0xC022U, 2U);
        REQUIRE(bus.read(0xC000U) == prg_byte(4U * 0x2000U));
        bus.write(0x8122U, 1U);
        for (unsigned cycle = 0U; cycle < 4096U; ++cycle)
            cartridge->clock_cpu();
        REQUIRE(cartridge->irq_pending());
        bus.write(0x4122U, 0U);
        REQUIRE_FALSE(cartridge->irq_pending());
    }
}

TEST_CASE("Mapper 043 twelve-bit IRQ counts M2 cycles and latches until acknowledged",
          "[mapper043]") {
    auto mapper = board();
    for (unsigned cycle = 0U; cycle < 8192U; ++cycle)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_expansion(0x4122U, 1U);
    for (unsigned cycle = 1U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0x8122U, 1U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 1U; cycle < 4096U; ++cycle)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    mapper->write_expansion(0x4122U, 0xFEU);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0U; cycle < 8192U; ++cycle)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_expansion(0x4122U, 1U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 043 reset and power-on restore deterministic register defaults", "[mapper043]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    mapper->write_expansion(0x4022U, 7U);
    mapper->write_expansion(0x4122U, 1U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0xC000U) == prg_byte(4U * 0x2000U));
    mapper->write_expansion(0x4022U, 1U);
    mapper->write_expansion(0x4122U, 1U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 043 validates exact chip geometry and ROM-only metadata", "[mapper043]") {
    for (bool nes2 : {false, true})
        for (bool vertical : {false, true}) {
            const auto h = header(nes2, vertical);
            REQUIRE(Mapper043::validate_geometry(h, kPrgSize, kChrSize));
            auto m = Mapper043(h, make_prg(), make_chr());
            REQUIRE(m.mirror_mode() == (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }

    for (auto size : {0U, 1U, 0x13FFFU, 0x14001U, 0x20000U})
        REQUIRE_FALSE(Mapper043::validate_geometry(header(), size, kChrSize));
    for (auto size : {0U, 1U, 0x1FFFU, 0x2001U, 0x4000U})
        REQUIRE_FALSE(Mapper043::validate_geometry(header(), kPrgSize, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header(true);
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper043::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (bool nes2 : {false, true}) {
        auto h = header(nes2);
        h.has_battery = true;
        REQUIRE_FALSE(Mapper043::validate_geometry(h, kPrgSize, kChrSize));
        for (auto mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            h = header(nes2);
            h.mirror_mode = mirror;
            REQUIRE_FALSE(Mapper043::validate_geometry(h, kPrgSize, kChrSize));
        }
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header(true);
        switch (field) {
        case 0:
            h.prg_ram_size = 8192U;
            break;
        case 1:
            h.prg_nvram_size = 8192U;
            break;
        case 2:
            h.chr_ram_size = 8192U;
            break;
        case 3:
            h.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper043::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy_chr_ram = header();
    legacy_chr_ram.chr_ram_size = 8192U;
    REQUIRE_FALSE(Mapper043::validate_geometry(legacy_chr_ram, kPrgSize, kChrSize));

    register_test_mapper();
    for (bool nes2 : {false, true})
        for (bool vertical : {false, true}) {
            auto cartridge = Cartridge::from_data(make_rom(nes2, vertical));
            REQUIRE(cartridge);
            REQUIRE(cartridge->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE_FALSE(cartridge->has_battery());
        }
}

TEST_CASE("Mapper 043 fixed snapshots replay IRQ progress and reject corruption atomically",
          "[mapper043]") {
    auto mapper = board();
    mapper->write_expansion(0x4022U, 7U);
    mapper->write_expansion(0x4122U, 1U);
    for (unsigned cycle = 0U; cycle < 123U; ++cycle)
        mapper->clock_cpu();
    const auto partial = state(*mapper);
    mapper->power_on();
    StateReader partial_reader(partial);
    mapper->load_state(partial_reader);
    REQUIRE(partial_reader.ok());
    REQUIRE(state(*mapper) == partial);
    for (unsigned cycle = 123U; cycle < 4095U; ++cycle)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte bank : {8U, 0x80U, 0xFFU}) {
        auto bad = saved;
        bad[2] = bank;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto bad_counter = saved;
    bad_counter[3] = 0U;
    bad_counter[4] = 0x10U;
    StateReader counter_reader(bad_counter);
    mapper->load_state(counter_reader);
    REQUIRE_FALSE(counter_reader.ok());
    REQUIRE(state(*mapper) == saved);
    for (Byte flags : {2U, 4U, 0x80U, 0xFFU}) {
        auto bad = saved;
        bad[5] = flags;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto other = board(false);
    const auto before = state(*other);
    StateReader mismatched(saved);
    other->load_state(mismatched);
    REQUIRE_FALSE(mismatched.ok());
    REQUIRE(state(*other) == before);
    mapper->power_on();
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(state(*mapper) == saved);
}

TEST_CASE("Mapper 043 original firmware switches banks and delivers a CPU IRQ", "[mapper043]") {
    register_test_mapper();
    for (bool nes2 : {false, true}) {
        auto rom = make_rom(nes2);
        constexpr std::size_t fixed = 16U + 0x12000U;
        const std::vector<Byte> program = {
            0x78U,               // SEI
            0xA9U, 0x40U,        // LDA #$40
            0x8DU, 0x17U, 0x40U, // STA $4017
            0xA9U, 0x05U,        // LDA #5
            0x8DU, 0x22U, 0x40U, // STA $4022
            0xADU, 0x00U, 0xC0U, // LDA $C000
            0x85U, 0x40U,        // STA $40
            0xA9U, 0x02U,        // LDA #2
            0x8DU, 0x22U, 0xC0U, // STA $C022
            0xADU, 0x00U, 0xC0U, // LDA $C000
            0x85U, 0x41U,        // STA $41
            0xA9U, 0x01U,        // LDA #1
            0x8DU, 0x22U, 0x41U, // STA $4122
            0x58U,               // CLI
            0x4CU, 0x20U, 0xE0U, // JMP $E020
        };
        std::copy(program.begin(), program.end(), rom.begin() + fixed);
        const std::vector<Byte> irq = {0xA9U, 0x5AU, 0x85U, 0x42U, 0x40U};
        std::copy(irq.begin(), irq.end(), rom.begin() + fixed + 0x100U);
        rom[fixed + 0x1FFAU] = 0x00U;
        rom[fixed + 0x1FFBU] = 0xE0U;
        rom[fixed + 0x1FFCU] = 0x00U;
        rom[fixed + 0x1FFDU] = 0xE0U;
        rom[fixed + 0x1FFEU] = 0x00U;
        rom[fixed + 0x1FFFU] = 0xE1U;

        auto cartridge = Cartridge::from_data(rom);
        REQUIRE(cartridge);
        Emulator emulator;
        REQUIRE(emulator.load_cartridge(std::move(*cartridge), ""));
        emulator.power_on();
        emulator.step_frame();
        REQUIRE(emulator.read_cpu(0x40U) == prg_byte(7U * 0x2000U));
        REQUIRE(emulator.read_cpu(0x41U) == prg_byte(4U * 0x2000U));
        REQUIRE(emulator.read_cpu(0x42U) == 0x5AU);
        const auto saved = emulator.save_state();
        emulator.step_frame();
        const auto expected = emulator.save_state();
        REQUIRE(emulator.load_state(saved));
        emulator.step_frame();
        REQUIRE(emulator.save_state() == expected);
    }
}

} // namespace
