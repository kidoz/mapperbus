#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mmc5.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 5U;
    h.mirror_mode = mirror;
    return h;
}

Byte cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

std::vector<Byte> chr_rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = cell(static_cast<unsigned>(i / 1024U), static_cast<unsigned>(i % 1024U));
    return data;
}

std::unique_ptr<Mmc5> board(MirrorMode mirror = MirrorMode::Vertical) {
    // 32 KiB PRG, 64 KiB CHR so every bank value used below stays in range.
    std::vector<Byte> prg(32768U);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = static_cast<Byte>(i);
    return std::make_unique<Mmc5>(header(mirror), std::move(prg), chr_rom(65536U));
}

void reg(Mmc5& mapper, Address addr, Byte value) {
    mapper.write_expansion(addr, value);
}

/// Expected CHR byte for a translated 1 KiB-window offset.
Byte chr_byte(unsigned offset) {
    return cell(offset / 1024U, offset % 1024U);
}

TEST_CASE("Mapper 005 decodes $5105 quadrant selects into CIRAM pages", "[mappers][mapper005]") {
    auto mapper = board();

    // Header default (vertical) becomes the initial quadrant selects.
    REQUIRE(mapper->nametable_address(0x2000U) == 0x0000U);
    REQUIRE(mapper->nametable_address(0x2400U) == 0x0000U);
    REQUIRE(mapper->nametable_address(0x2800U) == 0x0400U);
    REQUIRE(mapper->nametable_address(0x2C00U) == 0x0400U);

    // $50 mirrors the Castlevania III arrangement: vertical mirroring.
    reg(*mapper, 0x5105U, 0x50U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->nametable_address(0x2000U) == 0x0000U);
    REQUIRE(mapper->nametable_address(0x23FFU) == 0x03FFU);
    REQUIRE(mapper->nametable_address(0x2400U) == 0x0000U);
    REQUIRE(mapper->nametable_address(0x2800U) == 0x0400U);
    REQUIRE(mapper->nametable_address(0x2C00U) == 0x0400U);
    // The $3000-$3EFF alias range folds onto the same quadrants.
    REQUIRE(mapper->nametable_address(0x3800U) == 0x0400U);

    // $44 is the equivalent of horizontal mirroring.
    reg(*mapper, 0x5105U, 0x44U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->nametable_address(0x2000U) == 0x0000U);
    REQUIRE(mapper->nametable_address(0x2400U) == 0x0400U);
    REQUIRE(mapper->nametable_address(0x2800U) == 0x0000U);
    REQUIRE(mapper->nametable_address(0x2C00U) == 0x0400U);
    REQUIRE(mapper->nametable_address(0x3400U) == 0x0400U);

    reg(*mapper, 0x5105U, 0x55U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(mapper->nametable_address(0x2000U) == 0x0400U);
    REQUIRE(mapper->nametable_address(0x2C00U) == 0x0400U);

    reg(*mapper, 0x5105U, 0x00U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(mapper->nametable_address(0x2800U) == 0x0000U);

    // ExRAM (2) and fill (3) selections are not modeled: those quadrants fall
    // back to the header mirror mode while representable quadrants stay exact.
    reg(*mapper, 0x5105U, 0xFFU);
    REQUIRE(mapper->nametable_address(0x2000U) == std::nullopt);
    REQUIRE(mapper->nametable_address(0x2C00U) == std::nullopt);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);

    reg(*mapper, 0x5105U, 0x3FU);
    REQUIRE(mapper->nametable_address(0x2C00U) == 0x0000U);
    REQUIRE(mapper->nametable_address(0x2000U) == std::nullopt);
    REQUIRE(mapper->nametable_address(0x2400U) == std::nullopt);
    REQUIRE(mapper->nametable_address(0x2800U) == std::nullopt);
}

TEST_CASE("Mapper 005 separates background and sprite CHR banks in 1 KiB mode",
          "[mappers][mapper005]") {
    auto mapper = board();
    reg(*mapper, 0x5101U, 0x03U);

    constexpr std::array<Byte, 8> kSprite = {{10U, 11U, 12U, 13U, 14U, 15U, 16U, 17U}};
    constexpr std::array<Byte, 4> kBackground = {{20U, 21U, 22U, 23U}};
    for (unsigned index = 0U; index < kSprite.size(); ++index)
        reg(*mapper, static_cast<Address>(0x5120U + index), kSprite[index]);
    for (unsigned index = 0U; index < kBackground.size(); ++index)
        reg(*mapper, static_cast<Address>(0x5128U + index), kBackground[index]);

    // Background fetches use $5128-$512B; each register covers the mirrored
    // pair of windows sharing the same 1 KiB index in both pattern tables.
    for (unsigned window = 0U; window < 8U; ++window) {
        const Address pattern = static_cast<Address>(window * 0x400U + 0x021U);
        const unsigned bank = kBackground[window & 3U];
        const auto fetch = mapper->background_fetch(0x2000U, pattern, 0U, 0U);
        REQUIRE(fetch.has_value());
        REQUIRE(fetch->planes.has_value());
        REQUIRE((*fetch->planes)[0] == chr_byte(bank * 0x400U + 0x021U));
        REQUIRE((*fetch->planes)[1] == chr_byte(bank * 0x400U + 0x029U));
    }

    // Sprite fetches use $5120-$5127 sequentially across all eight windows.
    for (unsigned window = 0U; window < 8U; ++window) {
        const Address pattern = static_cast<Address>(window * 0x400U + 0x043U);
        const auto planes = mapper->sprite_pattern(0U, pattern, false);
        REQUIRE(planes.has_value());
        REQUIRE((*planes)[0] == chr_byte(kSprite[window] * 0x400U + 0x043U));
        REQUIRE((*planes)[1] == chr_byte(kSprite[window] * 0x400U + 0x04BU));
    }
}

TEST_CASE("Mapper 005 applies window sizes per $5101 mode", "[mappers][mapper005]") {
    auto mapper = board();
    const Address offsets[] = {0x0000U, 0x0042U, 0x03FDU};

    SECTION("8 KiB mode: $5127 sprites, $512B background") {
        reg(*mapper, 0x5101U, 0x00U);
        reg(*mapper, 0x5127U, 1U);
        reg(*mapper, 0x512BU, 2U);
        for (Address offset : offsets) {
            const Address pattern = offset;
            auto sprite = mapper->sprite_pattern(0U, pattern, false);
            auto background = mapper->background_fetch(0x2000U, pattern, 0U, 0U);
            REQUIRE(sprite.has_value());
            REQUIRE((*sprite)[0] == chr_byte(0x2000U + offset));
            REQUIRE(background.has_value());
            REQUIRE(background->planes.has_value());
            REQUIRE((*background->planes)[0] == chr_byte(0x4000U + offset));
        }
    }

    SECTION("4 KiB mode: $5121/$5123 sprites, $5129/$512B background") {
        reg(*mapper, 0x5101U, 0x01U);
        reg(*mapper, 0x5121U, 3U);
        reg(*mapper, 0x5123U, 4U);
        reg(*mapper, 0x5129U, 5U);
        reg(*mapper, 0x512BU, 6U);
        for (Address offset : offsets) {
            auto sprite_low = mapper->sprite_pattern(0U, offset, false);
            auto sprite_high =
                mapper->sprite_pattern(0U, static_cast<Address>(0x1000U + offset), false);
            auto background_low = mapper->background_fetch(0x2000U, offset, 0U, 0U);
            auto background_high =
                mapper->background_fetch(0x2000U, static_cast<Address>(0x1000U + offset), 0U, 0U);
            REQUIRE(sprite_low.has_value());
            REQUIRE((*sprite_low)[0] == chr_byte(0x3000U + offset));
            REQUIRE(sprite_high.has_value());
            REQUIRE((*sprite_high)[0] == chr_byte(0x4000U + offset));
            REQUIRE(background_low.has_value());
            REQUIRE((*background_low->planes)[0] == chr_byte(0x5000U + offset));
            REQUIRE(background_high.has_value());
            REQUIRE((*background_high->planes)[0] == chr_byte(0x6000U + offset));
        }
    }

    SECTION("2 KiB mode: odd sprite registers, $5129/$512B background pairs") {
        reg(*mapper, 0x5101U, 0x02U);
        reg(*mapper, 0x5121U, 1U);
        reg(*mapper, 0x5123U, 2U);
        reg(*mapper, 0x5125U, 3U);
        reg(*mapper, 0x5127U, 4U);
        reg(*mapper, 0x5129U, 6U);
        reg(*mapper, 0x512BU, 7U);
        for (Address offset : offsets) {
            const Address bases[] = {0x0000U, 0x0800U, 0x1000U, 0x1800U};
            const Byte sprite_banks[] = {1U, 2U, 3U, 4U};
            const Byte background_banks[] = {6U, 7U, 6U, 7U};
            for (unsigned window = 0U; window < 4U; ++window) {
                const Address pattern = static_cast<Address>(bases[window] + offset);
                auto sprite = mapper->sprite_pattern(0U, pattern, false);
                auto background = mapper->background_fetch(0x2000U, pattern, 0U, 0U);
                REQUIRE(sprite.has_value());
                REQUIRE((*sprite)[0] ==
                        chr_byte(sprite_banks[window] * 0x0800U + (pattern & 0x07FFU)));
                REQUIRE(background.has_value());
                REQUIRE((*background->planes)[0] ==
                        chr_byte(background_banks[window] * 0x0800U + (pattern & 0x07FFU)));
            }
        }
    }
}

TEST_CASE("Mapper 005 PPUDATA CHR reads use the last-written register set",
          "[mappers][mapper005]") {
    auto mapper = board();
    reg(*mapper, 0x5101U, 0x03U);
    reg(*mapper, 0x5128U, 20U);

    // The background register write above made the background set current.
    CHECK(mapper->read_chr(0x0000U) == chr_byte(20U * 0x400U));
    // Sprite register writes move PPUDATA back onto the sprite set.
    reg(*mapper, 0x5120U, 10U);
    reg(*mapper, 0x5121U, 11U);
    CHECK(mapper->read_chr(0x0000U) == chr_byte(10U * 0x400U));
    CHECK(mapper->read_chr(0x0400U) == chr_byte(11U * 0x400U));
    // Another background write selects the background set, whose windows
    // mirror in pairs across both pattern tables.
    reg(*mapper, 0x5129U, 21U);
    CHECK(mapper->read_chr(0x0000U) == chr_byte(20U * 0x400U));
    CHECK(mapper->read_chr(0x0400U) == chr_byte(21U * 0x400U));
    CHECK(mapper->read_chr(0x1000U) == chr_byte(20U * 0x400U));
    // The rendered fetch hooks stay independent of the PPUDATA selection.
    auto background = mapper->background_fetch(0x2000U, 0x0000U, 0U, 0U);
    auto sprite = mapper->sprite_pattern(0U, 0x0000U, false);
    REQUIRE(background.has_value());
    REQUIRE((*background->planes)[0] == chr_byte(20U * 0x400U));
    REQUIRE(sprite.has_value());
    REQUIRE((*sprite)[0] == chr_byte(10U * 0x400U));
}

TEST_CASE("Mapper 005 state round-trips CHR sets, quadrant selects and IRQ",
          "[mappers][mapper005]") {
    auto mapper = board();
    reg(*mapper, 0x5101U, 0x01U);
    reg(*mapper, 0x5121U, 3U);
    reg(*mapper, 0x5123U, 4U);
    reg(*mapper, 0x5128U, 5U);
    reg(*mapper, 0x5129U, 6U);
    reg(*mapper, 0x512BU, 7U);
    reg(*mapper, 0x5105U, 0x44U);
    reg(*mapper, 0x5203U, 0x2EU);
    reg(*mapper, 0x5204U, 0x80U);
    reg(*mapper, 0x5205U, 6U);
    reg(*mapper, 0x5206U, 7U);
    mapper->write_prg(0x6000U, 0x9DU);

    StateWriter writer;
    mapper->save_state(writer);
    const auto saved = writer.take();

    auto copy = board(MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    CHECK(copy->read_chr(0x0000U) == mapper->read_chr(0x0000U));
    CHECK(copy->read_chr(0x1000U) == mapper->read_chr(0x1000U));
    CHECK(copy->nametable_address(0x2800U) == 0x0000U);
    CHECK(copy->read_prg(0x6000U) == 0x9DU);
    CHECK(copy->read_expansion(0x5205U) == 42U);
    CHECK(copy->mirror_mode() == MirrorMode::Horizontal);

    // The last-written set was the background set ($5128 above).
    CHECK(copy->read_chr(0x0000U) == chr_byte(6U * 0x1000U));
    for (unsigned tick = 0U; tick < 0x2EU; ++tick) {
        mapper->clock_irq_counter();
        copy->clock_irq_counter();
    }
    CHECK(mapper->irq_pending());
    CHECK(copy->irq_pending());

    // Truncated blobs must be rejected outright.
    for (std::size_t length :
         {std::size_t{0U}, std::size_t{1U}, std::size_t{4096U}, saved.size() - 1U}) {
        StateReader short_reader{std::span(saved).first(length)};
        copy->load_state(short_reader);
        CHECK_FALSE(short_reader.ok());
    }
    CHECK(copy->read_chr(0x0000U) == chr_byte(6U * 0x1000U));
}

TEST_CASE("Mapper 005 reconstructs pre-v23 snapshots from sprite registers",
          "[mappers][mapper005]") {
    auto mapper = board();
    reg(*mapper, 0x5101U, 0x03U);
    reg(*mapper, 0x5120U, 0U);
    reg(*mapper, 0x5121U, 1U);
    reg(*mapper, 0x5122U, 2U);
    reg(*mapper, 0x5123U, 3U);
    reg(*mapper, 0x5124U, 4U);
    reg(*mapper, 0x5125U, 5U);
    reg(*mapper, 0x5126U, 6U);
    reg(*mapper, 0x5127U, 7U);

    // A version 22 blob ends after the multiplier operands and has no
    // background registers or $5105 quadrant selects.
    StateWriter writer;
    std::array<Byte, 0x10000> prg_ram{};
    std::array<Byte, 0x2000> chr_ram{};
    std::array<Byte, 5> prg_banks = {{0U, 0U, 0U, 0U, 3U}};
    std::array<uint16_t, 8> sprite_registers = {{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}};
    std::array<Byte, 2> multiplier = {{0U, 0U}};
    writer.write_array(prg_ram);
    writer.write_array(chr_ram);
    writer.write<Byte>(3U);
    writer.write_array(prg_banks);
    writer.write<Byte>(3U);
    writer.write_array(sprite_registers);
    writer.write(static_cast<Byte>(MirrorMode::Vertical));
    writer.write(Mmc5Pulse{});
    writer.write(Mmc5Pulse{});
    writer.write<Byte>(0U);
    writer.write<Byte>(0U);
    writer.write<Byte>(0U);
    writer.write<Byte>(0U);
    writer.write<std::uint32_t>(0U);
    writer.write<Byte>(0U);
    writer.write<std::uint16_t>(0U);
    writer.write<Byte>(0U);
    writer.write<Byte>(0U);
    writer.write<Byte>(1U);
    writer.write_array(multiplier);

    StateReader reader(writer.data());
    mapper->load_state_version(reader, 22U);
    REQUIRE(reader.ok());

    // The background set mirrors the old sprite registers 4-7, and the
    // quadrant selects derive from the stored header mirroring.
    auto background = mapper->background_fetch(0x2000U, 0x1400U, 0U, 0U);
    REQUIRE(background.has_value());
    REQUIRE(background->planes.has_value());
    REQUIRE((*background->planes)[0] == chr_byte(5U * 0x400U));
    CHECK(mapper->nametable_address(0x2800U) == 0x0400U);
    CHECK(mapper->nametable_address(0x2400U) == 0x0000U);
    CHECK(mapper->read_chr(0x0000U) == chr_byte(0U * 0x400U));
}

} // namespace
