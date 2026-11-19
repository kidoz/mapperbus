#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper455.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::size_t kExtraOffset = 12UZ;
constexpr std::size_t kRamOffset = 29UZ;
constexpr std::size_t kStateSize = kRamOffset + kWramSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

enum class RamKind { Absent, Volatile, Battery };

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, RamKind ram = RamKind::Volatile) {
    INesHeader result{};
    result.mapper_number = 455U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = ram == RamKind::Battery;
    result.prg_ram_size = ram == RamKind::Volatile ? kWramSize : 0U;
    result.prg_nvram_size = ram == RamKind::Battery ? kWramSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 15U) * 43U +
                             (offset >> 16U) * 59U + (offset >> 17U) * 61U + (offset >> 18U) * 71U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper455> board(std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 RamKind ram = RamKind::Volatile,
                                 Byte salt = 0U) {
    const auto h = header(mirror, ram);
    REQUIRE(Mapper455::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper455>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper455& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper455& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

Address make_extra(Address addr, Byte value) {
    return static_cast<Address>((addr & 0x27U) | (static_cast<Address>(value & 0x1FU) << 8U));
}

void extra(Mapper455& mapper, Address addr, Byte value) {
    mapper.write_expansion(addr, value);
}

unsigned outer(Address latch) {
    return ((latch & 0x20U) >> 4U) | ((latch & 0x04U) >> 2U);
}

std::size_t expected_prg(Address addr, Address latch, Byte mode, Byte r6, Byte r7) {
    const unsigned outer_bank = outer(latch);
    if ((latch & 0x0100U) != 0U) {
        const bool nrom256 = (latch & 0x0200U) != 0U;
        unsigned selected = (latch >> 10U) & 7U;
        if (nrom256) {
            selected = (selected & ~1U) | ((addr >> 14U) & 1U);
        }
        return outer_bank * 0x020000UZ + selected * 0x004000UZ + (addr & 0x3FFFU);
    }

    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned mask = (latch & 0x02U) != 0U ? 0x1FU : 0x0FU;
    const unsigned selected =
        slot == 1U                                                                       ? r7 & mask
        : (((mode & 0x40U) == 0U && slot == 0U) || ((mode & 0x40U) != 0U && slot == 2U)) ? r6 & mask
        : slot == 3U ? mask
                     : mask - 1U;
    return (selected | (outer_bank << 4U)) * kPrgBankSize + (addr & 0x1FFFU);
}

std::size_t expected_chr(Address addr,
                         Address latch,
                         Byte mode,
                         const std::array<Byte, 6>& values) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner =
        slot < 4U ? (values[slot / 2U] & 0xFEU) | (slot & 1U) : values[slot - 2U];
    const unsigned mask = (latch & 1U) != 0U ? 0xFFU : 0x7FU;
    const unsigned selected = (inner & mask) | (outer(latch) << 7U);
    return selected * kChrBankSize + (addr & 0x3FFU);
}

void require_prg(Mapper455& mapper, Address addr, Address latch, Byte mode, Byte r6, Byte r7) {
    const auto physical = expected_prg(addr, latch, mode, r6, r7);
    CAPTURE(addr, latch, mode, r6, r7, physical);
    REQUIRE(physical < kMaxPrgSize);
    REQUIRE(mapper.read_prg(addr) == cell(physical));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 455 validates decoded ROM bounds memory and CIRAM metadata",
          "[mapper455][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const auto ram : {RamKind::Absent, RamKind::Volatile, RamKind::Battery}) {
            CAPTURE(mirror, ram);
            auto mapper = board(kMaxPrgSize, kMaxChrSize, mirror, ram);
            auto different_rom = board(kMaxPrgSize, kMaxChrSize, mirror, ram, 0x80U);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == (ram == RamKind::Absent ? kRamOffset : kStateSize));
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().size() == (ram == RamKind::Battery ? kWramSize : 0U));
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper455::validate_geometry(header(), size, kMaxChrSize));
    }
    for (std::size_t size = kMinChrSize; size <= kMaxChrSize; size += kChrBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper455::validate_geometry(header(), kMaxPrgSize, size));
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
        REQUIRE_FALSE(Mapper455::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 441U, 454U, 456U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, kMinChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, kMinChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x80001UZ, 0x100000UZ}) {
        reject(header(), size, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2200UZ, 0x80001UZ, 0x100000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, kMinChrSize);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x1000U;
            break;
        case 1U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = 0x2000U;
            break;
        case 2U:
            h.prg_ram_size = 0x2000U;
            h.prg_nvram_size = 0x2000U;
            break;
        case 3U:
            h.has_battery = true;
            break;
        case 4U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = 0x2000U;
            break;
        case 5U:
            h.chr_ram_size = 0x2000U;
            break;
        case 6U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, kMinChrSize);
    }
}

