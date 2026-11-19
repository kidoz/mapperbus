#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper337.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 16384UZ;
constexpr std::size_t kOuterSize = 128UZ * 1024UZ;
constexpr std::size_t kPrgSize = 4UZ * kOuterSize;
constexpr std::size_t kChrSize = 8192UZ;
constexpr std::size_t kOuterOffset = 6UZ;
constexpr std::size_t kInnerOffset = 7UZ;
constexpr std::size_t kProtectOffset = 8UZ;
constexpr std::size_t kChrStateOffset = 9UZ;
constexpr std::size_t kStateSize = kChrStateOffset + kChrSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 337U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 7U + (offset >> 8U) * 13U + (offset >> 14U) * 31U +
                             (offset >> 17U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset, salt);
    }
    return bytes;
}

std::unique_ptr<Mapper337> board(std::size_t prg_size = kPrgSize,
                                 Byte salt = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper337::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper337>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte outer, Byte inner, Address addr, std::size_t prg_size = kPrgSize) {
    if (addr >= 0x6000U && addr < 0x8000U) {
        return cell((8192UZ + addr - 0x6000U) % prg_size);
    }
    const std::size_t outer_base = static_cast<std::size_t>((outer >> 3U) & 3U) * kOuterSize;
    std::size_t selected = inner & 7U;
    switch (outer >> 6U) {
    case 1U:
        selected = (selected & 6U) | ((addr >> 14U) & 1U);
        break;
    case 2U:
    case 3U:
        if (addr >= 0xC000U) {
            selected = 7U;
        }
        break;
    default:
        break;
    }
    return cell((outer_base + selected * kPrgBankSize + (addr & 0x3FFFU)) % prg_size);
}

