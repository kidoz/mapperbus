#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper458.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x040000UZ;
constexpr std::size_t kChrSize = 0x040000UZ;
constexpr std::size_t kNromBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kConfigurationOffset = 10UZ;
constexpr std::size_t kExtraOffset = 12UZ;
constexpr std::size_t kBankSelectOffset = 13UZ;
constexpr std::size_t kBankModeOffset = 14UZ;
constexpr std::size_t kBankRegistersOffset = 15UZ;
constexpr std::size_t kMirrorOffset = 23UZ;
constexpr std::size_t kIrqCounterOffset = 24UZ;
constexpr std::size_t kIrqReloadOffset = 25UZ;
constexpr std::size_t kIrqFlagsOffset = 26UZ;
constexpr std::size_t kRamFlagsOffset = 27UZ;
constexpr std::size_t kStateSize = 28UZ;
constexpr std::array<Address, 10> kPrgAddresses{
    0x8000U, 0x8001U, 0x801FU, 0x8020U, 0x8123U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xFFFFU};

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 458U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 13U) * 37U + (offset >> 14U) * 47U +
                             (offset >> 15U) * 43U + (offset >> 16U) * 59U + (offset >> 17U) * 61U +
                             (offset >> 18U) * 71U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper458> board(Byte submapper = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper458::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper458>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper458& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper458& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

Byte normalized_extra(Byte submapper, Byte value) {
    return value & (submapper == 0U ? 0x6FU : 0xAFU);
}

void extra(Mapper458& mapper, Byte value, Address addr = 0x6000U) {
    mapper.write_prg(0xA001U, 0xC0U);
    mapper.write_prg(addr, value);
}

std::size_t expected_prg(
    Address addr, Byte submapper, Byte control, Byte pads = 0U, std::size_t prg_size = kPrgSize) {
    const auto extra = normalized_extra(submapper, control);
    unsigned selected = extra & 0x0FU;
    const Byte nrom256 = submapper == 0U ? 0x40U : 0x80U;
    if ((extra & nrom256) != 0U) {
        selected = (selected & ~1U) | ((addr >> 14U) & 1U);
    }
    auto physical =
        static_cast<std::size_t>(selected) * kNromBankSize + (addr & (kNromBankSize - 1U));
    if ((extra & 0x20U) != 0U) {
        physical = (physical & ~std::size_t{0x1FU}) | pads;
    }
    return physical % prg_size;
}

unsigned expected_chr_bank(Address addr, Byte mode, const std::array<Byte, 6>& values) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (values[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return values[slot - 2U];
}

std::size_t expected_chr(Address addr,
                         Byte mode,
                         const std::array<Byte, 6>& values,
                         std::size_t chr_size = kChrSize) {
    const auto physical =
        expected_chr_bank(addr, mode, values) * kChrBankSize + (addr & (kChrBankSize - 1U));
    return physical % chr_size;
}

void require_prg(
    Mapper458& mapper, Address addr, Byte submapper, Byte control, Byte pads = 0U, Byte salt = 0U) {
    const auto physical = expected_prg(addr, submapper, control, pads);
    CAPTURE(addr, submapper, control, pads, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 458 validates both NES 2.0 ROM-only decoder geometries",
          "[mapper458][geometry][ram]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        for (std::size_t prg = 0x008000UZ; prg <= kPrgSize; prg *= 2U) {
            for (std::size_t chr = 0x002000UZ; chr <= kChrSize; chr *= 2U) {
                CAPTURE(submapper, prg, chr);
                REQUIRE(Mapper458::validate_geometry(header(submapper), prg, chr));
            }
        }
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(submapper, mirror, 0x35U);
            auto different_rom = board(submapper, mirror, 0x80U);
            CAPTURE(submapper, mirror);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->mirror_mode() == mirror);
            require_prg(*mapper, 0x8000U, submapper, 0U, 0U, 0x35U);
            require_prg(*mapper, 0xC000U, submapper, 0U, 0U, 0x35U);
            require_prg(*mapper, 0xE000U, submapper, 0U, 0U, 0x35U);
            REQUIRE(mapper->read_chr(0U) == cell(0U, 0x35U ^ 0x55U));
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }

    const auto reject = [](const INesHeader& h, std::size_t prg, std::size_t chr) {
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
        REQUIRE_FALSE(Mapper458::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 259U, 457U, 459U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, kChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, kChrSize);
    for (Byte submapper = 2U; submapper < 16U; ++submapper) {
        reject(header(submapper), kPrgSize, kChrSize);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x1000UZ,
                                   0x4000UZ,
                                   0x006000UZ,
                                   0x018000UZ,
                                   kPrgSize - 1U,
                                   kPrgSize + 1U,
                                   kPrgSize * 2U}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x1000UZ,
                                   0x3000UZ,
                                   0x006000UZ,
                                   0x018000UZ,
                                   kChrSize - 1U,
                                   kChrSize + 1U,
                                   kChrSize * 2U}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(0U, mirror), kPrgSize, kChrSize);
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, kChrSize);
    }
}

