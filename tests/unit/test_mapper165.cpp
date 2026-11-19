#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "core/mappers/mapper165.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgSize = 0x80000U;
constexpr std::size_t kChrSize = 0x10000U;

INesHeader make_header(bool nes2 = false, bool battery = false, bool wram = true) {
    INesHeader header{};
    header.prg_rom_banks = 32U;
    header.chr_rom_banks = 8U;
    header.mapper_number = 165U;
    header.submapper = 0U;
    header.mirror_mode = MirrorMode::Vertical;
    header.region = Region::NTSC;
    header.has_battery = battery;
    header.has_trainer = false;
    header.is_nes2 = nes2;
    header.prg_rom_size = kPrgSize;
    header.chr_rom_size = kChrSize;
    header.prg_ram_size = wram && !battery ? 0x2000U : 0U;
    header.prg_nvram_size = wram && battery ? 0x2000U : 0U;
    header.chr_ram_size = nes2 ? 0x1000U : 0U;
    if (!nes2)
        header.prg_nvram_size = 0U;
    return header;
}

std::vector<Byte> make_prg(std::size_t size = kPrgSize) {
    std::vector<Byte> rom(size);
    for (std::size_t bank = 0; bank < size / 0x2000U; ++bank)
        for (std::size_t offset = 0; offset < 0x2000U; ++offset)
            rom[bank * 0x2000U + offset] = static_cast<Byte>(bank * 17U + offset * 3U);
    return rom;
}

std::vector<Byte> make_chr(std::size_t size = kChrSize) {
    std::vector<Byte> rom(size);
    for (std::size_t bank = 0; bank < size / 0x1000U; ++bank)
        for (std::size_t offset = 0; offset < 0x1000U; ++offset)
            rom[bank * 0x1000U + offset] = static_cast<Byte>(bank * 29U + offset * 5U);
    return rom;
}

std::unique_ptr<Mapper165> make_mapper(const INesHeader& header = make_header()) {
    return std::make_unique<Mapper165>(header, make_prg(), make_chr());
}

