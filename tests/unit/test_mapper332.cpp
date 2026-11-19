#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper332.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kSmallPrgSize = 128UZ * 1024UZ;
constexpr std::size_t kLargePrgSize = 256UZ * 1024UZ;
constexpr std::size_t kSmallChrSize = 64UZ * 1024UZ;
constexpr std::size_t kLargeChrSize = 128UZ * 1024UZ;
constexpr std::size_t kPrgRegisterOffset = 10U;
constexpr std::size_t kChrRegisterOffset = 11U;
constexpr std::size_t kCnromLatchOffset = 12U;
constexpr std::size_t kStateSize = 13U;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 332U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 5U + (offset >> 8U) * 11U + (offset >> 12U) * 29U +
                             (offset >> 17U) * 37U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper332> board(bool large = false, MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    const auto prg_size = large ? kLargePrgSize : kSmallPrgSize;
    const auto chr_size = large ? kLargeChrSize : kSmallChrSize;
    REQUIRE(Mapper332::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper332>(h, rom(prg_size), rom(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte control, Address addr, std::size_t prg_size) {
    const std::size_t outer = static_cast<std::size_t>(control & 0x80U) << 10U;
    const std::size_t high = static_cast<std::size_t>(control & 0x0EU) << 13U;
    const std::size_t a14 = (control & 0x10U) != 0U ? static_cast<std::size_t>(control & 1U) << 14U
                                                    : static_cast<std::size_t>(addr & 0x4000U);
    return cell((outer | high | a14 | (addr & 0x3FFFU)) % prg_size);
}

Byte expected_chr(Byte control, Byte chr_control, Byte latch, Address addr, std::size_t chr_size) {
    Byte inner = latch;
    if ((chr_control & 0x30U) == 0x20U) {
        inner = static_cast<Byte>((latch & 1U) | ((chr_control & 1U) << 1U));
    } else if ((chr_control & 0x30U) == 0x30U) {
        inner = chr_control & 3U;
    }
    const std::size_t outer = static_cast<std::size_t>(control & 0x80U) << 9U;
    const std::size_t a15 = static_cast<std::size_t>(chr_control & 4U) << 13U;
    return cell((outer | a15 | (static_cast<std::size_t>(inner) << 13U) | (addr & 0x1FFFU)) %
                chr_size);
}

TEST_CASE("Mapper 332 validates only identified native memory populations", "[mapper332]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper332::validate_geometry(header(mirror), kSmallPrgSize, kSmallChrSize));
        REQUIRE(Mapper332::validate_geometry(header(mirror), kLargePrgSize, kLargeChrSize));
    }

    constexpr std::array sizes{0UZ,
                               1UZ,
                               4096UZ,
                               8192UZ,
                               16384UZ,
                               32768UZ,
                               65536UZ,
                               131072UZ,
                               262144UZ,
                               524288UZ,
                               1048576UZ};
    for (const auto prg_size : sizes) {
        for (const auto chr_size : sizes) {
            const bool valid = (prg_size == kSmallPrgSize && chr_size == kSmallChrSize) ||
                               (prg_size == kLargePrgSize && chr_size == kLargeChrSize);
            CAPTURE(prg_size, chr_size);
            REQUIRE(Mapper332::validate_geometry(header(), prg_size, chr_size).has_value() ==
                    valid);
        }
    }
    REQUIRE_FALSE(Mapper332::validate_geometry(header(), kSmallPrgSize, kLargeChrSize));
    REQUIRE_FALSE(Mapper332::validate_geometry(header(), kLargePrgSize, kSmallChrSize));

    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper332::validate_geometry(h, kSmallPrgSize, kSmallChrSize));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper332::validate_geometry(header(mirror), kSmallPrgSize, kSmallChrSize));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.mapper_number = 3U;
            break;
        case 1U:
            h.mapper_number = 331U;
            break;
        case 2U:
            h.mapper_number = 333U;
            break;
        case 3U:
            h.is_nes2 = false;
            break;
        case 4U:
            h.has_battery = true;
            break;
        case 5U:
            h.prg_ram_size = 8192U;
            break;
        case 6U:
            h.prg_nvram_size = 8192U;
            break;
        case 7U:
            h.chr_ram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper332::validate_geometry(h, kSmallPrgSize, kSmallChrSize));
    }
    auto h = header();
    h.chr_nvram_size = 8192U;
    REQUIRE_FALSE(Mapper332::validate_geometry(h, kSmallPrgSize, kSmallChrSize));
}

