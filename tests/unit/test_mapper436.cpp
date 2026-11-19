#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper436.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMaxPrgSize = 0x2000000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kControlOffset = 11UZ;
constexpr std::size_t kMirrorOffset = 12UZ;
constexpr std::size_t kPrgOffset = 13UZ;
constexpr std::size_t kModeOffset = 17UZ;
constexpr std::size_t kChrOffset = 18UZ;
constexpr std::size_t kIntermediateOffset = 24UZ;
constexpr std::size_t kChrMiddleOffset = 25UZ;
constexpr std::size_t kLatchOffset = 26UZ;
constexpr std::size_t kIrqOffset = 28UZ;
constexpr std::size_t kPinsOffset = 29UZ;
constexpr std::size_t kIoOffset = 30UZ;
constexpr std::size_t kStateSize = 31UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 436U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U +
                             (offset >> 23U) * 101U + (offset >> 24U) * 107U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper436> board(std::size_t prg_size = 0x20000UZ,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper436::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper436>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper436& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void edge(Mapper436& mapper, unsigned low_cycles = 3U) {
    mapper.observe_ppu_a12(false);
    for (unsigned cycle = 0U; cycle < low_cycles; ++cycle) {
        mapper.clock_cpu();
    }
    mapper.observe_ppu_a12(true);
}

std::size_t expected_prg(Address addr,
                         Byte control,
                         const std::array<Byte, 3>& prg,
                         Byte middle,
                         Byte mode,
                         Byte outer,
                         Byte io_port) {
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
    const unsigned ordinary = (inner & mask) | (middle & (255U ^ mask)) | ((outer >> 4U) << 8U);
    const unsigned bank =
        (ordinary & ~0xC00U) | ((io_port & 0x20U) << 5U) | ((outer & 0x40U) << 5U);
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t expected_chr(Address addr,
                         Byte control,
                         const std::array<Byte, 6>& chr,
                         Byte intermediate,
                         Byte middle,
                         Byte outer,
                         Byte io_port) {
    auto slot = static_cast<unsigned>((addr & (kChrBankSize * 8U - 1U)) / kChrBankSize);
    if ((control & 0x80U) != 0U) {
        slot ^= 4U;
    }
    const unsigned inner = slot < 4U ? (chr[4U + slot / 2U] & 254U) | (slot & 1U) : chr[slot - 4U];
    constexpr std::array<unsigned, 8> masks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};
    const unsigned mask = masks[middle & 7U];
    const unsigned ordinary = (inner & mask) | (middle & (255U ^ mask) & 248U) |
                              ((intermediate >> 4U) << 8U) | ((outer & 15U) << 11U);
    const unsigned bank =
        (ordinary & ~0x6000U) | ((io_port & 0x20U) << 8U) | ((outer & 0x04U) << 12U);
    return static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U));
}

TEST_CASE("Mapper 436 validates the bounded shared-ROM NES 2.0 geometry",
          "[mapper436][geometry][ram]") {
    for (std::size_t size = 0x8000UZ; size <= kMaxPrgSize; size *= 2U) {
        CAPTURE(size);
        REQUIRE(Mapper436::validate_geometry(header(), size, 0U));
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(0x20000UZ, mirror);
        auto identical_state = board(0x20000UZ, mirror, 0x80U);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper) == state(*identical_state));
        REQUIRE(state(*mapper)[kIoOffset] == 0x20U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    auto full = board(kMaxPrgSize);
    REQUIRE(full->read_prg(0xFFFCU) == cell(0x0087FFFCUZ));
    REQUIRE(full->read_prg(0xFFFDU) == cell(0x0087FFFDUZ));
    REQUIRE(full->read_prg(0xFFFEU) == cell(0x0087FFFEUZ));
    REQUIRE(full->read_prg(0xFFFFU) == cell(0x0087FFFFUZ));

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
        REQUIRE_FALSE(Mapper436::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 256U, 407U, 435U, 437U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, 0x20000UZ, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, 0x20000UZ, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, 0x20000UZ, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x18000UZ, kMaxPrgSize + 1U, kMaxPrgSize * 2U}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, 0x10000UZ, kMaxPrgSize}) {
        reject(header(), 0x20000UZ, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), 0x20000UZ, 0U);
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
        reject(h, 0x20000UZ, 0U);
    }
}

