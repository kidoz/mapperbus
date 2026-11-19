#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper530.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x040000UZ;
constexpr std::size_t kChrSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kStateSize = 38UZ;
constexpr std::size_t kPrgBanksOffset = 11UZ;
constexpr std::size_t kChrBanksOffset = 13UZ;
constexpr std::size_t kMirrorOffset = 29UZ;
constexpr std::size_t kModeOffset = 30UZ;
constexpr std::size_t kIrqLatchOffset = 31UZ;
constexpr std::size_t kIrqCounterOffset = 32UZ;
constexpr std::size_t kIrqPrescalerOffset = 33UZ;
constexpr std::size_t kIrqControlOffset = 35UZ;
constexpr std::size_t kIrqEnabledOffset = 36UZ;
constexpr std::size_t kIrqPendingOffset = 37UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 530U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kPrgSize;
    result.chr_rom_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 37U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + (offset >> 16U) * 71U +
                             (offset >> 18U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper530> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper530::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper530>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x5AU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper530& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper530& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0234U);
    const auto mirror = mapper.mirror_mode();
    const auto irq = mapper.irq_pending();
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0234U) == chr);
    REQUIRE(mapper.mirror_mode() == mirror);
    REQUIRE(mapper.irq_pending() == irq);
}

std::size_t prg_bank(std::size_t bank) {
    return (bank & ~0x05UZ) | ((bank & 0x01UZ) << 2U) | ((bank & 0x04UZ) >> 2U);
}

std::size_t chr_bank(std::size_t bank) {
    return (bank & ~0x60UZ) | ((bank & 0x20UZ) << 1U) | ((bank & 0x40UZ) >> 1U);
}

