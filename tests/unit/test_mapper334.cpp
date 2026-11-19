#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper334.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 128UZ * 1024UZ;
constexpr std::size_t kChrSize = 256UZ * 1024UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 334U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 13U + (offset >> 8U) * 29U + (offset >> 15U) * 47U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0; i < size; ++i)
        data[i] = cell(i);
    return data;
}

std::unique_ptr<Mapper334> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper334::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper334>(h, rom(prg_size), rom(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_chr(Mapper334& mapper, unsigned reg, Byte bank, bool inverted = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>(reg | (inverted ? 0x80U : 0U)));
    mapper.write_prg(0x8001U, bank);
}

TEST_CASE("Mapper 334 validates native ROM-only geometry", "[mapper334]") {
    for (std::size_t prg : {32768UZ, 65536UZ, 131072UZ}) {
        for (std::size_t chr : {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ})
            REQUIRE(Mapper334::validate_geometry(header(), prg, chr));
    }
    for (std::size_t prg : {0UZ, 16384UZ, 49152UZ, 262144UZ})
        REQUIRE_FALSE(Mapper334::validate_geometry(header(), prg, kChrSize));
    for (std::size_t chr : {0UZ, 4096UZ, 12288UZ, 524288UZ})
        REQUIRE_FALSE(Mapper334::validate_geometry(header(), kPrgSize, chr));
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper334::validate_geometry(h, kPrgSize, kChrSize));
    h = header();
    h.submapper = 1U;
    REQUIRE_FALSE(Mapper334::validate_geometry(h, kPrgSize, kChrSize));
    h = header();
    h.mapper_number = 333U;
    REQUIRE_FALSE(Mapper334::validate_geometry(h, kPrgSize, kChrSize));
    h = header();
    h.prg_ram_size = 8192U;
    REQUIRE_FALSE(Mapper334::validate_geometry(h, kPrgSize, kChrSize));
    h = header();
    h.mirror_mode = MirrorMode::FourScreen;
    REQUIRE_FALSE(Mapper334::validate_geometry(h, kPrgSize, kChrSize));
}

TEST_CASE("Mapper 334 maps only its outer 32 KiB PRG bank", "[mapper334]") {
    auto mapper = board();
    for (unsigned bank = 0; bank < 4; ++bank) {
        mapper->write_prg(0x6000U, static_cast<Byte>(bank << 1U));
        for (Address addr : {0x8000U, 0x8001U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
            const auto offset = bank * 32768UZ + (addr & 0x7FFFU);
            REQUIRE(mapper->read_prg(addr) == cell(offset));
        }
    }
    for (unsigned alias = 0; alias < 4; ++alias) {
        mapper->write_prg(static_cast<Address>(0x6000U + alias), 6U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(3UZ * 32768UZ));
    }
}

TEST_CASE("Mapper 334 gates and decodes low registers with MMC3 RAM control", "[mapper334]") {
    auto mapper = board();
    REQUIRE(mapper->maps_prg(0x6002U));
    REQUIRE(mapper->prg_read_mask(0x6002U) == 1U);
    REQUIRE(mapper->read_prg(0x6002U) == 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE_FALSE(mapper->maps_prg(0x6001U));
    REQUIRE_FALSE(mapper->maps_prg(0x6004U));
    REQUIRE_FALSE(mapper->maps_prg(0x6003U));
    REQUIRE(mapper->configure_board_jumpers(1U));
    REQUIRE(mapper->read_prg(0x6002U) == 1U);
    REQUIRE_FALSE(mapper->configure_board_jumpers(2U));
    REQUIRE_FALSE(mapper->maps_prg(0x6003U));

    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6002U));
    mapper->write_prg(0x6000U, 6U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    mapper->write_prg(0xA001U, 0x40U);
    REQUIRE_FALSE(mapper->maps_prg(0x6002U));
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6000U, 4U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(2UZ * 32768UZ));
}

TEST_CASE("Mapper 334 uses MMC3 CHR banking without changing PRG mode", "[mapper334]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 2U);
    select_chr(*mapper, 0U, 10U);
    select_chr(*mapper, 1U, 20U);
    select_chr(*mapper, 2U, 30U);
    select_chr(*mapper, 3U, 40U);
    select_chr(*mapper, 4U, 50U);
    select_chr(*mapper, 5U, 60U);
    const std::array banks{10U, 11U, 20U, 21U, 30U, 40U, 50U, 60U};
    for (unsigned slot = 0; slot < 8; ++slot) {
        const Address addr = static_cast<Address>(slot * 1024U + 17U);
        REQUIRE(mapper->read_chr(addr) == cell(banks[slot] * 1024UZ + 17UZ));
        REQUIRE(mapper->read_prg(0x8000U) == cell(32768UZ));
    }
    for (unsigned slot = 0; slot < 8; ++slot) {
        const Address addr = static_cast<Address>(slot * 1024U + 17U);
        const auto expected = banks[slot ^ 4U];
        select_chr(*mapper, 0U, 10U, true);
        REQUIRE(mapper->read_chr(addr) == cell(expected * 1024UZ + 17UZ));
    }
    const auto before = mapper->read_chr(0U);
    mapper->write_chr(0U, static_cast<Byte>(before ^ 0xFFU));
    REQUIRE(mapper->read_chr(0U) == before);
}

TEST_CASE("Mapper 334 controls mirroring and scanline IRQ state", "[mapper334]") {
    auto mapper = board();
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 334 state replay is bounded and rejection atomic", "[mapper334]") {
    auto mapper = board();
    REQUIRE(mapper->configure_board_jumpers(1U));
    mapper->write_prg(0x6001U, 1U);
    mapper->write_prg(0x6000U, 6U);
    select_chr(*mapper, 2U, 77U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xE001U, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 27U);
    REQUIRE(saved.size() < kPrgSize);

    auto copy = board();
    REQUIRE(copy->configure_board_jumpers(1U));
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));
    REQUIRE(copy->read_chr(0U) == mapper->read_chr(0U));

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader rejected(bytes);
        copy->load_state(rejected);
        REQUIRE_FALSE(rejected.ok());
        REQUIRE(state(*copy) == saved);
    };
    for (std::size_t size = 0; size < saved.size(); ++size)
        reject(std::span<const Byte>(saved).first(size));
    for (std::size_t offset : {0UZ, 2UZ, 6UZ, 9UZ, 10UZ, 11UZ, 13UZ, 22UZ, 25UZ, 26UZ}) {
        auto damaged = saved;
        damaged[offset] ^= 0xFFU;
        reject(damaged);
    }
}

TEST_CASE("Mapper 334 reset restores deterministic register defaults", "[mapper334]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    mapper->write_prg(0x6000U, 6U);
    mapper->write_prg(0x6001U, 0xA5U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper) == state(*board(kPrgSize, kChrSize, MirrorMode::Horizontal)));
}

} // namespace
