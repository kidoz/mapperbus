#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/multicart063.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(Byte submapper = 0U, bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 63U;
    h.submapper = submapper;
    h.is_nes2 = !legacy;
    h.chr_ram_size = legacy ? 0U : 8192U;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}

Byte cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 53U + offset + (offset >> 8U));
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t i = 0U; i < size; ++i)
        bytes[i] = cell(i / 0x4000U, i & 0x3FFFU);
    return bytes;
}

std::unique_ptr<Multicart063> board(Byte submapper = 0U,
                                    std::size_t prg_size = 0U,
                                    bool legacy = false) {
    if (prg_size == 0U)
        prg_size = submapper == 0U ? 0x400000U : 0x200000U;
    const auto h = header(submapper, legacy);
    REQUIRE(Multicart063::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Multicart063>(h, rom(prg_size), std::vector<Byte>{});
}

Address latch_mask(Byte submapper) {
    return submapper == 0U ? 0x07FFU : 0x03FFU;
}

unsigned expected_bank(Address latch, Address addr, Byte submapper, unsigned bank_count) {
    unsigned bank = (latch >> 3U) & (submapper == 0U ? 0xFFU : 0x7FU);
    const unsigned low = (latch & 2U) != 0U ? (addr >> 14U) & 1U : (latch >> 2U) & 1U;
    bank = (bank & ~1U) | low;
    return submapper == 1U ? bank % bank_count : bank;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 063 validates both bank-pin geometries and RAM metadata", "[mapper063]") {
    for (Byte submapper : {0U, 1U}) {
        const std::size_t maximum = submapper == 0U ? 0x400000U : 0x200000U;
        for (std::size_t size : {0x4000UZ, 0x30000UZ, maximum})
            REQUIRE(Multicart063::validate_geometry(header(submapper), size, 0U));
        REQUIRE(Multicart063::validate_geometry(header(15U, true), maximum, 0U));
        for (std::size_t size : {0UZ, 0x4001UZ, maximum + 0x4000UZ})
            REQUIRE_FALSE(Multicart063::validate_geometry(header(submapper), size, 0U));
        for (std::size_t size : {1U, 0x2000U, 0x4000U})
            REQUIRE_FALSE(Multicart063::validate_geometry(header(submapper), maximum, size));
        for (Byte invalid : {2U, 15U})
            REQUIRE_FALSE(Multicart063::validate_geometry(header(invalid), maximum, 0U));
        for (auto mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            auto h = header(submapper);
            h.mirror_mode = mirror;
            REQUIRE_FALSE(Multicart063::validate_geometry(h, maximum, 0U));
        }
        for (unsigned field = 0U; field < 6U; ++field) {
            auto h = header(submapper);
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
                h.chr_ram_size = 16384U;
                break;
            default:
                h.chr_nvram_size = 8192U;
                break;
            }
            REQUIRE_FALSE(Multicart063::validate_geometry(h, maximum, 0U));
        }
    }
}

TEST_CASE("Mapper 063 decodes every CPU address alias and ignores write data", "[mapper063]") {
    for (Byte submapper : {0U, 1U}) {
        const std::size_t size = submapper == 0U ? 0x400000U : 0x200000U;
        auto mapper = board(submapper, size);
        const unsigned bank_count = size / 0x4000U;
        for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            mapper->reset();
            mapper->write_prg(addr, static_cast<Byte>(raw ^ (raw >> 8U)));
            const Address latch = raw >= 0x8000U ? addr & latch_mask(submapper) : 0U;
            for (Address cpu : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
                const unsigned bank = expected_bank(latch, cpu, submapper, bank_count);
                REQUIRE(mapper->maps_prg(cpu));
                REQUIRE(mapper->read_prg(cpu) == cell(bank, cpu & 0x3FFFU));
            }
            REQUIRE(mapper->mirror_mode() ==
                    ((latch & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        }
        const auto before = state(*mapper);
        for (unsigned value = 0U; value < 256U; ++value)
            mapper->write_prg(static_cast<Address>(0x8000U | latch_mask(submapper)),
                              static_cast<Byte>(value));
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->battery_ram().empty());
    }
}

TEST_CASE("Mapper 063 shares C with the high PRG pin and protects CHR RAM", "[mapper063]") {
    for (Byte submapper : {0U, 1U}) {
        const std::size_t size = submapper == 0U ? 0x300000U : 0x200000U;
        auto mapper = board(submapper, size);
        const Address protect = submapper == 0U ? 0x0400U : 0x0200U;
        const unsigned high_bank = submapper == 0U ? 128U : 64U;
        mapper->write_prg(static_cast<Address>(0x8000U | (high_bank << 3U)), 0U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(high_bank, 0U));
        mapper->write_chr(0x1234U, 0x5AU);
        REQUIRE(mapper->read_chr(0x1234U) == 0U);
        mapper->write_prg(0x8000U, 0U);
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                    static_cast<Byte>(addr ^ (addr >> 8U)));
        mapper->write_prg(static_cast<Address>(0x8000U | protect), 0U);
        mapper->write_chr(0x1234U, 0xA7U);
        constexpr Byte expected = static_cast<Byte>(0x1234U ^ (0x1234U >> 8U));
        REQUIRE(mapper->read_chr(0x1234U) == expected);
        REQUIRE(mapper->read_chr(0x3234U) == expected);
    }
}

