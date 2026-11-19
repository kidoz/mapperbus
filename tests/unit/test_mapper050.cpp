#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/n32_mapper050.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader make_header(bool nes2 = true, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 50U;
    header.is_nes2 = nes2;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    header.prg_ram_size = nes2 ? 0U : 8192U;
    return header;
}

std::vector<Byte> make_prg() {
    std::vector<Byte> rom(131072U);
    for (std::size_t i = 0; i < rom.size(); ++i)
        rom[i] = static_cast<Byte>((i / 8192U) ^ (i & 0xFFU));
    return rom;
}

std::vector<Byte> make_chr() {
    std::vector<Byte> rom(8192U);
    for (std::size_t i = 0; i < rom.size(); ++i)
        rom[i] = static_cast<Byte>(0xA5U ^ (i & 0xFFU) ^ (i >> 8U));
    return rom;
}

std::unique_ptr<N32Mapper050> make_board(bool nes2 = true, bool vertical = true) {
    return std::make_unique<N32Mapper050>(make_header(nes2, vertical), make_prg(), make_chr());
}

std::vector<Byte> save(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte decode_prg_bank(Byte value) {
    return static_cast<Byte>((value & 0x08U) | ((value & 0x06U) >> 1U) | ((value & 0x01U) << 2U));
}

TEST_CASE("Mapper 050 exposes only its documented ROM and write-only registers", "[mapper050]") {
    for (bool nes2 : {false, true}) {
        for (bool vertical : {false, true}) {
            auto mapper = make_board(nes2, vertical);
            REQUIRE(N32Mapper050::validate_geometry(make_header(nes2, vertical), 131072U, 8192U));
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(save(*mapper).size() == 6U);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());

            for (Address addr :
                 {0x0000U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
                REQUIRE(mapper->maps_prg(addr) == (addr >= 0x6000U));
                REQUIRE(mapper->maps_expansion(addr) == (addr >= 0x4020U && addr <= 0x5FFFU));
                REQUIRE(mapper->expansion_read_mask(addr) ==
                        (addr >= 0x4020U && addr <= 0x5FFFU ? 0U : 0xFFU));
            }
        }
    }
}

TEST_CASE("Mapper 050 maps all fixed and switchable PRG cells", "[mapper050]") {
    auto mapper = make_board();
    const auto rom = make_prg();
    const std::array<unsigned, 5> fixed_banks{15U, 8U, 9U, 0U, 11U};
    const std::array<Address, 5> bases{0x6000U, 0x8000U, 0xA000U, 0xC000U, 0xE000U};

    for (std::size_t window = 0; window < bases.size(); ++window) {
        for (Address offset = 0; offset < 8192U; ++offset) {
            REQUIRE(mapper->read_prg(static_cast<Address>(bases[window] + offset)) ==
                    rom[fixed_banks[window] * 8192U + offset]);
        }
    }

    for (unsigned bank = 0; bank < 16U; ++bank) {
        for (unsigned value = 0; value < 256U; ++value) {
            if (decode_prg_bank(static_cast<Byte>(value)) != bank)
                continue;
            mapper->write_expansion(0x4020U, static_cast<Byte>(value));
            for (Address offset = 0; offset < 8192U; ++offset) {
                REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) ==
                        rom[bank * 8192U + offset]);
            }
        }
    }
}

TEST_CASE("Mapper 050 decodes every PRG register alias and all data bits", "[mapper050]") {
    auto mapper = make_board();
    const auto rom = make_prg();
    for (unsigned value = 0; value < 256U; ++value) {
        const Byte byte = static_cast<Byte>(value);
        mapper->write_expansion(0x4020U, byte);
        REQUIRE(mapper->read_prg(0xD123U) == rom[decode_prg_bank(byte) * 8192U + 0x1123U]);
        REQUIRE(mapper->read_prg(0x6123U) == rom[15U * 8192U + 0x123U]);
        REQUIRE(mapper->read_prg(0x8123U) == rom[8U * 8192U + 0x123U]);
        REQUIRE(mapper->read_prg(0xA123U) == rom[9U * 8192U + 0x123U]);
        REQUIRE(mapper->read_prg(0xE123U) == rom[11U * 8192U + 0x123U]);
    }

    for (Address addr = 0x4020U; addr <= 0x5FFFU; ++addr) {
        mapper->reset();
        mapper->write_expansion(addr, 0x0FU);
        const Byte expected = (addr & 0x4120U) == 0x4020U ? 15U : 0U;
        REQUIRE(mapper->read_prg(0xC000U) == rom[expected * 8192U]);
    }

    const auto before = save(*mapper);
    for (Address addr : {0x0000U, 0x4000U, 0x401FU, 0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
        mapper->write_expansion(addr, 0xFFU);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(save(*mapper) == before);
}

TEST_CASE("Mapper 050 has fixed read-only CHR and no hidden RAM", "[mapper050]") {
    auto mapper = make_board();
    const auto rom = make_chr();
    for (Address addr = 0; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(addr) == rom[addr]);
        mapper->write_chr(addr, static_cast<Byte>(~rom[addr]));
        REQUIRE(mapper->read_chr(addr) == rom[addr]);
    }
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xFFU));
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 050 IRQ counts exactly 4096 enabled CPU cycles and holds until acknowledged",
          "[mapper050]") {
    auto mapper = make_board();
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_expansion(0x4120U, 1U);
    for (unsigned cycle = 1; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        INFO("cycle " << cycle);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    for (unsigned cycle = 0; cycle < 10000U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
    }

    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4120U, 1U);
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4120U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned cycle = 0; cycle < 10000U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 050 decodes every IRQ alias and ignores upper data bits", "[mapper050]") {
    auto mapper = make_board();
    for (Address addr = 0x4020U; addr <= 0x5FFFU; ++addr) {
        mapper->reset();
        mapper->write_expansion(addr, 0xFFU);
        const bool selected = (addr & 0x4120U) == 0x4120U;
        for (unsigned cycle = 0; cycle < 4096U; ++cycle)
            mapper->clock_cpu();
        REQUIRE(mapper->irq_pending() == selected);

        mapper->write_expansion(addr, 0x01U);
        REQUIRE(mapper->irq_pending() == selected);
        mapper->write_expansion(addr, 0x00U);
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 050 reset and cold power restore deterministic initial state", "[mapper050]") {
    for (bool vertical : {false, true}) {
        auto mapper = make_board(true, vertical);
        mapper->write_expansion(0x4020U, 0x0FU);
        mapper->write_expansion(0x4120U, 1U);
        for (unsigned cycle = 0; cycle < 4096U; ++cycle)
            mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());

        mapper->reset();
        REQUIRE(save(*mapper) == save(*make_board(true, vertical)));
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));

        mapper->write_expansion(0x4020U, 0x0FU);
        mapper->write_expansion(0x4120U, 1U);
        mapper->clock_cpu();
        mapper->power_on();
        REQUIRE(save(*mapper) == save(*make_board(true, vertical)));
    }
}