TEST_CASE("Mapper 332 decodes every low register alias and ignores unrelated writes",
          "[mapper332]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw_addr = 0U; raw_addr < 0x8000U; ++raw_addr) {
        mapper->reset();
        auto expected = initial;
        const auto value = static_cast<Byte>(raw_addr * 29U + (raw_addr >> 8U));
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        if ((raw_addr & 0xE001U) == 0x6000U) {
            expected[kPrgRegisterOffset] = value;
        } else if ((raw_addr & 0xE001U) == 0x6001U) {
            expected[kChrRegisterOffset] = value;
        }
        CAPTURE(raw_addr, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(raw_addr)));
    }

    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        auto expected = initial;
        const auto value = static_cast<Byte>(raw_addr * 17U + (raw_addr >> 9U));
        expected[kCnromLatchOffset] = value & 3U;
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        CAPTURE(raw_addr, value);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 332 maps every PRG control value in both A14 modes", "[mapper332]") {
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (const bool large : {false, true}) {
        auto mapper = board(large);
        const auto prg_size = large ? kLargePrgSize : kSmallPrgSize;
        for (unsigned control = 0U; control < 256U; ++control) {
            mapper->write_prg(0x6000U, static_cast<Byte>(control));
            for (const auto addr : addresses) {
                CAPTURE(large, control, addr);
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(static_cast<Byte>(control), addr, prg_size));
            }
        }

        mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0x4000U));
        mapper->write_prg(0x6000U, 0x11U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0x4000U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0x4000U));
        mapper->write_prg(0x6000U, 0x10U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
    }
}

TEST_CASE("Mapper 332 maps every CHR control, latch and outer-bank value", "[mapper332]") {
    constexpr std::array<Address, 5> addresses{0U, 1U, 0x1234U, 0x1ABCU, 0x1FFFU};
    for (const bool large : {false, true}) {
        auto mapper = board(large);
        const auto chr_size = large ? kLargeChrSize : kSmallChrSize;
        for (const unsigned outer : {0U, 0x80U}) {
            mapper->write_prg(0x6000U, static_cast<Byte>(outer));
            for (unsigned chr_control = 0U; chr_control < 256U; ++chr_control) {
                mapper->write_prg(0x6001U, static_cast<Byte>(chr_control));
                for (unsigned latch = 0U; latch < 256U; ++latch) {
                    mapper->write_prg(0xFFFFU, static_cast<Byte>(latch));
                    for (const auto addr : addresses) {
                        CAPTURE(large, outer, chr_control, latch, addr);
                        REQUIRE(mapper->read_chr(addr) ==
                                expected_chr(static_cast<Byte>(outer),
                                             static_cast<Byte>(chr_control),
                                             static_cast<Byte>(latch & 3U),
                                             addr,
                                             chr_size));
                    }
                }
            }
        }

        for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
            const auto before = mapper->read_chr(static_cast<Address>(addr));
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(before ^ 0xFFU));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == before);
        }
    }
}

TEST_CASE("Mapper 332 MM=01 uses the explicit CNROM-256 fallback", "[mapper332]") {
    auto mapper = board(true);
    mapper->write_prg(0x6000U, 0x80U);
    mapper->write_prg(0x8000U, 2U);
    mapper->write_prg(0x6001U, 0x14U);
    REQUIRE(mapper->read_chr(0x0321U) == cell(0x10000U | 0x8000U | 0x4000U | 0x0321U));
}

