#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper460.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kConfigurationOffset = 10UZ;
constexpr std::size_t kExtraOffset = 12UZ;
constexpr std::size_t kBankSelectOffset = 14UZ;
constexpr std::size_t kBankModeOffset = 15UZ;
constexpr std::size_t kBankRegistersOffset = 16UZ;
constexpr std::size_t kMirrorOffset = 24UZ;
constexpr std::size_t kIrqCounterOffset = 25UZ;
constexpr std::size_t kIrqReloadOffset = 26UZ;
constexpr std::size_t kIrqFlagsOffset = 27UZ;
constexpr std::size_t kRamFlagsOffset = 28UZ;
constexpr std::size_t kChrRamOffset = 29UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 10> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0x9FFFU, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 460U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 13U) * 37U + (offset >> 15U) * 43U + (offset >> 17U) * 61U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 79U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper460> board(MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 bool solder_pad = false) {
    const auto h = header(mirror);
    REQUIRE(Mapper460::validate_geometry(h, prg_size, kChrSize));
    auto mapper = std::make_unique<Mapper460>(
        h, rom(prg_size, salt), rom(kChrSize, static_cast<Byte>(salt ^ 0x55U)));
    REQUIRE(mapper->configure_board_jumpers(solder_pad ? 1U : 0U));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper460& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper460& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void extra(Mapper460& mapper, Address control, Address addr = 0x6000U) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(addr | control, 0xA5U);
}

Byte expected_inner_prg(Address addr, Address extra, Byte mode, Byte r6, Byte r7) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if ((extra & 0x40U) != 0U) {
        Byte selected = (slot & 1U) == 0U ? r6 : r7;
        if ((extra & 0x20U) != 0U) {
            selected = static_cast<Byte>((selected & ~2U) | (slot & 2U));
        }
        return selected & 0x3FU;
    }
    const bool swapped = (mode & 0x40U) != 0U;
    return slot == 1U ? r7 & 0x3FU : slot == 3U ? 63U : (slot == 0U) == swapped ? 62U : r6 & 0x3FU;
}

std::size_t expected_prg(
    Address addr, Address extra, Byte mode, Byte r6, Byte r7, std::size_t prg_size = kPrgSize) {
    const unsigned inner = expected_inner_prg(addr, extra, mode, r6, r7) & 0x1FU;
    const auto physical = static_cast<std::size_t>(inner | ((extra & 3U) << 5U)) * kPrgBankSize +
                          (addr & (kPrgBankSize - 1U));
    return physical % prg_size;
}

void require_prg(Mapper460& mapper,
                 Address addr,
                 Address extra,
                 Byte mode,
                 Byte r6,
                 Byte r7,
                 Byte salt = 0U,
                 std::size_t prg_size = kPrgSize) {
    const auto physical = expected_prg(addr, extra, mode, r6, r7, prg_size);
    CAPTURE(addr, extra, mode, r6, r7, physical, salt, prg_size);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

Byte expected_chr_bank(Address addr, Byte mode, const std::array<Byte, 8>& banks) {
    const auto pins =
        static_cast<Address>((addr & 0x1000U) | ((addr & 0x800U) >> 1U) | (addr & 0x800U));
    const unsigned slot = ((pins >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    return slot < 4U ? static_cast<Byte>((banks[slot / 2U] & 0xFEU) | (slot & 1U))
                     : banks[slot - 2U];
}

std::size_t expected_chr(Address addr, Byte mode, const std::array<Byte, 8>& banks) {
    return static_cast<std::size_t>(expected_chr_bank(addr, mode, banks)) * 0x800U +
           (addr & 0x07FFU);
}

TEST_CASE("Mapper 460 validates its NES 2.0 ROM CHR-RAM and CIRAM geometry",
          "[mapper460][geometry][ram]") {
    for (std::size_t prg = 0x008000UZ; prg <= kPrgSize; prg *= 2U) {
        CAPTURE(prg);
        REQUIRE(Mapper460::validate_geometry(header(), prg, kChrSize));
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            REQUIRE(mapper->read_chr(addr) == 0U);
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
        REQUIRE_FALSE(Mapper460::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 197U, 459U, 461U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, kChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, kChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, kChrSize);
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
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x40000UZ, kChrSize - 1U, kChrSize + 1U, kChrSize * 2U}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, kChrSize);
    }
    for (unsigned field = 0U; field < 7U; ++field) {
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
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x1000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, kChrSize);
    }
}

