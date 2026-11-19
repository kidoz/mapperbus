#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper432.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x100000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kStateSize = 28UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kSolderOffset = 11UZ;
constexpr std::size_t kSelectOffset = 12UZ;
constexpr std::size_t kModeOffset = 13UZ;
constexpr std::size_t kBanksOffset = 14UZ;
constexpr std::size_t kMirrorOffset = 22UZ;
constexpr std::size_t kCounterOffset = 23UZ;
constexpr std::size_t kReloadOffset = 24UZ;
constexpr std::size_t kIrqOffset = 25UZ;
constexpr std::size_t kRamFlagsOffset = 26UZ;
constexpr std::size_t kPadOffset = 27UZ;
constexpr std::size_t kWramOffset = 28UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

enum class RamKind { None, Volatile, Nonvolatile };

std::size_t max_prg(Byte submapper) {
    return submapper == 0U ? kMaxPrgSize : kMaxPrgSize / 2U;
}

std::size_t max_chr(Byte submapper) {
    return submapper == 0U ? kMaxChrSize : kMaxChrSize / 2U;
}

Byte outer_mask(Byte submapper) {
    return submapper == 4U ? 0x5FU : 0xFFU;
}

INesHeader header(Byte submapper = 0U,
                  RamKind ram = RamKind::None,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 432U;
    result.is_nes2 = true;
    result.submapper = submapper;
    result.mirror_mode = mirror;
    result.has_battery = ram == RamKind::Nonvolatile;
    result.prg_ram_size = ram == RamKind::Volatile ? Mapper432::kWramSize : 0U;
    result.prg_nvram_size = ram == RamKind::Nonvolatile ? Mapper432::kWramSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 14U) * 47U + (offset >> 16U) * 59U + (offset >> 18U) * 71U +
                             (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper432> board(Byte submapper = 0U,
                                 std::size_t prg_size = 0U,
                                 std::size_t chr_size = 0U,
                                 RamKind ram = RamKind::None,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 Byte pad = 0U) {
    prg_size = prg_size == 0U ? max_prg(submapper) : prg_size;
    chr_size = chr_size == 0U ? max_chr(submapper) : chr_size;
    const auto h = header(submapper, ram, mirror);
    REQUIRE(Mapper432::validate_geometry(h, prg_size, chr_size));
    auto mapper = std::make_unique<Mapper432>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x35U)));
    REQUIRE(mapper->configure_board_jumpers(pad));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper432& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper432& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper432& mapper, Byte value) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(0x6001U, value);
}

bool nrom256(Byte submapper, Byte outer_bank) {
    return (submapper <= 1U && (outer_bank & 0x80U) != 0U) ||
           (submapper == 2U && (outer_bank & 0x20U) != 0U);
}

unsigned expected_prg_bank(
    Address addr, Byte submapper, Byte outer_bank, Byte r6, Byte r7, Byte mode) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    unsigned inner = 0U;
    if ((outer_bank & 0x40U) != 0U) {
        inner = ((slot & 1U) == 0U ? r6 : r7) & 0x3FU;
    } else if (nrom256(submapper, outer_bank)) {
        inner = (r6 & 0x3CU) | slot;
    } else {
        const bool swapped = (mode & 0x40U) != 0U;
        inner = slot == 1U                ? r7 & 0x3FU
                : slot == 3U              ? 63U
                : (slot == 0U) == swapped ? 62U
                                          : r6 & 0x3FU;
    }
    const unsigned a17 = (outer_bank & 2U) != 0U ? outer_bank & 1U : (inner >> 5U) & 1U;
    unsigned selected = (inner & 0x1FU) | (a17 << 5U) | (((outer_bank >> 4U) & 1U) << 6U);
    if (submapper == 0U) {
        selected |= ((outer_bank >> 5U) & 1U) << 7U;
    }
    return selected;
}

Byte raw_chr_bank(Address addr, const std::array<Byte, 8>& banks, Byte mode) {
    const unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    return slot < 4U ? static_cast<Byte>((banks[slot / 2U] & 0xFEU) | (slot & 1U))
                     : banks[slot - 2U];
}