TEST_CASE("Mapper 458 decodes and gates every extra-register address",
          "[mapper458][decoding][bus][ram]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
            for (const Byte gate :
                 {Byte{0U}, Byte{0x40U}, Byte{0x7FU}, Byte{0x80U}, Byte{0xC0U}, Byte{0xFFU}}) {
                mapper->reset();
                mapper->write_prg(0xA001U, gate);
                auto expected = state(*mapper);
                const auto addr = static_cast<Address>(raw);
                const auto value = static_cast<Byte>(raw * 41U + gate + 0xA5U);
                mapper->write_prg(addr, value);
                if (raw >= 0x6000U && (gate & 0x80U) != 0U) {
                    expected[kExtraOffset] = normalized_extra(submapper, value);
                }
                CAPTURE(submapper, raw, gate, value);
                REQUIRE(state(*mapper) == expected);
                REQUIRE_FALSE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0U);
                REQUIRE(mapper->read_prg(addr) == 0U);
                REQUIRE_FALSE(mapper->maps_expansion(addr));
            }
        }

        mapper->reset();
        mapper->write_prg(0xA001U, 0xC0U);
        extra(*mapper, 0xFFU, 0x7FFFU);
        REQUIRE(state(*mapper)[kExtraOffset] == normalized_extra(submapper, 0xFFU));
        REQUIRE(state(*mapper)[kRamFlagsOffset] == 0xC0U);
        mapper->write_prg(0xA001U, 0U);
        mapper->write_prg(0x6000U, 0U);
        REQUIRE(state(*mapper)[kExtraOffset] == normalized_extra(submapper, 0xFFU));
    }
}

TEST_CASE("Mapper 458 decodes every MMC3 alias without unrelated side effects",
          "[mapper458][decoding][bus]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        const auto cold = state(*mapper);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            mapper->reset();
            const auto addr = static_cast<Address>(raw);
            const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U) + 0xA5U);
            mapper->write_prg(addr, value);
            auto expected = cold;
            const bool even = (raw & 1U) == 0U;
            if (raw < 0xA000U) {
                if (even) {
                    expected[kBankSelectOffset] = value & 7U;
                    expected[kBankModeOffset] = value & 0xC0U;
                } else {
                    expected[kBankRegistersOffset] = value;
                }
            } else if (raw < 0xC000U) {
                if (even) {
                    expected[kMirrorOffset] = (value & 1U) ^ 1U;
                } else {
                    expected[kRamFlagsOffset] = value & 0xC0U;
                }
            } else if (raw < 0xE000U) {
                if (even) {
                    expected[kIrqReloadOffset] = value;
                } else {
                    expected[kIrqFlagsOffset] = 1U;
                }
            } else if (!even) {
                expected[kIrqFlagsOffset] = 2U;
            }
            CAPTURE(submapper, raw, value, even);
            REQUIRE(state(*mapper) == expected);
        }
    }
}

