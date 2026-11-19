#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "core/mappers/mapper301.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {
enum class Population { Multicart, Rpg, Et113 };
constexpr std::size_t kMainBytes = 524288U;
constexpr std::size_t kEtBytes = 655360U;

INesHeader make_header(Population population = Population::Multicart,
                       bool legacy = false,
                       MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader header{};
    header.mapper_number = 301U;
    header.is_nes2 = !legacy;
    header.has_battery = population == Population::Rpg;
    header.mirror_mode = mirror;
    header.prg_ram_size = legacy ? 8192U : 0U;
    header.prg_nvram_size = header.has_battery && !legacy ? 8192U : 0U;
    header.chr_ram_size = 8192U;
    return header;
}

Byte rom_byte(std::size_t offset) {
    return static_cast<Byte>((offset * 37U) ^ (offset >> 5U) ^ (offset >> 13U));
}

std::vector<Byte> make_rom(Population population) {
    const std::size_t size = population == Population::Et113 ? kEtBytes : kMainBytes;
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0; offset < size; ++offset)
        rom[offset] = rom_byte(offset);
    return rom;
}

std::unique_ptr<Mapper301> make_board(Population population = Population::Multicart,
                                      bool legacy = false,
                                      Byte pads = 0U) {
    auto board = std::make_unique<Mapper301>(
        make_header(population, legacy), make_rom(population), std::vector<Byte>{});
    REQUIRE(board->configure_board_jumpers(pads));
    return board;
}

std::size_t expected_offset(Address cpu, Address latch, Population population, Byte pads = 0U) {
    const bool upper = cpu >= 0xC000U;
    unsigned inner = (latch >> 2U) & 7U;
    if ((latch & 1U) != 0U) {
        inner = (inner & 6U) | (upper ? 1U : 0U);
    } else if (upper && population != Population::Rpg && (latch & 0x80U) == 0U) {
        inner = (latch & 0x200U) != 0U ? 7U : 0U;
    }

    std::size_t offset = 0U;
    if (population == Population::Et113 && (latch & 0x400U) == 0U) {
        offset = kMainBytes + inner * 16384U;
    } else {
        const unsigned outer = (latch >> 5U) & 3U;
        offset = (outer * 8U + inner) * 16384U;
    }
    offset += cpu & 0x3FFFU;
    if ((latch & 0x100U) != 0U)
        offset = (offset & ~31U) | pads;
    return offset;
}

std::vector<Byte> save(const Mapper301& board) {
    StateWriter writer;
    board.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 301 validates only the documented board populations", "[mapper301]") {
    for (bool legacy : {false, true}) {
        for (auto population : {Population::Multicart, Population::Rpg, Population::Et113}) {
            for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                const auto header = make_header(population, legacy, mirror);
                const std::size_t prg_size =
                    population == Population::Et113 ? kEtBytes : kMainBytes;
                REQUIRE(Mapper301::validate_geometry(header, prg_size, 0U));
            }
        }
    }

    for (std::size_t size :
         {0U, 16384U, 32768U, 131072U, 524287U, 524289U, 655359U, 655361U, 1048576U}) {
        REQUIRE_FALSE(Mapper301::validate_geometry(make_header(), size, 0U));
    }
    for (std::size_t chr_size : {1U, 8192U, 16384U})
        REQUIRE_FALSE(Mapper301::validate_geometry(make_header(), kMainBytes, chr_size));
    REQUIRE_FALSE(Mapper301::validate_geometry(make_header(Population::Rpg), kEtBytes, 0U));

    auto header = make_header();
    header.submapper = 1U;
    REQUIRE_FALSE(Mapper301::validate_geometry(header, kMainBytes, 0U));
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        header = make_header();
        header.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper301::validate_geometry(header, kMainBytes, 0U));
    }

    for (auto population : {Population::Multicart, Population::Rpg}) {
        for (unsigned field = 0U; field < 5U; ++field) {
            header = make_header(population);
            switch (field) {
            case 0:
                header.prg_ram_size = 8192U;
                break;
            case 1:
                header.prg_nvram_size = population == Population::Rpg ? 0U : 8192U;
                break;
            case 2:
                header.chr_ram_size = 0U;
                break;
            case 3:
                header.chr_ram_size = 16384U;
                break;
            case 4:
                header.chr_nvram_size = 8192U;
                break;
            }
            REQUIRE_FALSE(Mapper301::validate_geometry(header, kMainBytes, 0U));
        }
    }
}