std::uint32_t expected_chr_offset(
    Address addr, Byte submapper, Byte outer_bank, const std::array<Byte, 8>& banks, Byte mode) {
    std::uint32_t offset = 0U;
    if (submapper == 3U && (outer_bank & 0x20U) != 0U) {
        const auto pins =
            static_cast<Address>((addr & 0x1000U) | ((addr & 0x800U) >> 1U) | (addr & 0x800U));
        offset =
            static_cast<std::uint32_t>(raw_chr_bank(pins, banks, mode)) * 0x800U + (addr & 0x7FFU);
    } else {
        offset = static_cast<std::uint32_t>(raw_chr_bank(addr, banks, mode)) * kChrBankSize +
                 (addr & 0x3FFU);
    }
    const unsigned a17 = (outer_bank & 4U) != 0U ? outer_bank & 1U : (offset >> 17U) & 1U;
    offset = (offset & 0x1FFFFU) | (a17 << 17U) | (((outer_bank >> 3U) & 1U) << 18U);
    if (submapper == 0U) {
        offset |= static_cast<std::uint32_t>((outer_bank >> 5U) & 1U) << 19U;
    }
    return offset;
}

TEST_CASE("Mapper 432 validates submapper ROM WRAM and CIRAM geometry",
          "[mapper432][geometry][ram]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (const auto ram : {RamKind::None, RamKind::Volatile, RamKind::Nonvolatile}) {
                auto mapper = board(submapper, 0U, 0U, ram, mirror);
                CAPTURE(submapper, mirror, ram);
                REQUIRE(mapper->mirror_mode() == mirror);
                REQUIRE(state(*mapper).size() ==
                        kStateSize + (ram == RamKind::None ? 0U : Mapper432::kWramSize));
                REQUIRE(mapper->battery_ram().size() ==
                        (ram == RamKind::Nonvolatile ? Mapper432::kWramSize : 0U));
                REQUIRE(mapper->shared_nvram().empty());
                REQUIRE_FALSE(mapper->has_flash_storage());
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
                auto other_rom = board(submapper, 0U, 0U, ram, mirror, 0x80U);
                REQUIRE(state(*other_rom) == state(*mapper));
            }
        }
        for (const std::size_t prg : {kMinPrgSize, 0xA000UZ, 0x18000UZ, max_prg(submapper)}) {
            for (const std::size_t chr : {kMinChrSize, 0x2400UZ, 0x6000UZ, max_chr(submapper)}) {
                CAPTURE(submapper, prg, chr);
                REQUIRE(Mapper432::validate_geometry(header(submapper), prg, chr));
            }
        }
    }

    const auto reject = [](INesHeader h, std::size_t prg, std::size_t chr) {
        CAPTURE(h.mapper_number,
                h.is_nes2,
                h.submapper,
                h.has_battery,
                h.has_trainer,
                h.prg_ram_size,
                h.prg_nvram_size,
                h.chr_ram_size,
                h.chr_nvram_size,
                h.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper432::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 197U, 430U, 431U, 433U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, kMinChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, kMinChrSize);
    for (Byte submapper = 5U; submapper < 16U; ++submapper) {
        reject(header(submapper), kMinPrgSize, kMinChrSize);
    }
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        for (const std::size_t size : {0UZ,
                                       1UZ,
                                       0x4000UZ,
                                       0x7FFFUZ,
                                       0x8001UZ,
                                       max_prg(submapper) + kPrgBankSize,
                                       kMaxPrgSize + kPrgBankSize}) {
            CAPTURE(submapper, size);
            reject(header(submapper), size, kMinChrSize);
        }
        for (const std::size_t size : {0UZ,
                                       1UZ,
                                       0x400UZ,
                                       0x1FFFUZ,
                                       0x2001UZ,
                                       max_chr(submapper) + kChrBankSize,
                                       kMaxChrSize + kChrBankSize}) {
            CAPTURE(submapper, size);
            reject(header(submapper), kMinPrgSize, size);
        }
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(0U, RamKind::None, mirror), kMinPrgSize, kMinChrSize);
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x1000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x1000U;
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = Mapper432::kWramSize;
            h.prg_nvram_size = Mapper432::kWramSize;
            h.has_battery = true;
            break;
        case 4U:
            h.prg_nvram_size = Mapper432::kWramSize;
            break;
        case 5U:
            h.prg_ram_size = Mapper432::kWramSize;
            h.has_battery = true;
            break;
        case 6U:
            h.chr_ram_size = 0x2000U;
            break;
        case 7U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        reject(h, kMinPrgSize, kMinChrSize);
    }
}

