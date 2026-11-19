#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper327.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kOuterSize = 128U * 1024U;
constexpr std::size_t kMaxRomSize = 8U * kOuterSize;
constexpr std::size_t kCartridgeRamSize = 8192U;
constexpr std::size_t kStateSize = 27U + 2U * kCartridgeRamSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 327U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kCartridgeRamSize;
    result.chr_ram_size = kCartridgeRamSize;
    return result;
}

Byte pattern(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 7U) * 17U + (offset >> 13U) * 31U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = pattern(offset, salt);
    }
    return bytes;
}

std::unique_ptr<Mapper327> board(std::size_t prg_size = kMaxRomSize,
                                 std::size_t chr_size = kMaxRomSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    REQUIRE(Mapper327::validate_geometry(header(mirror), prg_size, chr_size));
    return std::make_unique<Mapper327>(
        header(mirror), rom(prg_size, salt), rom(chr_size, salt + 1U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void commit(Mapper327& mapper, Byte reg, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(mode | reg));
    mapper.write_prg(0x8001U, value);
}

std::size_t expected_prg_offset(
    Byte outer, Byte mode, Byte r6, Byte r7, unsigned slot, Address addr, std::size_t prg_size) {
    const unsigned mask = (outer & 8U) != 0U ? 31U : 15U;
    unsigned inner = r7;
    if (slot == 0U) {
        inner = (mode & 0x40U) != 0U ? mask - 1U : r6;
    } else if (slot == 2U) {
        inner = (mode & 0x40U) != 0U ? r6 : mask - 1U;
    } else if (slot == 3U) {
        inner = mask;
    }
    const unsigned bank = (inner & mask) | ((outer & 7U) << 4U);
    return (static_cast<std::size_t>(bank) * 8192U + (addr & 0x1FFFU)) % prg_size;
}

std::size_t expected_chr_offset(Byte outer,
                                Byte mode,
                                const std::array<Byte, 8>& banks,
                                unsigned physical_slot,
                                Address addr,
                                std::size_t chr_size) {
    const unsigned slot = physical_slot ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner = slot < 4U ? (banks[slot / 2U] & 0xFEU) | (slot & 1U) : banks[slot - 2U];
    const unsigned mask = (outer & 0x20U) != 0U ? 255U : 127U;
    const unsigned bank = (inner & mask) | ((outer & 7U) << 7U);
    return (static_cast<std::size_t>(bank) * 1024U + (addr & 0x03FFU)) % chr_size;
}

TEST_CASE("Mapper 327 validates its NES 2.0 board population", "[mapper327]") {
    for (std::size_t prg_size = kOuterSize; prg_size <= kMaxRomSize; prg_size += kOuterSize) {
        for (std::size_t chr_size = kOuterSize; chr_size <= kMaxRomSize; chr_size += kOuterSize) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                REQUIRE(Mapper327::validate_geometry(header(mirror), prg_size, chr_size));
            }
        }
    }

    auto invalid = header();
    invalid.mapper_number = 326U;
    REQUIRE_FALSE(Mapper327::validate_geometry(invalid, kMaxRomSize, kMaxRomSize));
    invalid = header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper327::validate_geometry(invalid, kMaxRomSize, kMaxRomSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper327::validate_geometry(invalid, kMaxRomSize, kMaxRomSize));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   8192UZ,
                                   65536UZ,
                                   131071UZ,
                                   131073UZ,
                                   196608UZ,
                                   1048577UZ,
                                   1179648UZ,
                                   2097152UZ}) {
        REQUIRE_FALSE(Mapper327::validate_geometry(header(), size, kMaxRomSize));
        REQUIRE_FALSE(Mapper327::validate_geometry(header(), kMaxRomSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        invalid = header();
        invalid.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper327::validate_geometry(invalid, kMaxRomSize, kMaxRomSize));
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        invalid = header();
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.prg_ram_size = 0U;
            break;
        case 2U:
            invalid.prg_ram_size = 4096U;
            break;
        case 3U:
            invalid.prg_ram_size = 16384U;
            break;
        case 4U:
            invalid.prg_nvram_size = kCartridgeRamSize;
            break;
        case 5U:
            invalid.chr_ram_size = 0U;
            break;
        case 6U:
            invalid.chr_ram_size = 4096U;
            break;
        case 7U:
            invalid.chr_ram_size = 16384U;
            break;
        default:
            invalid.chr_nvram_size = kCartridgeRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper327::validate_geometry(invalid, kMaxRomSize, kMaxRomSize));
    }
}