void require_prg(Mapper530& mapper,
                 Address addr,
                 std::size_t physical,
                 Byte salt = 0U,
                 std::size_t prg_size = kPrgSize) {
    CAPTURE(addr, physical, salt, prg_size);
    REQUIRE(mapper.read_prg(addr) == cell(physical % prg_size, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper530& mapper,
                 Address addr,
                 std::size_t physical,
                 Byte salt = 0U,
                 std::size_t chr_size = kChrSize) {
    CAPTURE(addr, physical, salt, chr_size);
    REQUIRE(mapper.read_chr(addr) == cell(physical % chr_size, salt ^ 0x5AU));
}

void set_chr(Mapper530& mapper, unsigned slot, unsigned bank) {
    const auto base = static_cast<Address>(0xB000U + slot / 2U * 0x1000U + (slot & 1U) * 2U);
    mapper.write_prg(base, static_cast<Byte>(bank & 0x0FU));
    mapper.write_prg(static_cast<Address>(base + 1U), static_cast<Byte>((bank >> 4U) & 0x1FU));
}

TEST_CASE("Mapper 530 validates VRC4f ROM geometry and bounded cold state",
          "[mapper530][geometry][ram][ciram][state]") {
    for (const std::size_t prg : {0x008000UZ, 0x010000UZ, 0x020000UZ, kPrgSize}) {
        for (const std::size_t chr :
             {0x002000UZ, 0x004000UZ, 0x008000UZ, 0x010000UZ, 0x020000UZ, 0x040000UZ, kChrSize}) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(prg, chr, mirror);
                REQUIRE(Mapper530::validate_geometry(header(mirror), prg, chr));
            }
        }
    }

    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, kChrSize, mirror, 0x35U);
        auto different_rom = board(kPrgSize, kChrSize, mirror, 0xA0U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8123U, 0x0123U, 0x35U);
        require_prg(*mapper, 0xA123U, prg_bank(1U) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xC123U, prg_bank(30U) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xE123U, prg_bank(31U) * kPrgBankSize + 0x0123U, 0x35U);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            require_chr(
                *mapper, static_cast<Address>(slot * kChrBankSize + 0x0234U), 0x0234U, 0x35U);
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
        REQUIRE_FALSE(Mapper530::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 23U, 25U, 527U, 529U, 531U, 4095U}) {
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
         {0UZ, 1UZ, 0x2000UZ, 0x4000UZ, 0x6000UZ, 0x8001UZ, 0xC000UZ, 0x080000UZ}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x0400UZ, 0x1000UZ, 0x2001UZ, 0x3000UZ, 0x100000UZ}) {
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

TEST_CASE("Mapper 530 exhaustively decodes VRC4f A0 A1 and floats unmapped CPU reads",
          "[mapper530][decoding][banking][bus][ram][irq][state]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = initial;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x8000U) {
            const auto group = raw >> 12U;
            const auto port = raw & 3U;
            if (group == 8U || group == 10U) {
                expected[kPrgBanksOffset + (group == 8U ? 0U : 1U)] = value & 0x1FU;
            } else if (group == 9U && port == 0U) {
                constexpr std::array<Byte, 4> modes{1U, 0U, 3U, 4U};
                expected[kMirrorOffset] = modes[value & 3U];
            } else if (group == 9U && port == 2U) {
                expected[kModeOffset] = value & 3U;
            } else if (group >= 11U && group <= 14U) {
                const auto offset = kChrBanksOffset + ((group - 11U) * 2U + port / 2U) * 2U;
                if ((port & 1U) == 0U) {
                    expected[offset] = value & 0x0FU;
                    expected[offset + 1U] = 0U;
                } else {
                    expected[offset] = (value & 0x1FU) << 4U;
                    expected[offset + 1U] = (value & 0x1FU) >> 4U;
                }
            } else if (group == 15U && port == 0U) {
                expected[kIrqLatchOffset] = value & 0x0FU;
            } else if (group == 15U && port == 1U) {
                expected[kIrqLatchOffset] = (value & 0x0FU) << 4U;
            } else if (group == 15U && port == 2U) {
                expected[kIrqControlOffset] = value & 7U;
                expected[kIrqEnabledOffset] = (value >> 1U) & 1U;
                expected[kIrqCounterOffset] = expected[kIrqLatchOffset];
                expected[kIrqPrescalerOffset] = 0x55U;
                expected[kIrqPrescalerOffset + 1U] = 0x01U;
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
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 530 exchanges PRG A14 A16 after VRC4 banking including fixed pages",
          "[mapper530][banking][prg][bus]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x35U);
    for (unsigned value = 0U; value < 256U; ++value) {
        const auto first = value & 0x1FU;
        const auto second = (value ^ 0xFFU) & 0x1FU;
        mapper->write_prg(static_cast<Address>(0x8000U | (value & 0xF0U)),
                          static_cast<Byte>(value));
        mapper->write_prg(static_cast<Address>(0xA000U | (value & 0xF0U)),
                          static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(0x9002U, 0U);
        CAPTURE(value, first, second);
        require_prg(*mapper, 0x8123U, prg_bank(first) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xA123U, prg_bank(second) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xC123U, prg_bank(30U) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xF123U, prg_bank(31U) * kPrgBankSize + 0x1123U, 0x35U);

        mapper->write_prg(0x9002U, 2U);
        require_prg(*mapper, 0x8123U, prg_bank(30U) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xA123U, prg_bank(second) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xC123U, prg_bank(first) * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xF123U, prg_bank(31U) * kPrgBankSize + 0x1123U, 0x35U);
    }

    auto small = board(0x8000UZ, 0x2000UZ, MirrorMode::Vertical, 0x35U);
    small->write_prg(0x8000U, 1U);
    small->write_prg(0xA000U, 4U);
    require_prg(*small, 0x8123U, prg_bank(1U) * kPrgBankSize + 0x0123U, 0x35U, 0x8000UZ);
    require_prg(*small, 0xA123U, prg_bank(4U) * kPrgBankSize + 0x0123U, 0x35U, 0x8000UZ);
    require_prg(*small, 0xC123U, prg_bank(30U) * kPrgBankSize + 0x0123U, 0x35U, 0x8000UZ);
    require_prg(*small, 0xE123U, prg_bank(31U) * kPrgBankSize + 0x0123U, 0x35U, 0x8000UZ);
}

TEST_CASE("Mapper 530 exchanges CHR A15 A16 for every bank and protects ROM",
          "[mapper530][banking][chr][ram][bus]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x35U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned bank = 0U; bank < 512U; ++bank) {
            set_chr(*mapper, slot, bank);
            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x0234U);
            CAPTURE(slot, bank);
            require_chr(*mapper, addr, chr_bank(bank) * kChrBankSize + 0x0234U, 0x35U);
            mapper->write_chr(addr, static_cast<Byte>(bank ^ 0xA5U));
            require_chr(*mapper, addr, chr_bank(bank) * kChrBankSize + 0x0234U, 0x35U);
        }
    }

    auto small = board(0x8000UZ, 0x2000UZ);
    set_chr(*small, 7U, 0x01FFU);
    require_chr(*small, 0x1E34U, chr_bank(0x01FFU) * kChrBankSize + 0x0234U, 0U, 0x2000UZ);
    const auto before = state(*small);
    REQUIRE(small->read_chr(0x2000U) == 0U);
    REQUIRE(small->read_chr(0xFFFFU) == 0U);
    small->write_chr(0x2000U, 0xA5U);
    small->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*small) == before);
}

