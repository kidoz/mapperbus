#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper015.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 15U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.chr_ram_size = 8192U;
    return h;
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>((i / 8192U) + (i & 8191U));
    return data;
}

std::unique_ptr<Mapper015> board(std::size_t size = 1048576U, bool legacy = false) {
    const auto h = header(legacy);
    REQUIRE(Mapper015::validate_geometry(h, size, 0U));
    return std::make_unique<Mapper015>(h, rom(size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper015& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(std::size_t size, Byte latch, Address addr) {
    const unsigned mode = latch >> 6U;
    const unsigned bank = latch & 0x3FU;
    std::size_t offset = 0U;
    switch (mode) {
    case 0U: {
        const unsigned selected = (bank & 0x3EU) | ((addr >> 14U) & 1U);
        offset = selected * 0x4000U + (addr & 0x3FFFU);
        break;
    }
    case 1U: {
        const unsigned selected = addr >= 0xC000U ? bank | 7U : bank;
        offset = selected * 0x4000U + (addr & 0x3FFFU);
        break;
    }
    case 2U:
        offset = bank * 0x4000U + ((latch & 0x80U) != 0U ? 0x2000U : 0U) + (addr & 0x1FFFU);
        break;
    case 3U:
        offset = bank * 0x4000U + (addr & 0x3FFFU);
        break;
    }
    const auto physical = offset % size;
    return static_cast<Byte>((physical / 8192U) + (physical & 8191U));
}

TEST_CASE("Mapper 015 decodes every latch value in all four PRG modes", "[mapper015]") {
    auto mapper = board();
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xDFFFU, 0xE000U, 0xFFFFU};
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->write_prg(0x8000U, static_cast<Byte>(latch));
        for (const auto addr : addresses) {
            INFO("latch=" << latch << " address=" << addr);
            REQUIRE(mapper->read_prg(addr) ==
                    expected_prg(1048576U, static_cast<Byte>(latch), addr));
        }
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
}

TEST_CASE("Mapper 015 register decode covers every CPU address alias", "[mapper015]") {
    auto mapper = board();
    auto expected = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        expected->reset();
        const auto value = static_cast<Byte>(addr);
        mapper->write_prg(static_cast<Address>(addr), value);
        if (addr >= 0x8000U)
            expected->write_prg(static_cast<Address>(0x8000U | (addr & 3U)), value);
        REQUIRE(mapper->read_prg(0x8123U) == expected->read_prg(0x8123U));
        REQUIRE(mapper->read_prg(0xC123U) == expected->read_prg(0xC123U));
        REQUIRE(mapper->read_prg(0xA123U) == expected->read_prg(0xA123U));
        REQUIRE(mapper->read_prg(0xE123U) == expected->read_prg(0xE123U));
        REQUIRE(mapper->mirror_mode() == expected->mirror_mode());
        mapper->write_chr(0x123U, static_cast<Byte>(value ^ 0xA5U));
        expected->write_chr(0x123U, static_cast<Byte>(value ^ 0xA5U));
        REQUIRE(mapper->read_chr(0x123U) == expected->read_chr(0x123U));
    }
}

TEST_CASE("Mapper 015 applies decoded bank pins before wrapping fitted ROMs", "[mapper015]") {
    for (const std::size_t pages : {1UZ, 3UZ, 17UZ, 64UZ}) {
        const auto size = pages * 16384U;
        auto mapper = board(size);
        for (unsigned latch = 0U; latch < 256U; ++latch) {
            mapper->write_prg(0xFFFFU, static_cast<Byte>(latch));
            for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(size, static_cast<Byte>(latch), addr));
        }
    }
}

TEST_CASE("Mapper 015 protects every CHR RAM cell in NROM-256 and NROM-128 modes", "[mapper015]") {
    auto mapper = board();
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        mapper->power_on();
        mapper->write_prg(0x8000U, 0x40U);
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr / 256U)));
        mapper->write_prg(0x8000U, static_cast<Byte>(mode << 6U));
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            mapper->write_chr(static_cast<Address>(addr), 0xA5U);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                    (mode == 1U || mode == 2U ? 0xA5U : static_cast<Byte>(addr ^ (addr / 256U))));
        }
    }
}

