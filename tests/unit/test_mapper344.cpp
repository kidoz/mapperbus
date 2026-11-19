#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper344.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 8192UZ;
constexpr std::size_t kChrBankSize = 1024UZ;
constexpr std::size_t kInnerPrgSize = 131072UZ;
constexpr std::size_t kPrgSize = 524288UZ;
constexpr std::size_t kChrSize = 524288UZ;
constexpr std::size_t kRamSize = 8192UZ;
constexpr std::size_t kConfigOffset = 10UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kBankSelectOffset = 13UZ;
constexpr std::size_t kFirstBankOffset = 14UZ;
constexpr std::size_t kMirrorOffset = 22UZ;
constexpr std::size_t kIrqFlagsOffset = 25UZ;
constexpr std::size_t kRamFlagsOffset = 26UZ;
constexpr std::size_t kRamLengthOffset = 27UZ;
constexpr std::size_t kStateHeaderSize = 35UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 344U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 19U + (offset >> 8U) * 29U + (offset >> 13U) * 43U +
                             (offset >> 17U) * 61U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper344> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 bool disable_pad = false) {
    const auto h = header(mirror);
    REQUIRE(Mapper344::validate_geometry(h, prg_size, chr_size));
    auto mapper = std::make_unique<Mapper344>(h, rom(prg_size, salt), rom(chr_size, salt));
    REQUIRE(mapper->configure_board_jumpers(disable_pad ? 1U : 0U));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper344& mapper, unsigned index, Byte value, bool prg_mode = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | (prg_mode ? 0x40U : 0U)));
    mapper.write_prg(0x8001U, value);
}

void write_outer(Mapper344& mapper, Byte value, Address addr = 0x6000U) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(addr, value);
}

Byte expected_chr_bank(Address addr, bool inversion, const std::array<Byte, 8>& registers) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ (inversion ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((registers[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers[slot - 2U];
}

std::size_t expected_prg_offset(Byte outer,
                                Byte select,
                                const std::array<Byte, 8>& registers,
                                Address addr) {
    std::size_t inner = 0U;
    if ((outer & 4U) != 0U) {
        inner = static_cast<std::size_t>((registers[6U] >> 1U) & 7U) * 2UZ * kPrgBankSize +
                (addr & 0x3FFFU);
    } else {
        const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
        const bool mode = (select & 0x40U) != 0U;
        unsigned bank = 0U;
        switch (slot) {
        case 0U:
            bank = mode ? 14U : registers[6U] & 0x0FU;
            break;
        case 1U:
            bank = registers[7U] & 0x0FU;
            break;
        case 2U:
            bank = mode ? registers[6U] & 0x0FU : 14U;
            break;
        default:
            bank = 15U;
            break;
        }
        inner = bank * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }
    return static_cast<std::size_t>(outer & 3U) * kInnerPrgSize + inner;
}

Byte expected_chr(Byte outer,
                  bool inversion,
                  const std::array<Byte, 8>& registers,
                  Address addr,
                  std::size_t chr_size,
                  Byte salt = 0U) {
    const Byte bank = expected_chr_bank(addr, inversion, registers);
    const std::size_t a18 = static_cast<std::size_t>((outer >> 1U) & 1U) << 18U;
    const std::size_t a17 = (outer & 4U) != 0U ? static_cast<std::size_t>(outer & 1U) << 17U
                                               : static_cast<std::size_t>(bank & 0x80U) << 10U;
    const std::size_t offset = a18 | a17 | static_cast<std::size_t>(bank & 0x7FU) * kChrBankSize |
                               (addr & (kChrBankSize - 1U));
    return cell(offset % chr_size, salt);
}

TEST_CASE("Mapper 344 validates mapper submapper ROM and exact memory population", "[mapper344]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg : {32768UZ, 65536UZ, 131072UZ, 262144UZ, kPrgSize}) {
            for (const std::size_t chr :
                 {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ, kChrSize}) {
                REQUIRE(Mapper344::validate_geometry(header(mirror), prg, chr));
            }
        }
    }

    for (const unsigned id : {0U, 4U, 339U, 343U, 345U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper344::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper344::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper344::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 8192UZ, 16384UZ, 32767UZ, 49152UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper344::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 4096UZ, 8191UZ, 12288UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper344::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper344::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }

    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0U;
            break;
        case 2U:
            h.prg_ram_size = 4096U;
            break;
        case 3U:
            h.prg_ram_size = 16384U;
            break;
        case 4U:
            h.prg_nvram_size = kRamSize;
            break;
        case 5U:
            h.chr_ram_size = kRamSize;
            break;
        default:
            h.chr_nvram_size = kRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper344::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 344 decodes outer and MMC3 aliases without bus conflicts", "[mapper344]") {
    auto mapper = board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        const auto value = static_cast<Byte>(addr * 37U + (addr >> 8U));
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(static_cast<Address>(addr), value);
        CAPTURE(addr, value);
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x0FU));
    }

    const auto actual = board();
    const auto expected = board();
    const auto prepare = [](Mapper344& target) {
        write_outer(target, 2U);
        select_bank(target, 7U, 0x2AU, true);
        target.write_prg(0xA000U, 1U);
        target.write_prg(0xC000U, 2U);
        target.write_prg(0xC001U, 0U);
        target.write_prg(0xE001U, 0U);
    };
    for (const unsigned raw : {0x8000U,
                               0x8001U,
                               0x9FFEU,
                               0x9FFFU,
                               0xA000U,
                               0xA001U,
                               0xBFFEU,
                               0xBFFFU,
                               0xC000U,
                               0xC001U,
                               0xDFFEU,
                               0xDFFFU,
                               0xE000U,
                               0xE001U,
                               0xFFFEU,
                               0xFFFFU}) {
        prepare(*actual);
        prepare(*expected);
        const auto value = static_cast<Byte>(raw * 29U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        expected->write_prg(static_cast<Address>(raw & 0xE001U), value);
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
    }
}

