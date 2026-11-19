#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper526.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kDefaultPrgSize = 0x200000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kDefaultChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kStateSize = 24UZ;
constexpr std::size_t kPrgBanksOffset = 10UZ;
constexpr std::size_t kChrBanksOffset = 14UZ;
constexpr std::size_t kIrqCounterOffset = 22UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 526U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kDefaultPrgSize;
    result.chr_rom_size = kDefaultChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 37U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + (offset >> 16U) * 83U +
                             (offset >> 20U) * 101U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper526> board(std::size_t prg_size = kDefaultPrgSize,
                                 std::size_t chr_size = kDefaultChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper526::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper526>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x5AU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper526& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper526& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0234U);
    const auto irq = mapper.irq_pending();
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0234U) == chr);
    REQUIRE(mapper.irq_pending() == irq);
}

void require_prg(
    Mapper526& mapper, Address addr, std::size_t physical, std::size_t prg_size, Byte salt) {
    const auto offset = physical % prg_size;
    CAPTURE(addr, physical, offset, prg_size, salt);
    REQUIRE(mapper.read_prg(addr) == cell(offset, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(
    Mapper526& mapper, Address addr, std::size_t physical, std::size_t chr_size, Byte salt) {
    const auto offset = physical % chr_size;
    CAPTURE(addr, physical, offset, chr_size, salt);
    REQUIRE(mapper.read_chr(addr) == cell(offset, salt ^ 0x5AU));
}

TEST_CASE("Mapper 526 validates UNL-BJ-56 geometry and bounded initial state",
          "[mapper526][geometry][ram][ciram][state]") {
    for (const auto prg : {kMinPrgSize, 0x010000UZ, 0x040000UZ, kDefaultPrgSize}) {
        for (const auto chr : {kMinChrSize, 0x008000UZ, kDefaultChrSize}) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper526::validate_geometry(header(), prg, chr));
            auto mapper = board(prg, chr, MirrorMode::Vertical, 0x35U);
            auto different_rom = board(prg, chr, MirrorMode::Vertical, 0xA0U);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                require_prg(*mapper,
                            static_cast<Address>(0x8000U + slot * kPrgBankSize + 0x123U),
                            slot * kPrgBankSize + 0x123U,
                            prg,
                            0x35U);
            }
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                require_chr(*mapper,
                            static_cast<Address>(slot * kChrBankSize + 0x234U),
                            slot * kChrBankSize + 0x234U,
                            chr,
                            0x35U);
            }
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
        REQUIRE_FALSE(Mapper526::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 22U, 23U, 525U, 527U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kDefaultPrgSize, kDefaultChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x4000UZ, 0x6000UZ, 0x8001UZ, 0xC000UZ, kDefaultPrgSize * 2UZ}) {
        reject(header(), size, kDefaultChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x0400UZ, 0x1000UZ, 0x2001UZ, 0x3000UZ, kDefaultChrSize * 2UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror : {MirrorMode::Horizontal,
                              MirrorMode::FourScreen,
                              MirrorMode::SingleLower,
                              MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, kDefaultChrSize);
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
        reject(h, kDefaultPrgSize, kDefaultChrSize);
    }
}

TEST_CASE("Mapper 526 exhaustively decodes the probable 800F register mask",
          "[mapper526][decoding][banking][bus][irq]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = initial;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x8000U) {
            const auto reg = raw & 0x0FU;
            if (reg < 8U) {
                expected[kChrBanksOffset + reg] = value;
            } else if (reg < 12U) {
                expected[kPrgBanksOffset + reg - 8U] = value;
            }
        }

        CAPTURE(raw, value);
        mapper->write_prg(addr, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
}

TEST_CASE("Mapper 526 selects four independent full-byte 8 KiB PRG banks",
          "[mapper526][banking][prg][bus]") {
    for (const std::size_t size : {kMinPrgSize, 0x010000UZ, 0x040000UZ, kDefaultPrgSize}) {
        auto mapper = board(size, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
        std::array<Byte, 4> banks{0U, 1U, 2U, 3U};
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto value = static_cast<Byte>(raw ^ (slot * 0x55U));
                const auto alias = static_cast<Address>((raw & 0xF0U) << 8U);
                mapper->write_prg(static_cast<Address>((0x8008U | alias) + slot), value);
                banks[slot] = value;
                CAPTURE(size, slot, raw, value);
                for (unsigned window = 0U; window < 4U; ++window) {
                    for (const Address offset : {Address{0U}, Address{0x123U}, Address{0x1FFFU}}) {
                        require_prg(*mapper,
                                    static_cast<Address>(0x8000U + window * kPrgBankSize + offset),
                                    static_cast<std::size_t>(banks[window]) * kPrgBankSize + offset,
                                    size,
                                    0x35U);
                    }
                }
            }
        }
    }

    auto mapper = board(kMinPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0xA0U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_prg(static_cast<Address>(0xF008U + slot), static_cast<Byte>(0xFFU - slot));
    }
    for (Address addr = 0x8000U;; ++addr) {
        const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
        require_prg(
            *mapper, addr, (0xFFU - slot) * kPrgBankSize + (addr & 0x1FFFU), kMinPrgSize, 0xA0U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
}

TEST_CASE("Mapper 526 selects eight independent full-byte 1 KiB CHR banks and protects ROM",
          "[mapper526][banking][chr][bus]") {
    for (const std::size_t size : {kMinChrSize, 0x008000UZ, kDefaultChrSize}) {
        auto mapper = board(kDefaultPrgSize, size, MirrorMode::Vertical, 0x35U);
        std::array<Byte, 8> banks{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto value = static_cast<Byte>(raw ^ (slot * 0x33U));
                const auto alias = static_cast<Address>(((raw + slot) & 0xF0U) << 8U);
                mapper->write_prg(static_cast<Address>((0x8000U | alias) + slot), value);
                banks[slot] = value;
                CAPTURE(size, slot, raw, value);
                for (unsigned window = 0U; window < 8U; ++window) {
                    const auto addr = static_cast<Address>(window * kChrBankSize + 0x234U);
                    require_chr(*mapper,
                                addr,
                                static_cast<std::size_t>(banks[window]) * kChrBankSize + 0x234U,
                                size,
                                0x35U);
                    mapper->write_chr(addr, static_cast<Byte>(raw ^ 0xFFU));
                    require_chr(*mapper,
                                addr,
                                static_cast<std::size_t>(banks[window]) * kChrBankSize + 0x234U,
                                size,
                                0x35U);
                }
            }
        }
    }

    auto mapper = board(kMinPrgSize, kMinChrSize, MirrorMode::Vertical, 0xA0U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0xF000U + slot), static_cast<Byte>(0xF8U + slot));
    }
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto bank = 0xF8U + ((addr >> 10U) & 7U);
        require_chr(*mapper, addr, bank * kChrBankSize + (addr & 0x3FFU), kMinChrSize, 0xA0U);
    }
    const auto before = state(*mapper);
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xA5U);
    mapper->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 526 keeps vertical CIRAM and exposes no cartridge memory or audio",
          "[mapper526][ciram][bus][ram][audio]") {
    auto mapper = board();
    for (unsigned reg = 0U; reg < 16U; ++reg) {
        mapper->write_prg(static_cast<Address>(0x8000U + reg), static_cast<Byte>(reg * 17U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }
    for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x2012U);
    mapper->observe_ppu_read(0x2012U);
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->led_outputs() == std::nullopt);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 526 IRQ follows counter bit 12 through the complete 16-bit cycle",
          "[mapper526][irq][bus][state]") {
    auto mapper = board();
    for (unsigned cycle = 1U; cycle <= 65536U; ++cycle) {
        mapper->clock_cpu();
        CAPTURE(cycle);
        REQUIRE(mapper->irq_pending() == ((cycle & 0x1000U) != 0U));
    }
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
    REQUIRE(state(*mapper)[kIrqCounterOffset + 1U] == 0U);

    mapper->reset();
    for (unsigned cycle = 0U; cycle < 5000U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    auto acknowledged = state(*mapper);
    REQUIRE(acknowledged[kIrqCounterOffset] == 0x88U);
    REQUIRE(acknowledged[kIrqCounterOffset + 1U] == 0x03U);
    for (unsigned cycle = 0U; cycle < 3191U; ++cycle) {
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());

    for (unsigned group = 8U; group < 16U; ++group) {
        mapper->reset();
        for (unsigned cycle = 0U; cycle < 5000U; ++cycle) {
            mapper->clock_cpu();
        }
        mapper->write_prg(static_cast<Address>((group << 12U) | 0x000DU), 0xA5U);
        CAPTURE(group);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper)[kIrqCounterOffset] == 0x88U);
        REQUIRE(state(*mapper)[kIrqCounterOffset + 1U] == 0x03U);

        mapper->reset();
        for (unsigned cycle = 0U; cycle < 5000U; ++cycle) {
            mapper->clock_cpu();
        }
        mapper->write_prg(static_cast<Address>((group << 12U) | 0x000FU), 0x5AU);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
        REQUIRE(state(*mapper)[kIrqCounterOffset + 1U] == 0U);
    }

    mapper->reset();
    for (unsigned cycle = 0U; cycle < 5000U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto before = state(*mapper);
    mapper->write_prg(0x800CU, 0U);
    mapper->write_prg(0x800EU, 0U);
    for (unsigned cycle = 0U; cycle < 2048U; ++cycle) {
        mapper->clock_audio();
        mapper->clock_irq_counter();
        mapper->clock_cpu_falling_edge();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 526 reset restores linear banks vertical CIRAM and a stopped IRQ line",
          "[mapper526][reset][banking][ciram][irq]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
    const auto cold = state(*mapper);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8008U + slot), static_cast<Byte>(0x80U + slot));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8000U + slot), static_cast<Byte>(0x40U + slot));
    }
    for (unsigned cycle = 0U; cycle < 5000U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper) != cold);

    mapper->reset();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        require_prg(*mapper,
                    static_cast<Address>(0x8123U + slot * kPrgBankSize),
                    slot * kPrgBankSize + 0x123U,
                    kDefaultPrgSize,
                    0x35U);
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        require_chr(*mapper,
                    static_cast<Address>(slot * kChrBankSize + 0x234U),
                    slot * kChrBankSize + 0x234U,
                    kDefaultChrSize,
                    0x35U);
    }

    mapper->write_prg(0x8008U, 0xFFU);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
    }
    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
}