TEST_CASE("Mapper 063 submapper 0 floats absent PRG while submapper 1 wraps", "[mapper063]") {
    for (Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper, 3U * 0x4000U);
        for (Address latch = 0U; latch <= latch_mask(submapper); ++latch) {
            mapper->write_prg(static_cast<Address>(0x8000U | latch), 0xFFU);
            for (Address cpu : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
                const unsigned bank = expected_bank(latch, cpu, submapper, 3U);
                const bool populated = submapper == 1U || bank < 3U;
                REQUIRE(mapper->maps_prg(cpu) == populated);
                REQUIRE(mapper->read_prg(cpu) == (populated ? cell(bank, cpu & 0x3FFFU) : 0U));
            }
        }
        for (Address addr : {0U, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 063 reset clears the latch but only cold power clears CHR RAM", "[mapper063]") {
    for (Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper);
        mapper->write_chr(0x0123U, 0xA7U);
        mapper->write_prg(static_cast<Address>(0x8000U | latch_mask(submapper)), 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0U, 0U));
        REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);
        mapper->write_chr(0x0123U, 0x5CU);
        REQUIRE(mapper->read_chr(0x0123U) == 0x5CU);
        mapper->power_on();
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
    }
}

TEST_CASE("Mapper 063 state replays latch and CHR RAM without ROM bytes", "[mapper063]") {
    for (Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper, 3U * 0x4000U);
        mapper->write_chr(0x0234U, 0xA7U);
        mapper->write_prg(static_cast<Address>(0x8000U | (submapper == 0U ? 0x0415U : 0x0215U)),
                          0U);
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == 8198U);
        auto copy = board(submapper, 3U * 0x4000U);
        copy->power_on();
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->read_chr(0x0234U) == 0xA7U);
        REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));
        auto large = board(submapper);
        REQUIRE(state(*large).size() == saved.size());
    }
}

TEST_CASE("Mapper 063 rejects malformed and mismatched states atomically", "[mapper063]") {
    for (Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper, 3U * 0x4000U);
        mapper->write_chr(0x0234U, 0xA7U);
        mapper->write_prg(static_cast<Address>(0x8000U | (submapper == 0U ? 0x0415U : 0x0215U)),
                          0U);
        const auto saved = state(*mapper);
        for (std::size_t length : {0U, 1U, 3U, 4U, 5U, 8197U}) {
            StateReader reader{std::span(saved).first(length)};
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
        for (const auto [offset, value] :
             std::array<std::pair<std::size_t, Byte>, 3>{{{0U, 0U}, {3U, 0U}, {5U, 0x08U}}}) {
            auto bad = saved;
            bad[offset] = value;
            StateReader reader(bad);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
        auto other_submapper = board(1U - submapper, 3U * 0x4000U);
        auto before = state(*other_submapper);
        StateReader wrong_submapper(saved);
        other_submapper->load_state(wrong_submapper);
        REQUIRE_FALSE(wrong_submapper.ok());
        REQUIRE(state(*other_submapper) == before);
        auto other_size = board(submapper, 4U * 0x4000U);
        before = state(*other_size);
        StateReader wrong_size(saved);
        other_size->load_state(wrong_size);
        REQUIRE_FALSE(wrong_size.ok());
        REQUIRE(state(*other_size) == before);
    }
}

} // namespace