TEST_CASE("Mapper 432 decodes every low register alias and outer lock",
          "[mapper432][decoding][bus]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        auto mapper = board(submapper);
        const auto initial = state(*mapper);
        for (unsigned raw = 0x4020U; raw < 0x8000U; ++raw) {
            mapper->reset();
            mapper->write_prg(0xA001U, 0x80U);
            const auto addr = static_cast<Address>(raw);
            auto wanted = initial;
            mapper->write_prg(addr, 0xA5U);
            if (raw >= 0x6000U) {
                if ((raw & 1U) == 0U && submapper != 1U) {
                    wanted[kSolderOffset] = 1U;
                } else if ((raw & 1U) != 0U) {
                    wanted[kOuterOffset] = 0xA5U & outer_mask(submapper);
                }
            }
            CAPTURE(submapper, raw);
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0U);
            REQUIRE(mapper->read_prg(addr) == 0U);
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
            REQUIRE(mapper->read_expansion(addr) == 0U);
            mapper->write_expansion(addr, 0xFFU);
            REQUIRE(state(*mapper) == wanted);
        }

        for (const Byte control : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->reset();
                mapper->write_prg(0xA001U, control);
                mapper->write_prg(0x6001U, static_cast<Byte>(value));
                CAPTURE(submapper, control, value);
                REQUIRE(state(*mapper)[kOuterOffset] ==
                        (control == 0x80U ? value & outer_mask(submapper) : 0U));
            }
        }
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    }

    for (const Byte submapper : {Byte{2U}, Byte{3U}}) {
        auto mapper = board(submapper);
        outer(*mapper, 0x15U);
        outer(*mapper, 0xBFU);
        outer(*mapper, 0U);
        outer(*mapper, 0x7FU);
        CAPTURE(submapper);
        REQUIRE(state(*mapper)[kOuterOffset] == 0xBFU);
        mapper->reset();
        outer(*mapper, 0x7FU);
        REQUIRE(state(*mapper)[kOuterOffset] == 0x7FU);
    }
}

TEST_CASE("Mapper 432 composes every MMC3 and outer PRG mode before ROM wrapping",
          "[mapper432][decoding][banking][prg]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        auto mapper = board(submapper);
        for (unsigned raw_outer = 0U; raw_outer < 256U; ++raw_outer) {
            const auto outer_bank = static_cast<Byte>(raw_outer & outer_mask(submapper));
            mapper->reset();
            outer(*mapper, static_cast<Byte>(raw_outer));
            for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
                for (unsigned raw = 0U; raw < 256U; ++raw) {
                    const auto r6 = static_cast<Byte>(raw & 0x3FU);
                    const auto r7 = static_cast<Byte>((raw ^ 0x35U) & 0x3FU);
                    bank(*mapper, 6U, static_cast<Byte>(raw), mode);
                    bank(*mapper, 7U, static_cast<Byte>(raw ^ 0x35U), mode);
                    for (const auto addr : kPrgAddresses) {
                        const auto selected =
                            expected_prg_bank(addr, submapper, outer_bank, r6, r7, mode);
                        const auto physical =
                            (selected * kPrgBankSize + (addr & 0x1FFFU)) % max_prg(submapper);
                        CAPTURE(submapper, outer_bank, mode, raw, addr, selected, physical);
                        REQUIRE(mapper->maps_prg(addr));
                        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                        REQUIRE(
                            mapper->read_prg(addr) ==
                            (submapper == 1U && (outer_bank & 0x20U) != 0U ? 0U : cell(physical)));
                    }
                }
            }
        }

        for (const std::size_t size : {kMinPrgSize, 0xA000UZ, 0x18000UZ}) {
            auto fitted = board(submapper, size, kMinChrSize);
            outer(*fitted, 0xFFU);
            bank(*fitted, 6U, 0x2AU);
            bank(*fitted, 7U, 0x15U);
            for (const auto addr : kPrgAddresses) {
                const auto selected =
                    expected_prg_bank(addr, submapper, outer_mask(submapper), 0x2AU, 0x15U, 0U);
                const auto physical = (selected * kPrgBankSize + (addr & 0x1FFFU)) % size;
                CAPTURE(submapper, size, addr, selected, physical);
                REQUIRE(fitted->read_prg(addr) == (submapper == 1U ? 0U : cell(physical)));
            }
        }
    }
}

