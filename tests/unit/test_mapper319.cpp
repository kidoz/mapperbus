#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper319.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 16384U;
constexpr std::size_t kChrBankSize = 8192U;
constexpr std::size_t kMaxPrgSize = 8U * kPrgBankSize;
constexpr std::size_t kMaxChrSize = 16U * kChrBankSize;
constexpr std::size_t kStateSize = 12U;

INesHeader header(MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 319U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 37U + (offset >> 8U) * 13U + (offset >> 14U) * 29U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper319> board(std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Horizontal,
                                 Byte solder_pad = 0U) {
    REQUIRE(Mapper319::validate_geometry(header(mirror), prg_size, chr_size));
    auto mapper = std::make_unique<Mapper319>(header(mirror), rom(prg_size), rom(chr_size));
    REQUIRE(mapper->configure_board_jumpers(solder_pad));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte control, Address addr) {
    const unsigned bank = (((control >> 4U) & 1U) << 2U) | (((control >> 3U) & 1U) << 1U) |
                          ((control & 0x40U) != 0U ? ((addr >> 14U) & 1U) : ((control >> 5U) & 1U));
    return cell(bank * kPrgBankSize + (addr & 0x3FFFU));
}

TEST_CASE("Mapper 319 validates ID submapper ROM bounds and memory population", "[mapper319]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = kPrgBankSize; prg <= kMaxPrgSize; prg *= 2U) {
            for (std::size_t chr = kChrBankSize; chr <= kMaxChrSize; chr *= 2U) {
                REQUIRE(Mapper319::validate_geometry(header(mirror), prg, chr));
            }
        }
    }

    auto invalid = header();
    invalid.mapper_number = 318U;
    REQUIRE_FALSE(Mapper319::validate_geometry(invalid, kMaxPrgSize, kMaxChrSize));
    invalid = header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper319::validate_geometry(invalid, kMaxPrgSize, kMaxChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper319::validate_geometry(invalid, kMaxPrgSize, kMaxChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 8192UZ, 16383UZ, 16385UZ, 24576UZ, 262144UZ}) {
        REQUIRE_FALSE(Mapper319::validate_geometry(header(), size, kMaxChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 4096UZ, 8191UZ, 8193UZ, 12288UZ, 262144UZ}) {
        REQUIRE_FALSE(Mapper319::validate_geometry(header(), kMaxPrgSize, size));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        invalid = header();
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.prg_ram_size = 8192U;
            break;
        case 2U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 3U:
            invalid.chr_ram_size = 8192U;
            break;
        default:
            invalid.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper319::validate_geometry(invalid, kMaxPrgSize, kMaxChrSize));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper319::validate_geometry(header(mirror), kMaxPrgSize, kMaxChrSize));
    }
}

TEST_CASE("Mapper 319 maps every PRG bank pin and NROM mode", "[mapper319]") {
    auto mapper = board();
    constexpr std::uint32_t addresses[] = {0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x6004U, static_cast<Byte>(value));
        for (const auto raw_addr : addresses) {
            const auto addr = static_cast<Address>(raw_addr);
            CAPTURE(value, raw_addr);
            REQUIRE(mapper->read_prg(addr) == expected_prg(static_cast<Byte>(value & 0xF8U), addr));
        }
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x80U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }

    for (std::size_t size = kPrgBankSize; size <= kMaxPrgSize; size *= 2U) {
        auto small = board(size);
        for (unsigned value = 0U; value < 256U; ++value) {
            small->write_prg(0xE004U, static_cast<Byte>(value));
            const unsigned bank = (((value >> 4U) & 1U) << 2U) | (((value >> 3U) & 1U) << 1U) |
                                  ((value & 0x40U) != 0U ? 1U : ((value >> 5U) & 1U));
            const auto offset = (bank * kPrgBankSize + 0x0123U) % size;
            CAPTURE(size, value);
            REQUIRE(small->read_prg(0xC123U) == cell(offset));
        }
    }
}

TEST_CASE("Mapper 319 decodes exactly the documented CHR and PRG register aliases", "[mapper319]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    REQUIRE(initial.size() == kStateSize);

    for (unsigned raw_addr = 0U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw_addr), 0xA5U);
        auto expected = initial;
        if ((raw_addr & 0x6004U) == 0x6000U) {
            expected[11U] = 0xA0U;
        } else if ((raw_addr & 0x6004U) == 0x6004U) {
            expected[10U] = 0xA0U;
        }
        CAPTURE(raw_addr);
        REQUIRE(state(*mapper) == expected);
    }

    mapper->reset();
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        mapper->write_expansion(static_cast<Address>(addr), 0xFFU);
    }
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 319 banks all CHR pins and protects ROM from writes", "[mapper319]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x6000U, static_cast<Byte>(value));
        const auto bank = (value >> 4U) & 0x0FU;
        for (const Address addr : {0x0000U, 0x0123U, 0x1000U, 0x1FFFU}) {
            CAPTURE(value, addr);
            REQUIRE(mapper->read_chr(addr) == cell(bank * kChrBankSize + addr));
        }
    }

    mapper->write_prg(0xE000U, 0xF0U);
    for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
        const auto expected = cell(15U * kChrBankSize + addr);
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(expected ^ 0xFFU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == expected);
    }

    for (std::size_t size = kChrBankSize; size <= kMaxChrSize; size *= 2U) {
        auto small = board(kMaxPrgSize, size);
        for (unsigned bank = 0U; bank < 16U; ++bank) {
            small->write_prg(0xE000U, static_cast<Byte>(bank << 4U));
            const auto offset = (bank * kChrBankSize + 0x0456U) % size;
            REQUIRE(small->read_chr(0x0456U) == cell(offset));
        }
    }
}

