#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/mappers/jy_company.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(uint16_t id = 160U, bool nes2 = false) {
    INesHeader h{};
    h.mapper_number = id;
    h.mirror_mode = MirrorMode::Vertical;
    h.is_nes2 = nes2;
    return h;
}

std::vector<Byte> patterned_rom(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; i += unit)
        std::fill_n(
            data.begin() + static_cast<std::ptrdiff_t>(i), unit, static_cast<Byte>(i / unit));
    return data;
}

std::unique_ptr<Mapper> board(uint16_t id = 160U, bool nes2 = false) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(
        header(id, nes2), patterned_rom(262144U, 8192U), patterned_rom(262144U, 1024U));
    REQUIRE(result);
    return std::move(*result);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> machine_image() {
    std::vector<Byte> image(16U + 32768U + 8192U, 0U);
    image[0] = 'N';
    image[1] = 'E';
    image[2] = 'S';
    image[3] = 0x1AU;
    image[4] = 2U;
    image[5] = 1U;
    image[6] = 1U;
    image[7] = 0xA0U;

    auto prg = std::span(image).subspan(16U, 32768U);
    prg[0] = 0xA0U;
    prg[8192U] = 0xA1U;
    constexpr std::array<Byte, 40> kProgram{
        0x78U,                             // SEI
        0xD8U,                             // CLD
        0xA2U, 0xFFU, 0x9AU,               // LDX #$FF; TXS
        0xA9U, 0x40U, 0x8DU, 0x17U, 0x40U, // Inhibit APU frame IRQ
        0xA9U, 0x02U, 0x8DU, 0x00U, 0xD0U, // Select 8 KiB PRG mode
        0xA9U, 0x00U, 0x8DU, 0x00U, 0x80U, // Select PRG bank 0
        0xADU, 0x00U, 0x80U, 0x8DU, 0x00U, 0x00U, 0xA9U, 0x01U, 0x8DU,
        0x00U, 0x80U,                                                  // Select PRG bank 1
        0xADU, 0x00U, 0x80U, 0x8DU, 0x01U, 0x00U, 0x4CU, 0x25U, 0xE0U, // Loop at $E025
    };
    std::ranges::copy(kProgram, prg.begin() + 0x6000U);
    prg[0x7FFCU] = 0x00U;
    prg[0x7FFDU] = 0xE0U;
    return image;
}

TEST_CASE("Mapper 160 is the mapper 90 ASIC with advanced nametables inhibited", "[mapper160]") {
    auto alias = board();
    auto original = board(90U);

    for (unsigned bank = 0U; bank < 4U; ++bank) {
        const auto value = static_cast<Byte>(bank + 3U);
        alias->write_prg(static_cast<Address>(0x8000U + bank), value);
        original->write_prg(static_cast<Address>(0x8000U + bank), value);
    }
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        const auto value = static_cast<Byte>(bank + 9U);
        alias->write_prg(static_cast<Address>(0x9000U + bank), value);
        original->write_prg(static_cast<Address>(0x9000U + bank), value);
        alias->write_prg(static_cast<Address>(0xA000U + bank), 1U);
        original->write_prg(static_cast<Address>(0xA000U + bank), 1U);
    }
    for (auto* mapper : {alias.get(), original.get()}) {
        mapper->write_prg(0xD000U, 0x7EU);
        mapper->write_prg(0xD001U, 1U);
        mapper->write_prg(0xB000U, 0x80U);
        mapper->write_prg(0xB004U, 1U);
        mapper->write_prg(0xC002U, 0U);
        mapper->write_prg(0xC001U, 0x40U);
        mapper->write_prg(0xC004U, 0xFFU);
        mapper->write_prg(0xC005U, 0xFFU);
        mapper->write_prg(0xC003U, 0U);
        mapper->write_expansion(0x5800U, 23U);
        mapper->write_expansion(0x5801U, 17U);
        for (unsigned cycle = 0U; cycle < 8U; ++cycle)
            mapper->clock_cpu();
    }

    for (unsigned slot = 0U; slot < 4U; ++slot) {
        const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
        REQUIRE(alias->read_prg(addr) == original->read_prg(addr));
        REQUIRE(alias->read_prg(addr) == slot + 3U);
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * 1024U);
        REQUIRE(alias->read_chr(addr) == original->read_chr(addr));
        REQUIRE(alias->read_chr(addr) == 9U + slot);
    }
    REQUIRE(alias->irq_pending());
    REQUIRE(original->irq_pending());
    REQUIRE(alias->read_expansion(0x5800U) == 135U);
    REQUIRE(alias->read_expansion(0x5801U) == 1U);
    REQUIRE(alias->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(alias->nametable_address(0x2000U));
    REQUIRE_FALSE(alias->read_nametable(0x2000U));
    REQUIRE_FALSE(alias->write_nametable(0x2000U, 0U));
    REQUIRE(state(*alias) == state(*original));
}