TEST_CASE("Mapper 530 register 9000 selects V H S0 S1 with ordinary console CIRAM",
          "[mapper530][ciram][banking][bus]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(static_cast<Address>(0x9000U | (value & 0xF0U)),
                          static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() == kModes[value & 3U]);
        for (Address addr = 0x2000U; addr < 0x3F00U; addr += 0x0101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 530 VRC IRQ supports every latch cycle mode prescaled mode and acknowledgements",
          "[mapper530][irq][audio][bus][state]") {
    auto mapper = board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xF000U, static_cast<Byte>(latch & 0x0FU));
        mapper->write_prg(0xF001U, static_cast<Byte>(latch >> 4U));
        mapper->write_prg(0xF002U, 6U);
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(latch);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xF003U, 0U);
        for (unsigned cycle = 0U; cycle < 300U; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
    }

    mapper->reset();
    mapper->write_prg(0xF000U, 0x0FU);
    mapper->write_prg(0xF001U, 0x0FU);
    mapper->write_prg(0xF002U, 3U);
    for (const unsigned period : {114U, 114U, 113U, 114U}) {
        for (unsigned cycle = 1U; cycle < period; ++cycle) {
            mapper->clock_cpu();
        }
        CAPTURE(period);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF003U, 0U);
    }
    mapper->write_prg(0xF002U, 7U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF003U, 0U);
    REQUIRE(state(*mapper)[kIrqEnabledOffset] == 1U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xF002U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    const auto before = state(*mapper);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_irq_counter();
        mapper->clock_cpu_falling_edge();
        mapper->clock_audio();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
        mapper->observe_ppu_a12((cycle & 1U) != 0U);
        mapper->observe_ppu_address(static_cast<Address>(cycle & 0x3FFFU));
        mapper->observe_ppu_read(static_cast<Address>(cycle & 0x3FFFU));
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    mapper->acknowledge_irq();
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->led_outputs() == std::nullopt);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
}

TEST_CASE("Mapper 530 reset restores VRC4f state without exposing cartridge RAM",
          "[mapper530][reset][ram][banking][ciram][irq][audio]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal, 0x35U);
    const auto cold = state(*mapper);
    mapper->write_prg(0x8000U, 0x17U);
    mapper->write_prg(0xA000U, 0x12U);
    set_chr(*mapper, 7U, 0x01A5U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x9002U, 3U);
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF001U, 0x0FU);
    mapper->write_prg(0xF002U, 7U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper) != cold);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    mapper->write_prg(0x6000U, 0xA5U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);

    mapper->reset();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    require_prg(*mapper, 0x8123U, 0x0123U, 0x35U);
    require_prg(*mapper, 0xC123U, prg_bank(30U) * kPrgBankSize + 0x0123U, 0x35U);
    mapper->power_on();
    REQUIRE(state(*mapper) == cold);

    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE(state(*mapper) == cold);
}

