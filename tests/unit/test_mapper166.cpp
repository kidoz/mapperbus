#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "core/mappers/mapper166.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 1024U * 1024U;
constexpr std::array<Byte, 4> kRegisterMasks = {0x11U, 0x1CU, 0x1FU, 0x1FU};

INesHeader header(uint16_t id, bool legacy = false, MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader h{};
    h.mapper_number = id;
    h.is_nes2 = !legacy;
    h.mirror_mode = mirror;
    h.prg_ram_size = legacy ? 0U : 8192U;
    h.chr_ram_size = legacy ? 0U : 8192U;
    return h;
}

std::vector<Byte> rom() {
    std::vector<Byte> bytes(kPrgSize);
    for (std::size_t offset = 0U; offset < bytes.size(); ++offset)
        bytes[offset] = static_cast<Byte>(offset / 0x4000U);
    return bytes;
}

std::unique_ptr<Mapper166> board(uint16_t id,
                                 bool legacy = false,
                                 MirrorMode mirror = MirrorMode::Horizontal) {
    auto bytes = rom();
    REQUIRE(Mapper166::validate_geometry(header(id, legacy, mirror), bytes.size(), 0U));
    return std::make_unique<Mapper166>(
        header(id, legacy, mirror), std::move(bytes), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper& mapper,
                 unsigned mode,
                 unsigned lower,
                 unsigned upper,
                 bool first_outer,
                 bool second_outer,
                 bool vertical = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>((first_outer ? 0x10U : 0U) | (vertical ? 1U : 0U)));
    mapper.write_prg(0xA000U, static_cast<Byte>((second_outer ? 0x10U : 0U) | ((mode & 3U) << 2U)));
    mapper.write_prg(0xC000U, static_cast<Byte>(upper & 0x1FU));
    mapper.write_prg(0xE000U, static_cast<Byte>(lower & 0x1FU));
}

std::pair<unsigned, unsigned> banks(uint16_t id, unsigned mode, unsigned selected) {
    switch (mode) {
    case 0U:
        return {selected, id == 166U ? 0x07U : 0x20U};
    case 1U:
        return {0x1FU, selected};
    default:
        return {id == 166U ? selected : selected ^ 1U, selected};
    }
}

TEST_CASE("Mappers 166 and 167 implement every XOR bank and PRG layout", "[mapper166][mapper167]") {
    for (uint16_t id : {166U, 167U}) {
        auto mapper = board(id);
        for (unsigned mode = 0U; mode < 4U; ++mode)
            for (unsigned lower = 0U; lower < 32U; ++lower)
                for (unsigned upper = 0U; upper < 32U; ++upper)
                    for (bool first_outer : {false, true})
                        for (bool second_outer : {false, true}) {
                            INFO("mapper=" << id << " mode=" << mode << " lower=" << lower
                                           << " upper=" << upper);
                            select_bank(*mapper, mode, lower, upper, first_outer, second_outer);
                            const unsigned selected =
                                ((first_outer != second_outer) ? 0x20U : 0U) | (lower ^ upper);
                            const auto [low_bank, high_bank] = banks(id, mode, selected);
                            REQUIRE(mapper->read_prg(0x8000U) == low_bank);
                            REQUIRE(mapper->read_prg(0xBFFFU) == low_bank);
                            REQUIRE(mapper->read_prg(0xC000U) == high_bank);
                            REQUIRE(mapper->read_prg(0xFFFFU) == high_bank);
                        }
    }
}

TEST_CASE("Mappers 166 and 167 decode all register aliases without bus conflicts",
          "[mapper166][mapper167]") {
    for (uint16_t id : {166U, 167U}) {
        auto mapper = board(id);
        auto canonical = board(id);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
            const auto canonical_addr =
                static_cast<Address>(0x8000U + ((addr >> 13U) & 3U) * 0x2000U);
            mapper->write_prg(static_cast<Address>(addr), value);
            canonical->write_prg(canonical_addr, value);
            REQUIRE(mapper->read_prg(0x8000U) == canonical->read_prg(0x8000U));
            REQUIRE(mapper->read_prg(0xC000U) == canonical->read_prg(0xC000U));
            REQUIRE(mapper->mirror_mode() == canonical->mirror_mode());
        }
        REQUIRE(state(*mapper) == state(*canonical));

        for (std::size_t index = 0U; index < kRegisterMasks.size(); ++index)
            for (unsigned value = 0U; value < 256U; ++value) {
                const auto addr = static_cast<Address>(0x8000U + index * 0x2000U);
                mapper->write_prg(addr, static_cast<Byte>(value));
                REQUIRE(state(*mapper)[index + 2U] ==
                        static_cast<Byte>(value & kRegisterMasks[index]));
            }
        select_bank(*mapper, 0U, 0U, 0U, false, false, true);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        select_bank(*mapper, 0U, 0U, 0U, false, false, false);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
}