TEST_CASE("Mapper 432 applies CHR pairing inversion outer pins and 512 KiB wiring",
          "[mapper432][decoding][banking][chr]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        auto mapper = board(submapper);
        for (unsigned raw_outer = 0U; raw_outer < 256U; ++raw_outer) {
            const auto outer_bank = static_cast<Byte>(raw_outer & outer_mask(submapper));
            mapper->reset();
            outer(*mapper, static_cast<Byte>(raw_outer));
            for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
                for (unsigned raw = 0U; raw < 256U; ++raw) {
                    std::array<Byte, 8> banks{};
                    for (Byte index = 0U; index < 6U; ++index) {
                        banks[index] = static_cast<Byte>(raw + index * 37U);
                        bank(*mapper, index, banks[index], mode);
                    }
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
                        const auto physical =
                            expected_chr_offset(addr, submapper, outer_bank, banks, mode) %
                            max_chr(submapper);
                        CAPTURE(submapper, outer_bank, mode, raw, slot, physical);
                        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                        mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                    }
                }
            }
        }

        for (const std::size_t size : {kMinChrSize, 0x6000UZ}) {
            auto fitted = board(submapper, kMinPrgSize, size);
            outer(*fitted, 0xFFU);
            bank(*fitted, 2U, 0xB4U);
            const auto physical =
                expected_chr_offset(
                    0x1123U, submapper, outer_mask(submapper), {0U, 0U, 0xB4U}, 0U) %
                size;
            CAPTURE(submapper, size, physical);
            REQUIRE(fitted->read_chr(0x1123U) == cell(physical, 0x35U));
        }
    }
}

TEST_CASE("Mapper 432 reads solder values and only submapper 4 floats disabled PRG",
          "[mapper432][bus][decoding][jumpers]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        for (const Byte pad : {Byte{0U}, Byte{1U}, Byte{0x5AU}, Byte{0xFFU}}) {
            auto mapper = board(
                submapper, kMinPrgSize, kMinChrSize, RamKind::None, MirrorMode::Vertical, 0U, pad);
            const auto rom_byte = mapper->read_prg(0x9234U);
            mapper->write_prg(0x6000U, 1U);
            if (submapper == 1U) {
                REQUIRE(mapper->read_prg(0x9234U) == rom_byte);
                REQUIRE(state(*mapper)[kSolderOffset] == 0U);
                outer(*mapper, 0x20U);
            }
            for (const auto addr : kPrgAddresses) {
                CAPTURE(submapper, pad, addr);
                const bool disabled = submapper == 4U && pad != 0U;
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == (disabled ? 0U : 0xFFU));
                if (submapper == 4U) {
                    REQUIRE(mapper->read_prg(addr) ==
                            (disabled ? 0U
                                      : cell((expected_prg_bank(addr, submapper, 0U, 0U, 0U, 0U) *
                                                  kPrgBankSize +
                                              (addr & 0x1FFFU)) %
                                             kMinPrgSize)));
                } else {
                    REQUIRE(mapper->read_prg(addr) == pad);
                }
            }
            mapper->reset();
            REQUIRE(mapper->read_prg(0x9234U) == rom_byte);
            REQUIRE(state(*mapper)[kPadOffset] == pad);
        }
    }
}

