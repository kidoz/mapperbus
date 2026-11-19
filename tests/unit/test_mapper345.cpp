#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper345.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kAxromBankSize = 0x8000UZ;
constexpr std::size_t kOuterBankSize = 0x20000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kPrgSize = 4UZ * kOuterBankSize;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kConfigOffset = 6UZ;
constexpr std::size_t kOuterOffset = 8UZ;
constexpr std::size_t kBankSelectOffset = 9UZ;
constexpr std::size_t kFirstBankOffset = 10UZ;
constexpr std::size_t kMirrorOffset = 18UZ;
constexpr std::size_t kIrqFlagsOffset = 21UZ;
constexpr std::size_t kRamFlagsOffset = 22UZ;
constexpr std::size_t kChrRamOffset = 23UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 345U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 23U + (offset >> 8U) * 31U + (offset >> 14U) * 47U +
                             (offset >> 17U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper345> board(std::size_t prg_size = kPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper345::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper345>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper345& mapper, unsigned index, Byte value, bool prg_mode = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | (prg_mode ? 0x40U : 0U)));
    mapper.write_prg(0x8001U, value);
}

void write_outer(Mapper345& mapper, Byte value, Address addr = 0x6000U) {
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

Byte expected_chr(Address addr, bool inversion, const std::array<Byte, 8>& registers) {
    const auto bank = expected_chr_bank(addr, inversion, registers);
    return cell((static_cast<std::size_t>(bank & 7U) * kChrBankSize) | (addr & 0x3FFU));
}

TEST_CASE("Mapper 345 validates ID submapper ROM and exact memory population", "[mapper345]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t size : {kOuterBankSize, 2UZ * kOuterBankSize, kPrgSize}) {
            REQUIRE(Mapper345::validate_geometry(header(mirror), size, 0U));
        }
    }

    for (const unsigned id : {0U, 4U, 7U, 344U, 346U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper345::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper345::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper345::validate_geometry(h, kPrgSize, 0U));
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
        REQUIRE_FALSE(Mapper345::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, 0x4000UZ}) {
        REQUIRE_FALSE(Mapper345::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper345::validate_geometry(header(mirror), kPrgSize, 0U));
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
        REQUIRE_FALSE(Mapper345::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 345 decodes outer aliases and MMC3 aliases without conflicts", "[mapper345]") {
    auto mapper = board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        const auto value = static_cast<Byte>(addr * 37U + (addr >> 8U));
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(static_cast<Address>(addr), value);
        CAPTURE(addr, value);
        REQUIRE(state(*mapper)[kOuterOffset] == value);
    }

    const auto actual = board();
    const auto expected = board();
    const auto prepare = [](Mapper345& target) {
        write_outer(target, 0x5CU);
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

TEST_CASE("Mapper 345 AxROM mode selects independent inner and outer 32 KiB banks", "[mapper345]") {
    constexpr std::array<Address, 6> addresses{
        0x8000U, 0x9123U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU};
    for (const std::size_t prg_size : {kOuterBankSize, 2UZ * kOuterBankSize, kPrgSize}) {
        auto mapper = board(prg_size);
        for (unsigned outer = 0U; outer < 4U; ++outer) {
            for (unsigned inner = 0U; inner < 4U; ++inner) {
                const auto control = static_cast<Byte>((outer << 6U) | 0x30U | inner);
                write_outer(*mapper, control);
                select_bank(*mapper, 6U, 0x3FU, true);
                select_bank(*mapper, 7U, 0x2AU, true);
                for (const auto addr : addresses) {
                    const std::size_t offset =
                        outer * kOuterBankSize + inner * kAxromBankSize + (addr & 0x7FFFU);
                    CAPTURE(prg_size, outer, inner, addr);
                    REQUIRE(mapper->read_prg(addr) == cell(offset % prg_size));
                }
            }
        }
    }
}

TEST_CASE("Mapper 345 MMC3 modes share PRG banking within each 128 KiB outer bank", "[mapper345]") {
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0xA000U, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (unsigned outer = 0U; outer < 4U; ++outer) {
        for (unsigned mode = 1U; mode < 4U; ++mode) {
            auto mapper = board();
            write_outer(*mapper, static_cast<Byte>((outer << 6U) | (mode << 2U) | 3U));
            for (const bool prg_mode : {false, true}) {
                for (const Byte r6 : {0U, 1U, 14U, 15U, 16U, 63U}) {
                    for (const Byte r7 : {0U, 5U, 15U, 42U, 63U}) {
                        select_bank(*mapper, 6U, r6, prg_mode);
                        select_bank(*mapper, 7U, r7, prg_mode);
                        for (const auto addr : addresses) {
                            const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
                            unsigned bank = 15U;
                            if (slot == 0U) {
                                bank = prg_mode ? 14U : r6 & 0x0FU;
                            } else if (slot == 1U) {
                                bank = r7 & 0x0FU;
                            } else if (slot == 2U) {
                                bank = prg_mode ? r6 & 0x0FU : 14U;
                            }
                            const std::size_t offset = outer * kOuterBankSize +
                                                       bank * kPrgBankSize +
                                                       (addr & (kPrgBankSize - 1U));
                            CAPTURE(outer, mode, prg_mode, r6, r7, addr);
                            REQUIRE(mapper->read_prg(addr) == cell(offset));
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 345 WRAM control gates only the write-only outer latch", "[mapper345]") {
    auto mapper = board();
    write_outer(*mapper, 0x40U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6123U, 0xB5U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x40U);
    mapper->write_prg(0xA001U, 0x40U);
    mapper->write_prg(0x7123U, 0x75U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x40U);
    mapper->write_prg(0xA001U, 0x00U);
    mapper->write_prg(0x7FFFU, 0x35U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x40U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6000U, 0x35U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x35U);

    for (const Address addr : {0x6000U, 0x6123U, 0x7FFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    for (const Address addr : {0x8000U, 0xA000U, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->maps_expansion(0x6000U));
}

TEST_CASE("Mapper 345 CHR RAM uses all MMC3 banks and inversion with three address pins",
          "[mapper345]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        select_bank(*mapper, 2U, static_cast<Byte>(bank));
        for (unsigned offset = 0U; offset < kChrBankSize; ++offset) {
            mapper->write_chr(static_cast<Address>(0x1000U + offset),
                              cell(bank * kChrBankSize + offset));
        }
    }

    std::array<Byte, 8> registers{};
    for (unsigned index = 0U; index < 6U; ++index) {
        const auto value = static_cast<Byte>(index * 39U + 0x83U);
        select_bank(*mapper, index, value);
        registers[index] = value;
    }
    for (const bool inversion : {false, true}) {
        mapper->write_prg(0x8000U, inversion ? 0x80U : 0U);
        for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
            const auto address = static_cast<Address>(addr);
            CAPTURE(inversion, address);
            REQUIRE(mapper->read_chr(address) == expected_chr(address, inversion, registers));
        }
    }

    select_bank(*mapper, 3U, 0xFFU);
    mapper->write_chr(0x1A55U, 0x69U);
    select_bank(*mapper, 3U, 7U);
    REQUIRE(mapper->read_chr(0x1A55U) == 0x69U);
}

TEST_CASE("Mapper 345 one-screen control selects CIRAM and retains MMC3 mirroring", "[mapper345]") {
    auto mapper = board(kPrgSize, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    mapper->write_prg(0xA000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);

    for (const Byte control : {0x20U, 0x30U, 0xACU, 0xBCU}) {
        write_outer(*mapper, control);
        const bool upper = (control & 0x10U) != 0U;
        CAPTURE(control);
        REQUIRE(mapper->mirror_mode() ==
                (upper ? MirrorMode::SingleUpper : MirrorMode::SingleLower));
        for (const Address addr : {0x2000U, 0x2123U, 0x23FFU, 0x2400U, 0x2800U, 0x2C00U, 0x3EFFU}) {
            const auto mapped = mapper->nametable_address(addr);
            REQUIRE(mapped);
            REQUIRE(*mapped == ((upper ? 0x400U : 0U) | (addr & 0x3FFU)));
        }
    }
    write_outer(*mapper, 0x04U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
}

TEST_CASE("Mapper 345 MMC3 IRQ behavior works in both PRG modes and has no audio",
          "[mapper345][irq]") {
    for (const Byte mode : {0U, 0x04U, 0x08U, 0x0CU}) {
        auto mapper = board();
        write_outer(*mapper, mode);
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
}

TEST_CASE("Mapper 345 reset restores controls and cold power clears only CHR RAM",
          "[mapper345][reset]") {
    auto mapper = board(kPrgSize, MirrorMode::Horizontal);
    select_bank(*mapper, 2U, 3U);
    mapper->write_chr(0x1000U, 0xA6U);
    write_outer(*mapper, 0xF0U);
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
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    select_bank(*mapper, 2U, 3U);
    REQUIRE(mapper->read_chr(0x1000U) == 0xA6U);

    mapper->power_on();
    select_bank(*mapper, 2U, 3U);
    REQUIRE(mapper->read_chr(0x1000U) == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(state(*mapper)[kRamFlagsOffset] == 0x80U);
}

TEST_CASE("Mapper 345 snapshots replay registers and CHR RAM without ROM serialization",
          "[mapper345][state]") {
    auto mapper = board();
    write_outer(*mapper, 0xACU);
    select_bank(*mapper, 6U, 0x2AU, true);
    select_bank(*mapper, 7U, 0x15U, true);
    for (unsigned index = 0U; index < 6U; ++index) {
        select_bank(*mapper, index, static_cast<Byte>(index * 31U + 3U), true);
    }
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        select_bank(*mapper, 2U, static_cast<Byte>(bank));
        for (unsigned offset = 0U; offset < kChrBankSize; ++offset) {
            mapper->write_chr(static_cast<Address>(0x1000U + offset),
                              static_cast<Byte>(bank * 29U + offset * 13U + 7U));
        }
    }
    write_outer(*mapper, 0xACU);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 16384UZ);
    REQUIRE(saved[kOuterOffset] == 0xACU);

    auto copy = board(kPrgSize, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(copy->irq_pending());
    REQUIRE(copy->read_prg(0x8000U) == static_cast<Byte>(mapper->read_prg(0x8000U) + 0x80U));
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        select_bank(*copy, 2U, static_cast<Byte>(bank));
        for (unsigned offset = 0U; offset < kChrBankSize; ++offset) {
            REQUIRE(copy->read_chr(static_cast<Address>(0x1000U + offset)) ==
                    static_cast<Byte>(bank * 29U + offset * 13U + 7U));
        }
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 345 rejects bounded malformed and incompatible states atomically",
          "[mapper345][state]") {
    auto mapper = board();
    write_outer(*mapper, 0xD8U);
    select_bank(*mapper, 6U, 0x2AU, true);
    select_bank(*mapper, 2U, 3U);
    mapper->write_chr(0x1000U, 0xA7U);
    write_outer(*mapper, 0xD8U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (const std::size_t length :
         {0UZ, 1UZ, 5UZ, 6UZ, 7UZ, 8UZ, 9UZ, 17UZ, 18UZ, 21UZ, 22UZ, saved.size() - 1UZ}) {
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (const std::size_t offset : {2UZ, kConfigOffset}) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (const std::size_t offset : {kBankSelectOffset,
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

    const auto incompatible = [&](std::unique_ptr<Mapper345> other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    incompatible(board(kPrgSize / 2U));
    incompatible(board(kPrgSize, MirrorMode::Horizontal));
}

} // namespace
