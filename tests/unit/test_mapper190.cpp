#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/mappers/mapper190.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 190U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = legacy ? 0U : 0x2000U;
    return h;
}

std::vector<Byte> prg_rom(std::size_t size = 0x40000U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 0x4000U);
    return data;
}

std::vector<Byte> chr_rom(std::size_t size = 0x80000U) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>((i / 0x800U) ^ 0xA5U);
    return data;
}

std::unique_ptr<Mapper190> board(INesHeader h = header(),
                                 std::size_t prg_size = 0x40000U,
                                 std::size_t chr_size = 0x80000U) {
    REQUIRE(Mapper190::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper190>(h, prg_rom(prg_size), chr_rom(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 190 PRG decode uses D0-D2 and A14 on every documented alias", "[mapper190]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        for (Address base : {0x8000U, 0xC000U}) {
            for (Address offset : {0U, 1U, 0x123U, 0x1FFFU}) {
                mapper->write_prg(static_cast<Address>(base + offset), static_cast<Byte>(value));
                const auto expected = static_cast<Byte>((value & 7U) | (base == 0xC000U ? 8U : 0U));
                REQUIRE(mapper->read_prg(0x8000U) == expected);
                REQUIRE(mapper->read_prg(0xBFFFU) == expected);
                REQUIRE(mapper->read_prg(0xC000U) == 0U);
                REQUIRE(mapper->read_prg(0xFFFFU) == 0U);
            }
        }
    }

    mapper->write_prg(0x8000U, 3U);
    for (Address addr : {0xA000U, 0xBFFFU, 0xE000U, 0xE001U, 0xFFFFU})
        mapper->write_prg(addr, 6U);
    REQUIRE(mapper->read_prg(0x8000U) == 3U);
}

TEST_CASE("Mapper 190 selects four independent full-byte CHR banks without bus conflicts",
          "[mapper190]") {
    auto mapper = board();
    std::array<Byte, 4> expected{};
    for (Address addr = 0xA000U; addr < 0xC000U; ++addr) {
        const auto value = static_cast<Byte>((addr * 37U) ^ (addr >> 8U));
        mapper->write_prg(addr, value);
        expected[addr & 3U] = value;
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto ppu = static_cast<Address>(slot * 0x800U);
            REQUIRE(mapper->read_chr(ppu) == static_cast<Byte>(expected[slot] ^ 0xA5U));
            REQUIRE(mapper->read_chr(static_cast<Address>(ppu + 0x7FFU)) ==
                    static_cast<Byte>(expected[slot] ^ 0xA5U));
        }
    }

    for (unsigned slot = 0U; slot < 4U; ++slot) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(value));
            const auto ppu = static_cast<Address>(slot * 0x800U + 0x321U);
            REQUIRE(mapper->read_chr(ppu) == static_cast<Byte>(value ^ 0xA5U));
            mapper->write_chr(ppu, static_cast<Byte>(value ^ 0x5AU));
            REQUIRE(mapper->read_chr(ppu) == static_cast<Byte>(value ^ 0xA5U));
        }
    }
}

TEST_CASE("Mapper 190 wraps fitted ROMs after bank decoding", "[mapper190]") {
    auto mapper = board(header(), 0xC000U, 0x3000U);
    for (unsigned bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(static_cast<Address>(bank < 8U ? 0x8000U : 0xC000U),
                          static_cast<Byte>(bank));
        REQUIRE(mapper->read_prg(0x8000U) == bank % 3U);
        REQUIRE(mapper->read_prg(0xC000U) == 0U);
    }
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        for (unsigned bank = 0U; bank < 256U; ++bank) {
            mapper->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(bank));
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x800U)) ==
                    static_cast<Byte>((bank % 6U) ^ 0xA5U));
        }
    }
}

TEST_CASE("Mapper 190 owns 8 KiB volatile RAM and fixed header CIRAM", "[mapper190]") {
    auto mapper = board();
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        REQUIRE(mapper->maps_prg(addr));
        mapper->write_prg(addr, static_cast<Byte>(addr ^ (addr >> 8U)));
    }
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(mapper->read_prg(addr) == static_cast<Byte>(addr ^ (addr >> 8U)));

    REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
    REQUIRE_FALSE(mapper->maps_expansion(0x4020U));
    REQUIRE_FALSE(mapper->maps_expansion(0x5FFFU));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
        mapper->write_prg(addr, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
}