TEST_CASE("Mapper 432 shares WRAM writes with outer and solder decoders",
          "[mapper432][bus][ram][decoding]") {
    for (const auto ram : {RamKind::None, RamKind::Volatile, RamKind::Nonvolatile}) {
        for (const Byte control : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
            auto mapper = board(0U, 0x20000UZ, 0x20000UZ, ram);
            outer(*mapper, 7U);
            mapper->write_prg(0x6123U, 0x5AU);
            mapper->write_prg(0x6000U, 0U);
            mapper->write_prg(0xA001U, control);
            mapper->write_prg(0x6001U, 0xA5U);
            mapper->write_prg(0x6123U, 0xA6U);
            mapper->write_prg(0x6000U, 0xA5U);
            const auto bytes = state(*mapper);
            CAPTURE(ram, control);
            REQUIRE(bytes[kOuterOffset] == (control == 0x80U ? 0xA6U : 0x5AU));
            REQUIRE(bytes[kSolderOffset] == 1U);
            REQUIRE(mapper->read_prg(0x9234U) == 0U);
            REQUIRE(mapper->maps_prg(0x6123U) == (ram != RamKind::None && (control & 0x80U) != 0U));
            REQUIRE(mapper->prg_read_mask(0x6123U) ==
                    (ram != RamKind::None && (control & 0x80U) != 0U ? 0xFFU : 0U));
            if (ram != RamKind::None) {
                REQUIRE(bytes[kWramOffset + 0x123U] == (control == 0x80U ? 0xA6U : 0x5AU));
                REQUIRE(mapper->read_prg(0x6123U) ==
                        ((control & 0x80U) != 0U ? bytes[kWramOffset + 0x123U] : 0U));
            }
        }
    }

    auto imported = board(0U, 0x20000UZ, 0x20000UZ, RamKind::Nonvolatile);
    std::array<Byte, Mapper432::kWramSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0x5BU);
    }
    imported->set_battery_ram(bytes);
    REQUIRE(
        std::equal(imported->battery_ram().begin(), imported->battery_ram().end(), bytes.begin()));
    const auto before = state(*imported);
    imported->set_battery_ram(std::span<const Byte>(bytes).first(bytes.size() - 1U));
    REQUIRE(state(*imported) == before);
    std::vector<Byte> oversized(bytes.begin(), bytes.end());
    oversized.push_back(0U);
    imported->set_battery_ram(oversized);
    REQUIRE(state(*imported) == before);
    imported->power_on();
    REQUIRE(imported->read_prg(0x6123U) == bytes[0x123U]);
}

TEST_CASE("Mapper 432 decodes every MMC3 alias and preserves ordinary CIRAM routing",
          "[mapper432][decoding][ciram][irq]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        auto wanted = initial;
        switch ((raw >> 13U) & 3U) {
        case 0U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xC6U);
                wanted[kSelectOffset] = 6U;
                wanted[kModeOffset] = 0xC0U;
            } else {
                mapper->write_prg(addr, 0xFFU);
                wanted[kBanksOffset] = 0xFFU;
            }
            break;
        case 1U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 1U);
                wanted[kMirrorOffset] = static_cast<Byte>(MirrorMode::Horizontal);
            } else {
                mapper->write_prg(addr, 0xC0U);
                wanted[kRamFlagsOffset] = 0xC0U;
            }
            break;
        case 2U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xA5U);
                wanted[kReloadOffset] = 0xA5U;
            } else {
                mapper->write_prg(addr, 0U);
                wanted[kIrqOffset] = 1U;
            }
            break;
        default:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(0xE001U, 0U);
                mapper->write_prg(0xC000U, 0U);
                mapper->write_prg(0xC001U, 0U);
                mapper->clock_irq_counter();
                mapper->write_prg(addr, 0U);
            } else {
                mapper->write_prg(addr, 0U);
                wanted[kIrqOffset] = 2U;
            }
            break;
        }
        CAPTURE(raw);
        REQUIRE(state(*mapper) == wanted);
    }

    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_prg(0xA000U, static_cast<Byte>(raw));
        CAPTURE(raw);
        REQUIRE(mapper->mirror_mode() ==
                ((raw & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x2000U, 0x2ABCU, 0x3000U, 0x3EFFU}) {
            REQUIRE_FALSE(mapper->ciram_address(addr));
            if (addr >= 0x2000U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
            }
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    mapper->set_shared_nvram(std::array<Byte, Mapper432::kWramSize>{});
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->write_ppu_register(0x2012U, 0xA5U));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2012U, 0xA7U));
}

