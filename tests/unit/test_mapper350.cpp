#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper350.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kOuterSize = 0x20000UZ;
constexpr std::size_t kPrgSize = 8UZ * kOuterSize;
constexpr std::size_t kChrSize = 0x2000UZ;
constexpr std::size_t kOuterOffset = 6UZ;
constexpr std::size_t kInnerOffset = 7UZ;
constexpr std::size_t kChrOffset = 8UZ;
constexpr std::size_t kStateSize = kChrOffset + kChrSize;
constexpr std::array<Address, 8> kAddresses{
    0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 350U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 7U + (offset >> 8U) * 13U + (offset >> 14U) * 31U +
                             (offset >> 17U) * 43U + (offset >> 19U) * 17U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset, salt);
    }
    return bytes;
}

std::unique_ptr<Mapper350> board(std::size_t prg_size = kPrgSize,
                                 Byte salt = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper350::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper350>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::size_t expected_prg_offset(Byte outer, Byte inner, Address addr) {
    const Byte mode = (outer >> 5U) & 3U;
    const Byte effective_outer = mode < 2U ? static_cast<Byte>(outer & 0xEFU) : outer;
    std::size_t selected = inner & 7U;
    if (mode == 1U) {
        selected = (selected & 6U) | ((addr >> 14U) & 1U);
    } else if (mode >= 2U && addr >= 0xC000U) {
        selected = 7U;
    }
    return static_cast<std::size_t>((effective_outer >> 1U) & 7U) * kOuterSize +
           selected * kPrgBankSize + (addr & 0x3FFFU);
}

TEST_CASE("Mapper 350 validates mapper submapper and native memory population", "[mapper350]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t size = kOuterSize; size <= kPrgSize; size *= 2U) {
            REQUIRE(Mapper350::validate_geometry(header(mirror), size, 0U));
        }
    }
    for (const auto id : {0U, 2U, 337U, 349U, 351U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper350::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper350::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper350::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x4000UZ,
                                   0x10000UZ,
                                   0x1FFFFUZ,
                                   0x20001UZ,
                                   0x30000UZ,
                                   0xFFFFFUZ,
                                   0x100001UZ,
                                   0x200000UZ}) {
        REQUIRE_FALSE(Mapper350::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        REQUIRE_FALSE(Mapper350::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper350::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = kChrSize;
            break;
        case 2U:
            h.prg_nvram_size = kChrSize;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper350::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 350 decodes both register aliases throughout the CPU window", "[mapper350]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = initial;
        const auto value = static_cast<Byte>(raw * 29U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0xC000U) {
            expected[kInnerOffset] = value & 7U;
        } else if (raw >= 0x8000U) {
            expected[kOuterOffset] = value;
        }
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
    }
    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    for (const Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
}

TEST_CASE("Mapper 350 fixed window and all banking modes address both PRG chips", "[mapper350]") {
    for (std::size_t size = kOuterSize; size <= kPrgSize; size *= 2U) {
        auto mapper = board(size);
        for (const Byte outer : {0U, 0x10U, 0x20U, 0x60U, 0xFEU}) {
            mapper->write_prg(0x8000U, outer);
            for (std::size_t offset = 0U; offset < kChrSize; ++offset) {
                CAPTURE(size, outer, offset);
                REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) ==
                        cell((0x2000UZ + offset) % size));
            }
        }
        for (unsigned outer = 0U; outer < 256U; ++outer) {
            for (Byte inner = 0U; inner < 8U; ++inner) {
                mapper->write_prg(0xBFFFU, static_cast<Byte>(outer));
                mapper->write_prg(0xFFFFU, inner);
                for (const auto addr : kAddresses) {
                    const auto offset =
                        expected_prg_offset(static_cast<Byte>(outer), inner, addr) % size;
                    CAPTURE(size, outer, inner, addr, offset);
                    REQUIRE(mapper->read_prg(addr) == cell(offset));
                }
            }
        }
    }
}

TEST_CASE("Mapper 350 chip select is ignored in NROM and active in both UNROM modes",
          "[mapper350]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    for (const Byte mode : {0U, 1U, 2U, 3U}) {
        for (const Byte chip : {0U, 0x10U}) {
            const auto outer = static_cast<Byte>((mode << 5U) | chip);
            mapper->write_prg(0x8000U, outer);
            for (const auto addr : kAddresses) {
                CAPTURE(mode, chip, addr);
                REQUIRE(mapper->read_prg(addr) == cell(expected_prg_offset(outer, 2U, addr)));
            }
        }
    }
}

TEST_CASE("Mapper 350 CHR RAM is protected only in NROM modes", "[mapper350]") {
    auto mapper = board();
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        mapper->write_prg(0x8000U, static_cast<Byte>(mode << 5U));
        for (unsigned addr = 0U; addr < kChrSize; ++addr) {
            const auto previous = mapper->read_chr(static_cast<Address>(addr));
            const auto value = static_cast<Byte>(previous ^ 0xFFU);
            mapper->write_chr(static_cast<Address>(addr), value);
            CAPTURE(mode, addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == (mode < 2U ? previous : value));
            REQUIRE(mapper->read_chr(static_cast<Address>(0x2000U + addr)) ==
                    mapper->read_chr(static_cast<Address>(addr)));
        }
    }
}

TEST_CASE("Mapper 350 controls CIRAM and exposes no RAM IRQ or audio", "[mapper350]") {
    auto mapper = board(kPrgSize, 0U, MirrorMode::Horizontal);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0x8000U, static_cast<Byte>(outer));
        CAPTURE(outer);
        REQUIRE(mapper->mirror_mode() ==
                ((outer & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 350 reset clears registers while cold power clears CHR RAM",
          "[mapper350][reset]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    mapper->write_prg(0x8000U, 0xFFU);
    mapper->write_prg(0xC000U, 7U);
    mapper->write_chr(0x0321U, 0xA7U);
    REQUIRE(state(*mapper) != initial);

    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    REQUIRE(state(*mapper)[kInnerOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x6000U) == cell(0x2000U));
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
    REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);

    mapper->write_prg(0x8000U, 0xFFU);
    mapper->write_prg(0xC000U, 7U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
    }
}

TEST_CASE("Mapper 350 snapshots replay state without ROM serialization", "[mapper350][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xD5U);
    mapper->write_prg(0xFFFFU, 5U);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U + 7U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8200UZ);
    REQUIRE(saved.size() < kPrgSize);

    auto copy = board(kPrgSize, 0x80U, MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kAddresses) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 13U + 7U));
    }
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 350 rejects bounded malformed and incompatible states atomically",
          "[mapper350][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xD5U);
    mapper->write_prg(0xC000U, 6U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 48U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte inner : {8U, 9U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kInnerOffset] = inner;
        reject(invalid);
    }
    auto smaller = board(kOuterSize);
    const auto before = state(*smaller);
    StateReader wrong_geometry(saved);
    smaller->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*smaller) == before);
}

TEST_CASE("Mapper 350 state validation accepts documented raw register values",
          "[mapper350][state]") {
    auto mapper = board();
    const auto saved = state(*mapper);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        auto valid = saved;
        valid[kOuterOffset] = static_cast<Byte>(outer);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*mapper) == valid);
        REQUIRE(mapper->read_prg(0x8123U) ==
                cell(expected_prg_offset(static_cast<Byte>(outer), 0U, 0x8123U)));
    }
    for (unsigned inner = 0U; inner < 8U; ++inner) {
        auto valid = saved;
        valid[kInnerOffset] = static_cast<Byte>(inner);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == valid);
    }
}

} // namespace