TEST_CASE("Mapper 301 decodes every address latch bit and ignores write data", "[mapper301]") {
    for (auto population : {Population::Multicart, Population::Rpg, Population::Et113}) {
        for (bool legacy : {false, true}) {
            auto board = make_board(population, legacy);
            for (Address latch = 0U; latch <= 0x7FFU; ++latch) {
                const auto port = static_cast<Address>(0x8000U | latch);
                board->write_prg(port, static_cast<Byte>(latch));
                for (Address cpu : {0x8000U, 0x9A1FU, 0xBFFFU, 0xC000U, 0xE5A0U, 0xFFFFU}) {
                    INFO("population=" << static_cast<int>(population) << " latch=" << latch
                                       << " cpu=" << cpu);
                    REQUIRE(board->read_prg(cpu) ==
                            rom_byte(expected_offset(cpu, latch, population)));
                }
                REQUIRE(board->mirror_mode() ==
                        ((latch & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));

                const auto before = save(*board);
                board->write_prg(port, static_cast<Byte>(~latch));
                REQUIRE(save(*board) == before);
            }
        }
    }
}

TEST_CASE("Mapper 301 ET-113 chip selection and solder pads follow physical pins", "[mapper301]") {
    auto et113 = make_board(Population::Et113);
    for (Address latch : {0x000U, 0x040U, 0x200U, 0x240U, 0x400U, 0x7C0U}) {
        et113->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        for (Address cpu : {0x8000U, 0xC000U, 0xFFFFU})
            REQUIRE(et113->read_prg(cpu) ==
                    rom_byte(expected_offset(cpu, latch, Population::Et113)));
    }

    for (auto population : {Population::Multicart, Population::Rpg, Population::Et113}) {
        auto board = make_board(population);
        for (Byte pads = 0U; pads < 32U; ++pads) {
            REQUIRE(board->configure_board_profile("menu-" + std::to_string(pads)));
            for (Address latch : {0x100U, 0x17CU, 0x301U, 0x5FDU, 0x7FFU}) {
                board->write_prg(static_cast<Address>(0x8000U | latch), 0U);
                for (Address cpu : {0x8000U, 0x801FU, 0xC000U, 0xFFDFU})
                    REQUIRE(board->read_prg(cpu) ==
                            rom_byte(expected_offset(cpu, latch, population, pads)));
            }
        }
    }
}

TEST_CASE("Mapper 301 RAM protection and open-bus participation match population", "[mapper301]") {
    for (auto population : {Population::Multicart, Population::Rpg, Population::Et113}) {
        auto board = make_board(population);
        const bool battery = population == Population::Rpg;
        REQUIRE(board->battery_ram().size() == (battery ? 8192U : 0U));
        for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
            REQUIRE(board->maps_prg(addr) == (addr >= 0x8000U || (battery && addr >= 0x6000U)));
            REQUIRE(board->prg_read_mask(addr) == 0xFFU);
            REQUIRE_FALSE(board->maps_expansion(addr));
        }
        REQUIRE(board->cpu_open_bus_mask() == 0xFFU);
        REQUIRE_FALSE(board->irq_pending());
        REQUIRE_FALSE(board->has_expansion_audio());

        for (Address latch = 0U; latch <= 0x7FFU; ++latch) {
            board->write_prg(0x8000U, 0U);
            board->write_chr(0x1234U, 0x5AU);
            board->write_prg(static_cast<Address>(0x8000U | latch), 0U);
            board->write_chr(0x1234U, 0xA7U);
            REQUIRE(board->read_chr(0x1234U) == (battery || (latch & 0x80U) == 0U ? 0xA7U : 0x5AU));
        }

        board->write_prg(0x8000U, 0U);
        for (Address addr = 0U; addr < 8192U; ++addr) {
            board->write_chr(addr, static_cast<Byte>(addr));
            board->write_prg(static_cast<Address>(0x6000U + addr), static_cast<Byte>(addr ^ 0x5AU));
        }
        for (Address addr = 0U; addr < 8192U; ++addr) {
            REQUIRE(board->read_chr(addr) == static_cast<Byte>(addr));
            if (battery)
                REQUIRE(board->read_prg(static_cast<Address>(0x6000U + addr)) ==
                        static_cast<Byte>(addr ^ 0x5AU));
        }
    }
}