TEST_CASE("Mapper 460 decodes and WRAM-gates every extra-register address",
          "[mapper460][decoding][bus][ram]") {
    constexpr std::array<Byte, 6> kGates{0U, 0x40U, 0x7FU, 0x80U, 0xC0U, 0xFFU};
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->reset();
        const Byte gate = kGates[raw % kGates.size()];
        mapper->write_prg(0xA001U, gate);
        auto expected = state(*mapper);
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + gate + 0xA5U);
        mapper->write_prg(addr, value);
        if (raw >= 0x6000U && (gate & 0x80U) != 0U) {
            expected[kExtraOffset] = raw & 0x67U;
            expected[kExtraOffset + 1U] = (raw >> 8U) & 1U;
        }
        CAPTURE(raw, gate, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }

    for (const Byte gate : kGates) {
        mapper->reset();
        mapper->write_prg(0xA001U, gate);
        mapper->write_prg(0x7FFFU, gate);
        const auto enabled = (gate & 0x80U) != 0U;
        REQUIRE(state(*mapper)[kExtraOffset] == (enabled ? 0x67U : 0U));
        REQUIRE(state(*mapper)[kExtraOffset + 1U] == (enabled ? 1U : 0U));
        REQUIRE(state(*mapper)[kRamFlagsOffset] == (gate & 0xC0U));
        const auto latched = state(*mapper);
        mapper->write_prg(0x7FFFU, static_cast<Byte>(~gate));
        REQUIRE(state(*mapper) == latched);
    }
}

TEST_CASE("Mapper 460 decodes every MMC3 alias without unrelated side effects",
          "[mapper460][decoding][bus]") {
    auto mapper = board();
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
        CAPTURE(raw, value, even);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 460 applies outer PRG pins after MMC3 bank selection",
          "[mapper460][banking][prg]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 4U; ++outer) {
        extra(*mapper, outer);
        for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                const Byte r6 = static_cast<Byte>(value & 0x3FU);
                const Byte r7 = static_cast<Byte>((value ^ 0xA5U) & 0x3FU);
                bank(*mapper, 6U, static_cast<Byte>(value), mode);
                bank(*mapper, 7U, static_cast<Byte>(value ^ 0xA5U), mode);
                for (const auto addr : kPrgAddresses) {
                    require_prg(*mapper, addr, outer, mode, r6, r7);
                }
            }
        }
    }

    bank(*mapper, 6U, 0xABU, 0x40U);
    bank(*mapper, 7U, 0xCDU, 0x40U);
    extra(*mapper, 0x23U, 0x7E00U);
    for (Address addr = 0x8000U; addr != 0U; ++addr) {
        require_prg(*mapper, addr, 0x23U, 0x40U, 0x2BU, 0x0DU);
    }
}

TEST_CASE("Mapper 460 NROM modes duplicate R6 R7 and substitute CPU A14 only in NROM-256",
          "[mapper460][banking][prg]") {
    auto mapper = board();
    for (const Byte control : {Byte{0x20U}, Byte{0x40U}, Byte{0x60U}, Byte{0x43U}, Byte{0x63U}}) {
        extra(*mapper, control);
        for (const Byte mode : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                const Byte r6 = static_cast<Byte>(value & 0x3FU);
                const Byte r7 = static_cast<Byte>((value * 73U + 19U) & 0x3FU);
                bank(*mapper, 6U, static_cast<Byte>(value), mode);
                bank(*mapper, 7U, static_cast<Byte>(value * 73U + 19U), mode);
                for (const auto addr : kPrgAddresses) {
                    require_prg(*mapper, addr, control, mode, r6, r7);
                }
                const auto lower = expected_prg(0x8123U, control, mode, r6, r7);
                const auto upper = expected_prg(0xC123U, control, mode, r6, r7);
                if ((control & 0x40U) == 0U) {
                    continue;
                }
                if ((control & 0x20U) == 0U) {
                    REQUIRE(lower == upper);
                } else {
                    REQUIRE((lower ^ upper) == 0x4000U);
                }
            }
        }
    }
}

TEST_CASE("Mapper 460 wraps fitted PRG ROM only after pin decoding", "[mapper460][banking][prg]") {
    for (const std::size_t size : {0x008000UZ, 0x010000UZ, 0x020000UZ, 0x040000UZ}) {
        auto mapper = board(MirrorMode::Vertical, 0x23U, size);
        bank(*mapper, 6U, 0xFFU);
        bank(*mapper, 7U, 0xFEU);
        extra(*mapper, 0x63U);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, 0x63U, 0U, 0x3FU, 0x3EU, 0x23U, size);
        }
    }
}

