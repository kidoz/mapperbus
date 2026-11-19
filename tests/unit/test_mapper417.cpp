#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper417.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kPrgBanksOffset = 14UZ;
constexpr std::size_t kChrBanksOffset = 17UZ;
constexpr std::size_t kNametableBanksOffset = 25UZ;
constexpr std::size_t kIrqCounterOffset = 29UZ;
constexpr std::size_t kIrqFlagsOffset = 31UZ;
constexpr std::size_t kStateSize = 32UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 417U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 47U +
                             (offset >> 13U) * 59U + (offset >> 16U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper417> board(Byte submapper = 0U,
                                 std::size_t prg_size = 0x040000UZ,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper417::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper417>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x35U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper417& mapper, std::span<const Byte> bytes) {
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

void write_u16(std::span<Byte> bytes, std::size_t offset, std::uint16_t value) {
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

std::size_t prg_offset(Address addr, Byte bank, std::size_t size) {
    return (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U))) % size;
}

std::size_t fixed_prg_offset(Address addr, std::size_t size) {
    return size - kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t chr_offset(Address addr, Byte bank, std::size_t size) {
    return (static_cast<std::size_t>(bank) * kChrBankSize + (addr & (kChrBankSize - 1U))) % size;
}

TEST_CASE("Mapper 417 validates both NES 2.0 geometries and deterministic reset",
          "[mapper417][geometry][reset]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(submapper, kMinPrgSize, kMinChrSize, mirror);
            CAPTURE(submapper, mirror);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(state(*mapper).size() == kStateSize);
            for (unsigned slot = 0U; slot < 3U; ++slot) {
                const auto addr = static_cast<Address>(0x8123U + slot * kPrgBankSize);
                REQUIRE(mapper->read_prg(addr) == cell(0x0123U));
            }
            REQUIRE(mapper->read_prg(0xE123U) == cell(fixed_prg_offset(0xE123U, kMinPrgSize)));
            REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U, 0x35U));
            REQUIRE(mapper->nametable_address(0x2ABCU) == 0x02BCU);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            auto other_rom = board(submapper, kMinPrgSize, kMinChrSize, mirror, 0x80U);
            REQUIRE(state(*other_rom) == state(*mapper));
        }
        for (const std::size_t prg : {kMinPrgSize, 0x00A000UZ, 0x020000UZ, kMaxPrgSize}) {
            for (const std::size_t chr : {kMinChrSize, 0x003000UZ, 0x010000UZ, kMaxChrSize}) {
                CAPTURE(submapper, prg, chr);
                REQUIRE(Mapper417::validate_geometry(header(submapper), prg, chr));
            }
        }
    }

    for (const unsigned id : {0U, 4U, 256U, 326U, 416U, 418U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper417::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper417::validate_geometry(legacy, kMinPrgSize, kMinChrSize));
    for (Byte submapper = 2U; submapper < 16U; ++submapper) {
        CAPTURE(submapper);
        REQUIRE_FALSE(Mapper417::validate_geometry(header(submapper), kMinPrgSize, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x6000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, kMaxPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper417::validate_geometry(header(), size, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x0400UZ, 0x1000UZ, 0x1C00UZ, 0x1FFFUZ, 0x2001UZ, kMaxChrSize + kChrBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper417::validate_geometry(header(), kMinPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper417::validate_geometry(header(0U, mirror), kMinPrgSize, kMinChrSize));
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
        REQUIRE_FALSE(Mapper417::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
}

TEST_CASE("Mapper 417 decodes every documented mask and alias without bus conflicts",
          "[mapper417][decoding][bus]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        const auto initial = state(*mapper);
        for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
            mapper->reset();
            const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
            mapper->write_prg(static_cast<Address>(raw), value);
            auto wanted = initial;
            switch (raw & 0x8070U) {
            case 0x8030U:
                wanted[kIrqFlagsOffset] = 1U;
                break;
            case 0x8040U:
                break;
            default:
                switch (raw & 0x8073U) {
                case 0x8000U:
                case 0x8001U:
                case 0x8002U:
                    wanted[kPrgBanksOffset + (raw & 3U)] = value;
                    break;
                case 0x8010U:
                case 0x8011U:
                case 0x8012U:
                case 0x8013U:
                    wanted[kChrBanksOffset + (raw & 3U)] = value;
                    if (submapper == 1U) {
                        wanted[kNametableBanksOffset + (raw & 3U)] =
                            (value & 0x80U) != 0U ? 1U : 0U;
                    }
                    break;
                case 0x8020U:
                case 0x8021U:
                case 0x8022U:
                case 0x8023U:
                    wanted[kChrBanksOffset + 4U + (raw & 3U)] = value;
                    break;
                case 0x8050U:
                case 0x8051U:
                case 0x8052U:
                case 0x8053U:
                    if (submapper == 0U) {
                        wanted[kNametableBanksOffset + (raw & 3U)] = value & 1U;
                    }
                    break;
                default:
                    break;
                }
                break;
            }
            CAPTURE(submapper, raw, value);
            REQUIRE(state(*mapper) == wanted);
        }
    }

    auto mapper = board();
    mapper->write_prg(0x8001U, 0xA5U);
    const auto before = state(*mapper);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    for (const auto addr : kPrgAddresses) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA7U));
}

TEST_CASE("Mapper 417 maps three full-byte PRG banks and the physical final bank",
          "[mapper417][banking][bus]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 3U; ++slot) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(0x8000U + slot), static_cast<Byte>(value));
            for (const auto addr : kPrgAddresses) {
                const auto current_slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
                const auto physical =
                    current_slot == 3U
                        ? fixed_prg_offset(addr, 0x040000UZ)
                        : prg_offset(addr, current_slot == slot ? value : 0U, 0x040000UZ);
                CAPTURE(slot, value, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, 0x020000UZ}) {
        auto fitted = board(0U, size, kMinChrSize);
        fitted->write_prg(0x8000U, 0xFEU);
        fitted->write_prg(0x8001U, 0xFFU);
        fitted->write_prg(0x8002U, 0xA5U);
        for (const auto addr : kPrgAddresses) {
            const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
            constexpr std::array<Byte, 3> kBanks{0xFEU, 0xFFU, 0xA5U};
            const auto physical =
                slot == 3U ? fixed_prg_offset(addr, size) : prg_offset(addr, kBanks[slot], size);
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 417 maps eight independent full-byte CHR banks and protects ROM",
          "[mapper417][banking][chr][bus]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->reset();
                const Address reg =
                    static_cast<Address>(0x8010U + (slot >= 4U ? 0x10U : 0U) + (slot & 3U));
                mapper->write_prg(reg, static_cast<Byte>(value));
                for (unsigned current = 0U; current < 8U; ++current) {
                    const auto addr = static_cast<Address>(current * kChrBankSize + 0x0155U);
                    const auto physical =
                        chr_offset(addr, current == slot ? value : 0U, kMaxChrSize);
                    CAPTURE(submapper, slot, value, current, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                    mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
                }
            }
        }
    }

    for (const std::size_t size : {kMinChrSize, 0x003000UZ, 0x010000UZ}) {
        auto fitted = board(0U, kMinPrgSize, size);
        fitted->write_prg(0x8023U, 0xFFU);
        const auto physical = chr_offset(0x1D55U, 0xFFU, size);
        REQUIRE(fitted->read_chr(0x1D55U) == cell(physical, 0x35U));
    }
}

TEST_CASE("Mapper 417 selects every CIRAM bank for both submapper wirings",
          "[mapper417][ciram][decoding]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper, 0x040000UZ, kMaxChrSize, MirrorMode::Horizontal);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->reset();
                const Address reg =
                    static_cast<Address>((submapper == 0U ? 0x8050U : 0x8010U) + slot);
                mapper->write_prg(reg, static_cast<Byte>(value));
                const unsigned bank = submapper == 0U         ? value & 1U
                                      : (value & 0x80U) != 0U ? 1U
                                                              : 0U;
                for (unsigned current = 0U; current < 4U; ++current) {
                    const auto addr = static_cast<Address>(0x2000U + current * 0x400U + 0x02AAU);
                    const Address expected =
                        static_cast<Address>((current == slot ? bank : 0U) * 0x400U + 0x02AAU);
                    CAPTURE(submapper, slot, value, current, expected);
                    REQUIRE(mapper->nametable_address(addr) == expected);
                }
            }
        }

        for (unsigned selection = 0U; selection < 16U; ++selection) {
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const Address reg =
                    static_cast<Address>((submapper == 0U ? 0x8050U : 0x8010U) + slot);
                const auto value =
                    static_cast<Byte>(((selection >> slot) & 1U) * (submapper == 0U ? 1U : 0x80U));
                mapper->write_prg(reg, value);
            }
            for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
                const unsigned slot = (addr >> 10U) & 3U;
                const Address expected =
                    static_cast<Address>((((selection >> slot) & 1U) << 10U) | (addr & 0x03FFU));
                CAPTURE(submapper, selection, addr, expected);
                REQUIRE(mapper->nametable_address(addr) == expected);
            }
        }
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x3F00U, 0x3FFFU, 0xFFFFU}) {
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
        }
        for (const Address addr : {0x2000U, 0x2ABCU, 0x3000U, 0x3EFFU}) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
    }
}

