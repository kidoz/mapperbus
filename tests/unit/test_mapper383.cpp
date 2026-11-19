#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper383.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x80000UZ;
constexpr std::size_t kChrSize = 0x80000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x0400UZ;
constexpr std::size_t kBankSelectOffset = 10UZ;
constexpr std::size_t kBankRegistersOffset = 11UZ;
constexpr std::size_t kMirrorOffset = 19UZ;
constexpr std::size_t kIrqCounterOffset = 20UZ;
constexpr std::size_t kIrqReloadOffset = 21UZ;
constexpr std::size_t kIrqFlagsOffset = 22UZ;
constexpr std::size_t kPalA15Offset = 23UZ;
constexpr std::size_t kPalA16Offset = 24UZ;
constexpr std::size_t kPalOuterOffset = 25UZ;
constexpr std::size_t kStateSize = 26UZ;
constexpr std::array<Address, 7> kPrgAddresses{
    0x6000U, 0x7000U, 0x8000U, 0xA000U, 0xC000U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 383U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + (offset >> 18U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper383> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper383::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper383>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper383& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void set_pal(Mapper383& mapper, Byte outer, bool a15, bool a16) {
    auto bytes = state(mapper);
    bytes[kPalOuterOffset] = outer;
    bytes[kPalA15Offset] = static_cast<Byte>(a15);
    bytes[kPalA16Offset] = static_cast<Byte>(a16);
    load(mapper, bytes);
}

void set_bank_registers(Mapper383& mapper,
                        Byte select,
                        const std::array<Byte, 8>& registers,
                        Byte outer = 0U,
                        bool a15 = false,
                        bool a16 = false) {
    auto bytes = state(mapper);
    bytes[kBankSelectOffset] = select;
    for (std::size_t index = 0U; index < registers.size(); ++index) {
        bytes[kBankRegistersOffset + index] = registers[index];
    }
    bytes[kPalOuterOffset] = outer;
    bytes[kPalA15Offset] = static_cast<Byte>(a15);
    bytes[kPalA16Offset] = static_cast<Byte>(a16);
    load(mapper, bytes);
}

Address reference_clone_address(Address addr, Byte outer) {
    if (outer != 3U) {
        return addr;
    }
    switch (addr & 0xE000U) {
    case 0x6000U:
    case 0xA000U:
        return 0xE000U;
    case 0x8000U:
        return 0xC000U;
    case 0xC000U:
        return 0x8000U;
    case 0xE000U:
        return 0xA000U;
    default:
        return addr;
    }
}

Byte reference_raw_prg(Byte select, const std::array<Byte, 8>& registers, Address clone_addr) {
    const bool mode = (select & 0x40U) != 0U;
    switch ((clone_addr >> 13U) & 3U) {
    case 0U:
        return mode ? 14U : registers[6U];
    case 1U:
        return registers[7U];
    case 2U:
        return mode ? registers[6U] : 14U;
    default:
        return 15U;
    }
}

std::size_t reference_prg_offset(Byte select,
                                 const std::array<Byte, 8>& registers,
                                 Byte outer,
                                 bool a15,
                                 bool a16,
                                 Address addr) {
    const auto raw = reference_raw_prg(select, registers, reference_clone_address(addr, outer));
    unsigned mask = 0x0FU;
    unsigned pins = static_cast<unsigned>(outer) << 4U;
    if (outer == 0U) {
        mask = a16 ? 0x07U : 0x03U;
        pins = (a15 ? 0x08U : 0U) | (a16 ? 0x04U : 0U);
    } else if (outer == 3U && addr < 0x8000U) {
        mask = 0x0BU;
    }
    return static_cast<std::size_t>((raw & mask) | pins) * kPrgBankSize + (addr & 0x1FFFU);
}

Byte reference_raw_chr(Byte select, const std::array<Byte, 8>& registers, Address addr) {
    const auto slot =
        static_cast<unsigned>(((addr >> 10U) & 7U) ^ ((select & 0x80U) != 0U ? 4U : 0U));
    if (slot < 4U) {
        return static_cast<Byte>((registers[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return registers[slot - 2U];
}

TEST_CASE("Mapper 383 validates the MMC3 PAL geometry and ROM-only metadata", "[mapper383]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper383::validate_geometry(header(mirror), 0x8000UZ, 0x2000UZ));
        REQUIRE(Mapper383::validate_geometry(header(mirror), 0xA000UZ, 0x2400UZ));
        REQUIRE(Mapper383::validate_geometry(header(mirror), kPrgSize, kChrSize));
        auto mapper = board(kPrgSize, kChrSize, mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const unsigned id : {0U, 4U, 382U, 384U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper383::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper383::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper383::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x7FFFUZ, 0x8001UZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper383::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper383::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper383::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
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
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper383::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 383 overlays the PAL and MMC3 decoders over every CPU write alias",
          "[mapper383][bus]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(0x8000U | (raw & 0xE001U)), value);
        }
        if ((raw & 0x8100U) == 0x8100U) {
            set_pal(*expected, static_cast<Byte>((raw >> 4U) & 3U), (raw & 0x2000U) != 0U, false);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }
}

TEST_CASE("Mapper 383 applies the complete PRG AND OR and mode-three slot table",
          "[mapper383][banking]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 4U; ++outer) {
        for (const bool a15 : {false, true}) {
            for (const bool a16 : {false, true}) {
                for (const Byte mode : {0U, 0x40U}) {
                    for (Byte value = 0U; value < 64U; ++value) {
                        std::array<Byte, 8> registers{};
                        registers[6U] = value;
                        registers[7U] = static_cast<Byte>((value * 13U + 7U) & 0x3FU);
                        const Byte select = static_cast<Byte>(mode | 6U);
                        set_bank_registers(*mapper, select, registers, outer, a15, a16);
                        bool expected_a16 = a16;
                        for (const auto addr : kPrgAddresses) {
                            const bool mapped = addr >= 0x8000U || outer == 3U;
                            CAPTURE(outer, a15, a16, mode, value, addr);
                            REQUIRE(mapper->maps_prg(addr) == mapped);
                            if (mapped) {
                                const auto physical = reference_prg_offset(
                                    select, registers, outer, a15, expected_a16, addr);
                                REQUIRE(mapper->read_prg(addr) == cell(physical));
                            } else {
                                REQUIRE(mapper->read_prg(addr) == 0U);
                            }
                            if (outer == 0U && addr >= 0x8000U && addr < 0xC000U) {
                                expected_a16 =
                                    (reference_raw_prg(select, registers, addr) & 0x04U) != 0U;
                            }
                            REQUIRE(state(*mapper)[kPalA16Offset] ==
                                    static_cast<Byte>(expected_a16));
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 383 read latch samples old raw A16 only at 8000-BFFF in outer zero",
          "[mapper383][bus][banking]") {
    auto mapper = board();
    for (Byte value = 0U; value < 64U; ++value) {
        for (const auto addr : {0x8000U, 0xA000U, 0xBFFFU, 0xC000U, 0xE000U}) {
            std::array<Byte, 8> registers{};
            registers[6U] = value;
            registers[7U] = static_cast<Byte>(value ^ 0x3FU);
            set_bank_registers(*mapper, 6U, registers, 0U, true, (value & 1U) != 0U);
            const bool old_a16 = (value & 1U) != 0U;
            const auto physical =
                reference_prg_offset(6U, registers, 0U, true, old_a16, static_cast<Address>(addr));
            CAPTURE(value, addr);
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == cell(physical));
            const bool expected =
                addr < 0xC000U
                    ? (reference_raw_prg(6U, registers, static_cast<Address>(addr)) & 0x04U) != 0U
                    : old_a16;
            REQUIRE(state(*mapper)[kPalA16Offset] == static_cast<Byte>(expected));
        }
    }

    for (Byte outer = 1U; outer < 4U; ++outer) {
        set_pal(*mapper, outer, false, true);
        mapper->read_prg(0x8000U);
        mapper->read_prg(0xBFFFU);
        REQUIRE(state(*mapper)[kPalA16Offset] == 1U);
    }
    set_pal(*mapper, 0U, false, false);
    mapper->observe_cpu_read(0x8000U, 0x04U);
    REQUIRE(state(*mapper)[kPalA16Offset] == 0U);
}

TEST_CASE("Mapper 383 wraps decoded PRG and CHR pins to fitted non-power-of-two ROMs",
          "[mapper383][banking]") {
    constexpr std::size_t prg_size = 0xA000UZ;
    constexpr std::size_t chr_size = 0x2400UZ;
    auto mapper = board(prg_size, chr_size);
    std::array<Byte, 8> registers{0xFEU, 0xFDU, 0xFCU, 0xFBU, 0xFAU, 0xF9U, 0x3FU, 0x3EU};
    set_bank_registers(*mapper, 0xC6U, registers, 3U, true, true);
    for (const auto addr : kPrgAddresses) {
        const auto physical = reference_prg_offset(0xC6U, registers, 3U, true, true, addr);
        CAPTURE(addr, physical);
        REQUIRE(mapper->read_prg(addr) == cell(physical % prg_size));
    }
    for (Address addr = 0U; addr < 0x2000U; addr += 0x0400U) {
        const auto bank =
            static_cast<std::size_t>(reference_raw_chr(0xC6U, registers, addr) | 0x180U);
        const auto physical = bank * kChrBankSize + (addr & 0x03FFU);
        CAPTURE(addr, physical);
        REQUIRE(mapper->read_chr(addr) == cell(physical % chr_size));
    }
}

TEST_CASE("Mapper 383 applies PAL outer pins after ordinary MMC3 CHR pairing and inversion",
          "[mapper383][banking]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 4U; ++outer) {
        for (const Byte inversion : {0U, 0x80U}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                std::array<Byte, 8> registers{};
                for (unsigned index = 0U; index < 6U; ++index) {
                    registers[index] = static_cast<Byte>(value + index * 37U);
                }
                const Byte select = static_cast<Byte>(inversion | 6U);
                set_bank_registers(*mapper, select, registers, outer);
                for (Address addr = 0U; addr < 0x2000U; addr += 0x0400U) {
                    const auto bank =
                        static_cast<std::size_t>(reference_raw_chr(select, registers, addr) |
                                                 (static_cast<unsigned>(outer) << 7U));
                    const auto physical = bank * kChrBankSize + 0x0123U;
                    CAPTURE(outer, inversion, value, addr);
                    REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x0123U)) ==
                            cell(physical));
                }
            }
        }
    }
    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto expected = mapper->read_chr(addr);
        mapper->write_chr(addr, static_cast<Byte>(addr ^ 0xFFU));
        REQUIRE(mapper->read_chr(addr) == expected);
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 383 provides MMC3 CIRAM control and only the mode-three low ROM window",
          "[mapper383][bus][ciram][ram]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xA000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x0100U) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    for (Byte outer = 0U; outer < 4U; ++outer) {
        set_pal(*mapper, outer, false, false);
        for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x0100U) {
            CAPTURE(outer, addr);
            REQUIRE(mapper->maps_prg(addr) == (outer == 3U));
            REQUIRE(mapper->prg_read_mask(addr) == (outer == 3U ? 0xFFU : 0U));
            if (outer != 3U) {
                REQUIRE(mapper->read_prg(addr) == 0U);
            }
        }
    }
    for (Address addr = 0U; addr < 0x6000U; addr += 0x0100U) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
}

TEST_CASE("Mapper 383 retains the MMC3 IRQ reload acknowledgement and zero-latch policy",
          "[mapper383][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto partial = state(*mapper);
    REQUIRE(partial[kIrqCounterOffset] == 2U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    load(*mapper, partial);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 3U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 383 reset restores deterministic PAL MMC3 IRQ and header CIRAM state",
          "[mapper383][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal, 0x35U);
    const auto initial = state(*mapper);
    std::array<Byte, 8> registers{1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U};
    set_bank_registers(*mapper, 0xC7U, registers, 3U, true, true);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper) != initial);

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
    REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U, 0x35U));

    set_pal(*mapper, 3U, true, true);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 383 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper383][state]") {
    auto mapper = board();
    std::array<Byte, 8> registers{0xFEU, 0xFDU, 0xFCU, 0xFBU, 0xFAU, 0xF9U, 0x23U, 0x31U};
    set_bank_registers(*mapper, 0xC7U, registers, 3U, true, true);
    auto programmed = state(*mapper);
    programmed[kMirrorOffset] = static_cast<Byte>(MirrorMode::Horizontal);
    programmed[kIrqCounterOffset] = 3U;
    programmed[kIrqReloadOffset] = 4U;
    programmed[kIrqFlagsOffset] = 7U;
    load(*mapper, programmed);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x8000UZ);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->irq_pending());
    for (const auto addr : kPrgAddresses) {
        const auto physical = reference_prg_offset(0xC7U, registers, 3U, true, true, addr);
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);

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
    for (unsigned bit = 0U; bit < kBankSelectOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    const std::array<std::pair<std::size_t, Byte>, 13> invalid_fields{
        {{kBankSelectOffset, 0x08U},
         {kBankRegistersOffset + 6U, 0x40U},
         {kBankRegistersOffset + 7U, 0xFFU},
         {kMirrorOffset, 2U},
         {kIrqFlagsOffset, 8U},
         {kIrqFlagsOffset, 4U},
         {kPalA15Offset, 2U},
         {kPalA16Offset, 2U},
         {kPalOuterOffset, 4U},
         {kPalOuterOffset, 0xFFU},
         {kBankSelectOffset, 0x20U},
         {kMirrorOffset, 0xFFU},
         {kIrqFlagsOffset, 0xFFU}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_geometry = board(kPrgSize / 2U, kChrSize / 2U);
    const auto before = state(*wrong_geometry);
    StateReader wrong_reader(saved);
    wrong_geometry->load_state(wrong_reader);
    REQUIRE_FALSE(wrong_reader.ok());
    REQUIRE(state(*wrong_geometry) == before);

    auto wrong_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto mirror_before = state(*wrong_mirror);
    StateReader mirror_reader(saved);
    wrong_mirror->load_state(mirror_reader);
    REQUIRE_FALSE(mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == mirror_before);
}

TEST_CASE("Mapper 383 leaves expansion audio and unrelated hardware observers inert",
          "[mapper383][bus][irq][audio]") {
    auto mapper = board();
    const auto before = state(*mapper);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

} // namespace