TEST_CASE("Mapper 301 reset and power policies retain only documented persistent state",
          "[mapper301]") {
    for (auto population : {Population::Multicart, Population::Rpg, Population::Et113}) {
        auto board = make_board(population, false, 23U);
        const bool battery = population == Population::Rpg;
        board->write_chr(0x2345U, 0xA7U);
        board->write_prg(0x6123U, 0x91U);
        board->write_prg(0xFFFFU, 0U);
        board->reset();
        REQUIRE(board->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(board->read_chr(0x2345U) == 0xA7U);
        REQUIRE(board->read_prg(0x8000U) ==
                rom_byte(population == Population::Et113 ? kMainBytes : 0U));
        REQUIRE(board->read_prg(0x6123U) == (battery ? 0x91U : 0U));

        board->power_on();
        REQUIRE(board->read_chr(0x2345U) == 0U);
        REQUIRE(board->read_prg(0x6123U) == (battery ? 0x91U : 0U));
        board->write_prg(0x8100U, 0U);
        REQUIRE(board->read_prg(0x8000U) ==
                rom_byte((population == Population::Et113 ? kMainBytes : 0U) + 23U));

        if (battery) {
            std::vector<Byte> imported(8193U, 0x7BU);
            board->set_battery_ram(imported);
            REQUIRE(board->read_prg(0x6000U) == 0x7BU);
            REQUIRE(board->read_prg(0x7FFFU) == 0x7BU);
        }
    }
}

TEST_CASE("Mapper 301 rejects invalid board configuration without mutation", "[mapper301]") {
    auto board = make_board(Population::Multicart, false, 19U);
    board->write_prg(0x8123U, 0U);
    const auto before = save(*board);
    REQUIRE_FALSE(board->configure_board_jumpers(32U));
    for (auto profile :
         {"menu-", "menu--1", "menu-32", "menu-256", "menu-999999999999", "menu-1x", "unknown"}) {
        REQUIRE_FALSE(board->configure_board_profile(profile));
    }
    REQUIRE(save(*board) == before);
}

TEST_CASE("Mapper 301 snapshots are bounded population-specific and atomic", "[mapper301]") {
    for (auto population : {Population::Multicart, Population::Rpg, Population::Et113}) {
        auto board = make_board(population, false, 19U);
        const bool battery = population == Population::Rpg;
        board->write_prg(0x8000U, 0U);
        board->write_chr(0x1234U, 0xA7U);
        board->write_prg(0x6000U, 0x91U);
        board->write_prg(0xFFFFU, 0U);
        const auto saved = save(*board);
        REQUIRE(saved.size() == (battery ? 16388U : 8196U));

        const std::array<std::size_t, 7> truncations{0U, 1U, 2U, 3U, 4U, 8195U, saved.size() - 1U};
        for (std::size_t size : truncations) {
            StateReader reader(std::span<const Byte>(saved).first(size));
            board->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(save(*board) == saved);
        }

        for (unsigned bit = 0U; bit < 16U; ++bit) {
            auto damaged = saved;
            damaged[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
            StateReader reader(damaged);
            board->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(save(*board) == saved);
        }
        auto invalid_latch = saved;
        invalid_latch[2] = 0xFFU;
        invalid_latch[3] = 0xFFU;
        StateReader invalid_reader(invalid_latch);
        board->load_state(invalid_reader);
        REQUIRE_FALSE(invalid_reader.ok());
        REQUIRE(save(*board) == saved);

        for (auto other : {Population::Multicart, Population::Rpg, Population::Et113}) {
            if (other == population)
                continue;
            auto other_board = make_board(other, false, 19U);
            const auto other_saved = save(*other_board);
            StateReader reader(saved);
            other_board->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(save(*other_board) == other_saved);
        }

        board->power_on();
        StateReader valid_reader(saved);
        board->load_state(valid_reader);
        REQUIRE(valid_reader.ok());
        REQUIRE(save(*board) == saved);

        REQUIRE(board->configure_board_jumpers(20U));
        const auto changed_pads = save(*board);
        StateReader wrong_pads(saved);
        board->load_state(wrong_pads);
        REQUIRE_FALSE(wrong_pads.ok());
        REQUIRE(save(*board) == changed_pads);
    }
}

} // namespace
