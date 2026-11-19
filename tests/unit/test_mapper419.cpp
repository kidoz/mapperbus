#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper419.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x800000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x400000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kOuterOffset = 14UZ;
constexpr std::size_t kControlOffset = 15UZ;
constexpr std::size_t kMirrorOffset = 16UZ;
constexpr std::size_t kPrgOffset = 17UZ;
constexpr std::size_t kModeOffset = 21UZ;
constexpr std::size_t kChrOffset = 22UZ;
constexpr std::size_t kIntermediateOffset = 28UZ;
constexpr std::size_t kChrMiddleOffset = 29UZ;
constexpr std::size_t kLatchOffset = 30UZ;
constexpr std::size_t kIrqOffset = 32UZ;
constexpr std::size_t kPinsOffset = 33UZ;
constexpr std::size_t kOpcodeOffset = 34UZ;
constexpr std::size_t kAudioBufferOffset = 35UZ;
constexpr std::size_t kAudioReadOffset = 131UZ;
constexpr std::size_t kAudioSizeOffset = 132UZ;
constexpr std::size_t kAudioFrameSampleOffset = 141UZ;
constexpr std::size_t kPredictorOffset = 142UZ;
constexpr std::size_t kAudioIndexOffset = 150UZ;
constexpr std::size_t kPeriodOffset = 151UZ;
constexpr std::size_t kPhaseOffset = 153UZ;
constexpr std::size_t kCpuClockOffset = 161UZ;
constexpr std::size_t kCommandOffset = 165UZ;
constexpr std::size_t kCommandPositionOffset = 166UZ;
constexpr std::size_t kAudioFlagsOffset = 168UZ;
constexpr std::size_t kNibbleOffset = 169UZ;
constexpr std::size_t kStateSize = 171UZ;
constexpr std::array<Byte, 11> kSubmappers{0U, 1U, 2U, 3U, 4U, 5U, 11U, 12U, 13U, 14U, 15U};
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
constexpr std::array<Byte, 4> kIndexStep{0U, 0U, 3U, 5U};
constexpr std::array<Byte, 26> kIndexTable{0U,  0U,  1U,  2U,  3U,  4U,  5U,  6U,  7U,
                                           8U,  9U,  10U, 11U, 12U, 13U, 14U, 15U, 16U,
                                           17U, 18U, 19U, 20U, 20U, 20U, 20U, 20U};