TEST_CASE("Mapper 526 bounded snapshots replay without ROM bytes",
          "[mapper526][state][atomic][banking][irq]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
    constexpr std::array<Byte, 4> kPrgBanks{0xF1U, 0x82U, 0x7FU, 0x03U};
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8008U + slot), kPrgBanks[slot]);
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8000U + slot),
                          static_cast<Byte>(0x20U + slot * 17U));
    }
    for (unsigned cycle = 0U; cycle < 5000U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x20UZ);
    REQUIRE(saved[0U] == 0x60U);
    REQUIRE(saved[1U] == 0x52U);
    REQUIRE(saved[kPrgBanksOffset] == 0xF1U);
    REQUIRE(saved[kPrgBanksOffset + 3U] == 0x03U);
    REQUIRE(saved[kChrBanksOffset] == 0x20U);
    REQUIRE(saved[kChrBanksOffset + 7U] == 0x97U);
    REQUIRE(saved[kIrqCounterOffset] == 0x88U);
    REQUIRE(saved[kIrqCounterOffset + 1U] == 0x13U);
    REQUIRE(mapper->irq_pending());

    auto copy = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    require_prg(*copy, 0x8123U, 0xF1U * kPrgBankSize + 0x123U, kDefaultPrgSize, 0xA0U);
    require_prg(*copy, 0xF123U, 0x03U * kPrgBankSize + 0x1123U, kDefaultPrgSize, 0xA0U);
    require_chr(*copy, 0x0234U, 0x20U * kChrBankSize + 0x234U, kDefaultChrSize, 0xA0U);
    require_chr(*copy, 0x1E34U, 0x97U * kChrBankSize + 0x234U, kDefaultChrSize, 0xA0U);
    REQUIRE(copy->irq_pending());
    mapper->clock_cpu();
    copy->clock_cpu();
    REQUIRE(state(*copy) == state(*mapper));
}

