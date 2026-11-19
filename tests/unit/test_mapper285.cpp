#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper285.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(Byte submapper = 0U, bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 285U;
    h.submapper = legacy ? 0U : submapper;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.chr_ram_size = 8192U;
    return h;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>((offset / 0x4000U) * 17U + (offset & 0x3FFFU));
}

std::vector<Byte> rom(std::size_t pages) {
    std::vector<Byte> data(pages * 0x4000U);
    for (std::size_t offset = 0U; offset < data.size(); ++offset)
        data[offset] = prg_cell(offset);
    return data;
}

std::unique_ptr<Mapper285> board(Byte submapper = 0U,
                                 bool legacy = false,
                                 std::size_t pages = 32U) {
    const auto h = header(submapper, legacy);
    auto prg = rom(pages);
    REQUIRE(Mapper285::validate_geometry(h, prg.size(), 0U));
    return std::make_unique<Mapper285>(h, std::move(prg), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::pair<unsigned, unsigned> banks(Byte value, Byte submapper) {
    if ((value & 0x40U) != 0U) {
        const unsigned low = (value & 7U) * 2U;
        return {low, low + 1U};
    }
    const unsigned outer = submapper == 0U ? (value >> 3U) & 3U : (value >> 4U) & 3U;
    return {outer * 8U + (value & 7U), outer * 8U + 7U};
}

MirrorMode mirror(Byte value, Byte submapper) {
    const Byte select = submapper == 0U || (value & 0x80U) != 0U ? value & 0x20U : value & 8U;
    if ((value & 0x80U) != 0U)
        return select != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
    return select != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

TEST_CASE("Mapper 285 decodes every latch value for both incompatible submappers", "[mapper285]") {
    for (Byte submapper : {0U, 1U})
        for (bool legacy : {false, true}) {
            if (legacy && submapper != 0U)
                continue;
            auto mapper = board(submapper, legacy);
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->write_prg(0x8000U, static_cast<Byte>(value));
                const auto [low, high] = banks(static_cast<Byte>(value), submapper);
                REQUIRE(mapper->read_prg(0x8000U) == prg_cell(low * 0x4000U));
                REQUIRE(mapper->read_prg(0xBFFFU) == prg_cell(low * 0x4000U + 0x3FFFU));
                REQUIRE(mapper->read_prg(0xC000U) == prg_cell(high * 0x4000U));
                REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(high * 0x4000U + 0x3FFFU));
                REQUIRE(mapper->mirror_mode() == mirror(static_cast<Byte>(value), submapper));
            }
        }
}

TEST_CASE("Mapper 285 treats the entire PRG window as one data-latch alias", "[mapper285]") {
    for (Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
            mapper->write_prg(0x8000U, 0U);
            mapper->write_prg(static_cast<Address>(addr), value);
            const auto [low, high] = banks(value, submapper);
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(low * 0x4000U));
            REQUIRE(mapper->read_prg(0xC000U) == prg_cell(high * 0x4000U));
            REQUIRE(mapper->mirror_mode() == mirror(value, submapper));
        }
        const auto before = state(*mapper);
        for (Address addr : {0U, 0x4020U, 0x6000U, 0x7FFFU}) {
            mapper->write_prg(addr, 0xFFU);
            REQUIRE(state(*mapper) == before);
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE_FALSE(mapper->maps_expansion(addr));
        }
        REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    }
}

TEST_CASE("Mapper 285 applies decoded bank pins before fitted-ROM wrapping", "[mapper285]") {
    for (Byte submapper : {0U, 1U})
        for (std::size_t pages : {1UZ, 3UZ, 5UZ, 16UZ, 32UZ}) {
            auto mapper = board(submapper, false, pages);
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->write_prg(0xFFFFU, static_cast<Byte>(value));
                const auto [low, high] = banks(static_cast<Byte>(value), submapper);
                for (Address addr : {0x8000U, 0x9234U, 0xBFFFU})
                    REQUIRE(mapper->read_prg(addr) ==
                            prg_cell((low % pages) * 0x4000U + (addr & 0x3FFFU)));
                for (Address addr : {0xC000U, 0xD567U, 0xFFFFU})
                    REQUIRE(mapper->read_prg(addr) ==
                            prg_cell((high % pages) * 0x4000U + (addr & 0x3FFFU)));
            }
        }
}