TEST_CASE("Mapper 458 selects NROM banks and solder pads independently of MMC3 PRG",
          "[mapper458][banking][prg]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (const Byte pads : {Byte{0U}, Byte{5U}, Byte{31U}}) {
            REQUIRE(mapper->configure_board_jumpers(pads));
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto control = static_cast<Byte>(raw);
                extra(*mapper, control, static_cast<Address>(0x6000U | (raw * 73U)));
                for (const auto addr : kPrgAddresses) {
                    require_prg(*mapper, addr, submapper, control, pads);
                }
                const auto lower = expected_prg(0x8123U, submapper, control, pads);
                const auto upper = expected_prg(0xC123U, submapper, control, pads);
                const Byte nrom256 = submapper == 0U ? 0x40U : 0x80U;
                if ((normalized_extra(submapper, control) & nrom256) == 0U) {
                    REQUIRE(lower == upper);
                } else {
                    REQUIRE(upper - lower == kNromBankSize);
                }
            }
        }

        extra(*mapper, submapper == 0U ? 0x6DU : 0xADU, 0x7FFFU);
        REQUIRE(mapper->configure_board_jumpers(19U));
        const auto before = state(*mapper);
        for (const Byte mode : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                bank(*mapper, 6U, static_cast<Byte>(value), mode);
                bank(*mapper, 7U, static_cast<Byte>(value ^ 0xA5U), mode);
                require_prg(*mapper, 0x8000U, submapper, submapper == 0U ? 0x6DU : 0xADU, 19U);
                require_prg(*mapper, 0xC000U, submapper, submapper == 0U ? 0x6DU : 0xADU, 19U);
                REQUIRE(state(*mapper)[kExtraOffset] == before[kExtraOffset]);
            }
        }
    }
}

TEST_CASE("Mapper 458 applies PRG decoding before fitted-ROM wrapping",
          "[mapper458][banking][prg]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        for (const std::size_t size : {0x008000UZ, 0x010000UZ, 0x020000UZ}) {
            auto mapper = board(submapper, MirrorMode::Vertical, 0x23U, size, kChrSize);
            REQUIRE(mapper->configure_board_jumpers(27U));
            const Byte control = submapper == 0U ? 0x6FU : 0xAFU;
            extra(*mapper, control);
            for (unsigned raw = 0x8000U; raw <= 0xFFFFU; raw += 0x101U) {
                const auto addr = static_cast<Address>(raw);
                const auto physical = expected_prg(addr, submapper, control, 27U, size);
                CAPTURE(submapper, size, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical, 0x23U));
            }
        }
    }
}

TEST_CASE("Mapper 458 retains MMC3 CHR pairing inversion and ROM protection",
          "[mapper458][banking][chr][ram]") {
    constexpr std::array<Byte, 6> values = {0xA2U, 0x8BU, 0x93U, 0xFCU, 0xB1U, 0xC7U};
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (const Byte mode : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
            for (Byte reg = 0U; reg < 6U; ++reg) {
                bank(*mapper, reg, values[reg], mode);
            }
            for (const Byte control :
                 {Byte{0U}, Byte{0x2FU}, Byte{0x6FU}, Byte{0xAFU}, Byte{0xFFU}}) {
                extra(*mapper, control);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    for (const Address within : {Address{0U}, Address{0x155U}, Address{0x3FFU}}) {
                        const auto addr = static_cast<Address>(slot * kChrBankSize + within);
                        const auto physical = expected_chr(addr, mode, values);
                        CAPTURE(submapper, mode, control, addr, physical);
                        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                        mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                    }
                }
            }
        }
        mapper->reset();
        bank(*mapper, 0U, 0xFEU, 0x80U);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            const auto physical = expected_chr(addr, 0x80U, {0xFEU, 0U, 0U, 0U, 0U, 0U});
            CAPTURE(submapper, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
        }

        for (const std::size_t size : {0x002000UZ, 0x008000UZ, 0x020000UZ}) {
            auto fitted = board(submapper, MirrorMode::Vertical, 0U, kPrgSize, size);
            for (Byte reg = 0U; reg < 6U; ++reg) {
                bank(*fitted, reg, values[reg], 0x80U);
            }
            for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
                const auto physical = expected_chr(addr, 0x80U, values, size);
                CAPTURE(submapper, size, addr, physical);
                REQUIRE(fitted->read_chr(addr) == cell(physical, 0x55U));
            }
        }
    }
}