TEST_CASE("Mapper 344 MMC3 PRG banking restricts inner pins before outer banking", "[mapper344]") {
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0xA000U, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (const Byte outer : {0U, 1U, 2U, 3U}) {
        auto mapper = board();
        write_outer(*mapper, outer);
        for (const bool mode : {false, true}) {
            for (const Byte r6 : {0U, 1U, 14U, 15U, 16U, 63U}) {
                for (const Byte r7 : {0U, 5U, 15U, 42U, 63U}) {
                    select_bank(*mapper, 6U, r6, mode);
                    select_bank(*mapper, 7U, r7, mode);
                    std::array<Byte, 8> registers{};
                    registers[6U] = r6;
                    registers[7U] = r7;
                    const auto select = static_cast<Byte>(mode ? 0x40U : 0U);
                    for (const auto addr : addresses) {
                        CAPTURE(outer, mode, r6, r7, addr);
                        const auto offset = expected_prg_offset(outer, select, registers, addr);
                        REQUIRE(mapper->read_prg(addr) == cell(offset % kPrgSize));
                    }
                }
            }
        }
    }

    for (const std::size_t prg_size : {32768UZ, 65536UZ, kInnerPrgSize}) {
        auto mapper = board(prg_size, kChrSize);
        write_outer(*mapper, 3U);
        select_bank(*mapper, 6U, 63U, true);
        select_bank(*mapper, 7U, 42U, true);
        std::array<Byte, 8> registers{};
        registers[6U] = 63U;
        registers[7U] = 42U;
        for (const auto addr : addresses) {
            const auto offset = expected_prg_offset(3U, 0x40U, registers, addr);
            REQUIRE(mapper->read_prg(addr) == cell(offset % prg_size));
        }
    }
}