TEST_CASE("Mapper 460 CHR ROM retains mapper 197.2 pairing inversion and A11 wiring",
          "[mapper460][banking][chr]") {
    constexpr std::array<Byte, 8> values{0xA2U, 0x8BU, 0x93U, 0xFCU, 0xB1U, 0xC7U, 0U, 0U};
    auto mapper = board();
    extra(*mapper, 0x04U);
    for (const Byte mode : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*mapper, reg, values[reg], mode);
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            for (const Address within : {Address{0U}, Address{0x155U}, Address{0x3FFU}}) {
                const auto addr = static_cast<Address>(slot * 0x400U + within);
                const auto physical = expected_chr(addr, mode, values);
                CAPTURE(mode, addr, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
            }
        }
    }

    mapper = board();
    extra(*mapper, 0x04U);
    bank(*mapper, 0U, 0xFEU, 0x80U);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto physical = expected_chr(addr, 0x80U, {0xFEU, 0U, 0U, 0U, 0U, 0U, 0U, 0U});
        CAPTURE(addr, physical);
        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
    }
}

TEST_CASE("Mapper 460 CHR RAM is writable unbanked and retained across soft reset",
          "[mapper460][banking][chr][ram][reset]") {
    auto mapper = board();
    bank(*mapper, 0U, 0xFEU, 0x80U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        const auto value = static_cast<Byte>(addr * 51U + (addr >> 8U) + 0xA7U);
        mapper->write_chr(addr, value);
        REQUIRE(mapper->read_chr(addr) == value);
        bank(*mapper, 0U, static_cast<Byte>(addr), 0x80U);
        REQUIRE(mapper->read_chr(addr) == value);
    }
    const auto populated = state(*mapper);
    extra(*mapper, 0x04U);
    REQUIRE(mapper->read_chr(0x155U) ==
            cell(expected_chr(0x155U, 0x80U, {0xFFU, 0U, 0U, 0U, 0U, 0U, 0U, 0U}), 0x55U));
    mapper->write_chr(0x155U, 0U);
    extra(*mapper, 0U);
    REQUIRE(state(*mapper) == populated);
    mapper->reset();
    REQUIRE(mapper->read_chr(0x155U) == populated[kChrRamOffset + 0x155U]);
    REQUIRE(state(*mapper) != populated);
    mapper->power_on();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 460 solder pad disables PRG reads without disabling register writes",
          "[mapper460][bus][decoding][prg]") {
    for (const bool connected : {false, true}) {
        auto mapper = board(MirrorMode::Vertical, 0U, kPrgSize, connected);
        extra(*mapper, 0x100U);
        for (const auto addr : kPrgAddresses) {
            CAPTURE(connected, addr);
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == (connected ? 0U : 0xFFU));
            if (connected) {
                REQUIRE(mapper->read_prg(addr) == 0U);
            } else {
                require_prg(*mapper, addr, 0x100U, 0U, 0U, 0U);
            }
        }
        bank(*mapper, 6U, 0x12U);
        extra(*mapper, 0x04U);
        require_prg(*mapper, 0x8000U, 0x04U, 0U, 0x12U, 0U);
        REQUIRE(mapper->read_chr(0U) == cell(0U, 0x55U));
    }

    auto mapper = board();
    REQUIRE(mapper->configure_board_jumpers(1U));
    REQUIRE(state(*mapper)[kConfigurationOffset] == 3U);
    REQUIRE(mapper->configure_board_jumpers(0U));
    REQUIRE(state(*mapper)[kConfigurationOffset] == 1U);
    const auto configured = state(*mapper);
    for (const Byte invalid : {Byte{2U}, Byte{3U}, Byte{127U}, Byte{255U}}) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(invalid));
        REQUIRE(state(*mapper) == configured);
    }
}

