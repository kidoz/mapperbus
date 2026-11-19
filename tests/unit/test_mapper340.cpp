#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper340.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kInnerBankSize = 0x4000UZ;
constexpr std::size_t kOuterBankSize = 0x20000UZ;
constexpr std::size_t kPrgSize = 4UZ * kOuterBankSize;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kAddressLatchOffset = 6UZ;
constexpr std::size_t kInnerBankOffset = 7UZ;
constexpr std::size_t kChrRamOffset = 8UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 340U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 11U + (offset >> 8U) * 17U + (offset >> 14U) * 29U +
                             (offset >> 17U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper340> board(std::size_t prg_size = kPrgSize,
                                 Byte salt = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper340::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper340>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(
    Byte control, Byte inner, Address addr, std::size_t prg_size = kPrgSize, Byte salt = 0U) {
    const std::size_t outer = (control >> 3U) & 3U;
    const bool nrom128 = (control & 0x20U) != 0U;
    const std::size_t selected = addr < 0xC000U || nrom128 ? inner & 7U : 7U;
    const std::size_t offset =
        outer * kOuterBankSize + selected * kInnerBankSize + (addr & (kInnerBankSize - 1U));
    return cell(offset % prg_size, salt);
}

TEST_CASE("Mapper 340 validates ID submapper ROM and exact memory population", "[mapper340]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg_size : {kOuterBankSize, 2UZ * kOuterBankSize, kPrgSize}) {
            REQUIRE(Mapper340::validate_geometry(header(mirror), prg_size, 0U));
        }
    }

    for (const unsigned id : {0U, 2U, 210U, 339U, 341U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper340::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper340::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper340::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x4000UZ,
                                   0x8000UZ,
                                   0x10000UZ,
                                   0x18000UZ,
                                   0x1FFFFUZ,
                                   0x20001UZ,
                                   0x30000UZ,
                                   0x80001UZ,
                                   0x100000UZ}) {
        REQUIRE_FALSE(Mapper340::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t chr_size : {1UZ, 0x2000UZ, 0x4000UZ}) {
        REQUIRE_FALSE(Mapper340::validate_geometry(header(), kPrgSize, chr_size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper340::validate_geometry(header(mirror), kPrgSize, 0U));
    }

    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper340::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 340 decodes every upper CPU alias and data bit without conflicts",
          "[mapper340]") {
    auto mapper = board();
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        const auto value = static_cast<Byte>(raw_addr * 37U + (raw_addr >> 8U));
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        const auto saved = state(*mapper);
        CAPTURE(raw_addr, value);
        REQUIRE(saved[kAddressLatchOffset] == (raw_addr & 0x3FU));
        REQUIRE(saved[kInnerBankOffset] == (value & 7U));
        REQUIRE(mapper->mirror_mode() ==
                ((raw_addr & 0x3FU) == 0x25U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kInnerBankOffset] == (value & 7U));
    }
    const auto before_low_writes = state(*mapper);
    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before_low_writes);
    for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
}

TEST_CASE("Mapper 340 selects outer and inner PRG banks in UNROM and NROM-128 modes",
          "[mapper340]") {
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (const std::size_t prg_size : {kOuterBankSize, 2UZ * kOuterBankSize, kPrgSize}) {
        auto mapper = board(prg_size);
        for (unsigned outer = 0U; outer < 4U; ++outer) {
            for (unsigned mode = 0U; mode < 2U; ++mode) {
                for (unsigned inner = 0U; inner < 8U; ++inner) {
                    const auto control = static_cast<Byte>((mode << 5U) | (outer << 3U));
                    mapper->write_prg(static_cast<Address>(0x8000U | control),
                                      static_cast<Byte>(0xF8U | inner));
                    for (const auto addr : addresses) {
                        CAPTURE(prg_size, outer, mode, inner, addr);
                        REQUIRE(mapper->read_prg(addr) ==
                                expected_prg(control, static_cast<Byte>(inner), addr, prg_size));
                    }
                }
            }
        }
    }

    auto mapper = board();
    for (const Byte control : {0U, 0x20U}) {
        mapper->write_prg(0x8000U | control, 3U);
        for (unsigned offset = 0U; offset < 0x8000U; ++offset) {
            const auto addr = static_cast<Address>(0x8000U + offset);
            REQUIRE(mapper->read_prg(addr) == expected_prg(control, 3U, addr));
        }
    }
}

TEST_CASE("Mapper 340 mirroring depends only on decoded address value 25", "[mapper340]") {
    auto mapper = board(kPrgSize, 0U, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (unsigned low_byte = 0U; low_byte < 256U; ++low_byte) {
        mapper->write_prg(static_cast<Address>(0xFF00U | low_byte), 0U);
        CAPTURE(low_byte);
        REQUIRE(mapper->mirror_mode() ==
                ((low_byte & 0x3FU) == 0x25U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
}

TEST_CASE("Mapper 340 provides fixed writable CHR RAM independent of PRG banking", "[mapper340]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 19U + 7U));
    }
    for (const Byte control : {0U, 0x08U, 0x18U, 0x20U, 0x38U}) {
        mapper->write_prg(0x8000U | control, 6U);
        for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
            CAPTURE(control, addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                    static_cast<Byte>(addr * 19U + 7U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                    static_cast<Byte>(addr * 19U + 7U));
        }
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
}

TEST_CASE("Mapper 340 reset clears latches but only cold power clears CHR RAM",
          "[mapper340][reset]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    mapper->write_prg(0x8025U, 5U);
    mapper->write_chr(0x0123U, 0xA7U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(state(*mapper) != initial);

    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(7UZ * kInnerBankSize));

    mapper->write_prg(0x8038U, 6U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 340 exposes no undocumented RAM persistence IRQ or audio", "[mapper340]") {
    auto mapper = board();
    const auto before = state(*mapper);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    mapper->write_expansion(0x5000U, 0xFFU);
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 340 snapshots replay latches and CHR RAM without ROM serialization",
          "[mapper340][state]") {
    auto mapper = board();
    mapper->write_prg(0x803DU, 2U);
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 23U + 5U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kOuterBankSize);
    REQUIRE(saved[kAddressLatchOffset] == 0x3DU);
    REQUIRE(saved[kInnerBankOffset] == 2U);
    REQUIRE(saved[kChrRamOffset] == 5U);

    auto copy = board(kPrgSize, 0x80U, MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == expected_prg(0x3DU, 2U, addr, kPrgSize, 0x80U));
    }
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 23U + 5U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 340 rejects bounded malformed and incompatible states atomically",
          "[mapper340][state]") {
    auto mapper = board();
    mapper->write_prg(0x8025U, 5U);
    mapper->write_chr(0x0123U, 0xA7U);
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
    for (std::size_t offset = 2U; offset < kAddressLatchOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (const Byte control : {0x40U, 0x41U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kAddressLatchOffset] = control;
        reject(invalid);
    }
    for (const Byte inner : {8U, 9U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kInnerBankOffset] = inner;
        reject(invalid);
    }

    auto other = board(kOuterBankSize);
    const auto before = state(*other);
    StateReader incompatible(saved);
    other->load_state(incompatible);
    REQUIRE_FALSE(incompatible.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