TEST_CASE("Mapper 526 rejects truncated damaged and mismatched states atomically",
          "[mapper526][state][atomic][banking][irq]") {
    auto mapper = board();
    mapper->write_prg(0x8008U, 0xF1U);
    mapper->write_prg(0x8000U, 0xA5U);
    mapper->write_prg(0x8007U, 0x5AU);
    for (unsigned cycle = 0U; cycle < 5000U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    auto trailing = saved;
    trailing.resize(1024U, 0U);
    load_rejected(*mapper, trailing);
    for (std::size_t offset = 0U; offset < kPrgBanksOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        load_rejected(*mapper, invalid);
    }

    auto wrong_prg = board(kDefaultPrgSize / 2U);
    load_rejected(*wrong_prg, saved);
    auto wrong_chr = board(kDefaultPrgSize, kDefaultChrSize / 2U);
    load_rejected(*wrong_chr, saved);

    auto changed = saved;
    changed[kPrgBanksOffset] = 0xFFU;
    changed[kChrBanksOffset] ^= 0xFFU;
    changed[kIrqCounterOffset] = 0x00U;
    changed[kIrqCounterOffset + 1U] = 0x00U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0x8123U, 0xFFU * kPrgBankSize + 0x123U, kDefaultPrgSize, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
}

} // namespace