TEST_CASE("Mapper 460 uses MMC3 CIRAM control and exposes no RAM or expansion audio",
          "[mapper460][ciram][ram][audio][bus]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg((value & 1U) != 0U ? 0xBFFEU : 0xA000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        extra(*mapper, 0x1FFU);
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

    auto mapper = board();
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
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

TEST_CASE("Mapper 460 retains MMC3 IRQ acknowledgement and zero repeats", "[mapper460][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    extra(*mapper, 0x77U);
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

TEST_CASE("Mapper 460 reset clears the extra latch and every MMC3 register but not CHR RAM",
          "[mapper460][reset]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        REQUIRE(mapper->configure_board_jumpers(1U));
        mapper->write_chr(0x155U, 0xA7U);
        const auto cold = state(*mapper);
        extra(*mapper, 0x1FFU);
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
        require_prg(*mapper, 0xE000U, 0U, 0U, 0U, 0U);
        REQUIRE(mapper->read_chr(0x155U) == 0xA7U);

        extra(*mapper, 0x04U);
        bank(*mapper, 0U, 0xFEU, 0x80U);
        mapper->power_on();
        REQUIRE(state(*mapper) != cold);
        REQUIRE(state(*mapper)[kExtraOffset] == 0U);
        REQUIRE(mapper->read_chr(0x155U) == 0U);
    }
}

TEST_CASE("Mapper 460 bounded snapshots replay without ROM and preserve RAM and partial IRQ state",
          "[mapper460][state][banking][irq][ram]") {
    auto mapper = board();
    REQUIRE(mapper->configure_board_jumpers(1U));
    extra(*mapper, 0x67U);
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
    extra(*mapper, 0x63U);
    mapper->write_chr(0x155U, 0xA7U);
    mapper->write_chr(0x1FFFU, 0x5CU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kChrSize);
    REQUIRE(saved[kExtraOffset] == 0x63U);
    REQUIRE(saved[kIrqCounterOffset] == 2U);
    REQUIRE(saved[kIrqFlagsOffset] == 2U);
    REQUIRE(saved[kChrRamOffset + 0x155U] == 0xA7U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    REQUIRE(copy->configure_board_jumpers(1U));
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x63U, 0xC0U, 0x19U, 0x07U, 0x80U);
    }
    REQUIRE(copy->read_chr(0x155U) == 0xA7U);
    REQUIRE(copy->read_chr(0x1FFFU) == 0x5CU);
    extra(*copy, 0x67U);
    REQUIRE(copy->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0xC0U, {0xA2U, 0x8BU, 0U, 0U, 0U, 0U, 0U, 0U}), 0xD5U));
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

TEST_CASE("Mapper 460 rejects truncated damaged and mismatched states atomically",
          "[mapper460][state][ram]") {
    auto mapper = board();
    REQUIRE(mapper->configure_board_jumpers(1U));
    extra(*mapper, 0x67U);
    bank(*mapper, 0U, 0xFEU, 0xC0U);
    bank(*mapper, 6U, 0x09U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    extra(*mapper, 0x63U);
    mapper->write_chr(0x155U, 0xA7U);
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
        {{0U, 1U},
         {2U, 1U},
         {6U, 1U},
         {10U, 0U},
         {11U, 1U},
         {kExtraOffset, 0x08U},
         {kExtraOffset, 0x88U},
         {kExtraOffset + 1U, 0xFEU},
         {kBankSelectOffset, 8U},
         {kBankModeOffset, 1U},
         {kBankRegistersOffset + 6U, 0x40U},
         {kBankRegistersOffset + 7U, 0x40U},
         {kMirrorOffset, 2U},
         {kRamFlagsOffset, 1U}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    auto invalid_irq = saved;
    invalid_irq[kIrqFlagsOffset] = 8U;
    reject(invalid_irq);
    auto pending_without_enable = saved;
    pending_without_enable[kIrqFlagsOffset] = 4U;
    reject(pending_without_enable);
    for (const Byte reserved : {Byte{0x08U}, Byte{0x10U}, Byte{0x88U}}) {
        auto invalid = saved;
        invalid[kExtraOffset] = reserved;
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper460>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_pad = board();
    mismatch(wrong_pad);
    auto wrong_mirror = board(MirrorMode::Horizontal);
    REQUIRE(wrong_mirror->configure_board_jumpers(1U));
    mismatch(wrong_mirror);
    auto wrong_prg = board(MirrorMode::Vertical, 0U, kPrgSize / 2U);
    REQUIRE(wrong_prg->configure_board_jumpers(1U));
    mismatch(wrong_prg);
    auto wrong_chr = std::make_unique<Mapper460>(header(), rom(kPrgSize), rom(kChrSize / 2U));
    REQUIRE(wrong_chr->configure_board_jumpers(1U));
    mismatch(wrong_chr);

    auto changed = saved;
    changed[kExtraOffset] = 0x67U;
    changed[kBankRegistersOffset] = 0x5AU;
    changed[kBankRegistersOffset + 6U] = 0x0EU;
    changed[kChrRamOffset + 0x155U] = 0x39U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0x8000U, 0x67U, 0xC0U, 0x0EU, 0x07U);
    REQUIRE(mapper->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0xC0U, {0x5AU, 0U, 0U, 0U, 0U, 0U, 0U, 0U}), 0x55U));
    extra(*mapper, 0x63U);
    REQUIRE(mapper->read_chr(0x155U) == 0x39U);
}

} // namespace