TEST_CASE("Mapper 436 decodes write-only native CPU and PPU register apertures",
          "[mapper436][decoding][bus]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        const bool system = raw >= 0x4100U && raw <= 0x410BU;
        const bool decoded = system || raw == 0x410FU;
        mapper->write_expansion(addr, value);
        auto wanted = initial;
        if (raw == 0x4100U) {
            wanted[kOuterOffset] = value;
        } else if (raw == 0x4101U) {
            wanted[kLatchOffset] = value;
        } else if (raw == 0x4104U) {
            wanted[kIrqOffset] = 1U;
        } else if (raw == 0x4105U) {
            wanted[kControlOffset] = value & 0xE7U;
        } else if (raw == 0x4106U) {
            wanted[kMirrorOffset] = value & 1U;
        } else if (raw >= 0x4107U && raw <= 0x4109U) {
            wanted[kPrgOffset + raw - 0x4107U] = value;
        } else if (raw == 0x410AU) {
            wanted[16U] = value;
        } else if (raw == 0x410BU) {
            wanted[kModeOffset] = value & 0xCFU;
        } else if (raw == 0x410FU) {
            wanted[kIoOffset] = value & 0x20U;
        }
        CAPTURE(raw, value, decoded);
        REQUIRE(state(*mapper) == wanted);
        REQUIRE(mapper->maps_expansion(addr) == decoded);
        REQUIRE(mapper->expansion_read_mask(addr) == (decoded ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }

    for (unsigned raw = 0x2000U; raw < 0x4000U; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const bool native = raw >= 0x2010U && raw <= 0x201FU;
        REQUIRE(mapper->write_ppu_register(addr, 0xD5U) == native);
        REQUIRE(mapper->read_ppu_register(addr, 0xA7U) ==
                (native ? std::optional<Byte>(0xA7U) : std::nullopt));
        auto wanted = initial;
        if (raw >= 0x2012U && raw <= 0x2017U) {
            wanted[kChrOffset + raw - 0x2012U] = 0xD5U;
        } else if (raw == 0x2018U) {
            wanted[kIntermediateOffset] = 0x50U;
        } else if (raw == 0x201AU) {
            wanted[kChrMiddleOffset] = 0xD5U;
        }
        CAPTURE(raw, native);
        REQUIRE(state(*mapper) == wanted);
    }

    const auto before = state(*mapper);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 436 forwards every MMC3 alias and gates only forwarded writes",
          "[mapper436][decoding][banking][bus]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw), 0xA3U);
        auto wanted = initial;
        if (raw >= 0x8000U) {
            switch (raw & 0xE001U) {
            case 0x8000U:
                wanted[kControlOffset] = 0x83U;
                break;
            case 0x8001U:
                wanted[kChrOffset + 4U] = 0xA3U;
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
        CAPTURE(raw);
        REQUIRE(state(*mapper) == wanted);
    }
    for (unsigned index = 0U; index < 8U; ++index) {
        mapper->reset();
        mapper->write_prg(0x8000U, static_cast<Byte>(index));
        mapper->write_prg(0x9FFFU, 0xFFU);
        const auto saved = state(*mapper);
        CAPTURE(index);
        REQUIRE(saved[index < 6U ? (index < 2U ? kChrOffset + 4U + index : kChrOffset + index - 2U)
                                 : kPrgOffset + index - 6U] == 0xFFU);
    }
    mapper->reset();
    mapper->write_expansion(0x4105U, 0x20U);
    mapper->write_prg(0x8000U, 0xDFU);
    REQUIRE(state(*mapper)[kControlOffset] == 0xE7U);
    mapper->write_expansion(0x410BU, 8U);
    const auto disabled = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
        REQUIRE(state(*mapper) == disabled);
    }
    mapper->write_expansion(0x4107U, 9U);
    mapper->write_expansion(0x410FU, 0U);
    REQUIRE(state(*mapper)[kPrgOffset] == 9U);
    REQUIRE(state(*mapper)[kIoOffset] == 0U);
}

TEST_CASE("Mapper 436 composes OneBus PRG banking before A23/A24 rewiring and wrapping",
          "[mapper436][banking][prg][bus]") {
    auto mapper = board(kMaxPrgSize);
    for (Byte mode = 0U; mode < 8U; ++mode) {
        for (const Byte outer :
             {Byte{0U}, Byte{0x10U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}, Byte{0xF0U}}) {
            for (const Byte middle : {Byte{0U}, Byte{0x55U}, Byte{0xAAU}, Byte{0xFFU}}) {
                for (const bool third : {false, true}) {
                    for (const bool swap : {false, true}) {
                        for (const Byte io_port : {Byte{0U}, Byte{0x20U}}) {
                            for (const Byte raw :
                                 {Byte{0U}, Byte{1U}, Byte{0x3FU}, Byte{0x55U}, Byte{0xFFU}}) {
                                const auto control = static_cast<Byte>(swap ? 0x40U : 0U);
                                const auto full_mode =
                                    static_cast<Byte>(mode | (third ? 0x40U : 0U));
                                const std::array<Byte, 3> banks{raw, 0x59U, 0xA6U};
                                mapper->write_expansion(0x4100U, outer);
                                mapper->write_expansion(0x4105U, control);
                                mapper->write_expansion(0x4107U, banks[0]);
                                mapper->write_expansion(0x4108U, banks[1]);
                                mapper->write_expansion(0x4109U, banks[2]);
                                mapper->write_expansion(0x410AU, middle);
                                mapper->write_expansion(0x410BU, full_mode);
                                mapper->write_expansion(0x410FU, io_port);
                                for (const auto addr : kPrgAddresses) {
                                    const auto physical = expected_prg(
                                        addr, control, banks, middle, full_mode, outer, io_port);
                                    CAPTURE(mode,
                                            outer,
                                            middle,
                                            third,
                                            swap,
                                            io_port,
                                            raw,
                                            addr,
                                            physical);
                                    REQUIRE(mapper->maps_prg(addr));
                                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    for (unsigned outer = 0U; outer < 256U; ++outer) {
        for (const Byte io_port : {Byte{0U}, Byte{0x20U}, Byte{0xFFU}}) {
            const auto raw_outer = static_cast<Byte>(outer);
            mapper->reset();
            mapper->write_expansion(0x4100U, raw_outer);
            mapper->write_expansion(0x410FU, io_port);
            const auto normalized_io = static_cast<Byte>(io_port & 0x20U);
            const auto physical =
                expected_prg(0xFFFFU, 0U, {0U, 1U, 0xFEU}, 0U, 0U, raw_outer, normalized_io);
            CAPTURE(outer, io_port, physical);
            REQUIRE(mapper->read_prg(0xFFFFU) == cell(physical));
            REQUIRE(((physical >> 23U) & 1U) == (normalized_io != 0U ? 1UZ : 0UZ));
            REQUIRE(((physical >> 24U) & 1U) == ((outer & 0x40U) != 0U ? 1UZ : 0UZ));
        }
    }

    auto fitted = board(0x8000UZ);
    fitted->write_expansion(0x4100U, 0xF0U);
    fitted->write_expansion(0x4107U, 0xFFU);
    fitted->write_expansion(0x4109U, 0xFEU);
    fitted->write_expansion(0x410AU, 0xFFU);
    fitted->write_expansion(0x410BU, 0x47U);
    fitted->write_expansion(0x410FU, 0U);
    for (const auto addr : kPrgAddresses) {
        const auto physical =
            expected_prg(addr, 0U, {0xFFU, 1U, 0xFEU}, 0xFFU, 0x47U, 0xF0U, 0U) & 0x7FFFU;
        CAPTURE(addr, physical);
        REQUIRE(fitted->read_prg(addr) == cell(physical));
    }
}

TEST_CASE("Mapper 436 routes shared CHR through its independent A24 source and I/O A23",
          "[mapper436][banking][chr][bus]") {
    auto mapper = board(kMaxPrgSize);
    for (Byte mode = 0U; mode < 8U; ++mode) {
        for (const Byte outer :
             {Byte{0U}, Byte{1U}, Byte{4U}, Byte{8U}, Byte{0x0CU}, Byte{0x0FU}}) {
            for (const Byte intermediate : {Byte{0U}, Byte{0x10U}, Byte{0x70U}}) {
                for (const bool flip : {false, true}) {
                    for (const Byte io_port : {Byte{0U}, Byte{0x20U}}) {
                        for (const Byte value : {Byte{0U},
                                                 Byte{1U},
                                                 Byte{0x1FU},
                                                 Byte{0x55U},
                                                 Byte{0xAAU},
                                                 Byte{0xFFU}}) {
                            const auto control = static_cast<Byte>(flip ? 0x80U : 0U);
                            const auto middle = static_cast<Byte>((value & 248U) | mode);
                            std::array<Byte, 6> banks{};
                            mapper->write_expansion(0x4100U, outer);
                            mapper->write_expansion(0x4105U, control);
                            mapper->write_expansion(0x410FU, io_port);
                            mapper->write_ppu_register(0x2018U, intermediate);
                            mapper->write_ppu_register(0x201AU, middle);
                            for (unsigned index = 0U; index < banks.size(); ++index) {
                                banks[index] = static_cast<Byte>(value + index * 17U);
                                mapper->write_ppu_register(static_cast<Address>(0x2012U + index),
                                                           banks[index]);
                            }
                            for (unsigned slot = 0U; slot < 8U; ++slot) {
                                const auto addr =
                                    static_cast<Address>(slot * kChrBankSize + 0x123U);
                                const auto physical = expected_chr(
                                    addr, control, banks, intermediate, middle, outer, io_port);
                                CAPTURE(mode,
                                        outer,
                                        intermediate,
                                        flip,
                                        io_port,
                                        value,
                                        slot,
                                        physical);
                                REQUIRE(mapper->read_chr(addr) == cell(physical));
                                REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                                        cell(physical));
                                mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                                REQUIRE(mapper->read_chr(addr) == cell(physical));
                            }
                        }
                    }
                }
            }
        }
    }

    for (unsigned outer = 0U; outer < 16U; ++outer) {
        for (const Byte io_port : {Byte{0U}, Byte{0x20U}, Byte{0xDFU}}) {
            const auto raw_outer = static_cast<Byte>(outer);
            mapper->write_expansion(0x4100U, raw_outer);
            mapper->write_expansion(0x410FU, io_port);
            mapper->write_expansion(0x4105U, 0U);
            mapper->write_ppu_register(0x2012U, 0U);
            mapper->write_ppu_register(0x2018U, 0U);
            mapper->write_ppu_register(0x201AU, 0U);
            const auto normalized_io = static_cast<Byte>(io_port & 0x20U);
            const auto physical = expected_chr(
                0x1000U, 0U, {0U, 5U, 6U, 7U, 0U, 2U}, 0U, 0U, raw_outer, normalized_io);
            CAPTURE(outer, io_port, physical);
            REQUIRE(mapper->read_chr(0x1000U) == cell(physical));
            REQUIRE(((physical >> 23U) & 1U) == (normalized_io != 0U ? 1UZ : 0UZ));
            REQUIRE(((physical >> 24U) & 1U) == ((outer & 4U) != 0U ? 1UZ : 0UZ));
        }
    }

    auto fitted = board(0x8000UZ);
    fitted->write_expansion(0x4100U, 0x0FU);
    fitted->write_expansion(0x410FU, 0x20U);
    fitted->write_ppu_register(0x2012U, 0xFFU);
    fitted->write_ppu_register(0x2018U, 0x70U);
    fitted->write_ppu_register(0x201AU, 0xF8U);
    const auto physical =
        expected_chr(0x1123U, 0U, {0xFFU, 5U, 6U, 7U, 0U, 2U}, 0x70U, 0xF8U, 0x0FU, 0x20U) &
        0x7FFFU;
    REQUIRE(fitted->read_chr(0x1123U) == cell(physical));
}

TEST_CASE("Mapper 436 controls H/V CIRAM and pattern-to-CIRAM routing without cartridge RAM",
          "[mapper436][ciram][ram][bus]") {
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
                                      ? std::optional<Address>(addr & 0x7FFU)
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
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
}

TEST_CASE("Mapper 436 clocks native IRQ from qualified A12 or HSYNC and exposes no audio",
          "[mapper436][irq][audio]") {
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
    mapper->write_expansion(0x4101U, 2U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    mapper->observe_ppu_a12(false);
    mapper->clock_cpu();
    mapper->clock_cpu();
    const auto partial = state(*mapper);
    mapper->reset();
    load(*mapper, partial);
    mapper->clock_cpu();
    mapper->observe_ppu_a12(true);
    REQUIRE_FALSE(mapper->irq_pending());
    edge(*mapper);
    REQUIRE(mapper->irq_pending());

    mapper->write_expansion(0x4103U, 0U);
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

    const auto before = state(*mapper);
    mapper->clock_irq_counter();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->on_ppu_frame_start();
    mapper->clock_cpu_falling_edge();
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 436 reset restores native defaults and the pulled-up A23 input",
          "[mapper436][reset][banking][ciram][irq]") {
    auto mapper = board(kMaxPrgSize, MirrorMode::Horizontal);
    const auto cold = state(*mapper);
    mapper->write_expansion(0x4100U, 0xF0U);
    mapper->write_expansion(0x4105U, 0xE7U);
    mapper->write_expansion(0x4107U, 0x92U);
    mapper->write_ppu_register(0x2012U, 0x11U);
    mapper->write_expansion(0x410FU, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIoOffset] == 0U);

    mapper->reset();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(state(*mapper)[kIoOffset] == 0x20U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x0087FFFFUZ));
    REQUIRE(mapper->ciram_address(0x0123U) == std::nullopt);

    mapper->write_expansion(0x4100U, 0x40U);
    mapper->write_expansion(0x410FU, 0U);
    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x0087FFFFUZ));
}

TEST_CASE("Mapper 436 bounded snapshots replay all mutable state without ROM",
          "[mapper436][state][banking][irq]") {
    auto mapper = board(0x20000UZ, MirrorMode::Vertical, 0U);
    mapper->write_expansion(0x4100U, 0xC7U);
    mapper->write_expansion(0x4105U, 0x60U);
    mapper->write_expansion(0x4107U, 0x95U);
    mapper->write_expansion(0x410BU, 0x40U);
    mapper->write_ppu_register(0x2012U, 0xFFU);
    mapper->write_expansion(0x410FU, 0U);
    mapper->write_expansion(0x4101U, 2U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    mapper->observe_ppu_a12(false);
    mapper->clock_cpu();
    mapper->clock_cpu();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x20000UZ);
    REQUIRE(saved[kOuterOffset] == 0xC7U);
    REQUIRE(saved[kControlOffset] == 0x60U);
    REQUIRE(saved[kPrgOffset] == 0x95U);
    REQUIRE(saved[kPinsOffset] == 4U);
    REQUIRE(saved[kIoOffset] == 0U);

    auto copy = board(0x20000UZ, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->ciram_address(0x0123U) == mapper->ciram_address(0x0123U));
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0x60U, {0x95U, 1U, 0xFEU}, 0U, 0x40U, 0xC7U, 0U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical & 0x1FFFFU, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical & 0x1FFFFU));
    }
    for (const Address addr : {0x0000U, 0x0400U, 0x1000U, 0x1123U, 0x1FFFU}) {
        const auto physical =
            expected_chr(addr, 0x60U, {0xFFU, 5U, 6U, 7U, 0U, 2U}, 0U, 0U, 0xC7U, 0U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical & 0x1FFFFU, 0x80U));
        REQUIRE(mapper->read_chr(addr) == cell(physical & 0x1FFFFU));
    }
    copy->clock_cpu();
    copy->observe_ppu_a12(true);
    REQUIRE_FALSE(copy->irq_pending());
    edge(*copy);
    REQUIRE(copy->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->reset();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 436 rejects truncated damaged and mismatched states atomically",
          "[mapper436][state]") {
    auto mapper = board();
    mapper->write_expansion(0x4100U, 0xC7U);
    mapper->write_expansion(0x4105U, 0x60U);
    mapper->write_expansion(0x4107U, 0x95U);
    mapper->write_expansion(0x410FU, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
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
    for (unsigned bit = 0U; bit < kOuterOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        invalid[kOuterOffset] ^= 0xFFU;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const auto& [offset, value] : std::array<std::pair<std::size_t, Byte>, 14>{{
             {kControlOffset, 0x08U},
             {kControlOffset, 0x18U},
             {kMirrorOffset, 2U},
             {kMirrorOffset, 0xFFU},
             {kModeOffset, 0x10U},
             {kModeOffset, 0x30U},
             {kIntermediateOffset, 0x01U},
             {kIntermediateOffset, 0x80U},
             {kIrqOffset, 2U},
             {kIrqOffset, 4U},
             {kPinsOffset, 3U},
             {kPinsOffset, 8U},
             {kIoOffset, 1U},
             {kIoOffset, 0xFFU},
         }}) {
        auto invalid = saved;
        invalid[kOuterOffset] ^= 0xFFU;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = board(0x10000UZ);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto changed = saved;
    changed[kOuterOffset] = 0x44U;
    changed[kIoOffset] = 0x20U;
    changed[kControlOffset] = 0U;
    changed[kMirrorOffset] = 1U;
    changed[kIrqOffset] = 0U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0xFFFFU) ==
            cell(expected_prg(0xFFFFU, 0U, {0x95U, 1U, 0xFEU}, 0U, 0U, 0x44U, 0x20U) & 0x1FFFFU));
}

} // namespace