TEST_CASE("Mapper 050 rejects unsupported geometry and native memory declarations", "[mapper050]") {
    const auto valid = [](const INesHeader& header, std::size_t prg, std::size_t chr) {
        return N32Mapper050::validate_geometry(header, prg, chr).has_value();
    };
    REQUIRE(valid(make_header(false), 131072U, 8192U));
    for (std::size_t size : {0U, 8192U, 16384U, 32768U, 65536U, 131071U, 131073U, 262144U})
        REQUIRE_FALSE(valid(make_header(), size, 8192U));
    for (std::size_t size : {0U, 1U, 4096U, 8191U, 8193U, 16384U})
        REQUIRE_FALSE(valid(make_header(), 131072U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto header = make_header();
        header.submapper = submapper;
        REQUIRE_FALSE(valid(header, 131072U, 8192U));
    }
    for (bool nes2 : {false, true}) {
        auto header = make_header(nes2);
        header.has_battery = true;
        REQUIRE_FALSE(valid(header, 131072U, 8192U));
        for (MirrorMode mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            header = make_header(nes2);
            header.mirror_mode = mode;
            REQUIRE_FALSE(valid(header, 131072U, 8192U));
        }
    }
    for (unsigned field = 0; field < 4U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0:
            header.prg_ram_size = 8192U;
            break;
        case 1:
            header.prg_nvram_size = 8192U;
            break;
        case 2:
            header.chr_ram_size = 8192U;
            break;
        case 3:
            header.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(valid(header, 131072U, 8192U));
    }
    for (unsigned field = 1; field < 4U; ++field) {
        auto header = make_header(false);
        switch (field) {
        case 1:
            header.prg_nvram_size = 8192U;
            break;
        case 2:
            header.chr_ram_size = 8192U;
            break;
        case 3:
            header.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(valid(header, 131072U, 8192U));
    }
}

TEST_CASE("Mapper 050 state replays partial and pending IRQs without ROM bytes", "[mapper050]") {
    auto mapper = make_board();
    mapper->write_expansion(0x4020U, 0x0FU);
    mapper->write_expansion(0x4120U, 1U);
    for (unsigned cycle = 0; cycle < 1234U; ++cycle)
        mapper->clock_cpu();
    const auto partial = save(*mapper);
    REQUIRE(partial.size() == 6U);

    mapper->reset();
    StateReader restore(partial);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(save(*mapper) == partial);
    REQUIRE(mapper->read_prg(0xC000U) == make_prg()[15U * 8192U]);
    for (unsigned cycle = 1234U; cycle < 4096U; ++cycle)
        mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    const auto pending = save(*mapper);

    mapper->write_expansion(0x4120U, 0U);
    StateReader pending_reader(pending);
    mapper->load_state(pending_reader);
    REQUIRE(pending_reader.ok());
    REQUIRE(mapper->irq_pending());
    REQUIRE(save(*mapper) == pending);
}

TEST_CASE("Mapper 050 malformed state rejection is atomic", "[mapper050]") {
    auto mapper = make_board();
    mapper->write_expansion(0x4020U, 0x0FU);
    mapper->write_expansion(0x4120U, 1U);
    for (unsigned cycle = 0; cycle < 4096U; ++cycle)
        mapper->clock_cpu();
    const auto saved = save(*mapper);

    for (std::size_t size = 0; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == saved);
    }
    for (unsigned bit = 0; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == saved);
    }
    for (Byte bank : {0x10U, 0x80U, 0xFFU}) {
        auto bad = saved;
        bad[2] = bank;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == saved);
    }
    for (std::uint16_t counter : {0x1001U, 0x1FFFU, 0xFFFFU}) {
        auto bad = saved;
        std::memcpy(bad.data() + 3, &counter, sizeof(counter));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == saved);
    }
    for (Byte enabled : {2U, 0xFFU}) {
        auto bad = saved;
        bad[5] = enabled;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == saved);
    }
    auto bad = saved;
    bad[3] = 1U;
    bad[4] = 0U;
    bad[5] = 0U;
    StateReader inconsistent(bad);
    mapper->load_state(inconsistent);
    REQUIRE_FALSE(inconsistent.ok());
    REQUIRE(save(*mapper) == saved);

    auto other = make_board(true, false);
    const auto other_saved = save(*other);
    StateReader mismatch(saved);
    other->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(save(*other) == other_saved);
}

} // namespace