TEST_CASE("Mapper 458 uses MMC3 CIRAM control and exposes no RAM or audio",
          "[mapper458][ciram][ram][audio][bus]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(submapper, initial);
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->write_prg((value & 1U) != 0U ? 0xBFFEU : 0xA000U, static_cast<Byte>(value));
                CAPTURE(submapper, initial, value);
                REQUIRE(mapper->mirror_mode() ==
                        ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            }
            extra(*mapper, 0xFFU);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            mapper->reset();
            REQUIRE(mapper->mirror_mode() == initial);
            for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->ciram_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
            }
            REQUIRE_FALSE(mapper->unmirrored_nametables());
            REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
            REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
            REQUIRE_FALSE(mapper->observes_ppu_a12());
            REQUIRE_FALSE(mapper->observes_ppu_address());
            REQUIRE_FALSE(mapper->observes_ppu_reads());
        }
    }

    auto mapper = board();
    const auto before = state(*mapper);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->maps_expansion(0x6000U));
    REQUIRE(mapper->expansion_read_mask(0x6000U) == 0xFFU);
    REQUIRE(mapper->read_expansion(0x6000U) == 0U);
    mapper->write_expansion(0x6000U, 0x0FU);

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 458 validates and retains all five solder-pad bits", "[mapper458][bus][state]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        auto mapper = board(submapper);
        for (unsigned pads = 0U; pads < 32U; ++pads) {
            REQUIRE(mapper->configure_board_jumpers(static_cast<Byte>(pads)));
            REQUIRE(state(*mapper)[kConfigurationOffset] ==
                    static_cast<Byte>(1U | (submapper << 1U) | (pads << 2U)));
            extra(*mapper, 0x2FU);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, submapper, 0x2FU, static_cast<Byte>(pads));
            }
            mapper->reset();
            REQUIRE(state(*mapper)[kConfigurationOffset] ==
                    static_cast<Byte>(1U | (submapper << 1U) | (pads << 2U)));
        }
        const auto configured = state(*mapper);
        for (const Byte invalid : {Byte{32U}, Byte{33U}, Byte{127U}, Byte{255U}}) {
            REQUIRE_FALSE(mapper->configure_board_jumpers(invalid));
            REQUIRE(state(*mapper) == configured);
        }
    }
}

TEST_CASE("Mapper 458 retains MMC3 IRQ acknowledgement and zero repeats", "[mapper458][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    extra(*mapper, 0x6FU);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xE001U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    for (unsigned clock = 0U; clock < 4U; ++clock) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 458 reset clears the extra latch and every MMC3 register", "[mapper458][reset]") {
    for (const Byte submapper : {Byte{0U}, Byte{1U}}) {
        for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(submapper, initial);
            REQUIRE(mapper->configure_board_jumpers(23U));
            const auto cold = state(*mapper);
            extra(*mapper, 0xFFU);
            bank(*mapper, 6U, 0x19U, 0xC0U);
            bank(*mapper, 7U, 0x07U, 0xC0U);
            mapper->write_prg(0xA000U, initial == MirrorMode::Vertical ? 1U : 0U);
            mapper->write_prg(0xA001U, 0U);
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(0xC001U, 0U);
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending());
            REQUIRE(state(*mapper) != cold);

            mapper->reset();
            REQUIRE(state(*mapper) == cold);
            REQUIRE(state(*mapper)[kExtraOffset] == 0U);
            REQUIRE(mapper->mirror_mode() == initial);
            REQUIRE_FALSE(mapper->irq_pending());
            require_prg(*mapper, 0xE000U, submapper, 0U, 23U);

            extra(*mapper, 0x2FU);
            bank(*mapper, 0U, 0xFEU, 0x80U);
            mapper->power_on();
            REQUIRE(state(*mapper) == cold);
            REQUIRE(mapper->read_chr(0U) == cell(0U, 0x55U));
        }
    }
}