TEST_CASE("Mapper 417 owns no RAM audio or unrelated observer side effects",
          "[mapper417][ram][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8002U, 0xA5U);
    mapper->write_prg(0x8011U, 0x5CU);
    const auto before = state(*mapper);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_irq_counter();
    mapper->acknowledge_irq();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("default"));
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 417 free-running M2 counters latch only enabled overflow IRQs",
          "[mapper417][irq][decoding][reset]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        const std::uint16_t mask = submapper == 0U ? 0x03FFU : 0x0FFFU;
        for (unsigned cycle = 0U; cycle < mask + 77U; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(read_u16(state(*mapper), kIrqCounterOffset) == 76U);

        mapper->write_prg(0x8030U, 0xA5U);
        auto bytes = state(*mapper);
        REQUIRE(read_u16(bytes, kIrqCounterOffset) == 0U);
        REQUIRE(bytes[kIrqFlagsOffset] == 1U);
        for (unsigned cycle = 1U; cycle <= mask; ++cycle) {
            mapper->clock_cpu();
            CAPTURE(submapper, cycle);
            REQUIRE_FALSE(mapper->irq_pending());
        }
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        bytes = state(*mapper);
        REQUIRE(read_u16(bytes, kIrqCounterOffset) == 0U);
        REQUIRE(bytes[kIrqFlagsOffset] == 3U);
        for (unsigned cycle = 0U; cycle < mask * 2U + 19U; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE(mapper->irq_pending());

        mapper->write_prg(0x803FU, 0U);
        REQUIRE(mapper->irq_pending());
        REQUIRE(read_u16(state(*mapper), kIrqCounterOffset) == 0U);
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper)[kIrqFlagsOffset] == 1U);

        for (unsigned cycle = 0U; cycle <= mask; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE(mapper->irq_pending());
        const auto before_disable = state(*mapper);
        mapper->write_prg(0x804AU, 0U);
        bytes = state(*mapper);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(bytes[kIrqFlagsOffset] == 0U);
        REQUIRE(read_u16(bytes, kIrqCounterOffset) == read_u16(before_disable, kIrqCounterOffset));
        for (unsigned cycle = 0U; cycle < mask * 3U + 11U; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0x8030U, 0U);
        REQUIRE(state(*mapper)[kIrqFlagsOffset] == 1U);
        REQUIRE(read_u16(state(*mapper), kIrqCounterOffset) == 0U);
    }
}