TEST_CASE("Mapper 160 snapshots restore bank IRQ and arithmetic state", "[mapper160][state]") {
    auto mapper = board();
    mapper->write_prg(0xD000U, 0x1AU);
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0x9000U, 11U);
    mapper->write_prg(0xC002U, 0U);
    mapper->write_prg(0xC001U, 0x40U);
    mapper->write_prg(0xC004U, 0xFFU);
    mapper->write_prg(0xC005U, 0xFFU);
    mapper->write_prg(0xC003U, 0U);
    mapper->write_expansion(0x5800U, 23U);
    mapper->write_expansion(0x5801U, 17U);
    const auto saved = state(*mapper);

    auto restored = board();
    StateReader reader(saved);
    restored->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*restored) == saved);
    REQUIRE(restored->read_prg(0x8000U) == 7U);
    REQUIRE(restored->read_chr(0U) == 11U);
    REQUIRE_FALSE(restored->irq_pending());
    restored->clock_cpu();
    REQUIRE(restored->irq_pending());
    for (unsigned cycle = 0U; cycle < 7U; ++cycle)
        restored->clock_cpu();
    REQUIRE(restored->read_expansion(0x5800U) == 135U);
    REQUIRE(restored->read_expansion(0x5801U) == 1U);

    auto invalid = saved;
    invalid[0] = 0xFFU;
    StateReader rejected(invalid);
    restored->load_state(rejected);
    REQUIRE_FALSE(rejected.ok());
}

TEST_CASE("Mapper 160 accepts only the mapper 90 header envelope", "[mapper160]") {
    register_builtin_mappers();
    REQUIRE(JyCompany::supports_mapper(160U));
    auto legacy = MapperRegistry::instance().create(
        header(), patterned_rom(32768U, 8192U), patterned_rom(8192U, 1024U));
    REQUIRE(legacy);
    auto nes2 = header(160U, true);
    REQUIRE(MapperRegistry::instance().create(
        nes2, patterned_rom(32768U, 8192U), patterned_rom(8192U, 1024U)));
    nes2.submapper = 1U;
    REQUIRE_FALSE(MapperRegistry::instance().create(
        nes2, patterned_rom(32768U, 8192U), patterned_rom(8192U, 1024U)));
    auto four_screen = header();
    four_screen.mirror_mode = MirrorMode::FourScreen;
    REQUIRE_FALSE(MapperRegistry::instance().create(
        four_screen, patterned_rom(32768U, 8192U), patterned_rom(8192U, 1024U)));
}

TEST_CASE("Mapper 160 cartridge boots switches PRG and replays machine state", "[mapper160]") {
    register_builtin_mappers();
    auto cartridge = Cartridge::from_data(machine_image(), "Aladdin (E) [p1].nes");
    REQUIRE(cartridge);
    Emulator emulator;
    REQUIRE(emulator.load_cartridge(std::move(*cartridge), ""));
    REQUIRE(emulator.mapper_number() == 160U);
    REQUIRE(emulator.region() == Region::PAL);
    emulator.power_on();
    for (unsigned instruction = 0U; instruction < 100U && emulator.read_cpu(1U) == 0U;
         ++instruction)
        emulator.step_instruction();
    REQUIRE(emulator.read_cpu(0U) == 0xA0U);
    REQUIRE(emulator.read_cpu(1U) == 0xA1U);

    const auto saved = emulator.save_state();
    emulator.step_instruction();
    REQUIRE(emulator.save_state() != saved);
    REQUIRE(emulator.load_state(saved));
    REQUIRE(emulator.read_cpu(0U) == 0xA0U);
    REQUIRE(emulator.read_cpu(1U) == 0xA1U);
    REQUIRE(emulator.save_state() == saved);
}

} // namespace