TEST_CASE("Mapper 432 retains MMC3 IRQ behavior and has no expansion audio path",
          "[mapper432][irq][audio]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xC000U, static_cast<Byte>(latch));
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        CAPTURE(latch);
        REQUIRE(state(*mapper)[kCounterOffset] == latch);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        for (unsigned count = 0U; count < latch; ++count) {
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->reset();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    outer(*mapper, 0x0FU);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    for (unsigned repeat = 0U; repeat < 3U; ++repeat) {
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    load(*mapper, state(*board()));

    const auto before = state(*mapper);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1234U);
    mapper->observe_ppu_read(0x1234U);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x6000U, 0xA5U);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 432 reset restores registers and preserves only appropriate WRAM and pads",
          "[mapper432][reset][ram][irq][ciram]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        for (const auto ram : {RamKind::None, RamKind::Volatile, RamKind::Nonvolatile}) {
            for (const bool cold : {false, true}) {
                auto mapper =
                    board(submapper, 0x20000UZ, 0x20000UZ, ram, MirrorMode::Horizontal, 0U, 0xA5U);
                outer(*mapper, submapper == 4U ? 0x5FU : 0xFFU);
                mapper->write_prg(0x6000U, 1U);
                for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
                    mapper->write_prg(addr, cell(addr));
                }
                bank(*mapper, 6U, 9U, 0xC0U);
                mapper->write_prg(0xA000U, 0U);
                mapper->write_prg(0xA001U, 0U);
                mapper->write_prg(0xC000U, 0U);
                mapper->write_prg(0xC001U, 0U);
                mapper->write_prg(0xE001U, 0U);
                mapper->clock_irq_counter();
                REQUIRE(mapper->irq_pending());

                if (cold) {
                    mapper->power_on();
                } else {
                    mapper->reset();
                }
                CAPTURE(submapper, ram, cold);
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
                REQUIRE(state(*mapper)[kOuterOffset] == 0U);
                REQUIRE(state(*mapper)[kSolderOffset] == 0U);
                REQUIRE(state(*mapper)[kRamFlagsOffset] == 0x80U);
                REQUIRE(state(*mapper)[kPadOffset] == 0xA5U);
                REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
                REQUIRE(mapper->read_prg(0xC000U) == cell((62UZ * kPrgBankSize) % 0x20000UZ));
                REQUIRE(mapper->read_prg(0xE000U) == cell((63UZ * kPrgBankSize) % 0x20000UZ));
                if (ram != RamKind::None) {
                    const Byte expected = cold && ram == RamKind::Volatile ? 0U : cell(0x6123U);
                    REQUIRE(mapper->read_prg(0x6123U) == expected);
                }
            }
        }
    }
}

TEST_CASE("Mapper 432 bounded snapshots replay all submappers without either ROM",
          "[mapper432][state][irq][ram]") {
    for (Byte submapper = 0U; submapper < 5U; ++submapper) {
        auto mapper = board(
            submapper, 0x20000UZ, 0x20000UZ, RamKind::Nonvolatile, MirrorMode::Vertical, 0U, 0xA5U);
        outer(*mapper, submapper == 4U ? 0x5FU : 0x1FU);
        bank(*mapper, 0U, 0x83U, 0xC0U);
        bank(*mapper, 6U, 0x39U, 0xC0U);
        bank(*mapper, 7U, 0x2AU, 0xC0U);
        mapper->write_prg(0x6122U, 0xA6U);
        mapper->write_prg(0xA000U, 0U);
        mapper->write_prg(0xA001U, 0xC0U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == kStateSize + Mapper432::kWramSize);
        REQUIRE(saved[kIrqOffset] == 6U);
        REQUIRE(saved[kWramOffset + 0x122U] == 0xA6U);
        const auto active_outer = saved[kOuterOffset];

        auto copy = board(submapper,
                          0x20000UZ,
                          0x20000UZ,
                          RamKind::Nonvolatile,
                          MirrorMode::Vertical,
                          0x80U,
                          0xA5U);
        load(*copy, saved);
        CAPTURE(submapper);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
        REQUIRE(copy->irq_pending());
        for (const auto addr : kPrgAddresses) {
            const auto selected =
                expected_prg_bank(addr, submapper, active_outer, 0x39U, 0x2AU, 0xC0U);
            const auto physical = (selected * kPrgBankSize + (addr & 0x1FFFU)) % 0x20000UZ;
            CAPTURE(addr, selected, physical);
            REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
            const auto physical =
                expected_chr_offset(addr, submapper, active_outer, {0x83U}, 0xC0U) % 0x20000UZ;
            CAPTURE(addr, physical);
            REQUIRE(copy->read_chr(addr) == cell(physical, 0xB5U));
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
        }

        auto with_tail = saved;
        with_tail.insert(with_tail.end(), {1U, 2U, 3U});
        copy->power_on();
        StateReader tail_reader(with_tail);
        copy->load_state(tail_reader);
        REQUIRE(tail_reader.ok());
        REQUIRE(tail_reader.remaining() == 3U);
        REQUIRE(state(*copy) == saved);
    }
}

