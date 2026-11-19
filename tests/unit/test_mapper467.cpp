#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper467.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrSize = 0x100000UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kBankSelectOffset = 11UZ;
constexpr std::size_t kBankRegistersOffset = 12UZ;
constexpr std::size_t kIrqLatchOffset = 20UZ;
constexpr std::size_t kIrqCounterOffset = 21UZ;
constexpr std::size_t kIrqFlagsOffset = 22UZ;
constexpr std::size_t kStateSize = 23UZ;
constexpr std::array<Address, 12> kPrgAddresses{0x8000U,
                                                0x8001U,
                                                0x8123U,
                                                0x9FFFU,
                                                0xA000U,
                                                0xBFFFU,
                                                0xC000U,
                                                0xC001U,
                                                0xD234U,
                                                0xE000U,
                                                0xFFFEU,
                                                0xFFFFU};
constexpr std::array<Address, 12> kChrAddresses{0x0000U,
                                                0x0001U,
                                                0x0123U,
                                                0x03FFU,
                                                0x0400U,
                                                0x07FFU,
                                                0x0800U,
                                                0x0FFFU,
                                                0x1000U,
                                                0x1555U,
                                                0x1FFEU,
                                                0x1FFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 467U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kPrgSize;
    result.chr_rom_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 13U) * 37U + (offset >> 14U) * 47U +
                             (offset >> 15U) * 43U + (offset >> 16U) * 59U + (offset >> 17U) * 61U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 79U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper467> board(MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper467::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper467>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x55U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper467& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void write_bank(Mapper467& mapper, unsigned index, Byte value, Byte select_flags = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(select_flags | index));
    mapper.write_prg(0x8001U, value);
}

std::size_t expected_prg(Address addr, Byte outer, Byte r6, Byte r7) {
    if ((outer & 0x20U) == 0U) {
        return static_cast<std::size_t>(outer & 0x1FU) * 0x4000U + (addr & 0x3FFFU);
    }
    const bool wide = (outer & 0x40U) != 0U;
    const unsigned slot = (addr - 0x8000U) / 0x2000U;
    const unsigned mask = wide ? 0x0FU : 0x03U;
    unsigned inner = slot == 0U   ? r6 & mask
                     : slot == 1U ? r7 & mask
                     : wide       ? (slot == 3U ? 15U : 14U)
                                  : (slot == 3U ? 3U : 2U);
    const std::size_t base = static_cast<std::size_t>(outer & (wide ? 0x38U : 0x3EU)) << 14U;
    return base + inner * 0x2000U + (addr & 0x1FFFU);
}

std::size_t expected_chr(Address addr, const std::array<Byte, 8>& banks, Byte outer) {
    const unsigned slot = (addr >> 10U) & 7U;
    const unsigned bank = slot < 4U ? (banks[slot / 2U] & 0xFEU) | (slot & 1U) : banks[slot - 2U];
    if ((outer & 0x40U) == 0U) {
        return (bank >> 2U) * 0x2000U + (addr & 0x1FFFU);
    }
    return 0x80000UZ + bank * 0x800U + (addr & 0x7FFU);
}

void require_prg(Mapper467& mapper,
                 Address addr,
                 Byte outer,
                 Byte r6,
                 Byte r7,
                 Byte salt = 0U,
                 std::size_t prg_size = kPrgSize) {
    const auto physical = expected_prg(addr, outer, r6, r7) % prg_size;
    CAPTURE(addr, outer, r6, r7, physical, salt, prg_size);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper467& mapper,
                 Address addr,
                 const std::array<Byte, 8>& banks,
                 Byte outer,
                 Byte salt = 0U,
                 std::size_t chr_size = kChrSize) {
    const auto physical = expected_chr(addr, banks, outer) % chr_size;
    CAPTURE(addr, outer, physical, salt, chr_size);
    REQUIRE(mapper.read_chr(addr) == cell(physical, static_cast<Byte>(salt ^ 0x55U)));
}

TEST_CASE("Mapper 467 validates NES 2.0 ROM geometry and owns no RAM or audio",
          "[mapper467][geometry][ram][audio]") {
    for (std::size_t prg = 0x008000UZ; prg <= kPrgSize; prg *= 2U) {
        for (std::size_t chr = 0x002000UZ; chr <= kChrSize; chr *= 2U) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper467::validate_geometry(header(), prg, chr));
        }
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8000U, 0U, 0U, 0U, 0x35U);
        require_prg(*mapper, 0xC000U, 0U, 0U, 0U, 0x35U);
        require_chr(*mapper, 0U, {}, 0U, 0x35U);
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
        REQUIRE_FALSE(Mapper467::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 197U, 466U, 468U, 4095U}) {
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
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x4000UZ, 0x6000UZ, 0x030000UZ, kPrgSize + 1U, kPrgSize * 2U}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x3000UZ, 0x018000UZ, kChrSize + 1U, kChrSize * 2U}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, kChrSize);
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