TEST_CASE("Mapper 458 bounded snapshots replay without ROM and preserve partial IRQ state",
          "[mapper458][state][banking][irq]") {
    auto mapper = board(1U);
    REQUIRE(mapper->configure_board_jumpers(17U));
    extra(*mapper, 0xAEU);
    bank(*mapper, 0U, 0xA2U, 0xC0U);
    bank(*mapper, 1U, 0x8BU, 0xC0U);
    bank(*mapper, 6U, 0x19U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kChrSize);
    REQUIRE(saved[kExtraOffset] == 0xAEU);
    REQUIRE(saved[kIrqCounterOffset] == 2U);
    REQUIRE(saved[kIrqFlagsOffset] == 2U);

    auto copy = board(1U, MirrorMode::Vertical, 0x80U);
    REQUIRE(copy->configure_board_jumpers(17U));
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 1U, 0xAEU, 17U, 0x80U);
    }
    REQUIRE(copy->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0xC0U, {0xA2U, 0x8BU, 0U, 0U, 0U, 0U}), 0xD5U));
    copy->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE_FALSE(copy->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 458 rejects truncated damaged and mismatched states atomically",
          "[mapper458][state]") {
    auto mapper = board(1U);
    REQUIRE(mapper->configure_board_jumpers(17U));
    extra(*mapper, 0xAFU);
    bank(*mapper, 0U, 0xFEU, 0xC0U);
    bank(*mapper, 6U, 0x09U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 14> invalid_fields = {
        {{0U, 0U},
         {2U, 1U},
         {6U, 1U},
         {10U, 0U},
         {11U, 1U},
         {kExtraOffset, 0x40U},
         {kExtraOffset, 0x10U},
         {kBankSelectOffset, 8U},
         {kBankModeOffset, 1U},
         {kBankRegistersOffset + 6U, 0x40U},
         {kBankRegistersOffset + 7U, 0x40U},
         {kMirrorOffset, 2U},
         {kIrqFlagsOffset, 8U},
         {kRamFlagsOffset, 1U}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    auto pending_without_enable = saved;
    pending_without_enable[kIrqFlagsOffset] = 4U;
    reject(pending_without_enable);

    const auto mismatch = [&](std::unique_ptr<Mapper458>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_submapper = board(0U);
    REQUIRE(wrong_submapper->configure_board_jumpers(17U));
    mismatch(wrong_submapper);
    auto wrong_pads = board(1U);
    REQUIRE(wrong_pads->configure_board_jumpers(16U));
    mismatch(wrong_pads);
    auto wrong_mirror = board(1U, MirrorMode::Horizontal);
    REQUIRE(wrong_mirror->configure_board_jumpers(17U));
    mismatch(wrong_mirror);
    auto wrong_prg = board(1U, MirrorMode::Vertical, 0U, kPrgSize / 2U, kChrSize);
    REQUIRE(wrong_prg->configure_board_jumpers(17U));
    mismatch(wrong_prg);
    auto wrong_chr = board(1U, MirrorMode::Vertical, 0U, kPrgSize, kChrSize / 2U);
    REQUIRE(wrong_chr->configure_board_jumpers(17U));
    mismatch(wrong_chr);

    auto submapper_zero = board(0U);
    extra(*submapper_zero, 0x6FU);
    const auto zero_saved = state(*submapper_zero);
    for (const Byte reserved : {Byte{0x80U}, Byte{0x90U}, Byte{0x10U}}) {
        auto invalid = zero_saved;
        invalid[kExtraOffset] = reserved;
        StateReader reader(invalid);
        submapper_zero->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*submapper_zero) == zero_saved);
    }

    auto changed = saved;
    changed[kExtraOffset] = 0x8DU;
    changed[kBankRegistersOffset] = 0x5AU;
    changed[kBankRegistersOffset + 6U] = 0x0EU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0x8000U, 1U, 0x8DU, 17U);
    REQUIRE(mapper->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0xC0U, {0x5AU, 0U, 0U, 0U, 0U, 0U}), 0x55U));
}

} // namespace