TEST_CASE("Mapper 190 soft reset clears banks and retains RAM; cold power clears RAM",
          "[mapper190]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 7U);
    for (unsigned slot = 0U; slot < 4U; ++slot)
        mapper->write_prg(static_cast<Address>(0xA000U + slot), static_cast<Byte>(0x40U + slot));
    mapper->write_prg(0x6A55U, 0x73U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot)
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * 0x800U)) == 0xA5U);
    REQUIRE(mapper->read_prg(0x6A55U) == 0x73U);

    mapper->write_prg(0xC000U, 7U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_prg(0x6A55U) == 0U);
}

TEST_CASE("Mapper 190 state replay is bounded and excludes immutable ROM", "[mapper190][state]") {
    auto mapper = board();
    mapper->write_prg(0xC123U, 6U);
    const std::array<Byte, 4> banks = {0U, 1U, 0xFEU, 0xFFU};
    for (unsigned slot = 0U; slot < 4U; ++slot)
        mapper->write_prg(static_cast<Address>(0xA000U + slot), banks[slot]);
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr)
        mapper->write_prg(addr, static_cast<Byte>(addr * 29U));
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 0x2007U);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == 14U);
    for (unsigned slot = 0U; slot < 4U; ++slot)
        REQUIRE(copy->read_chr(static_cast<Address>(slot * 0x800U)) ==
                static_cast<Byte>(banks[slot] ^ 0xA5U));
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr)
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(addr * 29U));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
}

TEST_CASE("Mapper 190 malformed state rejection leaves every mutable field unchanged",
          "[mapper190][state]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 5U);
    mapper->write_prg(0xA002U, 0x93U);
    mapper->write_prg(0x6123U, 0xA7U);
    const auto saved = state(*mapper);

    const auto reject = [&mapper](std::span<const Byte> candidate) {
        const auto before = state(*mapper);
        StateReader reader(candidate);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == before);
        REQUIRE(mapper->read_prg(0x8000U) == 13U);
        REQUIRE(mapper->read_chr(0x1000U) == 0x36U);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    };

    for (std::size_t length : {0UZ, 1UZ, 2UZ, 6UZ, saved.size() - 1U})
        reject(std::span(saved).first(length));
    auto bad_tag = saved;
    bad_tag[0] ^= 1U;
    reject(bad_tag);
    auto bad_prg = saved;
    bad_prg[2] = 0x10U;
    reject(bad_prg);
    bad_prg[2] = 0xFFU;
    reject(bad_prg);

    auto horizontal = board([] {
        auto h = header();
        h.mirror_mode = MirrorMode::Horizontal;
        return h;
    }());
    const auto before = state(*horizontal);
    StateReader wrong_mirror(saved);
    horizontal->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*horizontal) == before);
}

TEST_CASE("Mapper 190 validates decoded ROM bounds and exact native memory metadata",
          "[mapper190]") {
    for (std::size_t prg_size : {0x8000UZ, 0xC000UZ, 0x40000UZ})
        for (std::size_t chr_size : {0x2000UZ, 0x3000UZ, 0x80000UZ})
            REQUIRE(Mapper190::validate_geometry(header(), prg_size, chr_size));
    auto legacy = header(true);
    legacy.prg_ram_size = 0U;
    REQUIRE(Mapper190::validate_geometry(legacy, 0x20000U, 0x20000U));

    for (unsigned kind = 0U; kind < 20U; ++kind) {
        auto h = header();
        std::size_t prg_size = 0x20000U;
        std::size_t chr_size = 0x20000U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            prg_size = 0U;
            break;
        case 2U:
            prg_size = 0x4000U;
            break;
        case 3U:
            prg_size = 0x42000U;
            break;
        case 4U:
            prg_size = 0x8001U;
            break;
        case 5U:
            chr_size = 0U;
            break;
        case 6U:
            chr_size = 0x1000U;
            break;
        case 7U:
            chr_size = 0x80800U;
            break;
        case 8U:
            chr_size = 0x2001U;
            break;
        case 9U:
            h.has_battery = true;
            break;
        case 10U:
            h.prg_ram_size = 0U;
            break;
        case 11U:
            h.prg_ram_size = 0x1000U;
            break;
        case 12U:
            h.prg_ram_size = 0x4000U;
            break;
        case 13U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 14U:
            h.chr_ram_size = 0x2000U;
            break;
        case 15U:
            h.chr_nvram_size = 0x2000U;
            break;
        case 16U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 17U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 18U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 19U:
            h.submapper = 15U;
            break;
        default:
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(Mapper190::validate_geometry(h, prg_size, chr_size));
    }
}

} // namespace