TEST_CASE("Mapper 467 decodes outer MMC3 and IRQ aliases with floating low reads",
          "[mapper467][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x8000U && raw < 0x9000U) {
            if ((raw & 1U) == 0U) {
                expected[kBankSelectOffset] = value & 7U;
            } else {
                expected[kBankRegistersOffset] = value;
            }
        } else if (raw >= 0x9000U && raw < 0xA000U) {
            expected[kOuterOffset] = value;
        } else if (raw >= 0xC000U && raw < 0xE000U) {
            if ((raw & 1U) == 0U) {
                expected[kIrqLatchOffset] = value;
            } else {
                expected[kIrqFlagsOffset] |= 1U;
            }
        } else if (raw >= 0xE000U && (raw & 1U) != 0U) {
            expected[kIrqFlagsOffset] |= 2U;
        }

        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        mapper->write_expansion(addr, static_cast<Byte>(~value));
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }

    mapper->power_on();
    mapper->write_prg_cpu(0x9FFFU, 0x5AU, true);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x5AU);
    mapper->write_prg_cpu(0x8FFEU, 0xC7U, false);
    REQUIRE(state(*mapper)[kBankSelectOffset] == 7U);
}

TEST_CASE("Mapper 467 ignores pointer inversion bits and masks only PRG data registers",
          "[mapper467][decoding][banking]") {
    auto mapper = board();
    for (unsigned index = 0U; index < 8U; ++index) {
        for (unsigned raw = 0U; raw < 0x100U; ++raw) {
            mapper->power_on();
            write_bank(*mapper, index, static_cast<Byte>(raw), 0xC0U);
            const auto current = state(*mapper);
            CAPTURE(index, raw);
            REQUIRE(current[kBankSelectOffset] == index);
            REQUIRE(current[kBankRegistersOffset + index] == (index >= 6U ? (raw & 0x3FU) : raw));
            for (const Byte flags : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
                mapper->write_prg(0x8000U, static_cast<Byte>(flags | index));
                REQUIRE(state(*mapper) == current);
            }
        }
    }
}

TEST_CASE("Mapper 467 NROM-128 mirrors every outer 16 KiB page in both CPU windows",
          "[mapper467][banking][prg]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x100U; ++raw) {
        const auto outer = static_cast<Byte>(raw);
        if ((outer & 0x20U) != 0U) {
            continue;
        }
        mapper->write_prg(0x9000U, outer);
        write_bank(*mapper, 6U, 0x3FU);
        write_bank(*mapper, 7U, 0x2AU);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, outer, 0x3FU, 0x2AU);
        }
        REQUIRE(expected_prg(0x8123U, outer, 0x3FU, 0x2AU) ==
                expected_prg(0xC123U, outer, 0x3FU, 0x2AU));
    }

    mapper->write_prg(0x9000U, 0x9FU);
    for (Address addr = 0x8000U; addr != 0U; ++addr) {
        require_prg(*mapper, addr, 0x9FU, 0x3FU, 0x2AU);
    }
}

TEST_CASE("Mapper 467 MMC3 PRG modes apply outer pins before fixed inner banks",
          "[mapper467][banking][prg]") {
    auto mapper = board();
    constexpr std::array<Byte, 7> inner_values{0U, 1U, 2U, 3U, 14U, 15U, 0x3FU};
    for (unsigned raw = 0U; raw < 0x100U; ++raw) {
        const auto outer = static_cast<Byte>(raw);
        if ((outer & 0x20U) == 0U) {
            continue;
        }
        for (const auto r6 : inner_values) {
            for (const auto r7 : inner_values) {
                mapper->power_on();
                mapper->write_prg(0x9ABCU, outer);
                write_bank(*mapper, 6U, r6, 0xC0U);
                write_bank(*mapper, 7U, r7, 0xC0U);
                for (const auto addr : kPrgAddresses) {
                    require_prg(*mapper, addr, outer, r6, r7);
                }
            }
        }
    }
}

TEST_CASE("Mapper 467 selects 8 KiB lower or 2 KiB upper CHR pages and keeps ROM immutable",
          "[mapper467][banking][chr][ram]") {
    auto mapper = board();
    for (const Byte outer : {Byte{0x20U}, Byte{0x60U}}) {
        for (unsigned index = 0U; index < 6U; ++index) {
            for (unsigned raw = 0U; raw < 0x100U; ++raw) {
                mapper->power_on();
                mapper->write_prg(0x9000U, outer);
                write_bank(*mapper, index, static_cast<Byte>(raw), 0xC0U);
                std::array<Byte, 8> banks{};
                banks[index] = static_cast<Byte>(raw);
                for (const auto addr : kChrAddresses) {
                    require_chr(*mapper, addr, banks, outer);
                    const auto before = mapper->read_chr(addr);
                    mapper->write_chr(addr, static_cast<Byte>(~before));
                    REQUIRE(mapper->read_chr(addr) == before);
                }
            }
        }
    }
}