void select_bank(Mapper165& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

Byte prg_value(Byte bank, Address addr) {
    return static_cast<Byte>(static_cast<unsigned>(bank) * 17U + (addr & 0x1FFFU) * 3U);
}

Byte chr_value(Byte bank, Address addr) {
    return static_cast<Byte>(static_cast<unsigned>(bank) * 29U + (addr & 0x0FFFU) * 5U);
}

std::vector<Byte> save(const Mapper165& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void require_rejected_without_mutation(Mapper165& mapper, std::vector<Byte> bytes) {
    const auto before = save(mapper);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(save(mapper) == before);
}

} // namespace

TEST_CASE("Mapper 165 validates ROM and memory geometry", "[mapper165]") {
    REQUIRE(Mapper165::validate_geometry(make_header(), kPrgSize, kChrSize));
    REQUIRE(Mapper165::validate_geometry(make_header(), 0x4000U, 0x1000U));
    REQUIRE(Mapper165::validate_geometry(make_header(true, true), kPrgSize, kChrSize));
    REQUIRE(Mapper165::validate_geometry(make_header(true, false, false), kPrgSize, kChrSize));

    auto header = make_header();
    header.mapper_number = 4U;
    REQUIRE_FALSE(Mapper165::validate_geometry(header, kPrgSize, kChrSize));
    header = make_header(true);
    header.submapper = 1U;
    REQUIRE_FALSE(Mapper165::validate_geometry(header, kPrgSize, kChrSize));
    header = make_header();
    header.mirror_mode = MirrorMode::FourScreen;
    REQUIRE_FALSE(Mapper165::validate_geometry(header, kPrgSize, kChrSize));

    for (const auto size : {0U, 0x2000U, 0x100000U}) {
        REQUIRE_FALSE(Mapper165::validate_geometry(make_header(), size, kChrSize));
    }
    REQUIRE_FALSE(Mapper165::validate_geometry(make_header(), 0x9000U, kChrSize));
    for (const auto size : {0U, 0x800U, 0x200000U}) {
        REQUIRE_FALSE(Mapper165::validate_geometry(make_header(), kPrgSize, size));
    }
    REQUIRE_FALSE(Mapper165::validate_geometry(make_header(), kPrgSize, 0x1800U));

    header = make_header(true);
    header.chr_ram_size = 0U;
    REQUIRE_FALSE(Mapper165::validate_geometry(header, kPrgSize, kChrSize));
    header.chr_ram_size = 0x1000U;
    header.chr_nvram_size = 0x1000U;
    REQUIRE_FALSE(Mapper165::validate_geometry(header, kPrgSize, kChrSize));
    header = make_header(true, true);
    header.prg_ram_size = 0x2000U;
    REQUIRE_FALSE(Mapper165::validate_geometry(header, kPrgSize, kChrSize));
    header = make_header(true, false);
    header.prg_ram_size = 0x1000U;
    REQUIRE_FALSE(Mapper165::validate_geometry(header, kPrgSize, kChrSize));
    header.has_battery = true;
    header.prg_ram_size = 0x2000U;
    REQUIRE_FALSE(Mapper165::validate_geometry(header, kPrgSize, kChrSize));
}

TEST_CASE("Mapper 165 implements MMC3 PRG banking and all register aliases", "[mapper165]") {
    auto mapper = make_mapper();
    select_bank(*mapper, 6U, 5U);
    select_bank(*mapper, 7U, 9U);
    REQUIRE(mapper->read_prg(0x8123U) == prg_value(5U, 0x8123U));
    REQUIRE(mapper->read_prg(0xA123U) == prg_value(9U, 0xA123U));
    REQUIRE(mapper->read_prg(0xC123U) == prg_value(62U, 0xC123U));
    REQUIRE(mapper->read_prg(0xE123U) == prg_value(63U, 0xE123U));

    mapper->write_prg(0x9FFEU, 0x46U);
    REQUIRE(mapper->read_prg(0x8123U) == prg_value(62U, 0x8123U));
    REQUIRE(mapper->read_prg(0xA123U) == prg_value(9U, 0xA123U));
    REQUIRE(mapper->read_prg(0xC123U) == prg_value(5U, 0xC123U));
    REQUIRE(mapper->read_prg(0xE123U) == prg_value(63U, 0xE123U));

    for (Address addr = 0x8000U; addr < 0xA000U; addr += 2U) {
        const auto bank = static_cast<Byte>((addr / 2U) & 0x3FU);
        mapper->write_prg(addr, 6U);
        mapper->write_prg(static_cast<Address>(addr + 1U), bank);
        INFO("alias $" << std::hex << addr);
        REQUIRE(mapper->read_prg(0x8000U) == prg_value(bank, 0x8000U));
    }

    select_bank(*mapper, 6U, 0xFFU);
    REQUIRE(mapper->read_prg(0x8000U) == prg_value(63U, 0x8000U));
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 165 uses MMC2 CHR banks, latches, and MMC3 inversion", "[mapper165]") {
    auto mapper = make_mapper();
    select_bank(*mapper, 0U, 0U);
    select_bank(*mapper, 1U, 1U);
    select_bank(*mapper, 2U, 2U);
    select_bank(*mapper, 3U, 7U);
    select_bank(*mapper, 4U, 3U);
    select_bank(*mapper, 5U, 8U);

    mapper->write_chr(0x0123U, 0xA5U);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
    REQUIRE(mapper->read_chr(0x1123U) == chr_value(2U, 0x1123U));

    const auto old_left = mapper->read_chr(0x0FE8U);
    mapper->observe_ppu_read(0x0FE8U);
    REQUIRE(old_left == 0U);
    REQUIRE(mapper->read_chr(0x0123U) == chr_value(1U, 0x0123U));
    mapper->write_chr(0x0123U, 0x5AU);
    REQUIRE(mapper->read_chr(0x0123U) == chr_value(1U, 0x0123U));

    mapper->observe_ppu_read(0x0FD8U - 1U);
    REQUIRE(mapper->read_chr(0x0123U) == chr_value(1U, 0x0123U));
    mapper->observe_ppu_read(0x0FD8U);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
    mapper->observe_ppu_read(0x2FD8U);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);

    for (Address addr = 0x1FD8U; addr <= 0x1FDFU; ++addr) {
        mapper->observe_ppu_read(0x1FE8U);
        REQUIRE(mapper->read_chr(0x1123U) == chr_value(3U, 0x1123U));
        mapper->observe_ppu_read(addr);
        INFO("latch 1 FD alias $" << std::hex << addr);
        REQUIRE(mapper->read_chr(0x1123U) == chr_value(2U, 0x1123U));
    }
    for (Address addr = 0x1FE8U; addr <= 0x1FEFU; ++addr) {
        mapper->observe_ppu_read(addr);
        INFO("latch 1 FE alias $" << std::hex << addr);
        REQUIRE(mapper->read_chr(0x1123U) == chr_value(3U, 0x1123U));
        mapper->observe_ppu_read(0x1FD8U);
    }

    mapper->observe_ppu_read(0x0FD8U);
    mapper->observe_ppu_read(0x1FE8U);
    mapper->write_prg(0x8000U, 0x80U);
    REQUIRE(mapper->read_chr(0x0123U) == chr_value(3U, 0x0123U));
    mapper->write_chr(0x1123U, 0x3CU);
    REQUIRE(mapper->read_chr(0x1123U) == 0x3CU);
}

