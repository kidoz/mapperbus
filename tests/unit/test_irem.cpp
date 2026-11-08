#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader irem_header(uint16_t id, Byte submapper = 0U) {
    INesHeader header{};
    header.mapper_number = id;
    header.is_nes2 = true;
    header.submapper = submapper;
    header.mirror_mode = MirrorMode::Vertical;
    return header;
}

std::vector<Byte> irem_rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / bank_size);
    return data;
}

std::unique_ptr<Mapper> irem_board(INesHeader header, bool chr_ram = false) {
    register_builtin_mappers();
    auto result = MapperRegistry::instance().create(
        header,
        irem_rom(header.mapper_number == 32U ? 0x40000U : 0x80000U, 0x2000U),
        chr_ram ? std::vector<Byte>{} : irem_rom(0x40000U, 0x400U));
    REQUIRE(result);
    return std::move(*result);
}

void reload_irq(Mapper& mapper, Word value) {
    mapper.write_prg(0x9005U, static_cast<Byte>(value >> 8U));
    mapper.write_prg(0x9006U, static_cast<Byte>(value));
    mapper.write_prg(0x9004U, 0U);
}

} // namespace

TEST_CASE("Irem boards swap two programmable PRG banks around fixed vectors", "[mappers][irem]") {
    for (const uint16_t id : {32U, 65U}) {
        INFO(id);
        auto mapper = irem_board(irem_header(id));
        const Byte last = id == 32U ? 31U : 63U;
        REQUIRE(mapper->read_prg(0x8000U) == 0U);
        REQUIRE(mapper->read_prg(0xA000U) == 1U);
        REQUIRE(mapper->read_prg(0xC000U) == last - 1U);
        REQUIRE(mapper->read_prg(0xFFFFU) == last);
        // ROM drives zero here: mapper register writes have no bus conflict.
        mapper->write_prg(0x8FFFU, 0xFFU);
        mapper->write_prg(0xAFF7U, 7U);
        REQUIRE(mapper->read_prg(0x8000U) == last);
        REQUIRE(mapper->read_prg(0xA000U) == 7U);
        mapper->write_prg(0x9FF0U, id == 32U ? 2U : 0x80U);
        REQUIRE(mapper->read_prg(0x8000U) == last - 1U);
        REQUIRE(mapper->read_prg(0xC000U) == last);
        for (Address addr : {0xC000U, 0xC007U, 0xD000U, 0xE000U, 0xF000U})
            mapper->write_prg(addr, 3U);
        REQUIRE(mapper->read_prg(0xC000U) == last);
        REQUIRE(mapper->read_prg(0xE000U) == last);
        mapper->write_prg(0x9000U, 0U);
        REQUIRE(mapper->read_prg(0x8000U) == last);
        REQUIRE(mapper->read_prg(0xC000U) == last - 1U);
    }
}

TEST_CASE("Irem CHR registers use all eight bank bits and mirrored low address bits",
          "[mappers][irem]") {
    for (const uint16_t id : {32U, 65U}) {
        auto mapper = irem_board(irem_header(id));
        for (unsigned slot = 0U; slot < 8U; ++slot)
            mapper->write_prg(static_cast<Address>(0xBFF0U + slot), static_cast<Byte>(248U + slot));
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * 0x400U);
            REQUIRE(mapper->read_chr(addr) == 248U + slot);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFU)) == 248U + slot);
            mapper->write_chr(addr, 42U);
            REQUIRE(mapper->read_chr(addr) == 248U + slot);
        }
    }
}