TEST_CASE("Mapper 432 rejects damaged truncated and mismatched states atomically",
          "[mapper432][state][ram]") {
    auto mapper =
        board(2U, 0x20000UZ, 0x20000UZ, RamKind::Nonvolatile, MirrorMode::Vertical, 0U, 0xA5U);
    outer(*mapper, 0x3FU);
    bank(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0x6123U, 0xA6U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
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
    for (std::size_t offset = 0U; offset < kOuterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 13> invalid_fields{{
        {kSolderOffset, 2U},
        {kSelectOffset, 8U},
        {kModeOffset, 1U},
        {kBanksOffset + 6U, 64U},
        {kBanksOffset + 7U, 64U},
        {kMirrorOffset, 2U},
        {kIrqOffset, 8U},
        {kIrqOffset, 4U},
        {kRamFlagsOffset, 1U},
        {kRamFlagsOffset, 0x3FU},
        {kModeOffset, 0x3FU},
        {kPadOffset, 0U},
        {kPadOffset, 0x5AU},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper432>& other, const char* name) {
        CAPTURE(name);
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_submapper =
        board(3U, 0x20000UZ, 0x20000UZ, RamKind::Nonvolatile, MirrorMode::Vertical, 0U, 0xA5U);
    mismatch(wrong_submapper, "submapper");
    auto wrong_prg =
        board(2U, 0x10000UZ, 0x20000UZ, RamKind::Nonvolatile, MirrorMode::Vertical, 0U, 0xA5U);
    mismatch(wrong_prg, "prg");
    auto wrong_chr =
        board(2U, 0x20000UZ, 0x10000UZ, RamKind::Nonvolatile, MirrorMode::Vertical, 0U, 0xA5U);
    mismatch(wrong_chr, "chr");
    auto wrong_mirror =
        board(2U, 0x20000UZ, 0x20000UZ, RamKind::Nonvolatile, MirrorMode::Horizontal, 0U, 0xA5U);
    mismatch(wrong_mirror, "mirror");
    auto wrong_ram =
        board(2U, 0x20000UZ, 0x20000UZ, RamKind::Volatile, MirrorMode::Vertical, 0U, 0xA5U);
    mismatch(wrong_ram, "ram");
    auto no_ram = board(2U, 0x20000UZ, 0x20000UZ, RamKind::None, MirrorMode::Vertical, 0U, 0xA5U);
    mismatch(no_ram, "no ram");
    auto wrong_pad =
        board(2U, 0x20000UZ, 0x20000UZ, RamKind::Nonvolatile, MirrorMode::Vertical, 0U, 0x5AU);
    mismatch(wrong_pad, "pad");

    auto sub1 = board(1U);
    const auto sub1_saved = state(*sub1);
    auto invalid_sub1 = sub1_saved;
    invalid_sub1[kSolderOffset] = 1U;
    StateReader sub1_reader(invalid_sub1);
    sub1->load_state(sub1_reader);
    REQUIRE_FALSE(sub1_reader.ok());
    REQUIRE(state(*sub1) == sub1_saved);

    auto changed = saved;
    changed[kOuterOffset] = 0x3EU;
    changed[kWramOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