TEST_CASE("Mapper 327 decodes every MMC3 register alias", "[mapper327]") {
    auto mapper = board();
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw_addr), 0xA5U);
        const auto saved = state(*mapper);
        const unsigned reg = raw_addr & 0xE001U;
        CAPTURE(raw_addr, reg);
        REQUIRE(saved[12U] == (reg == 0x8000U ? 5U : 0U));
        REQUIRE(saved[13U] == (reg == 0x8000U ? 0x80U : 0U));
        REQUIRE(saved[14U] == (reg == 0x8001U ? 0xA5U : 0U));
        REQUIRE(saved[22U] ==
                static_cast<Byte>(reg == 0xA000U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(saved[24U] == (reg == 0xC000U ? 0xA5U : 0U));
        REQUIRE(saved[25U] == (reg == 0xC001U ? 1U : reg == 0xE001U ? 2U : 0U));
        REQUIRE(saved[26U] == 0x80U);
    }
}

TEST_CASE("Mapper 327 composes outer and inner PRG banks in both MMC3 modes", "[mapper327]") {
    auto mapper = board();
    constexpr std::array<Byte, 8> values{0U, 1U, 14U, 15U, 16U, 31U, 32U, 63U};
    for (Byte outer = 0U; outer < 8U; ++outer) {
        for (Byte wide : {0U, 8U}) {
            mapper->power_on();
            mapper->write_prg(0xA001U, 0x80U);
            const auto alias = static_cast<Address>(0x6000U + (outer * 733U + wide * 31U));
            mapper->write_prg(alias, static_cast<Byte>(outer | wide));
            for (Byte mode : {0U, 0x40U}) {
                for (const Byte r6 : values) {
                    const Byte r7 = static_cast<Byte>(63U - r6);
                    commit(*mapper, 6U, r6, mode);
                    commit(*mapper, 7U, r7, mode);
                    for (unsigned slot = 0U; slot < 4U; ++slot) {
                        const auto addr = static_cast<Address>(0x8000U + slot * 8192U + 0x123U);
                        const auto offset = expected_prg_offset(
                            static_cast<Byte>(outer | wide), mode, r6, r7, slot, addr, kMaxRomSize);
                        CAPTURE(outer, wide, mode, r6, slot);
                        REQUIRE(mapper->read_prg(addr) == pattern(offset));
                    }
                }
            }
        }
    }

    auto small = board(kOuterSize, kOuterSize);
    small->write_prg(0x6000U, 0x0FU);
    commit(*small, 6U, 63U);
    const auto offset = expected_prg_offset(0x0FU, 0U, 63U, 0U, 0U, 0x8123U, kOuterSize);
    REQUIRE(small->read_prg(0x8123U) == pattern(offset));
}

TEST_CASE("Mapper 327 applies CHR masks after MMC3 pairing and inversion", "[mapper327]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 8U; ++outer) {
        for (Byte wide : {0U, 0x20U}) {
            mapper->power_on();
            const Byte control = static_cast<Byte>(outer | wide);
            mapper->write_prg(0x7FFFU, control);
            std::array<Byte, 8> banks{};
            for (Byte mode : {0U, 0x80U}) {
                for (Byte reg = 0U; reg < 8U; ++reg) {
                    for (unsigned value = 0U; value < 256U; ++value) {
                        commit(*mapper, reg, static_cast<Byte>(value), mode);
                        banks[reg] = static_cast<Byte>(value);
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            const auto addr = static_cast<Address>(slot * 1024U + 0x155U);
                            const auto offset =
                                expected_chr_offset(control, mode, banks, slot, addr, kMaxRomSize);
                            CAPTURE(outer, wide, mode, reg, value, slot);
                            REQUIRE(mapper->read_chr(addr) == pattern(offset, 1U));
                        }
                    }
                }
            }
            for (Address addr : {0x0000U, 0x0155U, 0x0800U, 0x1000U, 0x1FFFU}) {
                const auto original = mapper->read_chr(addr);
                mapper->write_chr(addr, static_cast<Byte>(original ^ 0xFFU));
                REQUIRE(mapper->read_chr(addr) == original);
            }
        }
    }
}

TEST_CASE("Mapper 327 outer writes share gated WRAM and lock until cold power", "[mapper327]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < kCartridgeRamSize; ++offset) {
        const auto addr = static_cast<Address>(0x6000U + offset);
        const Byte value = static_cast<Byte>(offset & 0x38U);
        mapper->write_prg(addr, value);
        REQUIRE(mapper->read_prg(addr) == value);
        REQUIRE(state(*mapper)[10U] == value);
    }

    mapper->write_prg(0x6000U, 0x38U);
    mapper->write_prg(0xA001U, 0U);
    const auto disabled = state(*mapper);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    mapper->write_prg(0x6000U, 0x03U);
    REQUIRE(state(*mapper) == disabled);

    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6000U) == 0x38U);
    mapper->write_prg(0x6000U, 0x03U);
    REQUIRE(state(*mapper)[10U] == 0x38U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x38U);

    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x03U);
    REQUIRE(state(*mapper)[10U] == 0x03U);
    REQUIRE(state(*mapper)[11U] == 1U);
    mapper->write_prg(0x6123U, 0xA5U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    REQUIRE(state(*mapper)[10U] == 0x03U);
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(state(*mapper)[10U] == 0x03U);

    commit(*mapper, 6U, 19U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 7U);
    mapper->write_prg(0xE001U, 0U);
    mapper->reset();
    REQUIRE(state(*mapper)[10U] == 0x03U);
    REQUIRE(state(*mapper)[11U] == 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    REQUIRE(state(*mapper)[12U] == 0U);
    REQUIRE(state(*mapper)[13U] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(state(*mapper)[10U] == 0x03U);

    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board()));
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(state(*mapper)[11U] == 0U);
}