TEST_CASE("Mapper 467 wraps smaller fitted ROMs only after decoding all address pins",
          "[mapper467][geometry][banking]") {
    constexpr std::size_t prg_size = 0x008000UZ;
    constexpr std::size_t chr_size = 0x002000UZ;
    auto mapper = board(MirrorMode::Vertical, 0x35U, prg_size, chr_size);
    const std::array<Byte, 8> banks{0xFEU, 0x81U, 0x42U, 0xA3U, 0x24U, 0xF5U, 0x3FU, 0x2AU};
    for (const Byte outer : {Byte{0x1FU}, Byte{0x3FU}, Byte{0x7FU}, Byte{0xFFU}}) {
        mapper->write_prg(0x9000U, outer);
        for (unsigned index = 0U; index < banks.size(); ++index) {
            write_bank(*mapper, index, banks[index]);
        }
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, outer, banks[6U], banks[7U], 0x35U, prg_size);
        }
        for (const auto addr : kChrAddresses) {
            require_chr(*mapper, addr, banks, outer, 0x35U, chr_size);
        }
    }
}

TEST_CASE("Mapper 467 outer mirroring overrides MMC3 and header CIRAM without cartridge RAM",
          "[mapper467][ciram][ram][audio][bus]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        for (const Byte outer : {Byte{0U}, Byte{0x80U}, Byte{0x20U}, Byte{0xA0U}}) {
            mapper->write_prg(0x9000U, outer);
            CAPTURE(initial, outer);
            REQUIRE(mapper->mirror_mode() ==
                    ((outer & 0x80U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            const auto before = state(*mapper);
            mapper->write_prg(0xA000U, static_cast<Byte>(~outer));
            mapper->write_prg(0xA001U, 0U);
            mapper->write_prg(0xBFFEU, 1U);
            mapper->write_prg(0xBFFFU, 0xC0U);
            REQUIRE(state(*mapper) == before);
        }
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
    }

    auto mapper = board();
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_irq_counter();
    mapper->acknowledge_irq();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 467 MMC3 IRQ reload acknowledgement disable and zero-latch repeats",
          "[mapper467][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0xA5U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 2U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 1U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE(mapper->irq_pending());

    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 2U);
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xD000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xFFFEU, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 467 reset clears outer bank MMC3 IRQ and selects vertical NROM",
          "[mapper467][reset][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        const auto cold = state(*mapper);
        mapper->write_prg(0x9000U, 0xFFU);
        for (unsigned index = 0U; index < 8U; ++index) {
            write_bank(*mapper, index, static_cast<Byte>(index * 17U + 0x3FU), 0xC0U);
        }
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

        mapper->reset();
        CAPTURE(initial);
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        require_prg(*mapper, 0xFFFFU, 0U, 0U, 0U);

        mapper->write_prg(0x9000U, 0xAAU);
        mapper->write_prg(0xC000U, 1U);
        mapper->write_prg(0xE001U, 0U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }
}

TEST_CASE("Mapper 467 bounded snapshots replay banks and partial IRQ state without ROM",
          "[mapper467][state][banking]") {
    auto mapper = board();
    constexpr Byte outer = 0xBFU;
    const std::array<Byte, 8> banks{0xFEU, 0x81U, 2U, 3U, 4U, 5U, 0x2AU, 0x35U};
    mapper->write_prg(0x9000U, outer);
    for (unsigned index = 0U; index < banks.size(); ++index) {
        write_bank(*mapper, index, banks[index], 0xC0U);
    }
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == outer);
    REQUIRE(saved[kIrqCounterOffset] == 2U);
    REQUIRE(saved[kIrqFlagsOffset] == 2U);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, outer, banks[6U], banks[7U], 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, banks, outer, 0x80U);
    }
    copy->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE_FALSE(copy->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 467 rejects truncated damaged and mismatched states atomically",
          "[mapper467][state]") {
    auto mapper = board();
    mapper->write_prg(0x9000U, 0xFFU);
    const std::array<Byte, 8> banks{0xFEU, 0x81U, 2U, 3U, 4U, 5U, 0x2AU, 0x35U};
    for (unsigned index = 0U; index < banks.size(); ++index) {
        write_bank(*mapper, index, banks[index]);
    }
    mapper->write_prg(0xC000U, 0U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 9> invalid_fields = {
        {{0U, 1U},
         {1U, 1U},
         {2U, 1U},
         {6U, 1U},
         {kBankSelectOffset, 8U},
         {kBankRegistersOffset + 6U, 0x40U},
         {kBankRegistersOffset + 7U, 0xFFU},
         {kIrqFlagsOffset, 8U},
         {kIrqFlagsOffset, 4U}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = board(MirrorMode::Vertical, 0U, kPrgSize / 2U, kChrSize);
    auto wrong_chr = board(MirrorMode::Vertical, 0U, kPrgSize, kChrSize / 2U);
    for (auto* other : {&wrong_prg, &wrong_chr}) {
        const auto before = state(**other);
        StateReader reader(saved);
        (*other)->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(**other) == before);
    }
}

} // namespace