TEST_CASE("Mappers 166 and 167 expose volatile unbanked PRG and CHR RAM",
          "[mapper166][mapper167]") {
    for (uint16_t id : {166U, 167U})
        for (bool legacy : {false, true}) {
            auto mapper = board(id, legacy);
            const auto initial = state(*mapper);
            for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr)
                REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
            for (unsigned addr = 0U; addr < 0x6000U; ++addr)
                mapper->write_prg(static_cast<Address>(addr), 0xA7U);
            REQUIRE(state(*mapper) == initial);
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
                mapper->write_prg(static_cast<Address>(0x6000U + addr), value);
                mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(value ^ 0xA5U));
            }
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
                REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + addr)) == value);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        static_cast<Byte>(value ^ 0xA5U));
            }
            select_bank(*mapper, 1U, 17U, 9U, true, false, true);
            mapper->reset();
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            REQUIRE(mapper->read_prg(0x8000U) == 0U);
            REQUIRE(mapper->read_prg(0xC000U) == (id == 166U ? 7U : 32U));
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_prg(0x6001U) == 1U);
            REQUIRE(mapper->read_chr(0U) == 0xA5U);
            REQUIRE(mapper->read_chr(1U) == 0xA4U);

            mapper->power_on();
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + addr)) == 0U);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
            }
            REQUIRE(mapper->battery_ram().empty());
            const std::array<Byte, 2> ignored = {0xA7U, 0x5AU};
            mapper->set_battery_ram(ignored);
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            for (Address addr : {0x4020U, 0x5FFFU, 0x8000U, 0xFFFFU})
                REQUIRE_FALSE(mapper->maps_expansion(addr));
        }
}

TEST_CASE("Mappers 166 and 167 reject unsupported ROM and memory geometry",
          "[mapper166][mapper167]") {
    for (uint16_t id : {166U, 167U}) {
        REQUIRE(Mapper166::validate_geometry(header(id), kPrgSize, 0U));
        REQUIRE(Mapper166::validate_geometry(header(id, true), kPrgSize, 0U));
        for (unsigned kind = 0U; kind < 19U; ++kind) {
            auto h = header(id);
            std::size_t prg_size = kPrgSize;
            std::size_t chr_size = 0U;
            switch (kind) {
            case 0U:
                h.mapper_number = 0U;
                break;
            case 1U:
                h.submapper = 1U;
                break;
            case 2U:
                prg_size = 0U;
                break;
            case 3U:
                prg_size = 524288U;
                break;
            case 4U:
                prg_size = 2097152U;
                break;
            case 5U:
                --prg_size;
                break;
            case 6U:
                chr_size = 8192U;
                break;
            case 7U:
                h.has_battery = true;
                break;
            case 8U:
                h.prg_ram_size = 0U;
                break;
            case 9U:
                h.prg_ram_size = 16384U;
                break;
            case 10U:
                h.prg_nvram_size = 8192U;
                break;
            case 11U:
                h.chr_ram_size = 0U;
                break;
            case 12U:
                h.chr_ram_size = 16384U;
                break;
            case 13U:
                h.chr_nvram_size = 8192U;
                break;
            case 14U:
                h.mirror_mode = MirrorMode::FourScreen;
                break;
            case 15U:
                h.mirror_mode = MirrorMode::SingleLower;
                break;
            case 16U:
                h.mirror_mode = MirrorMode::SingleUpper;
                break;
            case 17U:
                h = header(id, true);
                h.has_battery = true;
                break;
            case 18U:
                h = header(id, true);
                h.mirror_mode = MirrorMode::FourScreen;
                break;
            default:
                break;
            }
            INFO("mapper=" << id << " rejection=" << kind);
            REQUIRE_FALSE(Mapper166::validate_geometry(h, prg_size, chr_size));
        }
    }
}

TEST_CASE("Mappers 166 and 167 replay bounded state and reject damage atomically",
          "[mapper166][mapper167][state]") {
    for (uint16_t id : {166U, 167U}) {
        auto mapper = board(id);
        select_bank(*mapper, 3U, 0x15U, 0x0AU, true, false, true);
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            mapper->write_prg(static_cast<Address>(0x6000U + addr), static_cast<Byte>(addr));
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr >> 3U));
        }
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == 16390U);
        auto copy = board(id);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        const unsigned selected = 0x20U | (0x15U ^ 0x0AU);
        const auto [low_bank, high_bank] = banks(id, 3U, selected);
        REQUIRE(copy->read_prg(0x8000U) == low_bank);
        REQUIRE(copy->read_prg(0xC000U) == high_bank);

        for (std::size_t length : {0UZ, 1UZ, 2UZ, 5UZ, 6UZ, 8197UZ, 16389UZ}) {
            StateReader truncated(std::span(saved).first(length));
            mapper->load_state(truncated);
            REQUIRE_FALSE(truncated.ok());
            REQUIRE(state(*mapper) == saved);
        }
        for (std::size_t offset = 0U; offset < 6U; ++offset) {
            auto bad = saved;
            bad[offset] = offset < 2U ? static_cast<Byte>(bad[offset] ^ 1U) : 0x80U;
            bad[6U + 0x123U] ^= 0xFFU;
            bad[6U + 8192U + 0x123U] ^= 0xFFU;
            StateReader invalid(bad);
            mapper->load_state(invalid);
            REQUIRE_FALSE(invalid.ok());
            REQUIRE(state(*mapper) == saved);
        }

        auto other = board(id == 166U ? 167U : 166U);
        const auto before = state(*other);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(*other) == before);
    }
}

} // namespace