TEST_CASE("Mapper 327 CHR RAM is unbanked, writable and volatile", "[mapper327]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x10U);
    for (Address addr = 0U; addr < kCartridgeRamSize; ++addr) {
        mapper->write_chr(addr, static_cast<Byte>(addr * 7U + (addr >> 8U)));
    }
    for (Byte reg = 0U; reg < 8U; ++reg) {
        commit(*mapper, reg, static_cast<Byte>(reg * 31U + 1U), 0x80U);
    }
    for (Address addr = 0U; addr < kCartridgeRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(addr * 7U + (addr >> 8U)));
    }

    mapper->write_prg(0x6000U, 0U);
    REQUIRE(
        mapper->read_chr(0x155U) ==
        pattern(expected_chr_offset(
                    0U, 0x80U, {1U, 32U, 63U, 94U, 125U, 156U, 0U, 0U}, 0U, 0x155U, kMaxRomSize),
                1U));
    mapper->write_prg(0x6000U, 0x10U);
    mapper->reset();
    REQUIRE(mapper->read_chr(0x155U) == static_cast<Byte>(0x155U * 7U + 1U));
    mapper->power_on();
    mapper->write_prg(0x6000U, 0x10U);
    for (Address addr : {0x0000U, 0x0155U, 0x0800U, 0x1000U, 0x1FFFU}) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 327 MMC3 IRQs reload, repeat at zero and acknowledge", "[mapper327][irq]") {
    auto mapper = board();
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
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned cycle = 0U; cycle < 8U; ++cycle) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
    }
    mapper->write_prg(0xE000U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 327 leaves unrelated buses and expansion audio absent", "[mapper327]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::vector<Byte>(kCartridgeRamSize, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->clock_audio();
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 327 snapshots replay RAM and registers without ROM bytes", "[mapper327][state]") {
    auto mapper = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, 0U);
    mapper->write_prg(0x6000U, 0x3BU);
    for (Byte reg = 0U; reg < 8U; ++reg) {
        commit(*mapper, reg, static_cast<Byte>(reg * 23U + 5U), 0xC0U);
    }
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 9U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    for (unsigned offset = 0U; offset < kCartridgeRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), static_cast<Byte>(offset * 3U));
        mapper->write_chr(static_cast<Address>(offset), static_cast<Byte>(offset * 5U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kMaxRomSize);

    auto copy = board(kMaxRomSize, kMaxRomSize, MirrorMode::Vertical, 0U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (const Address addr : {0x6123U, 0x8000U, 0x9234U, 0xA123U, 0xC000U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    for (const Address addr : {0x0000U, 0x0155U, 0x0800U, 0x1000U, 0x1FFFU}) {
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
    }
    copy->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 327 rejects malformed states atomically", "[mapper327][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x3BU);
    for (Byte reg = 0U; reg < 8U; ++reg) {
        commit(*mapper, reg, static_cast<Byte>(reg * 19U + 3U), 0xC0U);
    }
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_chr(0x0234U, 0x5CU);
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    std::vector<std::pair<std::size_t, Byte>> invalid{{0U, 0U},
                                                      {1U, 0U},
                                                      {2U, 1U},
                                                      {6U, 1U},
                                                      {10U, 0x40U},
                                                      {11U, 2U},
                                                      {11U, 0U},
                                                      {12U, 8U},
                                                      {13U, 1U},
                                                      {20U, 0x40U},
                                                      {21U, 0x40U},
                                                      {22U, 2U},
                                                      {25U, 8U},
                                                      {25U, 4U},
                                                      {26U, 1U}};
    for (const auto [offset, value] : invalid) {
        auto damaged = saved;
        damaged[offset] = value;
        CAPTURE(offset, value);
        StateReader reader(damaged);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto different_geometry = board(kOuterSize, kMaxRomSize);
    const auto before_geometry = state(*different_geometry);
    StateReader geometry_reader(saved);
    different_geometry->load_state(geometry_reader);
    REQUIRE_FALSE(geometry_reader.ok());
    REQUIRE(state(*different_geometry) == before_geometry);

    auto different_mirror = board(kMaxRomSize, kMaxRomSize, MirrorMode::Horizontal);
    const auto before_mirror = state(*different_mirror);
    StateReader mirror_reader(saved);
    different_mirror->load_state(mirror_reader);
    REQUIRE_FALSE(mirror_reader.ok());
    REQUIRE(state(*different_mirror) == before_mirror);
}

} // namespace
