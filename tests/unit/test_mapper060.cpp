#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/multicart060.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader make_header(bool nes2 = true, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 60U;
    header.is_nes2 = nes2;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    header.prg_ram_size = nes2 ? 0U : 8192U;
    return header;
}

std::vector<Byte> make_prg() {
    std::vector<Byte> rom(65536U);
    for (std::size_t addr = 0U; addr < rom.size(); ++addr)
        rom[addr] = static_cast<Byte>((addr / 16384U) ^ (addr & 0xFFU) ^ (addr >> 8U));
    return rom;
}

std::vector<Byte> make_chr() {
    std::vector<Byte> rom(32768U);
    for (std::size_t addr = 0U; addr < rom.size(); ++addr)
        rom[addr] = static_cast<Byte>(0x80U | ((addr / 8192U) ^ (addr & 0xFFU)));
    return rom;
}

std::unique_ptr<Multicart060> make_board(bool nes2 = true, bool vertical = true) {
    return std::make_unique<Multicart060>(make_header(nes2, vertical), make_prg(), make_chr());
}

std::vector<Byte> save(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 060 exposes only four NROM-128 games and fixed CIRAM wiring", "[mapper060]") {
    for (bool nes2 : {false, true}) {
        for (bool vertical : {false, true}) {
            auto mapper = make_board(nes2, vertical);
            REQUIRE(Multicart060::validate_geometry(make_header(nes2, vertical), 65536U, 32768U));
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(save(*mapper).size() == 3U);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
                REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x8000U));
                REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
                REQUIRE(mapper->prg_read_mask(static_cast<Address>(addr)) == 0xFFU);
                REQUIRE(mapper->expansion_read_mask(static_cast<Address>(addr)) == 0xFFU);
            }
        }
    }
}

TEST_CASE("Mapper 060 soft reset cycles through all PRG and CHR blocks", "[mapper060]") {
    auto mapper = make_board();
    const auto prg = make_prg();
    const auto chr = make_chr();
    for (unsigned selection = 0U; selection < 16U; ++selection) {
        const unsigned game = selection & 3U;
        INFO("selection " << selection);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr)
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) ==
                    prg[game * 16384U + (addr & 0x3FFFU)]);
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr[game * 8192U + addr]);
        mapper->reset();
    }
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8123U) == prg[0x123U]);
    REQUIRE(mapper->read_chr(0x123U) == chr[0x123U]);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x8123U) == prg[0x4123U]);
    REQUIRE(mapper->read_chr(0x123U) == chr[0x2123U]);
}

TEST_CASE("Mapper 060 has no writable registers bus conflicts RAM or CHR writes", "[mapper060]") {
    auto mapper = make_board();
    mapper->reset();
    mapper->reset();
    const auto selected = save(*mapper);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        mapper->write_prg_cpu(0xFFFFU, static_cast<Byte>(value), value % 2U == 0U);
        mapper->write_expansion(0x4020U, static_cast<Byte>(value));
        REQUIRE(save(*mapper) == selected);
    }
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
        mapper->write_expansion(static_cast<Address>(addr), 0xFFU);
    }
    REQUIRE(save(*mapper) == selected);

    const auto prg = make_prg();
    const auto chr = make_chr();
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(~chr[0x4000U + addr]));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr[0x4000U + addr]);
    }
    REQUIRE(mapper->read_prg(0x8123U) == prg[0x8123U]);
    REQUIRE(mapper->read_prg(0xC123U) == prg[0x8123U]);
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA7U));
    REQUIRE(mapper->battery_ram().empty());
    mapper->clock_cpu();
    mapper->clock_irq_counter();
    mapper->clock_audio();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 060 rejects unsupported geometry and native memory declarations", "[mapper060]") {
    const auto reject =
        [](const INesHeader& header, std::size_t prg_size = 65536U, std::size_t chr_size = 32768U) {
            REQUIRE_FALSE(Multicart060::validate_geometry(header, prg_size, chr_size));
        };
    for (std::size_t size : {0UZ, 16384UZ, 32768UZ, 49152UZ, 65535UZ, 65537UZ, 131072UZ})
        reject(make_header(), size);
    for (std::size_t size : {0UZ, 8192UZ, 16384UZ, 24576UZ, 32767UZ, 32769UZ, 65536UZ})
        reject(make_header(), 65536U, size);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto header = make_header();
        header.submapper = submapper;
        reject(header);
    }
    for (bool nes2 : {false, true}) {
        auto header = make_header(nes2);
        header.has_battery = true;
        reject(header);
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            header = make_header(nes2);
            header.mirror_mode = mode;
            reject(header);
        }
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.prg_ram_size = 8192U;
            break;
        case 1U:
            header.prg_nvram_size = 8192U;
            break;
        case 2U:
            header.chr_ram_size = 8192U;
            break;
        case 3U:
            header.chr_nvram_size = 8192U;
            break;
        default:
            break;
        }
        reject(header);
    }
}

TEST_CASE("Mapper 060 snapshots replay the reset phase and reject damage atomically",
          "[mapper060][state]") {
    auto mapper = make_board();
    mapper->reset();
    mapper->reset();
    const auto selected = save(*mapper);
    auto copy = make_board();
    StateReader replay(selected);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(save(*copy) == selected);
    REQUIRE(copy->read_prg(0x8123U) == mapper->read_prg(0x8123U));
    REQUIRE(copy->read_chr(0x123U) == mapper->read_chr(0x123U));

    for (std::size_t size = 0U; size < selected.size(); ++size) {
        StateReader truncated(std::span(selected).first(size));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(save(*mapper) == selected);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = selected;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader invalid(bad);
        mapper->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(save(*mapper) == selected);
    }
    for (unsigned game = 0U; game < 256U; ++game) {
        auto bytes = selected;
        bytes[2] = static_cast<Byte>(game);
        StateReader reader(bytes);
        mapper->load_state(reader);
        if (game < 4U) {
            REQUIRE(reader.ok());
            REQUIRE(save(*mapper) == bytes);
            StateReader restore_selected(selected);
            mapper->load_state(restore_selected);
            REQUIRE(restore_selected.ok());
        } else {
            REQUIRE_FALSE(reader.ok());
            REQUIRE(save(*mapper) == selected);
        }
    }
    mapper->power_on();
    StateReader restore(selected);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(save(*mapper) == selected);
    mapper->reset();
    copy->reset();
    REQUIRE(save(*mapper) == save(*copy));
}

} // namespace