TEST_CASE("Mapper 344 NROM mode uses R6 bits 1-3 for the full CPU range", "[mapper344]") {
    auto mapper = board();
    for (const Byte outer : {4U, 5U, 6U, 7U}) {
        write_outer(*mapper, outer);
        for (unsigned r6 = 0U; r6 < 64U; ++r6) {
            select_bank(*mapper, 6U, static_cast<Byte>(r6), true);
            select_bank(*mapper, 7U, 63U, true);
            std::array<Byte, 8> registers{};
            registers[6U] = static_cast<Byte>(r6);
            registers[7U] = 63U;
            for (const Address addr : {0x8000U, 0x9123U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
                CAPTURE(outer, r6, addr);
                const auto offset = expected_prg_offset(outer, 0x40U, registers, addr);
                REQUIRE(mapper->read_prg(addr) == cell(offset % kPrgSize));
            }
        }
    }
}

TEST_CASE("Mapper 344 CHR banking applies A18 and selectable A17 after MMC3", "[mapper344]") {
    for (const Byte outer : {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U}) {
        auto mapper = board();
        write_outer(*mapper, outer);
        std::array<Byte, 8> registers{};
        for (unsigned index = 0U; index < 6U; ++index) {
            const auto value = static_cast<Byte>(index * 37U + 0x81U);
            select_bank(*mapper, index, value);
            registers[index] = value;
        }
        for (const bool inversion : {false, true}) {
            mapper->write_prg(0x8000U, inversion ? 0x80U : 0U);
            for (unsigned addr = 0U; addr < 8192U; ++addr) {
                const auto address = static_cast<Address>(addr);
                CAPTURE(outer, inversion, address);
                REQUIRE(mapper->read_chr(address) ==
                        expected_chr(outer, inversion, registers, address, kChrSize));
            }
        }
    }

    auto mapper = board(32768UZ, 8192UZ);
    write_outer(*mapper, 7U);
    select_bank(*mapper, 2U, 0xFFU);
    std::array<Byte, 8> registers{};
    registers[2U] = 0xFFU;
    REQUIRE(mapper->read_chr(0x1000U) == expected_chr(7U, false, registers, 0x1000U, 8192UZ));
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto original = mapper->read_chr(static_cast<Address>(addr));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(original ^ 0xFFU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == original);
    }
}

TEST_CASE("Mapper 344 menu pad disables PRG only when explicitly connected", "[mapper344]") {
    auto open_pad = board();
    write_outer(*open_pad, 0x0BU);
    REQUIRE(open_pad->maps_prg(0x8000U));
    REQUIRE(open_pad->prg_read_mask(0xFFFFU) == 0xFFU);
    REQUIRE(open_pad->read_prg(0x8000U) == cell(3UZ * kInnerPrgSize));

    auto connected = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0U, true);
    write_outer(*connected, 0x0BU);
    for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xFFFFU}) {
        REQUIRE_FALSE(connected->maps_prg(addr));
        REQUIRE(connected->prg_read_mask(addr) == 0U);
        REQUIRE(connected->read_prg(addr) == 0U);
    }
    write_outer(*connected, 0x03U);
    REQUIRE(connected->maps_prg(0x8000U));
    REQUIRE(connected->read_prg(0x8000U) == cell(3UZ * kInnerPrgSize));
    const auto before = state(*connected);
    REQUIRE_FALSE(connected->configure_board_jumpers(2U));
    REQUIRE(state(*connected) == before);
}

TEST_CASE("Mapper 344 WRAM gates RAM and outer latch as documented", "[mapper344]") {
    auto mapper = board();
    mapper->write_prg(0x6123U, 0xABU);
    REQUIRE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0xFFU);
    REQUIRE(mapper->read_prg(0x6123U) == 0xABU);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0BU);

    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6123U, 0x53U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x03U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xABU);

    mapper->write_prg(0xA001U, 0x40U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0x07U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x03U);
    mapper->write_prg(0xA001U, 0x80U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xABU);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->maps_expansion(0x6000U));
}