TEST_CASE("Mapper 455 decodes every expansion alias and write data bit",
          "[mapper455][decoding][bus]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const bool decoded = raw >= 0x4020U && (raw & 0xE000U) == 0x4000U;
        mapper->write_expansion(addr, value);
        auto expected = cold;
        if (decoded) {
            expected[kExtraOffset] = static_cast<Byte>(raw & 0x27U);
            expected[kExtraOffset + 1U] = value & 0x1FU;
        }
        CAPTURE(raw, value, decoded);
        REQUIRE(mapper->maps_expansion(addr) == decoded);
        REQUIRE(mapper->expansion_read_mask(addr) == (decoded ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(state(*mapper) == expected);
    }

    mapper->reset();
    for (Address addr = 0U; addr < 0x6000U; ++addr) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
    }
    REQUIRE(state(*mapper) == cold);
}

TEST_CASE("Mapper 455 decodes every MMC3 alias without unrelated side effects",
          "[mapper455][decoding][bus]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    mapper->reset();
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
                expected[14U] = value & 7U;
                expected[15U] = value & 0xC0U;
            } else {
                expected[16U] = value;
            }
        } else if (raw < 0xC000U) {
            if (even) {
                expected[24U] = (value & 1U) ^ 1U;
            } else {
                expected[28U] = value & 0xC0U;
            }
        } else if (raw < 0xE000U) {
            if (even) {
                expected[26U] = value;
            } else {
                expected[27U] = 1U;
            }
        } else if (!even) {
            expected[27U] = 2U;
        }
        CAPTURE(raw, value, even);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 455 combines outer pins inner sizes and NROM modes in both MMC3 modes",
          "[mapper455][decoding][banking][prg]") {
    auto mapper = board();
    constexpr Byte kR6 = 0x35U;
    constexpr Byte kR7 = 0x27U;
    for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
        bank(*mapper, 6U, 0xB5U, mode);
        bank(*mapper, 7U, 0xE7U, mode);
        for (unsigned outer_bank = 0U; outer_bank < 4U; ++outer_bank) {
            for (unsigned inner_256 = 0U; inner_256 < 2U; ++inner_256) {
                const auto addr =
                    static_cast<Address>(0x4100U | ((outer_bank & 2U) << 4U) |
                                         ((outer_bank & 1U) << 2U) | (inner_256 << 1U));
                for (unsigned data = 0U; data < 32U; ++data) {
                    const auto value = static_cast<Byte>(data);
                    extra(*mapper, addr, value);
                    const auto latch = make_extra(addr, value);
                    REQUIRE(state(*mapper)[kExtraOffset] == (latch & 0xFFU));
                    REQUIRE(state(*mapper)[kExtraOffset + 1U] == (latch >> 8U));
                    for (const auto cpu_addr : kPrgAddresses) {
                        require_prg(*mapper, cpu_addr, latch, mode, kR6, kR7);
                    }
                }
            }
        }
    }

    constexpr std::array<Address, 6> representatives{
        0x0000U, 0x0107U, 0x020BU, 0x030FU, 0x1416U, 0x1F27U};
    bank(*mapper, 6U, kR6, 0x40U);
    bank(*mapper, 7U, kR7, 0x40U);
    for (const auto latch : representatives) {
        extra(*mapper,
              static_cast<Address>(0x4100U | (latch & 0x27U)),
              static_cast<Byte>(latch >> 8U));
        for (Address addr = 0x8000U; addr != 0U; ++addr) {
            require_prg(*mapper, addr, latch, 0x40U, kR6, kR7);
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, 0x018000UZ, 0x048000UZ}) {
        auto fitted = board(size, kMaxChrSize);
        bank(*fitted, 6U, 0xFFU, 0x40U);
        bank(*fitted, 7U, 0xFFU, 0x40U);
        for (const Address latch : {Address{0x0027U}, Address{0x1F27U}}) {
            extra(*fitted,
                  static_cast<Address>(0x4100U | (latch & 0x27U)),
                  static_cast<Byte>(latch >> 8U));
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, latch, 0x40U, 0x3FU, 0x3FU) % size;
                CAPTURE(size, latch, addr, physical);
                REQUIRE(fitted->read_prg(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 455 routes outer CHR pins and inner size through paired inverted banks",
          "[mapper455][decoding][banking][chr][ram]") {
    constexpr std::array<Byte, 6> values = {0xA2U, 0x8BU, 0x93U, 0xFCU, 0xB1U, 0xC7U};
    auto mapper = board();
    for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*mapper, reg, values[reg], mode);
        }
        for (unsigned outer_bank = 0U; outer_bank < 4U; ++outer_bank) {
            for (unsigned inner_256 = 0U; inner_256 < 2U; ++inner_256) {
                const auto addr = static_cast<Address>(0x4100U | ((outer_bank & 2U) << 4U) |
                                                       ((outer_bank & 1U) << 2U) | inner_256);
                extra(*mapper, addr, 0x1DU);
                const auto latch = make_extra(addr, 0x1DU);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto pattern_addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                    const auto physical = expected_chr(pattern_addr, latch, mode, values);
                    CAPTURE(mode, latch, pattern_addr, physical);
                    REQUIRE(physical < kMaxChrSize);
                    REQUIRE(mapper->read_chr(pattern_addr) == cell(physical, 0x55U));
                    mapper->write_chr(pattern_addr, static_cast<Byte>(physical ^ 0xFFU));
                    REQUIRE(mapper->read_chr(pattern_addr) == cell(physical, 0x55U));
                }
            }
        }
    }

    for (const std::size_t size : {kMinChrSize, 0x003000UZ, 0x018000UZ, 0x028000UZ}) {
        auto fitted = board(kMaxPrgSize, size);
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*fitted, reg, values[reg]);
        }
        extra(*fitted, 0x4127U, 0x1FU);
        for (const Address addr : {0x0000U, 0x03FFU, 0x0800U, 0x1000U, 0x1555U, 0x1FFFU, 0x3FFFU}) {
            const auto physical = expected_chr(addr, 0x1F27U, 0U, values) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_chr(addr) == cell(physical, 0x55U));
        }
    }
}

