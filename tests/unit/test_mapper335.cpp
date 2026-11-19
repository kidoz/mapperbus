#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper335.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMaxPrgSize = 256UZ * 1024UZ;
constexpr std::size_t kMaxChrSize = 128UZ * 1024UZ;
constexpr std::size_t kPrgRegisterOffset = 10UZ;
constexpr std::size_t kChrRegisterOffset = 11UZ;
constexpr std::size_t kStateSize = 12UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 335U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 7U + (offset >> 8U) * 13U + (offset >> 14U) * 31U +
                             (offset >> 17U) * 43U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper335> board(std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper335::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper335>(h, rom(prg_size), rom(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte control, Address addr, std::size_t prg_size = kMaxPrgSize) {
    const std::size_t bank = (control >> 1U) & 7U;
    const std::size_t a14 = (control & 0x10U) != 0U ? static_cast<std::size_t>(control & 1U) << 14U
                                                    : static_cast<std::size_t>(addr & 0x4000U);
    return cell((bank * 32768UZ | a14 | (addr & 0x3FFFUZ)) % prg_size);
}

Byte expected_chr(Byte control, Address addr, std::size_t chr_size = kMaxChrSize) {
    const std::size_t offset =
        static_cast<std::size_t>(control & 0x0FU) * 8192UZ | (addr & 0x1FFFUZ);
    return cell(offset % chr_size);
}

TEST_CASE("Mapper 335 validates native ROM-only geometry and address widths", "[mapper335]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ, 262144UZ}) {
            for (const std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ}) {
                REQUIRE(Mapper335::validate_geometry(header(mirror), prg_size, chr_size));
            }
        }
    }

    for (const std::size_t prg_size : {0UZ, 1UZ, 16384UZ, 49152UZ, 32767UZ, 262145UZ, 524288UZ}) {
        REQUIRE_FALSE(Mapper335::validate_geometry(header(), prg_size, kMaxChrSize));
    }
    for (const std::size_t chr_size : {0UZ, 1UZ, 4096UZ, 12288UZ, 8191UZ, 131073UZ, 262144UZ}) {
        REQUIRE_FALSE(Mapper335::validate_geometry(header(), kMaxPrgSize, chr_size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper335::validate_geometry(h, kMaxPrgSize, kMaxChrSize));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper335::validate_geometry(header(mirror), kMaxPrgSize, kMaxChrSize));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.mapper_number = 334U;
            break;
        case 1U:
            h.mapper_number = 336U;
            break;
        case 2U:
            h.mapper_number = 3U;
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
        default:
            h.chr_ram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper335::validate_geometry(h, kMaxPrgSize, kMaxChrSize));
    }
    auto h = header();
    h.chr_nvram_size = 8192U;
    REQUIRE_FALSE(Mapper335::validate_geometry(h, kMaxPrgSize, kMaxChrSize));
}

TEST_CASE("Mapper 335 decodes every register address and latches full data bytes", "[mapper335]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        auto expected = initial;
        const auto value = static_cast<Byte>(raw_addr * 23U + (raw_addr >> 8U));
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        expected[raw_addr >= 0xC000U ? kPrgRegisterOffset : kChrRegisterOffset] = value;
        CAPTURE(raw_addr, value);
        REQUIRE(state(*mapper) == expected);
    }

    for (Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        mapper->reset();
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(state(*mapper) == initial);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
}

TEST_CASE("Mapper 335 maps every PRG value in NROM-256 and NROM-128 modes", "[mapper335]") {
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (const std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ, kMaxPrgSize}) {
        auto mapper = board(prg_size);
        for (unsigned control = 0U; control < 256U; ++control) {
            mapper->reset();
            mapper->write_prg(0xC000U, static_cast<Byte>(control));
            for (const auto addr : addresses) {
                CAPTURE(prg_size, control, addr);
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(static_cast<Byte>(control), addr, prg_size));
            }
        }
    }

    auto mapper = board();
    mapper->write_prg(0xC000U, 0x10U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
    mapper->reset();
    mapper->write_prg(0xC000U, 0x11U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0x4000U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0x4000U));
}