TEST_CASE("Mapper 530 bounded snapshots replay banks pins and partial IRQs without ROM",
          "[mapper530][state][atomic][banking][irq][ram]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0x8000U, 1U);
    mapper->write_prg(0xA000U, 4U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_chr(*mapper, slot, slot + 29U);
    }
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x9002U, 2U);
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF001U, 0x0FU);
    mapper->write_prg(0xF002U, 7U);
    mapper->clock_cpu();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[0U] == 0x00U);
    REQUIRE(saved[1U] == 0x53U);
    REQUIRE(saved[kPrgBanksOffset] == 1U);
    REQUIRE(saved[kPrgBanksOffset + 1U] == 4U);
    REQUIRE(saved[kMirrorOffset] == static_cast<Byte>(MirrorMode::SingleUpper));
    REQUIRE(saved[kModeOffset] == 2U);
    REQUIRE(saved[kIrqCounterOffset] == 0xFFU);
    REQUIRE(saved[kIrqPendingOffset] == 0U);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleUpper);
    require_prg(*copy, 0x8123U, prg_bank(30U) * kPrgBankSize + 0x0123U, 0xA0U);
    require_prg(*copy, 0xA123U, prg_bank(4U) * kPrgBankSize + 0x0123U, 0xA0U);
    require_prg(*copy, 0xC123U, prg_bank(1U) * kPrgBankSize + 0x0123U, 0xA0U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        require_chr(*copy,
                    static_cast<Address>(slot * kChrBankSize + 0x0234U),
                    chr_bank(slot + 29U) * kChrBankSize + 0x0234U,
                    0xA0U);
    }
    copy->clock_cpu();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE_FALSE(copy->irq_pending());
}

TEST_CASE("Mapper 530 rejects truncated damaged and mismatched states atomically",
          "[mapper530][state][atomic][geometry][irq][ram]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 1U);
    mapper->write_prg(0xA000U, 4U);
    set_chr(*mapper, 7U, 0x01A5U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0x9002U, 2U);
    mapper->write_prg(0xF000U, 0x0EU);
    mapper->write_prg(0xF001U, 0x0FU);
    mapper->write_prg(0xF002U, 7U);
    mapper->clock_cpu();
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    auto trailing = saved;
    trailing.insert(trailing.end(), {1U, 2U, 3U});
    load_rejected(*mapper, trailing);
    for (unsigned bit = 0U; bit < kPrgBanksOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        load_rejected(*mapper, invalid);
    }

    const std::array<std::pair<std::size_t, Byte>, 9> invalid_fields{{
        {kPrgBanksOffset, 0x20U},
        {kPrgBanksOffset + 1U, 0x20U},
        {kChrBanksOffset + 1U, 0x02U},
        {kMirrorOffset, 2U},
        {kMirrorOffset, 5U},
        {kModeOffset, 4U},
        {kIrqControlOffset, 8U},
        {kIrqEnabledOffset, 2U},
        {kIrqPendingOffset, 2U},
    }};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        load_rejected(*mapper, invalid);
    }
    for (const std::uint16_t prescaler : {0U, 342U, 0xFFFFU}) {
        auto invalid = saved;
        std::memcpy(invalid.data() + kIrqPrescalerOffset, &prescaler, sizeof(prescaler));
        CAPTURE(prescaler);
        load_rejected(*mapper, invalid);
    }
    auto pending_without_enable = saved;
    pending_without_enable[kIrqEnabledOffset] = 0U;
    pending_without_enable[kIrqPendingOffset] = 1U;
    load_rejected(*mapper, pending_without_enable);

    auto wrong_prg = board(kPrgSize / 2U, kChrSize);
    load_rejected(*wrong_prg, saved);
    auto wrong_chr = board(kPrgSize, kChrSize / 2U);
    load_rejected(*wrong_chr, saved);
    auto wrong_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    load_rejected(*wrong_mirror, saved);

    auto changed = saved;
    changed[kPrgBanksOffset] = 4U;
    changed[kMirrorOffset] = static_cast<Byte>(MirrorMode::Horizontal);
    changed[kModeOffset] = 0U;
    changed[kIrqPendingOffset] = 1U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->irq_pending());
    require_prg(*mapper, 0x8123U, prg_bank(4U) * kPrgBankSize + 0x0123U);
}

} // namespace