TEST_CASE("Mapper 344 CIRAM and scanline IRQs retain MMC3 behavior", "[mapper344][irq]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned addr = 0xA000U; addr <= 0xBFFFU; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->write_prg(static_cast<Address>(addr), 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));

    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 344 reset restores controls and cold power clears volatile WRAM",
          "[mapper344][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal, 0U, true);
    mapper->write_prg(0x6123U, 0xABU);
    select_bank(*mapper, 6U, 0x2AU, true);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    REQUIRE(state(*mapper)[kBankSelectOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0xABU);
    REQUIRE(mapper->maps_prg(0x8000U));

    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->maps_prg(0x8000U));
}

TEST_CASE("Mapper 344 snapshots replay registers and WRAM without ROM serialization",
          "[mapper344][state]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0U, true);
    write_outer(*mapper, 2U);
    select_bank(*mapper, 6U, 0x2AU, true);
    select_bank(*mapper, 7U, 0x15U, true);
    for (unsigned index = 0U; index < 6U; ++index) {
        select_bank(*mapper, index, static_cast<Byte>(index * 31U + 3U), true);
    }
    mapper->write_prg(0xA000U, 1U);
    for (unsigned addr = 0U; addr < kRamSize; ++addr) {
        mapper->write_prg(static_cast<Address>(0x6000U + addr), static_cast<Byte>(addr * 13U + 7U));
    }
    write_outer(*mapper, 2U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateHeaderSize + kRamSize);
    REQUIRE(saved.size() < 32768UZ);
    REQUIRE(saved[kOuterOffset] == 2U);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U, true);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->irq_pending());
    for (std::size_t addr = 0U; addr < kRamSize; ++addr) {
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + addr)) ==
                (addr == 0U ? Byte{2U} : static_cast<Byte>(addr * 13U + 7U)));
    }
    REQUIRE(copy->read_chr(0x1000U) == static_cast<Byte>(mapper->read_chr(0x1000U) + 0x80U));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 344 rejects bounded malformed and incompatible states atomically",
          "[mapper344][state]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0U, true);
    write_outer(*mapper, 2U);
    select_bank(*mapper, 6U, 0x2AU, true);
    mapper->write_prg(0x6123U, 0xA7U);
    write_outer(*mapper, 2U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (const std::size_t length : {0UZ,
                                     1UZ,
                                     11UZ,
                                     12UZ,
                                     13UZ,
                                     21UZ,
                                     22UZ,
                                     25UZ,
                                     26UZ,
                                     27UZ,
                                     34UZ,
                                     35UZ,
                                     saved.size() - 1UZ}) {
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (const std::size_t offset : {2UZ, 6UZ, kConfigOffset}) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (const std::size_t offset : {kOuterOffset,
                                     kBankSelectOffset,
                                     kFirstBankOffset + 6U,
                                     kFirstBankOffset + 7U,
                                     kMirrorOffset,
                                     kIrqFlagsOffset,
                                     kRamFlagsOffset}) {
        auto invalid = saved;
        invalid[offset] = 0xFFU;
        CAPTURE(offset);
        reject(invalid);
    }
    auto pending_without_enable = saved;
    pending_without_enable[kIrqFlagsOffset] = 4U;
    reject(pending_without_enable);
    for (const std::uint64_t length : {0UZ, 1UZ, kRamSize - 1UZ, kRamSize + 1UZ, ~0UZ}) {
        auto invalid = saved;
        std::copy_n(reinterpret_cast<const Byte*>(&length),
                    sizeof(length),
                    invalid.begin() + static_cast<std::ptrdiff_t>(kRamLengthOffset));
        reject(invalid);
    }

    const auto incompatible = [&](std::unique_ptr<Mapper344> other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    incompatible(board(kPrgSize / 2U));
    incompatible(board(kPrgSize, kChrSize / 2U));
    incompatible(board(kPrgSize, kChrSize, MirrorMode::Horizontal, 0U, true));
    incompatible(board());
}

} // namespace