TEST_CASE("Mapper 015 reset clears the latch and cold power also clears CHR RAM", "[mapper015]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x40U);
    mapper->write_chr(0x123U, 0xA7U);
    mapper->write_prg(0xFFFFU, 0xFFU);
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(1048576U, 0U, 0x8000U));
    REQUIRE(mapper->read_prg(0xC000U) == expected_prg(1048576U, 0U, 0xC000U));
    REQUIRE(mapper->read_chr(0x123U) == 0xA7U);
    mapper->write_chr(0x123U, 0x55U);
    REQUIRE(mapper->read_chr(0x123U) == 0xA7U);
    mapper->power_on();
    REQUIRE(mapper->read_chr(0x123U) == 0U);
    REQUIRE(state(*mapper)[2] == 0U);
}

TEST_CASE("Mapper 015 exposes no mapper-hack CPU RAM IRQ or audio", "[mapper015]") {
    auto mapper = board();
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(state(*mapper)[2] == 0U);
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 015 accepts the decoded PRG envelope for legacy and native headers",
          "[mapper015]") {
    for (const bool legacy : {false, true}) {
        for (std::size_t pages = 1U; pages <= 64U; ++pages) {
            auto h = header(legacy);
            REQUIRE(Mapper015::validate_geometry(h, pages * 16384U, 0U));
            h.mirror_mode = MirrorMode::Horizontal;
            REQUIRE(Mapper015::validate_geometry(h, pages * 16384U, 0U));
        }
    }
}

TEST_CASE("Mapper 015 rejects unsupported geometry memory and native metadata", "[mapper015]") {
    for (const std::size_t size :
         {0UZ, 1UZ, 8192UZ, 16383UZ, 16385UZ, 24576UZ, 1048577UZ, 1064960UZ, 2097152UZ})
        REQUIRE_FALSE(Mapper015::validate_geometry(header(), size, 0U));
    for (const std::size_t size : {1UZ, 8192UZ, 65536UZ})
        REQUIRE_FALSE(Mapper015::validate_geometry(header(), 1048576U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper015::validate_geometry(h, 1048576U, 0U));
    }
    for (const bool legacy : {false, true}) {
        auto h = header(legacy);
        h.has_battery = true;
        REQUIRE_FALSE(Mapper015::validate_geometry(h, 1048576U, 0U));
        for (const auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            h = header(legacy);
            h.mirror_mode = mode;
            REQUIRE_FALSE(Mapper015::validate_geometry(h, 1048576U, 0U));
        }
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 8192U;
            break;
        case 1U:
            h.prg_nvram_size = 8192U;
            break;
        case 2U:
            h.chr_ram_size = 0U;
            break;
        case 3U:
            h.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper015::validate_geometry(h, 1048576U, 0U));
    }
    for (const std::size_t size : {4096UZ, 16384UZ}) {
        auto h = header();
        h.chr_ram_size = size;
        REQUIRE_FALSE(Mapper015::validate_geometry(h, 1048576U, 0U));
    }
}

TEST_CASE("Mapper 015 state replays latch and RAM without serializing ROM", "[mapper015]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x40U);
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 3U));
    mapper->write_prg(0x8000U, 0xFDU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 8195U);
    mapper->power_on();
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(state(*mapper) == saved);
    for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U})
        REQUIRE(mapper->read_prg(addr) == expected_prg(1048576U, 0xFDU, addr));
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 3U));
}

TEST_CASE("Mapper 015 rejects truncated corrupt and wrong-geometry states atomically",
          "[mapper015]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x40U);
    mapper->write_chr(0x123U, 0xA7U);
    mapper->write_prg(0x8000U, 0xFDU);
    const auto saved = state(*mapper);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader{std::span(saved).first(size)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto other = board(63U * 16384U);
    const auto before = state(*other);
    StateReader mismatch(saved);
    other->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other) == before);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        auto bytes = saved;
        bytes[2] = static_cast<Byte>(latch);
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == bytes);
    }
}

} // namespace