TEST_CASE("Mapper 332 solder-pad bits disable PRG ROM but not the CNROM latch", "[mapper332]") {
    auto mapper = board(true);
    for (const Byte solder : {0x40U, 0x80U, 0xC0U}) {
        mapper->reset();
        mapper->write_prg(0x6000U, 0x97U);
        mapper->write_prg(0x6001U, solder);
        for (Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
            CAPTURE(solder, addr);
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        mapper->write_prg(0xFFFFU, 2U);
        REQUIRE(state(*mapper)[kCnromLatchOffset] == 2U);
        mapper->write_prg(0x6001U, 0U);
        REQUIRE(mapper->maps_prg(0x8000U));
        REQUIRE(mapper->read_prg(0x8000U) == expected_prg(0x97U, 0x8000U, kLargePrgSize));
        REQUIRE(mapper->read_chr(0x0123U) == expected_chr(0x97U, 0U, 2U, 0x0123U, kLargeChrSize));
    }

    for (Address addr : {0U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x6001U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
}

TEST_CASE("Mapper 332 controls CIRAM arrangement and has no auxiliary hardware", "[mapper332]") {
    auto mapper = board(false, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x6000U, static_cast<Byte>(value));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 332 reset and power-on clear all latches without ROM or RAM", "[mapper332]") {
    for (const bool large : {false, true}) {
        auto mapper = board(large, MirrorMode::Horizontal);
        const auto initial = state(*mapper);
        for (unsigned pass = 0U; pass < 2U; ++pass) {
            mapper->write_prg(0x7FFFU, 0xFFU);
            mapper->write_prg(0x7FFEU, 0xFFU);
            mapper->write_prg(0xFFFFU, 0xFFU);
            REQUIRE(state(*mapper) != initial);
            REQUIRE_FALSE(mapper->maps_prg(0x8000U));
            if (pass == 0U) {
                mapper->reset();
            } else {
                mapper->power_on();
            }
            CAPTURE(large, pass);
            REQUIRE(state(*mapper) == initial);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(mapper->maps_prg(0x8000U));
            REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
            REQUIRE(mapper->read_prg(0xC000U) == cell(0x4000U));
            REQUIRE(mapper->read_chr(0U) == cell(0U));
        }
    }
}

TEST_CASE("Mapper 332 snapshots replay every latch without ROM serialization",
          "[mapper332][state]") {
    auto mapper = board(true);
    mapper->write_prg(0x6000U, 0xD7U);
    mapper->write_prg(0x6001U, 0x2BU);
    mapper->write_prg(0xFFFFU, 2U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board(true, MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == expected_prg(0xD7U, addr, kLargePrgSize));
    }
    for (Address addr : {0U, 0x0123U, 0x0FFFU, 0x1000U, 0x1ABCU, 0x1FFFU}) {
        REQUIRE(copy->read_chr(addr) == expected_chr(0xD7U, 0x2BU, 2U, addr, kLargeChrSize));
    }
}

TEST_CASE("Mapper 332 rejects bounded malformed and cross-geometry states atomically",
          "[mapper332][state]") {
    auto mapper = board(true);
    mapper->write_prg(0x6000U, 0xD7U);
    mapper->write_prg(0x6001U, 0x2BU);
    mapper->write_prg(0xFFFFU, 2U);
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
        reject(invalid);
    }
    for (std::size_t offset = 2U; offset < kPrgRegisterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (unsigned latch = 4U; latch < 256U; ++latch) {
        auto invalid = saved;
        invalid[kCnromLatchOffset] = static_cast<Byte>(latch);
        reject(invalid);
    }

    auto small = board(false);
    const auto small_before = state(*small);
    StateReader mismatch(saved);
    small->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*small) == small_before);
}

TEST_CASE("Mapper 332 state validation accepts every raw register byte", "[mapper332][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0xD7U);
    mapper->write_prg(0x6001U, 0x2BU);
    mapper->write_prg(0xFFFFU, 2U);
    const auto saved = state(*mapper);
    const auto restore = [&] {
        StateReader reader(saved);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
    };

    for (const std::size_t offset : {kPrgRegisterOffset, kChrRegisterOffset}) {
        for (unsigned value = 0U; value < 256U; ++value) {
            restore();
            auto valid = saved;
            valid[offset] = static_cast<Byte>(value);
            StateReader reader(valid);
            mapper->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == valid);
        }
    }
    restore();
}

} // namespace
