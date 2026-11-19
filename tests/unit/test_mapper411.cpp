#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper411.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kStateSize = 31UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 411U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper411> board(std::size_t prg_size = 0x20000UZ,
                                 std::size_t chr_size = 0x20000UZ,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper411::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper411>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x35U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    return {bytes.begin() + static_cast<std::ptrdiff_t>(kGeometrySize), bytes.end()};
}

void load(Mapper411& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void outer(Mapper411& mapper, Byte prg, Byte banks) {
    mapper.write_expansion(0x5FF0U, prg);
    mapper.write_expansion(0x5FF1U, banks);
}

void bank(Mapper411& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned mmc3_prg_bank(Address addr, Byte r6, Byte r7, Byte mode) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    return slot == 1U                               ? r7 & 0x3FU
           : slot == 3U                             ? 63U
           : (slot == 0U) == ((mode & 0x40U) != 0U) ? 62U
                                                    : r6 & 0x3FU;
}

unsigned expected_prg_bank(Address addr, Byte r6, Byte r7, Byte mode, Byte outer1, Byte outer2) {
    const unsigned raw = mmc3_prg_bank(addr, r6, r7, mode);
    const bool inner_256k = (outer2 & 2U) != 0U;
    const unsigned a17 = inner_256k ? (raw >> 4U) & 1U : (outer2 >> 3U) & 1U;
    const unsigned a18 = (outer2 >> 6U) & 1U;
    if ((outer1 & 0x40U) == 0U) {
        return (raw & (inner_256k ? 0x1FU : 0x0FU)) | (a17 << 4U) | (a18 << 5U);
    }
    const bool nrom_256 = (outer1 & 2U) != 0U;
    const unsigned a13 = (addr >> 13U) & 1U;
    const unsigned a14 = nrom_256 ? (addr >> 14U) & 1U : outer1 & 1U;
    return a13 | (a14 << 1U) | ((outer1 >> 2U) & 1U) << 2U | ((outer1 >> 3U) & 1U) << 3U |
           (a17 << 4U) | (a18 << 5U);
}

Byte raw_chr_bank(Address addr, const std::array<Byte, 8>& banks, Byte mode) {
    const unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    return slot < 4U ? static_cast<Byte>((banks[slot / 2U] & 0xFEU) | (slot & 1U))
                     : banks[slot - 2U];
}

unsigned expected_chr_bank(
    Address addr, const std::array<Byte, 8>& banks, Byte mode, Byte outer1, Byte outer2) {
    const unsigned raw = raw_chr_bank(addr, banks, mode);
    const bool inner_256k = (outer2 & 2U) != 0U;
    const unsigned a17 = inner_256k ? (raw >> 7U) & 1U : (outer2 >> 2U) & 1U;
    return (raw & 0x7FU) | (a17 << 7U) | (((outer1 >> 4U) & 1U) << 8U);
}

TEST_CASE("Mapper 411 validates A88S-1 geometry and documented power-on state",
          "[mapper411][geometry][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize, kMinChrSize, mirror);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(register_state(*mapper).size() == 17U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(2UZ * kPrgBankSize));
        REQUIRE(mapper->read_prg(0xE000U) == cell(3UZ * kPrgBankSize));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        auto other_rom = board(kMinPrgSize, kMinChrSize, mirror, 0x80U);
        REQUIRE(state(*other_rom) == state(*mapper));
    }
    for (const std::size_t prg : {kMinPrgSize, 0xA000UZ, 0x18000UZ, 0x80000UZ}) {
        for (const std::size_t chr : {kMinChrSize, 0x2400UZ, 0x6000UZ, kMaxChrSize}) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper411::validate_geometry(header(), prg, chr));
        }
    }

    for (const unsigned id : {0U, 4U, 45U, 256U, 410U, 412U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper411::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper411::validate_geometry(legacy, kMinPrgSize, kMinChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        CAPTURE(submapper);
        REQUIRE_FALSE(Mapper411::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, kMaxPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper411::validate_geometry(header(), size, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x400UZ, 0x1FFFUZ, 0x2001UZ, kMaxChrSize + kChrBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper411::validate_geometry(header(), kMinPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper411::validate_geometry(header(mirror), kMinPrgSize, kMinChrSize));
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
        REQUIRE_FALSE(Mapper411::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
}

TEST_CASE("Mapper 411 uses only the probable F001 write-only outer decoder",
          "[mapper411][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const bool decoded = (raw & 0xF001U) == 0x5000U || (raw & 0xF001U) == 0x5001U;
        auto wanted = register_state(*mapper);
        REQUIRE(mapper->maps_expansion(addr) == decoded);
        REQUIRE(mapper->expansion_read_mask(addr) == (decoded ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xFFU);
        if (decoded) {
            wanted[15U + (raw & 1U)] = (raw & 1U) == 0U ? 0xDFU : 0xCEU;
        }
        CAPTURE(raw, decoded);
        REQUIRE(register_state(*mapper) == wanted);
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        outer(*mapper, static_cast<Byte>(value), static_cast<Byte>(value));
        const auto bytes = register_state(*mapper);
        CAPTURE(value);
        REQUIRE(bytes[15U] == static_cast<Byte>((value & 0xDFU) | 0x80U));
        REQUIRE(bytes[16U] == static_cast<Byte>((value & 0xCEU) | 0x80U));
    }

    mapper->reset();
    const auto before = state(*mapper);
    for (const Address addr :
         {0x0000U, 0x401FU, 0x4020U, 0x5FEFU, 0x5FF0U, 0x5FF1U, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->write_ppu_register(0x2012U, 0xA5U));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2012U, 0xA7U));
}

TEST_CASE("Mapper 411 composes MMC3 PRG inner A17 and outer A18 before wrapping",
          "[mapper411][decoding][banking][bus]") {
    auto mapper = board(kMaxPrgSize, kMinChrSize);
    for (const Byte outer1 : {Byte{0x80U}, Byte{0x90U}}) {
        for (const Byte outer2 : {Byte{0x80U},
                                  Byte{0x82U},
                                  Byte{0x88U},
                                  Byte{0x8AU},
                                  Byte{0xC0U},
                                  Byte{0xC2U},
                                  Byte{0xC8U},
                                  Byte{0xCAU}}) {
            outer(*mapper, outer1, outer2);
            for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
                for (unsigned raw = 0U; raw < 256U; ++raw) {
                    const auto r6 = static_cast<Byte>(raw);
                    const auto r7 = static_cast<Byte>(raw ^ 0x35U);
                    bank(*mapper, 6U, r6, mode);
                    bank(*mapper, 7U, r7, mode);
                    for (const auto addr : kPrgAddresses) {
                        const auto selected = expected_prg_bank(addr, r6, r7, mode, outer1, outer2);
                        const auto physical =
                            selected * kPrgBankSize + (addr & (kPrgBankSize - 1U));
                        CAPTURE(outer1, outer2, mode, raw, addr, selected, physical);
                        REQUIRE(mapper->maps_prg(addr));
                        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                        REQUIRE(mapper->read_prg(addr) == cell(physical));
                    }
                }
            }
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x18000UZ}) {
        auto fitted = board(size, kMinChrSize);
        outer(*fitted, 0x80U, 0xCAU);
        bank(*fitted, 6U, 0x2AU);
        for (const auto addr : kPrgAddresses) {
            const auto selected = expected_prg_bank(addr, 0x2AU, 0U, 0U, 0x80U, 0xCAU);
            const auto physical = (selected * kPrgBankSize + (addr & (kPrgBankSize - 1U))) % size;
            CAPTURE(size, addr, selected, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 411 NROM mode wires CPU A13 A14 A-C and retained A17-A18",
          "[mapper411][decoding][banking][bus]") {
    auto mapper = board(kMaxPrgSize, kMinChrSize);
    for (unsigned control = 0U; control < 256U; ++control) {
        const auto outer1 = static_cast<Byte>((control & 0xDFU) | 0xC0U);
        for (const Byte outer2 : {Byte{0x80U},
                                  Byte{0x82U},
                                  Byte{0x88U},
                                  Byte{0x8AU},
                                  Byte{0xC0U},
                                  Byte{0xC2U},
                                  Byte{0xC8U},
                                  Byte{0xCAU}}) {
            outer(*mapper, outer1, outer2);
            for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
                const auto r6 = static_cast<Byte>(control ^ 0x35U);
                const auto r7 = static_cast<Byte>(control * 73U);
                bank(*mapper, 6U, r6, mode);
                bank(*mapper, 7U, r7, mode);
                for (const auto addr : kPrgAddresses) {
                    const auto selected = expected_prg_bank(addr, r6, r7, mode, outer1, outer2);
                    const auto physical = selected * kPrgBankSize + (addr & (kPrgBankSize - 1U));
                    CAPTURE(control, outer1, outer2, mode, addr, selected, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0xA000UZ, 0x18000UZ}) {
        auto fitted = board(size, kMinChrSize);
        outer(*fitted, 0xFFU, 0xFFU);
        bank(*fitted, 6U, 0xFFU, 0x40U);
        for (const auto addr : kPrgAddresses) {
            const auto selected = expected_prg_bank(addr, 0x3FU, 0U, 0x40U, 0xDFU, 0xCEU);
            const auto physical = (selected * kPrgBankSize + (addr & (kPrgBankSize - 1U))) % size;
            CAPTURE(size, addr, selected, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 411 applies CHR pairing inversion inner A17 and outer A17-A18",
          "[mapper411][decoding][banking][chr]") {
    auto mapper = board(kMinPrgSize, kMaxChrSize);
    for (const Byte outer1 : {Byte{0x80U}, Byte{0x90U}, Byte{0xD0U}, Byte{0xF0U}}) {
        for (const Byte outer2 : {Byte{0x80U}, Byte{0x82U}, Byte{0x84U}, Byte{0x86U}}) {
            outer(*mapper, outer1, outer2);
            for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
                for (unsigned raw = 0U; raw < 256U; ++raw) {
                    std::array<Byte, 8> banks{};
                    for (Byte index = 0U; index < 6U; ++index) {
                        banks[index] = static_cast<Byte>(raw + index * 37U);
                        bank(*mapper, index, banks[index], mode);
                    }
                    for (unsigned slot = 0U; slot < 8U; ++slot) {
                        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
                        const auto selected = expected_chr_bank(addr, banks, mode, outer1, outer2);
                        const auto physical = selected * kChrBankSize + 0x123U;
                        CAPTURE(outer1, outer2, mode, raw, slot, selected, physical);
                        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                        mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                    }
                }
            }
        }
    }

    for (const std::size_t size : {kMinChrSize, 0x6000UZ}) {
        auto fitted = board(kMinPrgSize, size);
        outer(*fitted, 0x90U, 0x86U);
        bank(*fitted, 2U, 0xB4U);
        const auto selected = expected_chr_bank(0x1123U, {0U, 0U, 0xB4U}, 0U, 0x90U, 0x86U);
        const auto physical = (selected * kChrBankSize + 0x123U) % size;
        CAPTURE(size, selected, physical);
        REQUIRE(fitted->read_chr(0x1123U) == cell(physical, 0x35U));
    }
}

TEST_CASE("Mapper 411 decodes every MMC3 alias and ordinary H/V CIRAM without RAM",
          "[mapper411][decoding][ciram][ram][irq]") {
    auto mapper = board();
    const auto initial = register_state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        auto wanted = initial;
        switch ((raw >> 13U) & 3U) {
        case 0U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xC6U);
                wanted[0U] = 6U;
                wanted[1U] = 0xC0U;
            } else {
                mapper->write_prg(addr, 0xFFU);
                wanted[2U] = 0xFFU;
            }
            break;
        case 1U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 1U);
                wanted[10U] = static_cast<Byte>(MirrorMode::Horizontal);
            } else {
                mapper->write_prg(addr, 0xC0U);
                wanted[14U] = 0xC0U;
            }
            break;
        case 2U:
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xA5U);
                wanted[12U] = 0xA5U;
            } else {
                mapper->write_prg(addr, 0U);
                wanted[13U] = 1U;
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
                wanted[13U] = 2U;
            }
            break;
        }
        CAPTURE(raw);
        REQUIRE(register_state(*mapper) == wanted);
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
    mapper->set_battery_ram(std::array<Byte, 0x2000U>{});
    mapper->set_shared_nvram(std::array<Byte, 0x2000U>{});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
}

TEST_CASE("Mapper 411 retains MMC3 IRQ behavior and has no expansion audio path",
          "[mapper411][irq][audio]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xC000U, static_cast<Byte>(latch));
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        CAPTURE(latch);
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
    outer(*mapper, 0xD7U, 0xCAU);
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

    const auto before = register_state(*mapper);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1234U);
    mapper->observe_ppu_read(0x1234U);
    REQUIRE(register_state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 411 reset restores documented outer values and MMC3 defaults",
          "[mapper411][reset][irq][ciram]") {
    for (const bool cold : {false, true}) {
        auto mapper = board(0x20000UZ, 0x20000UZ, MirrorMode::Horizontal);
        outer(*mapper, 0xFFU, 0xFFU);
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
        CAPTURE(cold);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(14UZ * kPrgBankSize));
        REQUIRE(mapper->read_prg(0xE000U) == cell(15UZ * kPrgBankSize));
        const auto bytes = register_state(*mapper);
        REQUIRE(bytes[15U] == 0x80U);
        REQUIRE(bytes[16U] == 0x82U);
        REQUIRE(bytes[14U] == 0x80U);
    }
}

TEST_CASE("Mapper 411 bounded snapshots replay every register without either ROM",
          "[mapper411][state][irq]") {
    auto mapper = board();
    outer(*mapper, 0xD7U, 0xCAU);
    bank(*mapper, 0U, 0x83U, 0xC0U);
    bank(*mapper, 6U, 0x39U, 0xC0U);
    bank(*mapper, 7U, 0x2AU, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kGeometrySize + 13U] == 6U);
    REQUIRE(saved[kGeometrySize + 15U] == 0xD7U);
    REQUIRE(saved[kGeometrySize + 16U] == 0xCAU);

    auto copy = board(0x20000UZ, 0x20000UZ, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    REQUIRE(copy->irq_pending());
    for (const auto addr : kPrgAddresses) {
        const auto selected = expected_prg_bank(addr, 0x39U, 0x2AU, 0xC0U, 0xD7U, 0xCAU);
        const auto physical = (selected * kPrgBankSize + (addr & (kPrgBankSize - 1U))) % 0x20000UZ;
        CAPTURE(addr, selected, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
        const auto selected = expected_chr_bank(addr, {0x83U}, 0xC0U, 0xD7U, 0xCAU);
        const auto physical = (selected * kChrBankSize + 0x123U) % 0x20000UZ;
        CAPTURE(addr, selected, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0xB5U));
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

TEST_CASE("Mapper 411 rejects damaged truncated and mismatched states atomically",
          "[mapper411][state]") {
    auto mapper = board();
    outer(*mapper, 0xD7U, 0xCAU);
    bank(*mapper, 6U, 9U, 0xC0U);
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
    for (std::size_t offset = 0U; offset < kGeometrySize; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 15> invalid_fields{{
        {0U, 8U},
        {1U, 1U},
        {8U, 64U},
        {9U, 64U},
        {10U, 2U},
        {13U, 8U},
        {13U, 4U},
        {14U, 1U},
        {14U, 0x3FU},
        {15U, 0U},
        {15U, 0xA0U},
        {16U, 0U},
        {16U, 0x81U},
        {16U, 0x90U},
        {16U, 0xB0U},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[kGeometrySize + offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = board(0x10000UZ);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(0x20000UZ, 0x10000UZ);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_mirror = board(0x20000UZ, 0x20000UZ, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto changed = saved;
    changed[kGeometrySize + 15U] = 0x9FU;
    changed[kGeometrySize + 16U] = 0x8EU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