TEST_CASE("Mapper 319 exposes only D4 from the probable solder-pad aperture", "[mapper319]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal, 1U);
    for (unsigned raw_addr = 0x4020U; raw_addr < 0x6000U; ++raw_addr) {
        const auto addr = static_cast<Address>(raw_addr);
        const bool mapped = (raw_addr & 0xFFF0U) == 0x5FF0U;
        CAPTURE(raw_addr);
        REQUIRE(mapper->maps_expansion(addr) == mapped);
        REQUIRE(mapper->expansion_read_mask(addr) == (mapped ? 0x10U : 0U));
        REQUIRE(mapper->read_expansion(addr) == (mapped ? 0x10U : 0U));
        const Byte open_bus = 0xA7U;
        const auto merged =
            static_cast<Byte>((mapper->read_expansion(addr) & mapper->expansion_read_mask(addr)) |
                              (open_bus & ~mapper->expansion_read_mask(addr)));
        REQUIRE(merged == (mapped ? 0xB7U : 0xA7U));
    }

    REQUIRE(mapper->configure_board_jumpers(0U));
    REQUIRE(mapper->read_expansion(0x5FF0U) == 0U);
    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->configure_board_jumpers(2U));
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->read_expansion(0x5FFFU) == 0U);
}

TEST_CASE("Mapper 319 reset clears latches but preserves the solder pad", "[mapper319]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, 1U);
    mapper->write_prg(0x6000U, 0xF0U);
    mapper->write_prg(0x6004U, 0xF8U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
    REQUIRE(mapper->read_chr(0U) == cell(0U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_expansion(0x5FF0U) == 0x10U);

    mapper->write_prg(0x6000U, 0xA0U);
    mapper->write_prg(0x6004U, 0xD8U);
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, 1U)));
}

TEST_CASE("Mapper 319 has no RAM persistence IRQ or expansion audio", "[mapper319]") {
    auto mapper = board();
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE_FALSE(mapper->maps_prg(0x7FFFU));
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 319 state is bounded and replays latches without ROM", "[mapper319][state]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal, 1U);
    mapper->write_prg(0x6000U, 0xB0U);
    mapper->write_prg(0x6004U, 0xD8U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgBankSize);

    auto replay = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal, 1U);
    StateReader reader(saved);
    replay->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*replay) == saved);
    REQUIRE(replay->read_prg(0x8000U) == mapper->read_prg(0x8000U));
    REQUIRE(replay->read_prg(0xC000U) == mapper->read_prg(0xC000U));
    REQUIRE(replay->read_chr(0x0123U) == mapper->read_chr(0x0123U));
    REQUIRE(replay->mirror_mode() == mapper->mirror_mode());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    replay->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
}

TEST_CASE("Mapper 319 rejects malformed and mismatched states atomically", "[mapper319][state]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal, 1U);
    mapper->write_prg(0x6000U, 0x70U);
    mapper->write_prg(0x6004U, 0xA8U);
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
    for (std::size_t offset = 0U; offset < 10U; ++offset) {
        auto damaged = saved;
        damaged[offset] ^= 1U;
        reject(damaged);
    }
    for (unsigned bit = 0U; bit < 3U; ++bit) {
        auto damaged = saved;
        damaged[10U] |= static_cast<Byte>(1U << bit);
        reject(damaged);
    }
    for (unsigned bit = 0U; bit < 4U; ++bit) {
        auto damaged = saved;
        damaged[11U] |= static_cast<Byte>(1U << bit);
        reject(damaged);
    }

    auto wrong_pad = board();
    auto before = state(*wrong_pad);
    StateReader pad_reader(saved);
    wrong_pad->load_state(pad_reader);
    REQUIRE_FALSE(pad_reader.ok());
    REQUIRE(state(*wrong_pad) == before);

    for (const auto geometry :
         {std::pair{kPrgBankSize, kMaxChrSize}, std::pair{kMaxPrgSize, kChrBankSize}}) {
        auto other = board(geometry.first, geometry.second);
        before = state(*other);
        StateReader mismatch(saved);
        other->load_state(mismatch);
        REQUIRE_FALSE(mismatch.ok());
        REQUIRE(state(*other) == before);
    }
}

} // namespace