constexpr std::array<std::array<int, 21>, 4> kStepTable{{
    {0, 1, 1, 1, 1, 1, 2, 2, 2, 3, 3, 4, 5, 5, 6, 7, 8, 10, 11, 13, 15},
    {1, 3, 3, 3, 4, 4, 6, 6, 7, 9, 10, 12, 15, 16, 19, 22, 25, 30, 34, 40, 46},
    {3, 5, 5, 6, 7, 8, 10, 11, 13, 16, 18, 21, 25, 28, 32, 38, 43, 51, 58, 68, 78},
    {4, 7, 7, 8, 10, 11, 14, 15, 18, 22, 25, 29, 35, 39, 45, 53, 60, 71, 81, 95, 109},
}};

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 419U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + (offset >> 22U) * 97U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper419> board(Byte submapper = 0U,
                                 std::size_t prg_size = 0x20000UZ,
                                 std::size_t chr_size = 0x20000UZ,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper419::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper419>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x35U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper419& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::uint16_t read_u16(std::span<const Byte> bytes, std::size_t offset) {
    REQUIRE(offset + sizeof(std::uint16_t) <= bytes.size());
    std::uint16_t value = 0U;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::uint32_t read_u32(std::span<const Byte> bytes, std::size_t offset) {
    REQUIRE(offset + sizeof(std::uint32_t) <= bytes.size());
    std::uint32_t value = 0U;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

std::uint64_t read_u64(std::span<const Byte> bytes, std::size_t offset) {
    REQUIRE(offset + sizeof(std::uint64_t) <= bytes.size());
    std::uint64_t value = 0U;
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

void write_u32(std::span<Byte> bytes, std::size_t offset, std::uint32_t value) {
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

void write_u64(std::span<Byte> bytes, std::size_t offset, std::uint64_t value) {
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

unsigned canonical_chr(Address ppu_base) {
    const auto slot = static_cast<unsigned>((ppu_base & 0x1FFFU) / kChrBankSize);
    return slot < 4U ? 4U + slot / 2U : slot - 4U;
}

Address native_base(Byte submapper, unsigned index) {
    constexpr std::array<std::array<Address, 6>, 6> bases{{
        {0x1000U, 0x1400U, 0x1800U, 0x1C00U, 0x0000U, 0x0800U},
        {0x1400U, 0x1000U, 0x0800U, 0x0000U, 0x1C00U, 0x1800U},
        {0x1000U, 0x1400U, 0x1800U, 0x1C00U, 0x0000U, 0x0800U},
        {0x0800U, 0x0000U, 0x1C00U, 0x1800U, 0x1000U, 0x1400U},
        {0x1800U, 0x0800U, 0x1000U, 0x0000U, 0x1C00U, 0x1400U},
        {0x1400U, 0x1000U, 0x0800U, 0x0000U, 0x0000U, 0x0800U},
    }};
    return submapper <= 5U ? bases[submapper][index] : bases[0][index];
}

Address forwarded_base(Byte submapper, unsigned index) {
    constexpr std::array<Address, 6> normal{0x0000U, 0x0800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
    constexpr std::array<Address, 6> reversed{0x1C00U, 0x1800U, 0x1400U, 0x1000U, 0x0800U, 0x0000U};
    return submapper == 1U ? reversed[index] : normal[index];
}

std::size_t expected_prg(Address addr,
                         Byte control,
                         const std::array<Byte, 3>& prg,
                         Byte middle,
                         Byte mode,
                         Byte outer) {
    auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    if ((control & 0x40U) != 0U && (slot & 1U) == 0U) {
        slot ^= 2U;
    }
    const unsigned inner = slot < 2U                            ? prg[slot]
                           : slot == 2U && (mode & 0x40U) != 0U ? prg[2]
                           : slot == 2U                         ? 254U
                                                                : 255U;
    constexpr std::array<unsigned, 8> masks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
    const unsigned mask = masks[mode & 7U];
    const unsigned bank = (inner & mask) | (middle & (255U ^ mask)) | ((outer >> 4U) << 8U);
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t expected_chr(Address addr,
                         Byte control,
                         const std::array<Byte, 6>& chr,
                         Byte intermediate,
                         Byte middle,
                         Byte outer) {
    auto slot = static_cast<unsigned>((addr & (kChrBankSize * 8U - 1U)) / kChrBankSize);
    if ((control & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const unsigned inner = slot < 4U ? (chr[4U + slot / 2U] & 254U) | (slot & 1U) : chr[slot - 4U];
    constexpr std::array<unsigned, 8> masks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
    const unsigned mask = masks[middle & 7U];
    const unsigned bank = (inner & mask) | (middle & (255U ^ mask) & 248U) |
                          ((intermediate >> 4U) << 8U) | ((outer & 15U) << 11U);
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
}

void edge(Mapper419& mapper, unsigned low_cycles = 3U) {
    mapper.observe_ppu_a12(false);
    for (unsigned cycle = 0U; cycle < low_cycles; ++cycle) {
        mapper.clock_cpu();
    }
    mapper.observe_ppu_a12(true);
}

void enable_audio(Mapper419& mapper) {
    mapper.write_expansion(0x410DU, 0x30U);
}

void send_byte(Mapper419& mapper, Byte value) {
    mapper.write_expansion(0x410FU, static_cast<Byte>(value >> 4U));
    mapper.observe_cpu_write(0x4016U, 4U);
    REQUIRE((mapper.read_expansion(0x4017U) & 8U) == 0U);
    mapper.write_expansion(0x410FU, static_cast<Byte>(value & 0x0FU));
    mapper.observe_cpu_write(0x4016U, 0U);
    REQUIRE((mapper.read_expansion(0x4017U) & 8U) != 0U);
}

void send_bytes(Mapper419& mapper, std::span<const Byte> bytes) {
    for (const auto value : bytes) {
        send_byte(mapper, value);
    }
}

void clock_until_sample(Mapper419& mapper, unsigned sample) {
    for (unsigned cycle = 0U; cycle < 100U && state(mapper)[kAudioFrameSampleOffset] != sample;
         ++cycle) {
        mapper.clock_audio();
    }
    REQUIRE(state(mapper)[kAudioFrameSampleOffset] == sample);
}

std::array<Byte, 8> frame(const std::array<Byte, 21>& codes, bool silent = false) {
    std::uint64_t bits = silent ? 0x8000000000000000ULL : 0U;
    for (unsigned index = 0U; index < codes.size(); ++index) {
        bits |= static_cast<std::uint64_t>(codes[index] & 7U) << (index * 3U);
    }
    std::array<Byte, 8> bytes{};
    for (unsigned index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<Byte>(bits >> (index * 8U));
    }
    return bytes;
}

TEST_CASE("Mapper 419 validates documented submappers ROM geometry and no-RAM population",
          "[mapper419][geometry][reset]") {
    for (const auto submapper : kSubmappers) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(submapper, kMinPrgSize, 0U, mirror);
            CAPTURE(submapper, mirror);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->read_prg(0x8123U) == cell(0x0123U));
            REQUIRE(mapper->read_prg(0xA123U) == cell(kPrgBankSize + 0x0123U));
            REQUIRE(mapper->read_prg(0xC123U) == cell(2UZ * kPrgBankSize + 0x0123U));
            REQUIRE(mapper->read_prg(0xE123U) == cell(3UZ * kPrgBankSize + 0x0123U));
            REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U));
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE(mapper->read_expansion(0x4017U) == 8U);
            auto other_rom = board(submapper, kMinPrgSize, 0U, mirror, 0x80U);
            REQUIRE(state(*other_rom) == state(*mapper));
        }
        for (std::size_t prg = kMinPrgSize; prg <= kMaxPrgSize; prg *= 2U) {
            REQUIRE(Mapper419::validate_geometry(header(submapper), prg, 0U));
            for (std::size_t chr = kMinChrSize; chr <= kMaxChrSize; chr *= 2U) {
                CAPTURE(submapper, prg, chr);
                REQUIRE(Mapper419::validate_geometry(header(submapper), prg, chr));
            }
        }
    }

    for (const unsigned id : {0U, 4U, 256U, 407U, 408U, 418U, 420U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper419::validate_geometry(h, kMinPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper419::validate_geometry(legacy, kMinPrgSize, 0U));
    for (Byte submapper = 6U; submapper <= 10U; ++submapper) {
        CAPTURE(submapper);
        REQUIRE_FALSE(Mapper419::validate_geometry(header(submapper), kMinPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x18000UZ, kMaxPrgSize + 1U, kMaxPrgSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper419::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size :
         {1UZ, 0x400UZ, 0x1FFFUZ, 0x2001UZ, 0x3000UZ, kMaxChrSize + 1U, kMaxChrSize * 2U}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper419::validate_geometry(header(), kMinPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper419::validate_geometry(header(0U, mirror), kMinPrgSize, 0U));
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
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper419::validate_geometry(h, kMinPrgSize, 0U));
    }
}

TEST_CASE("Mapper 419 applies every native PPU permutation and write-only bus aperture",
          "[mapper419][decoding][bus][chr]") {
    for (const auto submapper : kSubmappers) {
        auto mapper = board(submapper);
        const auto initial = state(*mapper);
        for (unsigned raw = 0x2000U; raw < 0x4000U; ++raw) {
            mapper->reset();
            const auto addr = static_cast<Address>(raw);
            const bool native = raw >= 0x2010U && raw <= 0x201FU;
            REQUIRE(mapper->write_ppu_register(addr, 0xD5U) == native);
            REQUIRE(mapper->read_ppu_register(addr, 0xA7U) ==
                    (native ? std::optional<Byte>(0xA7U) : std::nullopt));
            auto wanted = initial;
            if (raw >= 0x2012U && raw <= 0x2017U) {
                wanted[kChrOffset + canonical_chr(native_base(submapper, raw - 0x2012U))] = 0xD5U;
            } else if (raw == 0x2018U) {
                wanted[kIntermediateOffset] = 0x50U;
            } else if (raw == 0x201AU) {
                wanted[kChrMiddleOffset] = 0xD5U;
            }
            CAPTURE(submapper, raw, native);
            REQUIRE(state(*mapper) == wanted);
        }
    }
}

TEST_CASE("Mapper 419 forwards all MMC3 aliases with documented CPU bank permutations",
          "[mapper419][decoding][banking][bus]") {
    for (const auto submapper : kSubmappers) {
        auto mapper = board(submapper);
        const auto initial = state(*mapper);
        for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(raw), 0xA3U);
            auto wanted = initial;
            if (raw >= 0x8000U) {
                const unsigned decoded =
                    submapper == 2U && (raw & 0x4000U) == 0U ? raw ^ 0x2000U : raw;
                switch (decoded & 0xE001U) {
                case 0x8000U:
                    wanted[kControlOffset] = 0x83U;
                    break;
                case 0x8001U:
                    wanted[kChrOffset + canonical_chr(forwarded_base(submapper, 0U))] = 0xA3U;
                    break;
                case 0xA000U:
                    wanted[kMirrorOffset] = 1U;
                    break;
                case 0xC000U:
                    wanted[kLatchOffset] = 0xA3U;
                    break;
                case 0xE001U:
                    wanted[kIrqOffset] = 1U;
                    break;
                default:
                    break;
                }
            }
            CAPTURE(submapper, raw);
            REQUIRE(state(*mapper) == wanted);
        }
        for (unsigned index = 0U; index < 8U; ++index) {
            mapper->reset();
            mapper->write_prg(submapper == 2U ? 0xA000U : 0x8000U, static_cast<Byte>(index));
            mapper->write_prg(submapper == 2U ? 0xBFFFU : 0x9FFFU, 0xFFU);
            const auto saved = state(*mapper);
            CAPTURE(submapper, index);
            if (index < 6U) {
                REQUIRE(saved[kChrOffset + canonical_chr(forwarded_base(submapper, index))] ==
                        0xFFU);
            } else {
                REQUIRE(saved[kPrgOffset + index - 6U] == 0xFFU);
            }
        }
        mapper->reset();
        mapper->write_expansion(0x410BU, 8U);
        const auto disabled = state(*mapper);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
            REQUIRE(state(*mapper) == disabled);
        }
        mapper->write_expansion(0x4107U, 9U);
        REQUIRE(state(*mapper)[kPrgOffset] == 9U);
    }
}

TEST_CASE("Mapper 419 decodes native audio opcode and open-bus expansion ports",
          "[mapper419][decoding][bus][audio]") {
    for (const auto submapper : kSubmappers) {
        auto mapper = board(submapper);
        const auto initial = state(*mapper);
        for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
            mapper->reset();
            const auto addr = static_cast<Address>(raw);
            const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
            mapper->write_expansion(addr, value);
            auto wanted = initial;
            if (raw >= 0x4100U && raw <= 0x410BU) {
                switch (raw & 15U) {
                case 0U:
                    wanted[kOuterOffset] = value;
                    break;
                case 1U:
                    wanted[kLatchOffset] = value;
                    break;
                case 2U:
                case 3U:
                    break;
                case 4U:
                    wanted[kIrqOffset] = 1U;
                    break;
                case 5U:
                    wanted[kControlOffset] = value & 0xE7U;
                    break;
                case 6U:
                    wanted[kMirrorOffset] = value & 1U;
                    break;
                case 7U:
                case 8U:
                case 9U:
                    wanted[kPrgOffset + (raw & 15U) - 7U] = value;
                    break;
                case 10U:
                    wanted[kPrgOffset + 3U] = value;
                    break;
                default:
                    wanted[kModeOffset] = value & 0xCFU;
                    break;
                }
            } else if (raw == 0x410DU && value == 0x30U) {
                wanted[kAudioFlagsOffset] |= 2U;
            } else if (raw == 0x410FU) {
                wanted[kNibbleOffset] = value & 15U;
            } else if (raw == 0x411CU &&
                       (submapper == 11U || submapper == 12U || submapper == 14U)) {
                wanted[kOpcodeOffset] = value & (submapper == 11U ? 0x42U : 0x40U);
            } else if (raw == 0x4169U && (submapper == 13U || submapper == 15U)) {
                wanted[kOpcodeOffset] = value & 1U;
            }
            const bool mapped =
                (raw >= 0x4100U && raw <= 0x410BU) || raw == 0x410DU || raw == 0x410FU ||
                (raw == 0x411CU && (submapper == 11U || submapper == 12U || submapper == 14U)) ||
                (raw == 0x4169U && (submapper == 13U || submapper == 15U));
            CAPTURE(submapper, raw, value, mapped);
            REQUIRE(state(*mapper) == wanted);
            REQUIRE(mapper->maps_expansion(addr) == mapped);
            REQUIRE(mapper->expansion_read_mask(addr) == (mapped ? 0U : 0xFFU));
            REQUIRE(mapper->read_expansion(addr) == 0U);
        }
        REQUIRE(mapper->maps_expansion(0x4017U));
        REQUIRE(mapper->expansion_read_mask(0x4017U) == 0x18U);
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        for (const Address addr : {0x0000U, 0x4016U, 0x4018U, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0U);
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 419 composes full OneBus PRG addresses before fitted-ROM wrapping",
          "[mapper419][banking][bus]") {
    auto mapper = board(0U, kMaxPrgSize, kMinChrSize);
    for (Byte mode = 0U; mode < 8U; ++mode) {
        for (const Byte outer : {Byte{0U}, Byte{0x10U}, Byte{0x60U}, Byte{0xF0U}}) {
            for (const Byte middle : {Byte{0U}, Byte{0x55U}, Byte{0xAAU}, Byte{0xFFU}}) {
                for (const bool third : {false, true}) {
                    for (const bool swap : {false, true}) {
                        for (const Byte raw :
                             {Byte{0U}, Byte{1U}, Byte{0x3FU}, Byte{0x55U}, Byte{0xFFU}}) {
                            const auto control = static_cast<Byte>(swap ? 0x40U : 0U);
                            const auto full_mode = static_cast<Byte>(mode | (third ? 0x40U : 0U));
                            const std::array<Byte, 3> banks{raw, 0x59U, 0xA6U};
                            mapper->write_expansion(0x4100U, outer);
                            mapper->write_expansion(0x4105U, control);
                            mapper->write_expansion(0x4107U, banks[0]);
                            mapper->write_expansion(0x4108U, banks[1]);
                            mapper->write_expansion(0x4109U, banks[2]);
                            mapper->write_expansion(0x410AU, middle);
                            mapper->write_expansion(0x410BU, full_mode);
                            for (const auto addr : kPrgAddresses) {
                                const auto physical =
                                    expected_prg(addr, control, banks, middle, full_mode, outer);
                                CAPTURE(mode, outer, middle, third, swap, raw, addr, physical);
                                REQUIRE(mapper->maps_prg(addr));
                                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                                REQUIRE(mapper->read_prg(addr) ==
                                        cell(physical & (kMaxPrgSize - 1U)));
                            }
                        }
                    }
                }
            }
        }
    }
    auto fitted = board(0U, kMinPrgSize, 0U);
    fitted->write_expansion(0x4107U, 0xFFU);
    fitted->write_expansion(0x4109U, 0xFEU);
    fitted->write_expansion(0x410AU, 0xFFU);
    fitted->write_expansion(0x410BU, 0x47U);
    for (const auto addr : kPrgAddresses) {
        const auto physical =
            expected_prg(addr, 0U, {0xFFU, 1U, 0xFEU}, 0xFFU, 0x47U, 0U) & (kMinPrgSize - 1U);
        CAPTURE(addr, physical);
        REQUIRE(fitted->read_prg(addr) == cell(physical));
    }
}

TEST_CASE("Mapper 419 applies CHR pairing inversion masks and shared or split read-only storage",
          "[mapper419][banking][chr][bus]") {
    auto mapper = board(0U, 0x20000UZ, kMaxChrSize);
    for (Byte mode = 0U; mode < 8U; ++mode) {
        for (const Byte outer : {Byte{0U}, Byte{1U}, Byte{8U}, Byte{15U}}) {
            for (const Byte intermediate : {Byte{0U}, Byte{0x10U}, Byte{0x70U}}) {
                for (const bool flip : {false, true}) {
                    for (const Byte value :
                         {Byte{0U}, Byte{1U}, Byte{0x1FU}, Byte{0x55U}, Byte{0xAAU}, Byte{0xFFU}}) {
                        const auto control = static_cast<Byte>(flip ? 0x80U : 0U);
                        const auto middle = static_cast<Byte>((value & 248U) | mode);
                        std::array<Byte, 6> banks{};
                        mapper->write_expansion(0x4100U, outer);
                        mapper->write_expansion(0x4105U, control);
                        mapper->write_ppu_register(0x2018U, intermediate);
                        mapper->write_ppu_register(0x201AU, middle);
                        for (unsigned index = 0U; index < banks.size(); ++index) {
                            banks[index] = static_cast<Byte>(value + index * 17U);
                            mapper->write_ppu_register(static_cast<Address>(0x2012U + index),
                                                       banks[index]);
                        }
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
                            const auto physical =
                                expected_chr(addr, control, banks, intermediate, middle, outer);
                            CAPTURE(mode, outer, intermediate, flip, value, slot, physical);
                            REQUIRE(mapper->read_chr(addr) ==
                                    cell(physical & (kMaxChrSize - 1U), 0x35U));
                            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                                    cell(physical & (kMaxChrSize - 1U), 0x35U));
                            mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                            REQUIRE(mapper->read_chr(addr) ==
                                    cell(physical & (kMaxChrSize - 1U), 0x35U));
                        }
                    }
                }
            }
        }
    }
    auto shared = board(0U, 0x20000UZ, 0U);
    shared->write_ppu_register(0x2012U, 0xFFU);
    shared->write_ppu_register(0x2018U, 0x70U);
    shared->write_ppu_register(0x201AU, 0xF8U);
    shared->write_expansion(0x4100U, 0x0FU);
    const auto physical =
        expected_chr(0x1123U, 0U, {0xFFU, 5U, 6U, 7U, 0U, 2U}, 0x70U, 0xF8U, 0x0FU) & 0x1FFFFU;
    REQUIRE(shared->read_chr(0x1123U) == cell(physical));
}

TEST_CASE("Mapper 419 controls CIRAM independently and owns no cartridge RAM",
          "[mapper419][ciram][ram][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_expansion(0x4105U, static_cast<Byte>(raw));
        mapper->write_expansion(0x4106U, static_cast<Byte>(raw));
        CAPTURE(raw);
        REQUIRE(mapper->mirror_mode() ==
                ((raw & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (const Address addr :
             {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1FFFU, 0x2000U, 0x2FFFU, 0x3EFFU}) {
            const auto expected = addr < 0x2000U && (raw & 0x20U) != 0U
                                      ? std::optional<Address>(addr & 0x07FFU)
                                      : std::nullopt;
            REQUIRE(mapper->ciram_address(addr) == expected);
            if (addr >= 0x2000U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
            }
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
}

TEST_CASE("Mapper 419 clocks native IRQ from qualified A12 or HSYNC", "[mapper419][irq][reset]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_expansion(0x4101U, static_cast<Byte>(latch));
        mapper->write_expansion(0x4102U, 0U);
        mapper->write_expansion(0x4104U, 0U);
        mapper->on_ppu_hblank();
        REQUIRE_FALSE(mapper->irq_pending());
        edge(*mapper);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        for (unsigned count = 0U; count < latch; ++count) {
            edge(*mapper);
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->write_expansion(0x4103U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    for (unsigned low = 0U; low <= 4U; ++low) {
        mapper->reset();
        mapper->write_expansion(0x4104U, 0U);
        edge(*mapper, low);
        CAPTURE(low);
        REQUIRE(mapper->irq_pending() == (low >= 3U));
    }
    mapper->reset();
    mapper->write_expansion(0x410BU, 0x80U);
    mapper->write_expansion(0x4101U, 2U);
    mapper->write_expansion(0x4102U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->on_ppu_hblank();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->on_ppu_hblank();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->on_ppu_hblank();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4103U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 419 TK-8007 nibble protocol commands buffer READY and reset sequence",
          "[mapper419][audio][bus][decoding]") {
    auto mapper = board();
    enable_audio(*mapper);
    send_bytes(*mapper, std::array<Byte, 3>{0x03U, 0x02U, 0x01U});
    auto bytes = state(*mapper);
    REQUIRE(read_u16(bytes, kPeriodOffset) == 0x0102U);
    REQUIRE((bytes[kAudioFlagsOffset] & 2U) != 0U);

    send_byte(*mapper, 0x04U);
    for (unsigned index = 0U; index < 96U; ++index) {
        send_byte(*mapper, static_cast<Byte>(index * 37U + 11U));
    }
    bytes = state(*mapper);
    REQUIRE(bytes[kAudioSizeOffset] == 96U);
    REQUIRE(bytes[kCommandOffset] == 0U);
    for (unsigned index = 0U; index < 96U; ++index) {
        CAPTURE(index);
        REQUIRE(bytes[kAudioBufferOffset + index] == static_cast<Byte>(index * 37U + 11U));
    }
    send_byte(*mapper, 0x07U);
    bytes = state(*mapper);
    REQUIRE(bytes[kAudioSizeOffset] == 0U);
    REQUIRE(bytes[kAudioFrameSampleOffset] == 21U);
    REQUIRE(read_u16(bytes, kPeriodOffset) == 0x0102U);

    send_byte(*mapper, 0x06U);
    for (unsigned group = 0U; group < 12U; ++group) {
        for (unsigned index = 0U; index < 8U; ++index) {
            send_byte(*mapper, static_cast<Byte>(group * 8U + index));
        }
        CAPTURE(group);
        REQUIRE((mapper->read_expansion(0x4017U) & 0x10U) == (group < 11U ? 0x10U : 0U));
    }
    bytes = state(*mapper);
    REQUIRE(bytes[kAudioSizeOffset] == 96U);
    REQUIRE(bytes[kCommandOffset] == 0U);
    send_byte(*mapper, 0x06U);
    REQUIRE((mapper->read_expansion(0x4017U) & 0x10U) == 0U);

    send_bytes(*mapper, std::array<Byte, 3>{0x03U, 1U, 0U});
    mapper->clock_audio();
    REQUIRE(state(*mapper)[kAudioSizeOffset] == 88U);
    send_byte(*mapper, 0x06U);
    REQUIRE((mapper->read_expansion(0x4017U) & 0x10U) != 0U);
    send_bytes(*mapper, std::array<Byte, 3>{0x91U, 0x92U, 0x93U});
    bytes = state(*mapper);
    REQUIRE(bytes[kCommandOffset] == 6U);
    REQUIRE(bytes[kCommandPositionOffset] == 3U);
    REQUIRE(bytes[kAudioSizeOffset] == 91U);
    send_bytes(*mapper, std::array<Byte, 2>{0x55U, 0xAAU});
    bytes = state(*mapper);
    REQUIRE(bytes[kAudioSizeOffset] == 0U);
    REQUIRE(bytes[kCommandOffset] == 0U);
    REQUIRE(read_u16(bytes, kPeriodOffset) == 0U);
    REQUIRE(mapper->audio_output() == 0.0F);

    send_bytes(*mapper, std::array<Byte, 3>{0x03U, 1U, 0U});
    send_byte(*mapper, 0x04U);
    for (unsigned index = 0U; index < 95U; ++index) {
        send_byte(*mapper, 0U);
    }
    mapper->clock_audio();
    mapper->clock_audio();
    REQUIRE(state(*mapper)[kAudioSizeOffset] == 95U);
    REQUIRE(state(*mapper)[kAudioFrameSampleOffset] == 21U);
    send_byte(*mapper, 0U);
    mapper->clock_audio();
    REQUIRE(state(*mapper)[kAudioSizeOffset] == 88U);
    REQUIRE(state(*mapper)[kAudioFrameSampleOffset] == 2U);

    mapper->write_expansion(0x410DU, 0U);
    const auto disabled = state(*mapper);
    mapper->write_expansion(0x410FU, 0x0BU);
    mapper->observe_cpu_write(0x4016U, 4U);
    mapper->write_expansion(0x410FU, 0x0CU);
    mapper->observe_cpu_write(0x4016U, 0U);
    auto after_disabled = state(*mapper);
    REQUIRE(after_disabled[kAudioSizeOffset] == disabled[kAudioSizeOffset]);
    REQUIRE(after_disabled[kCommandOffset] == disabled[kCommandOffset]);
    REQUIRE(after_disabled[kAudioFlagsOffset] == disabled[kAudioFlagsOffset]);
}

TEST_CASE("Mapper 419 decodes three-bit ADPCM samples bottom-first with silent frames",
          "[mapper419][audio][banking]") {
    auto mapper = board();
    enable_audio(*mapper);
    constexpr std::array<Byte, 21> codes{1U, 5U, 2U, 6U, 3U, 7U, 4U, 0U, 1U, 2U, 3U,
                                         4U, 5U, 6U, 7U, 0U, 3U, 3U, 3U, 3U, 3U};
    const auto first = frame(codes);
    std::array<Byte, 21> ignored_codes{};
    ignored_codes.fill(7U);
    const auto silent = frame(ignored_codes, true);
    const auto following = frame(std::array<Byte, 21>{3U});
    std::array<Byte, 96> stream{};
    for (unsigned index = 0U; index < 8U; ++index) {
        stream[index] = first[index];
        stream[8U + index] = silent[index];
        stream[16U + index] = following[index];
    }
    send_bytes(*mapper, std::array<Byte, 3>{0x03U, 3U, 0U});
    send_byte(*mapper, 0x04U);
    send_bytes(*mapper, stream);

    int predictor = 0;
    unsigned index = 0U;
    for (unsigned sample = 0U; sample < codes.size(); ++sample) {
        clock_until_sample(*mapper, sample + 1U);
        const auto code = codes[sample];
        const auto table = code & 3U;
        predictor += kStepTable[table][index] * ((code & 4U) != 0U ? -1 : 1);
        index = kIndexTable[index + kIndexStep[table]];
        CAPTURE(sample, code, predictor, index, state(*mapper)[kAudioFrameSampleOffset]);
        REQUIRE(mapper->audio_output() == static_cast<float>(predictor) / 32768.0F);
    }
    REQUIRE(index == 20U);
    const auto before_silence = state(*mapper);
    for (unsigned sample = 0U; sample < 21U; ++sample) {
        clock_until_sample(*mapper, sample + 1U);
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    const auto after_silence = state(*mapper);
    REQUIRE(read_u32(after_silence, kPredictorOffset) ==
            read_u32(before_silence, kPredictorOffset));
    REQUIRE(after_silence[kAudioIndexOffset] == before_silence[kAudioIndexOffset]);

    clock_until_sample(*mapper, 1U);
    const int resumed = predictor + kStepTable[3][index];
    REQUIRE(mapper->audio_output() == static_cast<float>(resumed) / 32768.0F);
    for (unsigned cycle = 0U; cycle < 300U; ++cycle) {
        mapper->clock_audio();
    }
    const auto starved = mapper->audio_output();
    for (unsigned cycle = 0U; cycle < 10U; ++cycle) {
        mapper->clock_audio();
    }
    REQUIRE(mapper->audio_output() == starved);

    send_bytes(*mapper, std::array<Byte, 3>{0x03U, 0U, 0U});
    const auto stopped = state(*mapper);
    for (unsigned cycle = 0U; cycle < 100U; ++cycle) {
        mapper->clock_audio();
    }
    REQUIRE(state(*mapper) == stopped);
}

TEST_CASE("Mapper 419 reset restores banking IRQ opcode and audio defaults without RAM",
          "[mapper419][reset][irq][audio]") {
    for (const bool cold : {false, true}) {
        auto mapper = board(11U, 0x20000UZ, 0x20000UZ, MirrorMode::Horizontal);
        const auto initial = state(*mapper);
        mapper->write_expansion(0x4100U, 0xA7U);
        mapper->write_expansion(0x4105U, 0xE7U);
        mapper->write_expansion(0x4107U, 0x95U);
        mapper->write_ppu_register(0x2012U, 0xFFU);
        mapper->write_expansion(0x4104U, 0U);
        edge(*mapper);
        mapper->write_expansion(0x411CU, 0x42U);
        enable_audio(*mapper);
        send_bytes(*mapper, std::array<Byte, 3>{0x03U, 1U, 0U});
        send_byte(*mapper, 0x04U);
        send_bytes(*mapper, std::array<Byte, 8>{1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U});
        REQUIRE(mapper->irq_pending());
        if (cold) {
            mapper->power_on();
        } else {
            mapper->reset();
        }
        CAPTURE(cold);
        REQUIRE(state(*mapper) == initial);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(mapper->read_expansion(0x4017U) == 8U);
    }
}

TEST_CASE("Mapper 419 bounded snapshots replay live banking IRQ and ADPCM state without ROM",
          "[mapper419][state][audio][irq]") {
    auto mapper = board(11U);
    mapper->write_expansion(0x4100U, 0xA7U);
    mapper->write_expansion(0x4105U, 0x60U);
    mapper->write_expansion(0x4107U, 0x95U);
    mapper->write_expansion(0x4108U, 0x59U);
    mapper->write_expansion(0x4109U, 0xA6U);
    mapper->write_expansion(0x410AU, 0x40U);
    mapper->write_expansion(0x410BU, 0x40U);
    mapper->write_ppu_register(0x2012U, 0x12U);
    mapper->write_ppu_register(0x2016U, 0xFEU);
    mapper->write_ppu_register(0x2018U, 0x70U);
    mapper->write_ppu_register(0x201AU, 0x20U);
    mapper->write_expansion(0x411CU, 0x42U);
    mapper->write_expansion(0x4101U, 2U);
    mapper->write_expansion(0x4102U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    mapper->observe_ppu_a12(false);
    mapper->clock_cpu();
    mapper->clock_cpu();
    enable_audio(*mapper);
    send_bytes(*mapper, std::array<Byte, 3>{0x03U, 1U, 0U});
    send_byte(*mapper, 0x04U);
    std::array<Byte, 96> stream{};
    for (unsigned index = 0U; index < stream.size(); ++index) {
        stream[index] = static_cast<Byte>(index * 19U + 7U);
    }
    send_bytes(*mapper, stream);
    for (unsigned cycle = 0U; cycle < 7U; ++cycle) {
        mapper->clock_audio();
    }
    send_byte(*mapper, 0x06U);
    send_bytes(*mapper, std::array<Byte, 3>{0x81U, 0x82U, 0x83U});
    mapper->set_region(Region::PAL);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 1024U);
    REQUIRE(saved[kOpcodeOffset] == 0x42U);
    REQUIRE(saved[kCommandOffset] == 6U);
    REQUIRE(saved[kCommandPositionOffset] == 3U);
    REQUIRE(saved[kAudioFrameSampleOffset] == 15U);

    auto copy = board(11U, 0x20000UZ, 0x20000UZ, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->ciram_address(0x123U) == mapper->ciram_address(0x123U));
    REQUIRE(copy->audio_output() == mapper->audio_output());
    for (unsigned cycle = 0U; cycle < 50U; ++cycle) {
        copy->clock_audio();
        mapper->clock_audio();
        copy->clock_cpu();
        mapper->clock_cpu();
    }
    send_bytes(*copy, std::array<Byte, 5>{0x84U, 0x85U, 0x86U, 0x87U, 0x88U});
    send_bytes(*mapper, std::array<Byte, 5>{0x84U, 0x85U, 0x86U, 0x87U, 0x88U});
    edge(*copy);
    edge(*mapper);
    edge(*copy);
    edge(*mapper);
    REQUIRE(state(*copy) == state(*mapper));
    REQUIRE(copy->irq_pending());

    for (const auto addr : kPrgAddresses) {
        const auto physical =
            expected_prg(addr, 0x60U, {0x95U, 0x59U, 0xA6U}, 0x40U, 0x40U, 0xA7U) & 0x1FFFFU;
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->reset();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 419 rejects damaged truncated and mismatched states atomically",
          "[mapper419][state][audio]") {
    auto mapper = board(11U);
    mapper->write_expansion(0x4100U, 0xA7U);
    mapper->write_expansion(0x4105U, 0xE7U);
    mapper->write_expansion(0x4107U, 0x95U);
    mapper->write_expansion(0x410BU, 0x40U);
    mapper->write_ppu_register(0x2012U, 0xFFU);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    enable_audio(*mapper);
    send_bytes(*mapper, std::array<Byte, 3>{0x03U, 1U, 0U});
    send_byte(*mapper, 0x04U);
    std::array<Byte, 96> stream{};
    send_bytes(*mapper, stream);
    mapper->clock_audio();
    send_byte(*mapper, 0x06U);
    send_bytes(*mapper, std::array<Byte, 3>{1U, 2U, 3U});
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
    for (std::size_t offset = 0U; offset < kGeometrySize; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 20> invalid_fields{{
        {kControlOffset, 0x08U},
        {kMirrorOffset, 2U},
        {kModeOffset, 0x10U},
        {kIntermediateOffset, 0x80U},
        {kIrqOffset, 2U},
        {kIrqOffset, 4U},
        {kPinsOffset, 3U},
        {kPinsOffset, 8U},
        {kOpcodeOffset, 1U},
        {kAudioReadOffset, 1U},
        {kAudioReadOffset, 96U},
        {kAudioSizeOffset, 97U},
        {kAudioFrameSampleOffset, 22U},
        {kAudioIndexOffset, 21U},
        {kCommandOffset, 5U},
        {kCommandPositionOffset, 8U},
        {kAudioFlagsOffset, 0x40U},
        {kAudioFlagsOffset, 0x20U},
        {kNibbleOffset, 16U},
        {kPeriodOffset, 0U},
    }};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    auto invalid_clock = saved;
    write_u32(invalid_clock, kCpuClockOffset, 0U);
    reject(invalid_clock);
    auto invalid_phase = saved;
    write_u64(invalid_phase, kPhaseOffset, read_u64(saved, kPhaseOffset) + 0x100000000ULL);
    reject(invalid_phase);

    auto wrong_submapper = board(12U);
    const auto wrong_submapper_before = state(*wrong_submapper);
    StateReader wrong_submapper_reader(saved);
    wrong_submapper->load_state(wrong_submapper_reader);
    REQUIRE_FALSE(wrong_submapper_reader.ok());
    REQUIRE(state(*wrong_submapper) == wrong_submapper_before);

    auto wrong_prg = board(11U, 0x10000UZ);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(11U, 0x20000UZ, 0x10000UZ);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto changed = saved;
    changed[kAudioBufferOffset + 7U] ^= 0xFFU;
    changed[kPrgOffset] = 0xFEU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
