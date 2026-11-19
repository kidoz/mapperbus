#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/cartridge_story_274.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::size_t kMainSize = 0x200000U;
constexpr std::array<std::size_t, 3> kExtraSizes{0U, 0x4000U, 0x10000U};

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 274U;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.chr_ram_size = 8192U;
    h.mirror_mode = MirrorMode::Horizontal;
    return h;
}

std::vector<Byte> prg_image(std::size_t extra_size) {
    std::vector<Byte> image(kMainSize + extra_size);
    for (std::size_t addr = 0U; addr < image.size(); ++addr)
        image[addr] = static_cast<Byte>((addr >> 14U) ^ (addr >> 6U) ^ addr);
    return image;
}

std::unique_ptr<CartridgeStory274> board(std::size_t population = 2U, bool legacy = false) {
    const auto extra_size = kExtraSizes[population];
    REQUIRE(CartridgeStory274::validate_geometry(header(legacy), kMainSize + extra_size, 0U));
    return std::make_unique<CartridgeStory274>(
        header(legacy), prg_image(extra_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_byte(const std::vector<Byte>& image, std::size_t page, Address addr) {
    return image[page * 0x4000U + (addr & 0x3FFFU)];
}

TEST_CASE("Mapper 274 accepts exactly the three documented chip populations", "[mapper274]") {
    for (bool legacy : {false, true})
        for (std::size_t population = 0U; population < kExtraSizes.size(); ++population) {
            auto mapper = board(population, legacy);
            REQUIRE(state(*mapper).size() == 8196U);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        }
}

TEST_CASE("Mapper 274 rejects unsupported ROM and native memory geometry", "[mapper274]") {
    for (std::size_t size : {0U,
                             0x4000U,
                             0x1FF000U,
                             0x1FFFFFU,
                             0x200001U,
                             0x208000U,
                             0x20C000U,
                             0x20FFFFU,
                             0x210001U,
                             0x220000U,
                             0x400000U})
        REQUIRE_FALSE(CartridgeStory274::validate_geometry(header(), size, 0U));
    for (std::size_t size : {1U, 8192U, 65536U})
        REQUIRE_FALSE(CartridgeStory274::validate_geometry(header(), 0x210000U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(CartridgeStory274::validate_geometry(h, 0x210000U, 0U));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_nvram_size = 8192U;
            break;
        default:
            FAIL("unexpected field");
        }
        REQUIRE_FALSE(CartridgeStory274::validate_geometry(h, 0x210000U, 0U));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mode;
        REQUIRE_FALSE(CartridgeStory274::validate_geometry(h, 0x210000U, 0U));
    }
    auto legacy = header(true);
    legacy.has_battery = true;
    REQUIRE_FALSE(CartridgeStory274::validate_geometry(legacy, 0x210000U, 0U));
    legacy = header(true);
    legacy.chr_nvram_size = 8192U;
    REQUIRE_FALSE(CartridgeStory274::validate_geometry(legacy, 0x210000U, 0U));
}

TEST_CASE("Mapper 274 inner register aliases select low main pages and H/V mirroring",
          "[mapper274]") {
    for (std::size_t population = 0U; population < kExtraSizes.size(); ++population) {
        auto mapper = board(population);
        const auto image = prg_image(kExtraSizes[population]);
        for (Address addr = 0x8000U; addr < 0xA000U; ++addr) {
            const auto value = static_cast<Byte>(static_cast<unsigned>(addr) ^
                                                 (static_cast<unsigned>(addr) >> 8U));
            mapper->write_prg(0xE000U, 0U);
            mapper->write_prg(addr, value);
            REQUIRE(mapper->read_prg(0x8000U) == expected_byte(image, value & 0x0FU, 0x8000U));
            REQUIRE(mapper->read_prg(0xBFFFU) == expected_byte(image, value & 0x0FU, 0xBFFFU));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            mapper->write_prg(0xC000U, 0U);
            const auto extra_page = (value & 0x0FU) & ((kExtraSizes[population] / 0x4000U) - 1U);
            if (population == 0U) {
                REQUIRE(mapper->prg_read_mask(0x8000U) == 0U);
            } else {
                REQUIRE(mapper->read_prg(0x8000U) == image[kMainSize + extra_page * 0x4000U]);
                REQUIRE(mapper->read_prg(0xBFFFU) ==
                        image[kMainSize + extra_page * 0x4000U + 0x3FFFU]);
            }
        }
    }
}

TEST_CASE("Mapper 274 outer aliases select high pages and chip select comes from A13",
          "[mapper274]") {
    auto mapper = board();
    const auto image = prg_image(0x10000U);
    mapper->write_prg(0x8000U, 0x0BU);
    for (Address addr = 0xC000U; addr != 0U; ++addr) {
        const auto raw_addr = static_cast<unsigned>(addr);
        const auto value = static_cast<Byte>((raw_addr * 29U) ^ (raw_addr >> 8U));
        mapper->write_prg(addr, value);
        REQUIRE(mapper->read_prg(0xC000U) == expected_byte(image, value & 0x7FU, 0xC000U));
        REQUIRE(mapper->read_prg(0xFFFFU) == expected_byte(image, value & 0x7FU, 0xFFFFU));
        if ((addr & 0x2000U) != 0U) {
            const auto page = (value & 0x70U) | 0x0BU;
            REQUIRE(mapper->read_prg(0x8000U) == expected_byte(image, page, 0x8000U));
        } else {
            REQUIRE(mapper->read_prg(0x8000U) == image[kMainSize + 3UZ * 0x4000U]);
        }
        if (addr == 0xFFFFU)
            break;
    }
}

TEST_CASE("Mapper 274 covers every main page and masks extra-chip high pins", "[mapper274]") {
    for (std::size_t population = 0U; population < kExtraSizes.size(); ++population) {
        auto mapper = board(population);
        const auto image = prg_image(kExtraSizes[population]);
        for (Byte outer = 0U; outer < 128U; ++outer) {
            mapper->write_prg(0xE000U, outer);
            for (Address addr : {0xC000U, 0xD555U, 0xFFFFU})
                REQUIRE(mapper->read_prg(addr) == expected_byte(image, outer, addr));
            for (Byte inner = 0U; inner < 16U; ++inner) {
                mapper->write_prg(0x8000U, inner);
                mapper->write_prg(0xE000U, outer);
                const auto page = (outer & 0x70U) | inner;
                REQUIRE(mapper->read_prg(0x8000U) == expected_byte(image, page, 0x8000U));
                REQUIRE(mapper->read_prg(0xBFFFU) == expected_byte(image, page, 0xBFFFU));
                mapper->write_prg(0xC000U, outer);
                if (population != 0U) {
                    const auto extra_pages = kExtraSizes[population] / 0x4000U;
                    const auto extra_page = inner & (extra_pages - 1U);
                    REQUIRE(mapper->read_prg(0x8000U) == image[kMainSize + extra_page * 0x4000U]);
                }
            }
        }
    }
}

TEST_CASE("Mapper 274 absent extra chip leaves only the lower CPU window floating", "[mapper274]") {
    auto mapper = board(0U);
    const auto image = prg_image(0U);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0U);
    REQUIRE(mapper->prg_read_mask(0xBFFFU) == 0U);
    REQUIRE(mapper->prg_read_mask(0xC000U) == 0xFFU);
    REQUIRE(mapper->read_prg(0xC000U) == expected_byte(image, 0U, 0xC000U));
    mapper->write_prg(0x8000U, 0x0AU);
    mapper->write_prg(0xE234U, 0x5AU);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->read_prg(0x8000U) == expected_byte(image, 0x5AU, 0x8000U));
    mapper->write_prg(0xC000U, 0x5AU);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0U);
    for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        mapper->write_prg(addr, 0xFFU);
    }
}