TEST_CASE("Mapper 417 soft and cold reset restore all latches and IRQ state",
          "[mapper417][reset][ciram][irq]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        for (const bool cold : {false, true}) {
            auto mapper = board(submapper, 0x040000UZ, kMaxChrSize, MirrorMode::Horizontal);
            const auto initial = state(*mapper);
            mapper->write_prg(0x8000U, 0x12U);
            mapper->write_prg(0x8001U, 0x34U);
            mapper->write_prg(0x8002U, 0x56U);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                mapper->write_prg(
                    static_cast<Address>(0x8010U + (slot >= 4U ? 0x10U : 0U) + (slot & 3U)),
                    static_cast<Byte>(0x80U | slot));
            }
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                mapper->write_prg(static_cast<Address>(0x8050U + slot), 1U);
            }
            mapper->write_prg(0x8030U, 0U);
            for (unsigned cycle = 0U; cycle < (submapper == 0U ? 1024U : 4096U); ++cycle) {
                mapper->clock_cpu();
            }
            REQUIRE(mapper->irq_pending());
            if (cold) {
                mapper->power_on();
            } else {
                mapper->reset();
            }
            CAPTURE(submapper, cold);
            REQUIRE(state(*mapper) == initial);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(mapper->read_prg(0x8123U) == cell(0x0123U));
            REQUIRE(mapper->read_prg(0xE123U) == cell(fixed_prg_offset(0xE123U, 0x040000UZ)));
            REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U, 0x35U));
            REQUIRE(mapper->nametable_address(0x2ABCU) == 0x02BCU);
        }
    }
}