TEST_CASE("Mapper 455 uses MMC3 horizontal vertical console CIRAM only", "[mapper455][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, kMaxChrSize, initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg((value & 1U) != 0U ? 0xBFFEU : 0xA000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        extra(*mapper, 0x5127U, 0x1FU);
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

TEST_CASE("Mapper 455 gates optional WRAM and exposes no unrelated expansion or audio",
          "[mapper455][bus][ram][irq][audio]") {
    auto absent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    REQUIRE_FALSE(absent->maps_prg(0x6000U));
    REQUIRE(absent->prg_read_mask(0x7FFFU) == 0U);
    REQUIRE(absent->read_prg(0x6123U) == 0U);
    REQUIRE(absent->battery_ram().empty());
    absent->write_prg(0x6123U, 0xA5U);
    REQUIRE(absent->read_prg(0x6123U) == 0U);

    auto mapper = board();
    mapper->write_prg(0x6123U, 0xA5U);
    REQUIRE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    extra(*mapper, 0x4127U, 0x1FU);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    REQUIRE(mapper->prg_read_mask(0x6123U) == 0U);
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->write_prg(0xA001U, 0xC0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);

    mapper->reset();
    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x4020U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

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
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 455 MMC3 IRQ latch reload acknowledge disable and zero repeats",
          "[mapper455][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    extra(*mapper, 0x5127U, 0x1FU);
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
    for (unsigned clock = 0U; clock < 3U; ++clock) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
    }
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 455 reset restores registers and cold power clears only volatile WRAM",
          "[mapper455][reset][ram]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    extra(*mapper, 0x5127U, 0x1FU);
    bank(*mapper, 6U, 0x19U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper)[kExtraOffset] == 0U);
    REQUIRE(state(*mapper)[kExtraOffset + 1U] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);

    auto persistent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Battery);
    std::array<Byte, kWramSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0xA5U);
    }
    persistent->set_battery_ram(bytes);
    REQUIRE(persistent->battery_ram().size() == kWramSize);
    persistent->write_prg(0x6123U, 0x07U);
    persistent->power_on();
    REQUIRE(persistent->read_prg(0x6123U) == 0x07U);
    REQUIRE(persistent->read_prg(0x6124U) == bytes[0x124U]);
    REQUIRE(state(*persistent)[kExtraOffset] == 0U);
    persistent->set_battery_ram(std::array<Byte, 4>{9U, 8U, 7U, 6U});
    REQUIRE(persistent->read_prg(0x6003U) == 6U);
    REQUIRE(persistent->read_prg(0x6004U) == bytes[4U]);
}