TEST_CASE("Mapper 165 WRAM gates, mirroring, reset, and persistence", "[mapper165]") {
    auto mapper = make_mapper();
    mapper->write_prg(0x6000U, 0x12U);
    mapper->write_prg(0x7FFFU, 0x34U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x12U);
    REQUIRE(mapper->read_prg(0x7FFFU) == 0x34U);

    mapper->write_prg(0xBFFFU, 0x00U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x6000U, 0x99U);
    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x6000U, 0x99U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x12U);
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6000U, 0x56U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6000U) == 0x56U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->maps_prg(0x6000U));
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6000U) == 0U);

    auto battery = make_mapper(make_header(true, true));
    const std::array<Byte, 0x2000> imported = [] {
        std::array<Byte, 0x2000> data{};
        data[0] = 0x6BU;
        data[0x1FFFU] = 0x7CU;
        return data;
    }();
    battery->set_battery_ram(imported);
    REQUIRE(battery->battery_ram().size() == imported.size());
    REQUIRE(battery->read_prg(0x6000U) == 0x6BU);
    REQUIRE(battery->read_prg(0x7FFFU) == 0x7CU);
    battery->power_on();
    REQUIRE(battery->read_prg(0x6000U) == 0x6BU);
    battery->set_battery_ram(std::span<const Byte>(imported).first(1U));
    REQUIRE(battery->read_prg(0x6000U) == 0x6BU);

    auto no_wram = make_mapper(make_header(true, false, false));
    REQUIRE_FALSE(no_wram->maps_prg(0x6000U));
    REQUIRE(no_wram->battery_ram().empty());
}

TEST_CASE("Mapper 165 clocks and acknowledges MMC3 IRQs", "[mapper165]") {
    auto mapper = make_mapper();
    mapper->write_prg(0xDFFEU, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 165 reset restores bank and CHR latch defaults", "[mapper165]") {
    auto mapper = make_mapper();
    mapper->write_chr(0x1000U, 0x91U);
    select_bank(*mapper, 1U, 1U);
    select_bank(*mapper, 6U, 8U, 0xC0U);
    mapper->observe_ppu_read(0x0FE8U);
    mapper->observe_ppu_read(0x1FE8U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == prg_value(0U, 0x8000U));
    REQUIRE(mapper->read_chr(0x0000U) == 0x91U);
    REQUIRE(mapper->read_chr(0x1000U) == 0x91U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_chr(0x0000U, 0x22U);
    REQUIRE(mapper->read_chr(0x0000U) == 0x22U);
    mapper->power_on();
    REQUIRE(mapper->read_chr(0x0000U) == 0U);
    REQUIRE(mapper->read_chr(0x1000U) == 0U);
}

TEST_CASE("Mapper 165 state replays all mutable state and rejects damage atomically",
          "[mapper165]") {
    auto mapper = make_mapper(make_header(true, false));
    mapper->write_chr(0x0042U, 0xABU);
    select_bank(*mapper, 0U, 0U);
    select_bank(*mapper, 1U, 1U);
    select_bank(*mapper, 2U, 2U);
    select_bank(*mapper, 3U, 7U);
    select_bank(*mapper, 4U, 3U);
    select_bank(*mapper, 5U, 8U);
    select_bank(*mapper, 6U, 5U);
    select_bank(*mapper, 7U, 9U);
    mapper->write_prg(0x8000U, 0xC0U);
    mapper->write_prg(0x6000U, 0x55U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->observe_ppu_read(0x0FE8U);
    mapper->observe_ppu_read(0x1FE8U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto expected = save(*mapper);

    mapper->power_on();
    mapper->write_prg(0x6000U, 0x99U);
    StateReader replay(expected);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(save(*mapper) == expected);
    REQUIRE(mapper->read_prg(0x6000U) == 0x55U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0xC000U) == prg_value(5U, 0xC000U));

    const std::array<std::size_t, 12> offsets = {
        0U, 2U, 6U, 10U, 14U, 18U, 19U, 26U, 28U, 29U, 32U, 33U};
    for (std::size_t index = 0; index < offsets.size(); ++index) {
        auto damaged = expected;
        damaged[offsets[index]] ^= 0xFFU;
        INFO("damaged state offset " << offsets[index]);
        require_rejected_without_mutation(*mapper, damaged);
    }

    auto excessive_ram = expected;
    const std::uint64_t excessive_size = 0x2001U;
    std::memcpy(excessive_ram.data() + 35U, &excessive_size, sizeof(excessive_size));
    require_rejected_without_mutation(*mapper, excessive_ram);
    auto truncated = expected;
    truncated.pop_back();
    require_rejected_without_mutation(*mapper, truncated);

    auto other = std::make_unique<Mapper165>(make_header(), make_prg(0x20000U), make_chr());
    const auto other_before = save(*other);
    StateReader wrong_geometry(expected);
    other->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(save(*other) == other_before);
}