TEST_CASE("Mapper 417 bounded snapshots replay banks CIRAM and IRQ without ROM",
          "[mapper417][state][banking][ciram][irq]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        mapper->write_prg(0x8000U, 0x12U);
        mapper->write_prg(0x8001U, 0x34U);
        mapper->write_prg(0x8002U, 0x56U);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            mapper->write_prg(
                static_cast<Address>(0x8010U + (slot >= 4U ? 0x10U : 0U) + (slot & 3U)),
                static_cast<Byte>(slot * 17U + 3U));
        }
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            mapper->write_prg(static_cast<Address>((submapper == 0U ? 0x8050U : 0x8010U) + slot),
                              submapper == 0U ? static_cast<Byte>(slot & 1U)
                                              : static_cast<Byte>((slot & 1U) << 7U));
        }
        mapper->write_prg(0x8030U, 0U);
        for (unsigned cycle = 0U; cycle < 123U; ++cycle) {
            mapper->clock_cpu();
        }
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == kStateSize);
        REQUIRE(saved.size() < kMinChrSize);
        REQUIRE(read_u16(saved, kIrqCounterOffset) == 123U);

        auto copy = board(submapper, 0x040000UZ, kMaxChrSize, MirrorMode::Vertical, 0x80U);
        load(*copy, saved);
        REQUIRE(state(*copy) == saved);
        constexpr std::array<Byte, 3> kBanks{0x12U, 0x34U, 0x56U};
        for (const auto addr : kPrgAddresses) {
            const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
            const auto physical = slot == 3U ? fixed_prg_offset(addr, 0x040000UZ)
                                             : prg_offset(addr, kBanks[slot], 0x040000UZ);
            CAPTURE(submapper, addr, physical);
            REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
        for (Address addr = 0U; addr < 0x2000U; addr += 0x0111U) {
            const auto slot = static_cast<unsigned>(addr >> 10U);
            const auto bank = submapper == 1U && slot < 4U ? static_cast<Byte>((slot & 1U) << 7U)
                                                           : static_cast<Byte>(slot * 17U + 3U);
            REQUIRE(copy->read_chr(addr) == cell(chr_offset(addr, bank, kMaxChrSize), 0xB5U));
            REQUIRE(mapper->read_chr(addr) == cell(chr_offset(addr, bank, kMaxChrSize), 0x35U));
        }
        for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
            REQUIRE(copy->nametable_address(addr) == mapper->nametable_address(addr));
        }
        const std::uint16_t mask = submapper == 0U ? 0x03FFU : 0x0FFFU;
        for (unsigned cycle = 123U; cycle <= mask; ++cycle) {
            copy->clock_cpu();
        }
        REQUIRE(copy->irq_pending());
        const auto pending = state(*copy);

        auto with_tail = pending;
        with_tail.insert(with_tail.end(), {1U, 2U, 3U});
        copy->reset();
        StateReader tail_reader(with_tail);
        copy->load_state(tail_reader);
        REQUIRE(tail_reader.ok());
        REQUIRE(tail_reader.remaining() == 3U);
        REQUIRE(state(*copy) == pending);
        REQUIRE(copy->irq_pending());
    }
}

TEST_CASE("Mapper 417 rejects damaged truncated and mismatched states atomically",
          "[mapper417][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xFEU);
    mapper->write_prg(0x8001U, 0xA5U);
    mapper->write_prg(0x8002U, 0x5AU);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8010U + (slot >= 4U ? 0x10U : 0U) + (slot & 3U)),
                          static_cast<Byte>(slot * 29U + 0x80U));
    }
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8050U + slot), static_cast<Byte>(slot & 1U));
    }
    mapper->write_prg(0x8030U, 0U);
    for (unsigned cycle = 0U; cycle < 1024U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved[kIrqFlagsOffset] == 3U);
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
    for (std::size_t offset = 0U; offset < 4U; ++offset) {
        for (const Byte bank : {2U, 0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[kNametableBanksOffset + offset] = bank;
            CAPTURE(offset, bank);
            reject(invalid);
        }
    }
    for (const std::uint16_t counter : {0x0400U, 0x1000U, 0xFFFFU}) {
        auto invalid = saved;
        write_u16(invalid, kIrqCounterOffset, counter);
        CAPTURE(counter);
        reject(invalid);
    }
    for (const Byte flags : {2U, 4U, 5U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kIrqFlagsOffset] = flags;
        CAPTURE(flags);
        reject(invalid);
    }

    auto wrong_submapper = board(1U);
    const auto wrong_submapper_before = state(*wrong_submapper);
    StateReader wrong_submapper_reader(saved);
    wrong_submapper->load_state(wrong_submapper_reader);
    REQUIRE_FALSE(wrong_submapper_reader.ok());
    REQUIRE(state(*wrong_submapper) == wrong_submapper_before);

    auto wrong_mirror = board(0U, 0x040000UZ, kMaxChrSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto wrong_prg = board(0U, kMinPrgSize);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(0U, 0x040000UZ, kMinChrSize);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto changed = saved;
    changed[kPrgBanksOffset] = 0xFFU;
    changed[kChrBanksOffset + 7U] = 0xFFU;
    changed[kNametableBanksOffset] ^= 1U;
    write_u16(changed, kIrqCounterOffset, 0x03FEU);
    changed[kIrqFlagsOffset] = 1U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE_FALSE(mapper->irq_pending());
}

} // namespace