TEST_CASE("G-101 control aliases and Major League wiring remain distinct", "[mappers][irem]") {
    auto normal = irem_board(irem_header(32U));
    normal->write_prg(0x9FFFU, 3U);
    REQUIRE(normal->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(normal->read_prg(0x8000U) == 30U);
    normal->write_prg(0x9001U, 0U);
    REQUIRE(normal->mirror_mode() == MirrorMode::Vertical);
    auto fixed = irem_board(irem_header(32U, 1U));
    REQUIRE(fixed->mirror_mode() == MirrorMode::SingleUpper);
    fixed->write_prg(0x8000U, 5U);
    fixed->write_prg(0x9FFFU, 3U);
    REQUIRE(fixed->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(fixed->read_prg(0x8000U) == 5U);
    REQUIRE(fixed->read_prg(0xC000U) == 30U);
    fixed->reset();
    REQUIRE(fixed->mirror_mode() == MirrorMode::SingleUpper);
}

TEST_CASE("H3001 decodes separate layout and three-way mirroring registers", "[mappers][irem]") {
    auto mapper = irem_board(irem_header(65U));
    const std::array modes = {MirrorMode::Vertical,
                              MirrorMode::SingleLower,
                              MirrorMode::Horizontal,
                              MirrorMode::SingleLower};
    mapper->write_prg(0x8000U, 5U);
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        mapper->write_prg(0x9FF1U, static_cast<Byte>(mode * 64U));
        REQUIRE(mapper->mirror_mode() == modes[mode]);
        REQUIRE(mapper->read_prg(0x8000U) == 5U);
    }
    mapper->write_prg(0x9000U, 0x80U);
    REQUIRE(mapper->read_prg(0xC000U) == 5U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    mapper->write_prg(0x9002U, 0U);
    mapper->write_prg(0x9007U, 0U);
    REQUIRE(mapper->read_prg(0xC000U) == 5U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
}

TEST_CASE("H3001 IRQ fires on terminal decrement and does not wrap or reload",
          "[mappers][irem][irq]") {
    auto mapper = irem_board(irem_header(65U));
    reload_irq(*mapper, 0x0102U);
    for (unsigned i = 0U; i < 300U; ++i)
        mapper->clock_cpu();
    mapper->write_prg(0x9FF3U, 0x80U);
    for (unsigned i = 0U; i < 257U; ++i) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x9003U, 0x80U); // Acknowledge without reload.
    for (unsigned i = 0U; i < 65537U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0x9004U, 0U); // Enable survives terminal count and acknowledgement.
    for (unsigned i = 0U; i < 258U; ++i)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    reload_irq(*mapper, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned i = 0U; i < 65537U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("H3001 enable pauses the counter while latch writes leave it running",
          "[mappers][irem][irq]") {
    auto mapper = irem_board(irem_header(65U));
    reload_irq(*mapper, 3U);
    mapper->write_prg(0x9003U, 0x80U);
    mapper->clock_cpu();
    mapper->write_prg(0x9003U, 0U);
    for (unsigned i = 0U; i < 10U; ++i)
        mapper->clock_cpu();
    mapper->write_prg(0x9005U, 0xFFU);
    mapper->write_prg(0x9006U, 0xFFU);
    mapper->write_prg(0x9003U, 0x80U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x9006U, 1U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x9004U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->reset();
    for (unsigned i = 0U; i < 65537U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Irem CHR RAM banks and explicitly declared PRG memory retain contents",
          "[mappers][irem][state]") {
    for (const uint16_t id : {32U, 65U}) {
        auto header = irem_header(id);
        header.prg_ram_size = 0x1000U;
        header.prg_nvram_size = 0x1000U;
        auto mapper = irem_board(header, true);
        REQUIRE(mapper->maps_prg(0x6000U));
        REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
        mapper->write_prg(0x6000U, 0x11U);
        mapper->write_prg(0x7000U, 0x22U);
        REQUIRE(mapper->battery_ram().size() == 0x1000U);
        REQUIRE(mapper->battery_ram()[0U] == 0x22U);
        mapper->write_prg(0xB000U, 7U);
        mapper->write_chr(0U, 0x33U);
        mapper->write_prg(0xB001U, 7U);
        REQUIRE(mapper->read_chr(0x400U) == 0x33U);
        StateWriter state;
        mapper->save_state(state);
        mapper->write_chr(0U, 0x99U);
        mapper->write_prg(0x6000U, 0x99U);
        StateReader reader(state.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(mapper->read_chr(0U) == 0x33U);
        REQUIRE(mapper->read_prg(0x6000U) == 0x11U);
        mapper->reset();
        REQUIRE(mapper->read_prg(0x7000U) == 0x22U);
        std::vector<Byte> battery(0x1000U, 0x44U);
        mapper->set_battery_ram(battery);
        REQUIRE(mapper->read_prg(0x6000U) == 0x11U);
        REQUIRE(mapper->read_prg(0x7000U) == 0x44U);
        auto no_ram = irem_board(irem_header(id));
        REQUIRE_FALSE(no_ram->maps_prg(0x6000U));
        REQUIRE(no_ram->battery_ram().empty());
    }
}

TEST_CASE("H3001 snapshots preserve countdown and asserted IRQ", "[mappers][irem][state]") {
    auto mapper = irem_board(irem_header(65U));
    reload_irq(*mapper, 3U);
    mapper->write_prg(0x9003U, 0x80U);
    mapper->clock_cpu();
    StateWriter running;
    mapper->save_state(running);
    for (unsigned replay = 0U; replay < 2U; ++replay) {
        StateReader reader(running.data());
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
    }
    StateWriter pending;
    mapper->save_state(pending);
    mapper->write_prg(0x9003U, 0U);
    StateReader reader(pending.data());
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Irem declared small RAM mirrors and legacy implicit RAM stays absent",
          "[mappers][irem]") {
    for (const uint16_t id : {32U, 65U}) {
        auto header = irem_header(id);
        header.prg_ram_size = 0x800U;
        header.chr_ram_size = 0x800U;
        auto small = irem_board(header, true);
        small->write_prg(0x6000U, 0x12U);
        REQUIRE(small->read_prg(0x7800U) == 0x12U);
        small->write_prg(0xB000U, 3U); // Bank 3 aliases bank 1 on 2 KiB RAM.
        small->write_chr(0U, 0x34U);
        small->write_prg(0xB001U, 1U);
        REQUIRE(small->read_chr(0x400U) == 0x34U);
        REQUIRE(small->battery_ram().empty());
        header = irem_header(id);
        header.is_nes2 = false;
        header.prg_ram_size = 0x2000U; // Legacy parser's implicit default.
        auto legacy = irem_board(header);
        REQUIRE_FALSE(legacy->maps_prg(0x6000U));
        header.has_battery = true;
        auto battery = irem_board(header);
        REQUIRE(battery->maps_prg(0x6000U));
        REQUIRE(battery->battery_ram().size() == 0x2000U);
    }
}

TEST_CASE("Irem factory rejects unknown wiring and memory beyond the decoded address lines",
          "[mappers][irem]") {
    register_builtin_mappers();
    for (const uint16_t id : {32U, 65U}) {
        auto create = [](INesHeader header, std::size_t prg_size, std::size_t chr_size) {
            return MapperRegistry::instance().create(
                header, std::vector<Byte>(prg_size), std::vector<Byte>(chr_size));
        };
        auto header = irem_header(id);
        REQUIRE_FALSE(create(irem_header(id, 2U), 0x8000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x4000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8001U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x100000U, 0x2000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x80000U));
        REQUIRE_FALSE(create(header, 0x8000U, 0x2001U));
        header.prg_ram_size = 0x2000U;
        header.prg_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0x2000U));
        header = irem_header(id);
        header.chr_ram_size = 0x4000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0U));
        header = irem_header(id);
        header.chr_nvram_size = 0x2000U;
        REQUIRE_FALSE(create(header, 0x8000U, 0U));
    }
    REQUIRE_FALSE(MapperRegistry::instance().create(
        irem_header(65U, 1U), std::vector<Byte>(0x8000U), std::vector<Byte>(0x2000U)));
}

TEST_CASE("Irem snapshots reject invalid registers and flags", "[mappers][irem][state]") {
    for (const uint16_t id : {32U, 65U}) {
        auto mapper = irem_board(irem_header(id));
        StateWriter writer;
        mapper->save_state(writer);
        // Payload: two PRG banks, eight CHR banks, mode, mirror, two 16-bit
        // IRQ values, enable and pending bytes, then fixed-size RAM arrays.
        for (const std::size_t offset : {0U, 1U, 10U, 11U, 16U, 17U}) {
            auto damaged = writer.data();
            damaged[offset] = 0xFFU;
            StateReader reader(damaged);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
        }
    }
    auto fixed = irem_board(irem_header(32U, 1U));
    StateWriter writer;
    fixed->save_state(writer);
    auto damaged = writer.data();
    damaged[10U] = 1U;
    StateReader reader(damaged);
    fixed->load_state(reader);
    REQUIRE_FALSE(reader.ok());
}
