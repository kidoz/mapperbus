#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper349.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrSize = 0x2000UZ;
constexpr std::size_t kLatchOffset = 6UZ;
constexpr std::size_t kChrOffset = 8UZ;
constexpr std::size_t kStateSize = kChrOffset + kChrSize;
constexpr std::array<Address, 6> kPrgAddresses{
    0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xC456U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 349U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 23U + (offset >> 8U) * 31U + (offset >> 14U) * 47U +
                             (offset >> 17U) * 19U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper349> board(std::size_t prg_size = kPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper349::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper349>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::uint16_t canonical_latch(Address addr, Byte value) {
    return static_cast<std::uint16_t>((addr & 0x1000U) | value);
}

std::size_t expected_prg_offset(std::uint16_t latch, Address addr) {
    const Byte inner = latch & 7U;
    const bool upper = (addr & 0x4000U) != 0U;
    Byte bank = inner;
    if ((latch & 0x1000U) != 0U) {
        bank = upper ? 7U : static_cast<Byte>(inner | ((latch & 0x40U) != 0U ? 1U : 0U));
    } else if ((latch & 0x40U) == 0U) {
        bank = static_cast<Byte>((inner & 6U) | (upper ? 1U : 0U));
    }
    return static_cast<std::size_t>((latch >> 3U) & 7U) * 0x20000UZ +
           static_cast<std::size_t>(bank) * 0x4000UZ + (addr & 0x3FFFU);
}

TEST_CASE("Mapper 349 validates ID submapper ROM memory and CIRAM geometry", "[mapper349]") {
    for (std::size_t size = 0x20000UZ; size <= kPrgSize; size *= 2U) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            REQUIRE(Mapper349::validate_geometry(header(mirror), size, 0U));
        }
    }

    for (const unsigned id : {0U, 4U, 226U, 348U, 350U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper349::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper349::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper349::validate_geometry(h, kPrgSize, 0U));
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
        REQUIRE_FALSE(Mapper349::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        REQUIRE_FALSE(Mapper349::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper349::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
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
        REQUIRE_FALSE(Mapper349::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 349 decodes all outer inner NROM and UNROM-like banks", "[mapper349]") {
    auto mapper = board();
    for (const Address unrom_addr : {0x8000U, 0x9000U}) {
        for (Byte outer = 0U; outer < 8U; ++outer) {
            for (Byte inner = 0U; inner < 8U; ++inner) {
                for (Byte mode = 0U; mode < 4U; ++mode) {
                    const auto value = static_cast<Byte>((outer << 3U) | inner | (mode << 6U));
                    mapper->write_prg(unrom_addr, value);
                    const auto latch = canonical_latch(unrom_addr, value);
                    for (const auto addr : kPrgAddresses) {
                        const auto offset = expected_prg_offset(latch, addr);
                        CAPTURE(unrom_addr, outer, inner, mode, addr, offset);
                        REQUIRE(mapper->read_prg(addr) == cell(offset));
                    }
                }
            }
        }
    }

    for (const std::size_t size : {0x20000UZ, 0x40000UZ, 0x80000UZ, kPrgSize}) {
        auto wrapped = board(size);
        wrapped->write_prg(0x9000U, 0xFFU);
        for (const auto addr : kPrgAddresses) {
            const auto offset = expected_prg_offset(0x10FFU, addr) % size;
            CAPTURE(size, addr, offset);
            REQUIRE(wrapped->read_prg(addr) == cell(offset));
        }
    }
}

TEST_CASE("Mapper 349 address latch uses only A12 and data without conflicts", "[mapper349]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto canonical = static_cast<Address>(0x8000U | (raw & 0x1000U));
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U));
        actual->write_prg(addr, value);
        expected->write_prg(canonical, value);
        CAPTURE(raw, canonical, value);
        REQUIRE(state(*actual) == state(*expected));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        actual->write_prg(0xD123U, static_cast<Byte>(value));
        const auto saved = state(*actual);
        REQUIRE(saved[kLatchOffset] == value);
        REQUIRE(saved[kLatchOffset + 1U] == 0x10U);
        REQUIRE(actual->mirror_mode() ==
                ((value & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (const auto addr : kPrgAddresses) {
            REQUIRE(actual->read_prg(addr) ==
                    cell(expected_prg_offset(canonical_latch(0xD123U, value), addr)));
        }
    }
}

TEST_CASE("Mapper 349 maps unbanked volatile CHR RAM and open CPU windows", "[mapper349]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < kChrSize; ++offset) {
        REQUIRE(mapper->read_chr(static_cast<Address>(offset)) == 0U);
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0xA7U));
    }
    for (unsigned offset = 0U; offset < kChrSize; ++offset) {
        CAPTURE(offset);
        REQUIRE(mapper->read_chr(static_cast<Address>(offset)) == cell(offset, 0xA7U));
        REQUIRE(mapper->read_chr(static_cast<Address>(0x2000U + offset)) == cell(offset, 0xA7U));
    }

    const auto before = state(*mapper);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }
    for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::vector<Byte>(kChrSize, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
}

TEST_CASE("Mapper 349 reset restores zero latch and power clears CHR RAM", "[mapper349][reset]") {
    auto mapper = board();
    mapper->write_prg(0x9000U, 0xFFU);
    mapper->write_chr(0x0456U, 0xC7U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0xC000U) == cell(0xFC000UZ));

    mapper->reset();
    auto fresh = board();
    REQUIRE(state(*mapper) != state(*fresh));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0x4000UZ));
    REQUIRE(mapper->read_chr(0x0456U) == 0xC7U);

    mapper->write_prg(0x9000U, 0xFFU);
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*fresh));
    REQUIRE(mapper->read_chr(0x0456U) == 0U);
}

TEST_CASE("Mapper 349 snapshots replay latch and CHR RAM without ROM serialization",
          "[mapper349][state]") {
    auto mapper = board();
    mapper->write_prg(0x9000U, 0xD5U);
    for (unsigned offset = 0U; offset < kChrSize; ++offset) {
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0x91U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8200UZ);

    auto copy = board(kPrgSize, MirrorMode::Horizontal, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
    }
    for (unsigned offset = 0U; offset < kChrSize; ++offset) {
        REQUIRE(copy->read_chr(static_cast<Address>(offset)) == cell(offset, 0x91U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);

    auto other_geometry = board(0x80000UZ);
    const auto before = state(*other_geometry);
    StateReader mismatch(saved);
    other_geometry->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other_geometry) == before);
}

TEST_CASE("Mapper 349 rejects bounded malformed states atomically", "[mapper349][state]") {
    auto mapper = board();
    mapper->write_prg(0x9000U, 0xD5U);
    for (unsigned offset = 0U; offset < kChrSize; ++offset) {
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0x42U));
    }
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
    constexpr std::uint16_t kAllowedLatchBits = 0x10FFU;
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        if ((kAllowedLatchBits & (1U << bit)) != 0U) {
            continue;
        }
        auto invalid = saved;
        invalid[kLatchOffset + bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }

    for (const std::uint16_t latch :
         {0x0000U, 0x0040U, 0x0080U, 0x00FFU, 0x1000U, 0x1040U, 0x1080U, 0x10FFU}) {
        auto valid = saved;
        valid[kLatchOffset] = static_cast<Byte>(latch);
        valid[kLatchOffset + 1U] = static_cast<Byte>(latch >> 8U);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == valid);
    }
}

} // namespace