TEST_CASE("Mapper 285 provides writable unbanked CHR RAM without persistence or IRQ audio",
          "[mapper285]") {
    for (Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper);
        mapper->write_prg(0x8000U, 0xFFU);
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
        }
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                    static_cast<Byte>(addr ^ (addr >> 8U)));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        for (unsigned cycle = 0U; cycle < 1024U; ++cycle) {
            mapper->clock_cpu();
            mapper->clock_audio();
            mapper->clock_irq_counter();
        }
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 285 reset clears the latch and retains CHR while cold power clears both",
          "[mapper285]") {
    for (Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper);
        mapper->write_prg(0x8000U, 0xFFU);
        mapper->write_chr(0x1234U, 0xA7U);
        mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(7U * 0x4000U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_chr(0x1234U) == 0xA7U);
        mapper->write_prg(0xFFFFU, 0xFFU);
        mapper->power_on();
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(7U * 0x4000U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_chr(0x1234U) == 0U);
    }
}

TEST_CASE("Mapper 285 validates both native profiles and rejects unsupported geometry",
          "[mapper285]") {
    for (Byte submapper : {0U, 1U})
        for (std::size_t pages : {1UZ, 2UZ, 3UZ, 16UZ, 32UZ}) {
            const auto h = header(submapper);
            REQUIRE(Mapper285::validate_geometry(h, pages * 0x4000U, 0U));
        }
    REQUIRE(Mapper285::validate_geometry(header(0U, true), 0x80000U, 0U));
    for (std::size_t size : {0UZ, 0x2000UZ, 0x4001UZ, 0x81000UZ, 0x100000UZ})
        REQUIRE_FALSE(Mapper285::validate_geometry(header(), size, 0U));
    REQUIRE_FALSE(Mapper285::validate_geometry(header(), 0x80000U, 0x2000U));
    for (Byte submapper = 2U; submapper < 16U; ++submapper)
        REQUIRE_FALSE(Mapper285::validate_geometry(header(submapper), 0x80000U, 0U));
    for (unsigned kind = 0U; kind < 10U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.mapper_number = 284U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 8192U;
            break;
        case 3U:
            h.prg_nvram_size = 8192U;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 4096U;
            break;
        case 6U:
            h.chr_ram_size = 16384U;
            break;
        case 7U:
            h.chr_nvram_size = 8192U;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        }
        REQUIRE_FALSE(Mapper285::validate_geometry(h, 0x80000U, 0U));
    }
}

TEST_CASE("Mapper 285 state replay is bounded and malformed loads are atomic", "[mapper285]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xD7U);
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U));
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 8199U);

    mapper->power_on();
    mapper->write_prg(0xFFFFU, 0x2AU);
    mapper->write_chr(0U, 0xFFU);
    StateReader valid(saved);
    mapper->load_state(valid);
    REQUIRE(valid.ok());
    REQUIRE(valid.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto corrupt = saved;
        corrupt[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        StateReader reader(corrupt);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto wrong_size = saved;
    wrong_size[2U] ^= 1U;
    StateReader size_reader(wrong_size);
    mapper->load_state(size_reader);
    REQUIRE_FALSE(size_reader.ok());
    REQUIRE(state(*mapper) == saved);

    auto other_profile = board(1U);
    const auto profile_before = state(*other_profile);
    StateReader profile_reader(saved);
    other_profile->load_state(profile_reader);
    REQUIRE_FALSE(profile_reader.ok());
    REQUIRE(state(*other_profile) == profile_before);

    auto other_size = board(0U, false, 16U);
    const auto size_before = state(*other_size);
    StateReader geometry_reader(saved);
    other_size->load_state(geometry_reader);
    REQUIRE_FALSE(geometry_reader.ok());
    REQUIRE(state(*other_size) == size_before);
}

} // namespace