TEST_CASE("Mapper 337 validates mapper submapper and native memory population", "[mapper337]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t size : {kOuterSize, 2UZ * kOuterSize, kPrgSize}) {
            REQUIRE(Mapper337::validate_geometry(header(mirror), size, 0U));
        }
    }

    for (const auto mapper_number : {0U, 2U, 335U, 336U, 338U, 350U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(mapper_number);
        REQUIRE_FALSE(Mapper337::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper337::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper337::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   8192UZ,
                                   16384UZ,
                                   32768UZ,
                                   65536UZ,
                                   131071UZ,
                                   131073UZ,
                                   393216UZ,
                                   524289UZ,
                                   1048576UZ}) {
        REQUIRE_FALSE(Mapper337::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 4096UZ, 8192UZ, 131072UZ}) {
        REQUIRE_FALSE(Mapper337::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper337::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        h = header();
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
            h.chr_ram_size = 4096U;
            break;
        case 5U:
            h.chr_ram_size = 16384U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper337::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 337 decodes A15 and A14 throughout the upper CPU window", "[mapper337]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw_addr = 0U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        auto expected = initial;
        const auto value = static_cast<Byte>(raw_addr * 29U + (raw_addr >> 8U));
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        if (raw_addr >= 0xC000U) {
            expected[kInnerOffset] = value & 7U;
            expected[kProtectOffset] = (raw_addr & 0x2000U) != 0U ? 1U : 0U;
        } else if (raw_addr >= 0x8000U) {
            expected[kOuterOffset] = value;
        }
        CAPTURE(raw_addr, value);
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

TEST_CASE("Mapper 337 fixed eight-KiB window uses PRG bank one", "[mapper337]") {
    for (const std::size_t size : {kOuterSize, 2UZ * kOuterSize, kPrgSize}) {
        auto mapper = board(size);
        for (const Byte outer : {0U, 0x08U, 0x18U, 0xFFU}) {
            mapper->write_prg(0x8000U, outer);
            for (std::size_t offset = 0U; offset < kChrSize; ++offset) {
                const auto addr = static_cast<Address>(0x6000U + offset);
                CAPTURE(size, outer, offset);
                REQUIRE(mapper->read_prg(addr) == cell(8192UZ + offset));
            }
        }
        const auto before = state(*mapper);
        mapper->write_prg(0x6000U, 0xFFU);
        mapper->write_prg(0x7FFFU, 0xFFU);
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 337 implements NROM and UNROM banking in every outer bank", "[mapper337]") {
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (const std::size_t size : {kOuterSize, 2UZ * kOuterSize, kPrgSize}) {
        auto mapper = board(size);
        for (unsigned outer = 0U; outer < 256U; ++outer) {
            for (unsigned inner = 0U; inner < 8U; ++inner) {
                mapper->write_prg(0x8000U, static_cast<Byte>(outer));
                mapper->write_prg(0xC000U, static_cast<Byte>(inner));
                for (const auto addr : addresses) {
                    CAPTURE(size, outer, inner, addr);
                    REQUIRE(mapper->read_prg(addr) ==
                            expected_prg(
                                static_cast<Byte>(outer), static_cast<Byte>(inner), addr, size));
                }
            }
        }
    }
}

TEST_CASE("Mapper 337 NROM modes protect CHR and UNROM modes honor address write protection",
          "[mapper337]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x80U);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 17U + (addr >> 8U)));
    }
    REQUIRE(mapper->read_chr(0x2123U) == mapper->read_chr(0x0123U));

    for (unsigned mode = 0U; mode < 4U; ++mode) {
        for (const bool protected_by_address : {false, true}) {
            for (unsigned addr = 0U; addr < kChrSize; ++addr) {
                const auto previous = mapper->read_chr(static_cast<Address>(addr));
                mapper->write_prg(0x8000U, static_cast<Byte>(mode << 6U));
                mapper->write_prg(protected_by_address ? 0xE000U : 0xC000U, 3U);
                mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(previous ^ 0xFFU));
                const bool writable = mode >= 2U && !protected_by_address;
                CAPTURE(mode, protected_by_address, addr, writable);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        static_cast<Byte>(writable ? previous ^ 0xFFU : previous));
            }
        }
    }
}

TEST_CASE("Mapper 337 outer register controls CIRAM and exposes no auxiliary hardware",
          "[mapper337]") {
    auto mapper = board(kPrgSize, 0U, MirrorMode::Horizontal);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0xBFFFU, static_cast<Byte>(outer));
        CAPTURE(outer);
        REQUIRE(mapper->mirror_mode() ==
                ((outer & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
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

TEST_CASE("Mapper 337 reset clears registers while only cold power clears CHR RAM",
          "[mapper337][reset]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    mapper->write_prg(0x8000U, 0xFFU);
    mapper->write_prg(0xE000U, 7U);
    mapper->write_prg(0xC000U, 7U);
    mapper->write_chr(0x0321U, 0xA7U);
    REQUIRE(state(*mapper) != initial);

    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    REQUIRE(state(*mapper)[kInnerOffset] == 0U);
    REQUIRE(state(*mapper)[kProtectOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x6000U) == cell(8192U));
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

TEST_CASE("Mapper 337 snapshots replay registers and CHR RAM without ROM serialization",
          "[mapper337][state]") {
    auto mapper = board();
    mapper->write_prg(0xFFFFU, 0xADU);
    mapper->write_prg(0x8000U, 0x80U);
    mapper->write_prg(0xC000U, 5U);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U + 7U));
    }
    mapper->write_prg(0xE000U, 5U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kOuterOffset] == 0x80U);
    REQUIRE(saved[kInnerOffset] == 5U);
    REQUIRE(saved[kProtectOffset] == 1U);

    auto copy = board(kPrgSize, 0x80U, MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x6000U, 0x7123U, 0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) ==
                static_cast<Byte>(expected_prg(0x80U, 5U, addr, kPrgSize) + 0x80U));
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

TEST_CASE("Mapper 337 rejects bounded malformed and incompatible states atomically",
          "[mapper337][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xB5U);
    mapper->write_prg(0xE000U, 6U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (std::size_t offset = 2U; offset < kOuterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (const Byte inner : {8U, 9U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kInnerOffset] = inner;
        reject(invalid);
    }
    for (const Byte protection : {2U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kProtectOffset] = protection;
        reject(invalid);
    }

    auto smaller = board(kOuterSize);
    const auto before = state(*smaller);
    StateReader wrong_geometry(saved);
    smaller->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*smaller) == before);
}

TEST_CASE("Mapper 337 state validation accepts documented raw register values",
          "[mapper337][state]") {
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
        REQUIRE(mapper->read_prg(0x8123U) == expected_prg(static_cast<Byte>(outer), 0U, 0x8123U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        auto candidate = saved;
        candidate[kInnerOffset] = static_cast<Byte>(value);
        candidate[kProtectOffset] = value & 1U;
        StateReader reader(candidate);
        mapper->load_state(reader);
        if (value < 8U) {
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == candidate);
        } else {
            REQUIRE_FALSE(reader.ok());
        }
    }
}

} // namespace