TEST_CASE("Mapper 335 selects every CHR bank and ignores high data and ROM writes", "[mapper335]") {
    constexpr std::array<Address, 6> addresses{0U, 1U, 0x0123U, 0x0FFFU, 0x1000U, 0x1FFFU};
    for (const std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ, kMaxChrSize}) {
        auto mapper = board(kMaxPrgSize, chr_size);
        for (unsigned control = 0U; control < 256U; ++control) {
            mapper->write_prg(0x8000U, static_cast<Byte>(control));
            for (const auto addr : addresses) {
                CAPTURE(chr_size, control, addr);
                REQUIRE(mapper->read_chr(addr) ==
                        expected_chr(static_cast<Byte>(control), addr, chr_size));
            }
        }
    }

    auto mapper = board();
    for (unsigned addr = 0U; addr < 0x2000U; ++addr) {
        const auto before = mapper->read_chr(static_cast<Address>(addr));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(before ^ 0xFFU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == before);
    }
}

TEST_CASE("Mapper 335 lock freezes both registers until reset", "[mapper335]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xA5U);
    mapper->write_prg(0xC000U, 0xF7U);
    const auto locked = state(*mapper);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8123U) == expected_prg(0xF7U, 0x8123U));
    REQUIRE(mapper->read_chr(0x0123U) == expected_chr(0xA5U, 0x0123U));

    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        const auto value = static_cast<Byte>(raw_addr * 37U + (raw_addr >> 7U));
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        CAPTURE(raw_addr, value);
        REQUIRE(state(*mapper) == locked);
    }
    mapper->reset();
    REQUIRE(state(*mapper) == state(*board()));
    mapper->write_prg(0x8000U, 3U);
    REQUIRE(state(*mapper)[kChrRegisterOffset] == 3U);
}

TEST_CASE("Mapper 335 controls CIRAM and exposes no auxiliary hardware", "[mapper335]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0xC000U, static_cast<Byte>(value));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x20U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }
    mapper->reset();
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

TEST_CASE("Mapper 335 reset and power-on restore deterministic zeroed latches", "[mapper335]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical);
    const auto initial = state(*mapper);
    for (unsigned pass = 0U; pass < 2U; ++pass) {
        mapper->write_prg(0x8000U, 0xFFU);
        mapper->write_prg(0xFFFFU, 0xFFU);
        REQUIRE(state(*mapper) != initial);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        if (pass == 0U) {
            mapper->reset();
        } else {
            mapper->power_on();
        }
        CAPTURE(pass);
        REQUIRE(state(*mapper) == initial);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0x4000U));
        REQUIRE(mapper->read_chr(0U) == cell(0U));
    }
}

TEST_CASE("Mapper 335 snapshots replay locked registers without ROM serialization",
          "[mapper335][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xA5U);
    mapper->write_prg(0xC000U, 0xF7U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kMaxChrSize);

    auto copy = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == expected_prg(0xF7U, addr));
    }
    for (const Address addr : {0U, 0x0123U, 0x0FFFU, 0x1000U, 0x1ABCU, 0x1FFFU}) {
        REQUIRE(copy->read_chr(addr) == expected_chr(0xA5U, addr));
    }
    copy->write_prg(0x8000U, 0U);
    copy->write_prg(0xC000U, 0U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 335 rejects bounded malformed and cross-geometry states atomically",
          "[mapper335][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xA5U);
    mapper->write_prg(0xC000U, 0xF7U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
        REQUIRE(mapper->read_prg(0x8123U) == expected_prg(0xF7U, 0x8123U));
        REQUIRE(mapper->read_chr(0x0123U) == expected_chr(0xA5U, 0x0123U));
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

    auto small = board(32768UZ, 8192UZ);
    const auto small_before = state(*small);
    StateReader mismatch(saved);
    small->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*small) == small_before);
}

TEST_CASE("Mapper 335 state validation accepts every raw register byte", "[mapper335][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xA5U);
    mapper->write_prg(0xC000U, 0xF7U);
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
            REQUIRE(reader.remaining() == 0U);
            REQUIRE(state(*mapper) == valid);
        }
    }
    restore();
}

} // namespace