TEST_CASE("Mapper 455 bounded snapshots replay without ROM and preserve partial IRQ state",
          "[mapper455][state][banking][ram][irq]") {
    auto mapper = board();
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        mapper->write_prg(addr, static_cast<Byte>(cell(addr)));
    }
    extra(*mapper, 0x5126U, 0x1BU);
    bank(*mapper, 6U, 0x19U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    bank(*mapper, 0U, 0xA2U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    const auto latch = make_extra(0x5126U, 0x1BU);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8221U);
    REQUIRE(saved[kExtraOffset] == (latch & 0xFFU));
    REQUIRE(saved[kExtraOffset + 1U] == (latch >> 8U));

    auto copy = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Volatile, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, latch, 0xC0U, 0x19U, 0x07U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    REQUIRE(copy->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, latch, 0xC0U, {0xA2U, 0U, 0U, 0U, 0U, 0U}), 0xD5U));
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(cell(addr)));
    }
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

    auto absent = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    REQUIRE(state(*absent).size() == kRamOffset);
    REQUIRE(state(*absent) ==
            state(*board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent, 0x35U)));
    REQUIRE(state(*absent) !=
            state(*board(kMinPrgSize, kMinChrSize, MirrorMode::Vertical, RamKind::Absent)));
}

TEST_CASE("Mapper 455 rejects truncated damaged and mismatched states atomically",
          "[mapper455][state][ram]") {
    auto mapper = board();
    extra(*mapper, 0x5126U, 0x1BU);
    bank(*mapper, 6U, 0x09U, 0xC0U);
    bank(*mapper, 7U, 0x07U, 0xC0U);
    mapper->write_prg(0x6123U, 0x05U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    const auto saved = state(*mapper);
    const auto active = saved;
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 16> invalid_fields = {{{0U, 0U},
                                                                              {2U, 1U},
                                                                              {6U, 1U},
                                                                              {10U, 0U},
                                                                              {12U, 0x80U},
                                                                              {13U, 0xE0U},
                                                                              {14U, 8U},
                                                                              {15U, 1U},
                                                                              {22U, 0x40U},
                                                                              {23U, 0x40U},
                                                                              {24U, 2U},
                                                                              {27U, 8U},
                                                                              {27U, 4U},
                                                                              {28U, 1U},
                                                                              {28U, 0x3FU},
                                                                              {11U, 0x40U}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        invalid[kRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(offset, value);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper455>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg = board(kMinPrgSize, kMaxChrSize);
    mismatch(wrong_prg);
    auto wrong_chr = board(kMaxPrgSize, kMinChrSize);
    mismatch(wrong_chr);
    auto no_ram = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Absent);
    mismatch(no_ram);
    auto battery = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, RamKind::Battery);
    mismatch(battery);
    auto wrong_mirror = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    mismatch(wrong_mirror);

    auto changed = saved;
    changed[kExtraOffset] = 0x27U;
    changed[kExtraOffset + 1U] = 0x12U;
    changed[16U] = 0x5AU;
    changed[22U] = 0x0EU;
    changed[kRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x05U ^ 0xFFU));
    REQUIRE(mapper->read_chr(0x1155U) ==
            cell(expected_chr(0x1155U, 0x1227U, 0xC0U, {0x5AU, 0U, 0U, 0U, 0U, 0U}), 0x55U));
}

} // namespace
