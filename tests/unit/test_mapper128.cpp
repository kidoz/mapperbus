#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper128.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr Address kAddressMask = 0x23E3U;
constexpr std::size_t kStateSize = 8197U;

INesHeader header(uint16_t mapper = 128U, bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = mapper;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.chr_ram_size = 8192U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper128> board(uint16_t mapper = 128U,
                                 bool legacy = false,
                                 bool vertical = true,
                                 std::size_t prg_size = 0x80000U) {
    const auto h = header(mapper, legacy, vertical);
    REQUIRE(Mapper128::validate_geometry(h, prg_size, 0U));
    std::vector<Byte> prg(prg_size);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = prg_cell(static_cast<unsigned>(i / 0x4000U), static_cast<unsigned>(i % 0x4000U));
    return std::make_unique<Mapper128>(h, std::move(prg), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper128& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

uint16_t state_u16(const std::vector<Byte>& bytes, std::size_t offset) {
    return static_cast<uint16_t>(bytes[offset] | (static_cast<unsigned>(bytes[offset + 1U]) << 8U));
}

Address write_address(
    unsigned outer, bool mirrored_inner, bool cpu_a14, bool horizontal = false, bool lock = false) {
    return static_cast<Address>(0x8000U | ((outer & 0x0CU) << 6U) | ((outer & 3U) << 5U) |
                                (mirrored_inner ? 0x80U : 0U) | (cpu_a14 ? 2U : 0U) |
                                (horizontal ? 1U : 0U) | (lock ? 0x2000U : 0U));
}

unsigned expected_inner(Byte data, Address address, bool upper) {
    unsigned inner = data & 7U;
    if ((address & 0x80U) == 0U && upper)
        inner = 7U;
    if ((address & 2U) != 0U)
        inner = (inner & 6U) | static_cast<unsigned>(upper);
    return inner;
}

TEST_CASE("Mappers 128 and 265 share the BMC-T-262 power-on configuration",
          "[mapper128][mapper265]") {
    for (uint16_t mapper : {128U, 265U})
        for (bool legacy : {false, true})
            for (bool vertical : {false, true}) {
                auto m = board(mapper, legacy, vertical);
                REQUIRE(m->read_prg(0x8000U) == prg_cell(0U, 0U));
                REQUIRE(m->read_prg(0xC000U) == prg_cell(7U, 0U));
                REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
                REQUIRE(m->battery_ram().empty());
                REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
                REQUIRE(state(*m).size() == kStateSize);
                REQUIRE_FALSE(m->irq_pending());
                REQUIRE_FALSE(m->has_expansion_audio());
                for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
                    REQUIRE_FALSE(m->maps_prg(addr));
                    REQUIRE(m->read_prg(addr) == 0U);
                }
            }
}

TEST_CASE("Mapper 128 address and data latches decode every CPU write alias",
          "[mapper128][mapper265]") {
    auto m = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        m->reset();
        m->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto saved = state(*m);
        const Address expected = addr >= 0x8000U ? static_cast<Address>(addr & kAddressMask) : 0U;
        REQUIRE(state_u16(saved, 2U) == expected);
        REQUIRE(saved[4] == (addr >= 0x8000U ? 5U : 0U));
        REQUIRE(m->mirror_mode() ==
                ((expected & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
}

TEST_CASE("Mapper 128 locking preserves address pins while all data values remain writable",
          "[mapper128][mapper265]") {
    auto m = board();
    constexpr std::array<Address, 10> kAliases = {
        0x0000U, 0x7FFFU, 0x8000U, 0x8001U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xFFFEU, 0xFFFFU};
    for (bool mirrored_inner : {false, true})
        for (bool cpu_a14 : {false, true}) {
            const auto address = write_address(13U, mirrored_inner, cpu_a14, true, true);
            m->reset();
            m->write_prg(address, 3U);
            for (Address alias : kAliases)
                for (Byte value : {0x00U, 0x38U, 0xF8U, 0xFFU}) {
                    const Byte previous = state(*m)[4];
                    m->write_prg(alias, value);
                    const auto saved = state(*m);
                    REQUIRE(state_u16(saved, 2U) == (address & kAddressMask));
                    REQUIRE(saved[4] == (alias >= 0x8000U ? (value & 7U) : previous));
                }
        }
}

TEST_CASE("Mapper 128 data latch uses only its low three bits without ROM bus conflicts",
          "[mapper128][mapper265]") {
    auto m = board();
    m->write_prg(0x8000U, 0U);
    for (unsigned value = 0U; value < 256U; ++value) {
        m->write_prg(0x8000U, static_cast<Byte>(value));
        REQUIRE(m->read_prg(0x8000U) == prg_cell(value & 7U, 0U));
        REQUIRE(m->read_prg(0xC000U) == prg_cell(7U, 0U));
        REQUIRE(state(*m)[4] == (value & 7U));
    }
}

TEST_CASE("Mapper 128 outer banks and all UNROM NROM-128 NROM-256 combinations map correctly",
          "[mapper128][mapper265]") {
    for (uint16_t mapper : {128U, 265U}) {
        auto m = board(mapper);
        for (unsigned outer = 0U; outer < 16U; ++outer)
            for (bool mirrored_inner : {false, true})
                for (bool cpu_a14 : {false, true})
                    for (Byte inner = 0U; inner < 8U; ++inner) {
                        const auto address = write_address(outer, mirrored_inner, cpu_a14);
                        m->write_prg(address, static_cast<Byte>(inner | 0xF8U));
                        for (bool upper : {false, true}) {
                            const Address addr = upper ? 0xC123U : 0x8123U;
                            const unsigned bank =
                                outer * 8U + expected_inner(inner, address, upper);
                            INFO("mapper=" << mapper << " outer=" << outer << " mirrored="
                                           << mirrored_inner << " cpu_a14=" << cpu_a14
                                           << " inner=" << +inner << " upper=" << upper);
                            REQUIRE(m->read_prg(addr) == prg_cell(bank % 32U, 0x123U));
                        }
                    }
    }
}

TEST_CASE("Mapper 128 applies decoded bank pins before wrapping fitted PRG images",
          "[mapper128][mapper265]") {
    for (std::size_t prg_size : {0x4000U, 0xC000U, 0x30000U}) {
        auto m = board(128U, false, true, prg_size);
        const unsigned bank_count = static_cast<unsigned>(prg_size / 0x4000U);
        for (unsigned outer = 0U; outer < 16U; ++outer)
            for (bool mirrored_inner : {false, true})
                for (bool cpu_a14 : {false, true})
                    for (Byte inner = 0U; inner < 8U; ++inner) {
                        const auto address = write_address(outer, mirrored_inner, cpu_a14);
                        m->write_prg(address, inner);
                        for (bool upper : {false, true}) {
                            const unsigned bank =
                                (outer * 8U + expected_inner(inner, address, upper)) % bank_count;
                            const Address addr = upper ? 0xC123U : 0x8123U;
                            REQUIRE(m->read_prg(addr) == prg_cell(bank, 0x123U));
                        }
                    }
    }
}

TEST_CASE("Mapper 128 reaches every PRG cell and provides independent writable CHR RAM",
          "[mapper128][mapper265]") {
    auto m = board();
    for (unsigned bank = 0U; bank < 32U; ++bank) {
        m->write_prg(write_address(bank / 8U, true, false), static_cast<Byte>(bank % 8U));
        for (unsigned offset = 0U; offset < 0x4000U; ++offset) {
            REQUIRE(m->read_prg(static_cast<Address>(0x8000U + offset)) == prg_cell(bank, offset));
            REQUIRE(m->read_prg(static_cast<Address>(0xC000U + offset)) == prg_cell(bank, offset));
        }
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        m->write_chr(static_cast<Address>(addr), prg_cell(0U, addr));
        REQUIRE(m->read_chr(static_cast<Address>(addr)) == prg_cell(0U, addr));
        REQUIRE(m->read_chr(static_cast<Address>(addr + 0x2000U)) == prg_cell(0U, addr));
    }
}

TEST_CASE("Mapper 128 reset unlocks and clears latches while cold power clears only CHR RAM",
          "[mapper128][mapper265]") {
    auto m = board();
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr));
    m->write_prg(write_address(15U, true, true, true, true), 7U);
    m->reset();
    REQUIRE(m->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(m->read_prg(0xC000U) == prg_cell(7U, 0U));
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(m->read_chr(0x1234U) == 0x34U);
    m->write_prg(0x8001U, 5U);
    REQUIRE(m->read_prg(0x8000U) == prg_cell(5U, 0U));
    m->power_on();
    REQUIRE(m->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(m->read_prg(0xC000U) == prg_cell(7U, 0U));
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(m->read_chr(static_cast<Address>(addr)) == 0U);
}

TEST_CASE("Mapper 128 geometry accepts only bounded ROM and volatile CHR populations",
          "[mapper128][mapper265]") {
    for (uint16_t mapper : {128U, 265U})
        for (bool legacy : {false, true})
            for (std::size_t size = 0x4000U; size <= 0x80000U; size += 0x4000U)
                REQUIRE(Mapper128::validate_geometry(header(mapper, legacy), size, 0U));

    const auto reject = [](const INesHeader& h, std::size_t prg = 0x80000U, std::size_t chr = 0U) {
        REQUIRE_FALSE(Mapper128::validate_geometry(h, prg, chr));
    };
    for (std::size_t size : {0U, 1U, 0x2000U, 0x3FFFU, 0x4001U, 0x6000U, 0x80001U, 0x100000U})
        reject(header(), size);
    reject(header(), 0x80000U, 8192U);
    auto wrong_id = header();
    wrong_id.mapper_number = 129U;
    reject(wrong_id);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h);
    }
    for (bool legacy : {false, true}) {
        auto h = header(128U, legacy);
        h.has_battery = true;
        reject(h);
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            h = header(128U, legacy);
            h.mirror_mode = mode;
            reject(h);
        }
    }
    for (unsigned field = 0U; field < 5U; ++field) {
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
            h.chr_ram_size = 4096U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        reject(h);
    }
}

TEST_CASE("Mapper 128 bounded state replays CHR and latches and rejects damage atomically",
          "[mapper128][mapper265]") {
    for (uint16_t mapper : {128U, 265U}) {
        auto m = board(mapper);
        m->write_prg(write_address(13U, true, true, true, true), 0xFDU);
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            m->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 3U));
        const auto saved = state(*m);
        REQUIRE(saved.size() == kStateSize);
        REQUIRE(state_u16(saved, 0U) == (0x8000U | mapper));

        m->reset();
        m->write_chr(0x1234U, 0U);
        StateReader replay(saved);
        m->load_state(replay);
        REQUIRE(replay.ok());
        REQUIRE(replay.remaining() == 0U);
        REQUIRE(state(*m) == saved);
        REQUIRE(m->read_chr(0x1234U) == static_cast<Byte>(0x1234U * 3U));
        REQUIRE(m->mirror_mode() == MirrorMode::Horizontal);

        for (std::size_t length : {0U, 1U, 2U, 3U, 4U, 5U, 8195U, 8196U}) {
            StateReader reader{std::span(saved).first(length)};
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }
        for (unsigned field = 0U; field < 5U; ++field) {
            auto bad = saved;
            switch (field) {
            case 0U:
                bad[0] ^= 1U;
                break;
            case 1U:
                bad[1] ^= 1U;
                break;
            case 2U:
                bad[2] |= 4U;
                break;
            case 3U:
                bad[3] |= 4U;
                break;
            default:
                bad[4] = 8U;
                break;
            }
            StateReader reader(bad);
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }

        auto other = board(mapper == 128U ? 265U : 128U);
        const auto before = state(*other);
        StateReader mismatched(saved);
        other->load_state(mismatched);
        REQUIRE_FALSE(mismatched.ok());
        REQUIRE(state(*other) == before);
    }
}

} // namespace