TEST_CASE("Mapper 274 does not alias the outer register into A000-BFFF", "[mapper274]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x1AU);
    mapper->write_prg(0xE000U, 0x35U);
    const auto saved = state(*mapper);
    for (Address addr = 0xA000U; addr < 0xC000U; ++addr) {
        mapper->write_prg(
            addr,
            static_cast<Byte>(static_cast<unsigned>(addr) ^ (static_cast<unsigned>(addr) >> 8U)));
        REQUIRE(state(*mapper) == saved);
    }
}

TEST_CASE("Mapper 274 CHR RAM is writable at every cell and independent of PRG banking",
          "[mapper274]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(addr) == 0U);
        mapper->write_chr(
            addr,
            static_cast<Byte>(static_cast<unsigned>(addr) ^ (static_cast<unsigned>(addr) >> 8U)));
    }
    for (Byte bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(0x8000U, bank);
        mapper->write_prg(0xE000U, static_cast<Byte>(bank * 7U));
        for (Address addr = 0U; addr < 8192U; ++addr)
            REQUIRE(mapper->read_chr(addr) ==
                    static_cast<Byte>(static_cast<unsigned>(addr) ^
                                      (static_cast<unsigned>(addr) >> 8U)));
    }
}

TEST_CASE("Mapper 274 reset restores default extra-chip mapping and cold power clears CHR",
          "[mapper274]") {
    for (std::size_t population = 0U; population < kExtraSizes.size(); ++population) {
        auto mapper = board(population);
        mapper->write_prg(0x8000U, 0x1FU);
        mapper->write_prg(0xFFFFU, 0x7FU);
        mapper->write_chr(0x1234U, 0xA7U);
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_chr(0x1234U) == 0xA7U);
        REQUIRE(mapper->prg_read_mask(0x8000U) == (population == 0U ? 0U : 0xFFU));
        if (population != 0U)
            REQUIRE(mapper->read_prg(0x8000U) == prg_image(kExtraSizes[population])[kMainSize]);
        mapper->power_on();
        REQUIRE(mapper->read_chr(0x1234U) == 0U);
    }
}

TEST_CASE("Mapper 274 snapshots replay latches and CHR without ROM bytes", "[mapper274]") {
    for (std::size_t population = 0U; population < kExtraSizes.size(); ++population) {
        auto mapper = board(population);
        mapper->write_prg(0x9FFFU, 0x1DU);
        mapper->write_prg(0xDFFFU, 0x65U);
        for (Address addr = 0U; addr < 8192U; addr += 97U)
            mapper->write_chr(addr, static_cast<Byte>(addr * 13U));
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == 8196U);
        auto copy = board(population);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(copy->prg_read_mask(0x8000U) == (population == 0U ? 0U : 0xFFU));
        for (Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU})
            REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
        for (Address addr = 0U; addr < 8192U; ++addr)
            REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
    }
}

TEST_CASE("Mapper 274 malformed and cross-population snapshots reject atomically", "[mapper274]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x1DU);
    mapper->write_prg(0xC000U, 0x65U);
    mapper->write_chr(0x321U, 0xC3U);
    const auto saved = state(*mapper);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset : {0UZ, 1UZ, 2UZ}) {
        auto bad = saved;
        bad[offset] ^= 0x80U;
        bad[4U + 0x321U] = 0U;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte inner : {0x20U, 0x40U, 0x80U, 0xFFU}) {
        auto bad = saved;
        bad[2U] = inner;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t population = 0U; population < 2U; ++population) {
        auto other = board(population);
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    }
}
} // namespace
