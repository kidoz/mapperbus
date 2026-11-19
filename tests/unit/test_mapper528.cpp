#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper528.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x060000UZ;
constexpr std::size_t kChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kStateSize = 0x201FUZ;
constexpr std::size_t kGameOffset = 11UZ;
constexpr std::size_t kChrBanksOffset = 12UZ;
constexpr std::size_t kPrgBanksOffset = 20UZ;
constexpr std::size_t kLowPrgBankOffset = 22UZ;
constexpr std::size_t kMirrorOffset = 23UZ;
constexpr std::size_t kIrqLatchOffset = 24UZ;
constexpr std::size_t kIrqCounterOffset = 25UZ;
constexpr std::size_t kIrqPrescalerOffset = 26UZ;
constexpr std::size_t kIrqControlOffset = 28UZ;
constexpr std::size_t kIrqEnabledOffset = 29UZ;
constexpr std::size_t kIrqPendingOffset = 30UZ;
constexpr std::size_t kRamOffset = 31UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 528U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kPrgSize;
    result.chr_rom_size = kChrSize;
    result.prg_ram_size = 0x2000U;
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

std::unique_ptr<Mapper528> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper528::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper528>(h, rom(kPrgSize, salt), rom(kChrSize, salt ^ 0x5AU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper528& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper528& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
}

void require_prg(Mapper528& mapper, Address addr, std::size_t physical, Byte salt = 0U) {
    CAPTURE(addr, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper528& mapper, Address addr, std::size_t physical, Byte salt = 0U) {
    CAPTURE(addr, physical, salt);
    REQUIRE(mapper.read_chr(addr) == cell(physical, salt ^ 0x5AU));
}

void select_game(Mapper528& mapper, unsigned game) {
    mapper.write_prg(game == 0U ? 0xA00BU : 0xC00BU, 0xA5U);
}

TEST_CASE("Mapper 528 validates the two-game population and bounded initial state",
          "[mapper528][geometry][ram][state]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0xA0U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper)[kGameOffset] == 0U);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8123U, 0x0123U, 0x35U);
        require_prg(*mapper, 0xA123U, kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xC123U, 14U * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xE123U, 15U * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0x6123U, 0x0123U, 0x35U);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            require_chr(*mapper,
                        static_cast<Address>(slot * kChrBankSize + 0x0234U),
                        slot * kChrBankSize + 0x0234U,
                        0x35U);
        }

        select_game(*mapper, 1U);
        require_prg(*mapper, 0xC123U, 46U * kPrgBankSize + 0x0123U, 0x35U);
        require_prg(*mapper, 0xE123U, 47U * kPrgBankSize + 0x0123U, 0x35U);
        require_chr(*mapper, 0x0234U, 0x020000UZ + 0x0234U, 0x35U);
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
        REQUIRE_FALSE(Mapper528::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 23U, 25U, 69U, 527U, 529U, 4095U}) {
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
         {0UZ, 1UZ, 0x2000UZ, 0x020000UZ, 0x040000UZ, 0x050000UZ, 0x060001UZ, 0x080000UZ}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x0400UZ, 0x2000UZ, 0x020000UZ, 0x030000UZ, 0x040001UZ, 0x080000UZ}) {
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
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 0x1000U;
            break;
        case 4U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 5U:
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

TEST_CASE("Mapper 528 exhaustively decodes only A00x and C00x direct registers",
          "[mapper528][decoding][banking][bus][irq][state]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = initial;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const auto group = raw & 0xF000U;
        if (group == 0xA000U || group == 0xC000U) {
            const auto reg = raw & 0x0FU;
            expected[kGameOffset] = group == 0xC000U ? 1U : 0U;
            if (reg <= 7U) {
                expected[kChrBanksOffset + reg] = value;
            } else if (reg == 8U) {
                expected[kLowPrgBankOffset] = value;
            } else if (reg == 9U || reg == 10U) {
                expected[kPrgBanksOffset + reg - 9U] = value;
            } else if (reg == 12U) {
                expected[kMirrorOffset] = value & 3U;
            } else if (reg == 13U) {
                expected[kIrqControlOffset] = value & 7U;
                expected[kIrqEnabledOffset] = (value >> 1U) & 1U;
                expected[kIrqCounterOffset] = expected[kIrqLatchOffset];
                expected[kIrqPendingOffset] = 0U;
                expected[kIrqPrescalerOffset] = 0x55U;
                expected[kIrqPrescalerOffset + 1U] = 0x01U;
            } else if (reg == 14U) {
                expected[kIrqEnabledOffset] = expected[kIrqControlOffset] & 1U;
                expected[kIrqPendingOffset] = 0U;
            } else if (reg == 15U) {
                expected[kIrqLatchOffset] = value;
            }
        }

        CAPTURE(raw, value);
        mapper->write_prg(addr, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x6000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x6000U ? 0xFFU : 0x00U));
        if (raw < 0x6000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
}

TEST_CASE("Mapper 528 masks PRG banks within each game and fixes the final two banks",
          "[mapper528][banking][prg][bus][ram]") {
    for (unsigned game = 0U; game < 2U; ++game) {
        const std::size_t base_bank = game == 0U ? 0U : 16U;
        const std::size_t mask = game == 0U ? 0x0FU : 0x1FU;
        const Address port_base = game == 0U ? 0xA000U : 0xC000U;
        for (unsigned reg = 8U; reg <= 10U; ++reg) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                if (reg == 8U && raw == 1U) {
                    continue;
                }
                auto mapper = board(MirrorMode::Vertical, 0x35U);
                select_game(*mapper, game);
                mapper->write_prg(static_cast<Address>(port_base | reg | (raw & 0xF0U)),
                                  static_cast<Byte>(raw));
                const Address addr = reg == 8U ? 0x6123U : (reg == 9U ? 0x8123U : 0xA123U);
                CAPTURE(game, reg, raw);
                require_prg(
                    *mapper, addr, (base_bank + (raw & mask)) * kPrgBankSize + 0x0123U, 0x35U);
                require_prg(
                    *mapper, 0xC123U, (base_bank + mask - 1U) * kPrgBankSize + 0x0123U, 0x35U);
                require_prg(*mapper, 0xF123U, (base_bank + mask) * kPrgBankSize + 0x1123U, 0x35U);
            }
        }
    }
}

TEST_CASE("Mapper 528 selects eight 1 KiB CHR banks inside independent 128 KiB games",
          "[mapper528][banking][chr][bus]") {
    for (unsigned game = 0U; game < 2U; ++game) {
        const std::size_t base_bank = game == 0U ? 0U : 0x80U;
        const Address port_base = game == 0U ? 0xA000U : 0xC000U;
        for (unsigned reg = 0U; reg < 8U; ++reg) {
            std::array<Byte, 8> banks{0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
            auto mapper = board(MirrorMode::Vertical, 0x35U);
            select_game(*mapper, game);
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto value = static_cast<Byte>(raw);
                mapper->write_prg(static_cast<Address>(port_base | reg | (raw & 0xF0U)), value);
                banks[reg] = value;
                CAPTURE(game, reg, raw);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + 0x0234U);
                    require_chr(*mapper,
                                addr,
                                (base_bank + (banks[slot] & 0x7FU)) * kChrBankSize + 0x0234U,
                                0x35U);
                    mapper->write_chr(addr, static_cast<Byte>(raw ^ 0xFFU));
                    require_chr(*mapper,
                                addr,
                                (base_bank + (banks[slot] & 0x7FU)) * kChrBankSize + 0x0234U,
                                0x35U);
                }
            }
        }
    }

    auto mapper = board();
    const auto before = state(*mapper);
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xA5U);
    mapper->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 528 register C selects V H S0 S1 without replacing CIRAM routing",
          "[mapper528][ciram][banking][bus]") {
    auto mapper = board(MirrorMode::Horizontal);
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleUpper};
    for (unsigned game = 0U; game < 2U; ++game) {
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            mapper->write_prg(
                static_cast<Address>((game == 0U ? 0xA00CU : 0xC00CU) | (raw & 0xF0U)),
                static_cast<Byte>(raw));
            CAPTURE(game, raw);
            REQUIRE(mapper->mirror_mode() == kModes[raw & 3U]);
            for (Address addr = 0x2000U; addr < 0x3F00U; addr += 0x0101U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->ciram_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
            }
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 528 value one alone enables shared RAM and other values protect ROM",
          "[mapper528][ram][banking][bus][reset]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0xA008U, 1U);
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(addr, static_cast<Byte>(addr * 37U + (addr >> 8U)));
    }
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_prg(addr) == static_cast<Byte>(addr * 37U + (addr >> 8U)));
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }

    select_game(*mapper, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x6123U * 37U + 0x61U));
    mapper->write_prg(0xC008U, 0x23U);
    require_prg(*mapper, 0x6123U, 19U * kPrgBankSize + 0x0123U, 0x35U);
    const auto before_rom_write = state(*mapper);
    mapper->write_prg(0x6123U, 0xFFU);
    REQUIRE(state(*mapper) == before_rom_write);
    mapper->write_prg(0xA008U, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x6123U * 37U + 0x61U));

    mapper->reset();
    mapper->write_prg(0xC008U, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x6123U * 37U + 0x61U));
    mapper->power_on();
    mapper->write_prg(0xA008U, 1U);
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        REQUIRE(mapper->read_prg(addr) == 0U);
    }

    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 528 VRC IRQ supports cycle and 114-cycle modes with both acknowledgements",
          "[mapper528][irq][audio][bus][state]") {
    auto mapper = board();
    mapper->write_prg(0xA00FU, 0xFEU);
    mapper->write_prg(0xA00DU, 0x06U);
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFEU);
    mapper->clock_cpu();
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFEU);
    mapper->write_prg(0xA00EU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqEnabledOffset] == 0U);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC00FU, 0xFEU);
    mapper->write_prg(0xC00DU, 0x07U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC00EU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqEnabledOffset] == 1U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xC00DU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqEnabledOffset] == 0U);

    mapper->write_prg(0xA00FU, 0xFDU);
    mapper->write_prg(0xA00DU, 0x02U);
    for (unsigned cycle = 0U; cycle < 340U; ++cycle) {
        CAPTURE(cycle);
        mapper->clock_cpu();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kIrqCounterOffset] == 0xFDU);

    mapper->reset();
    mapper->write_prg(0xA00FU, 0U);
    mapper->write_prg(0xA00DU, 0x06U);
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
}

TEST_CASE("Mapper 528 reset restores game zero banks CIRAM and IRQ while retaining RAM",
          "[mapper528][reset][banking][ciram][irq][ram]") {
    auto mapper = board(MirrorMode::Horizontal, 0x35U);
    const auto cold = state(*mapper);
    mapper->write_prg(0xC009U, 0xFFU);
    mapper->write_prg(0xC00AU, 0xFEU);
    mapper->write_prg(0xC003U, 0xF0U);
    mapper->write_prg(0xC00CU, 3U);
    mapper->write_prg(0xC008U, 1U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_prg(0xC00FU, 0xFEU);
    mapper->write_prg(0xC00DU, 7U);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper) != cold);

    mapper->reset();
    auto reset_with_ram = cold;
    reset_with_ram[kRamOffset + 0x0123U] = 0xA5U;
    REQUIRE(state(*mapper) == reset_with_ram);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    require_prg(*mapper, 0x8123U, 0x0123U, 0x35U);
    require_prg(*mapper, 0xA123U, kPrgBankSize + 0x0123U, 0x35U);
    require_chr(*mapper, 0x0C34U, 3U * kChrBankSize + 0x0034U, 0x35U);
    mapper->write_prg(0xA008U, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
    mapper->write_prg(0xA008U, 1U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
}

TEST_CASE("Mapper 528 bounded snapshots replay banks RAM and partial IRQs without ROM bytes",
          "[mapper528][state][atomic][banking][irq][ram]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0xC009U, 0xF1U);
    mapper->write_prg(0xC00AU, 0x82U);
    for (unsigned reg = 0U; reg < 8U; ++reg) {
        mapper->write_prg(static_cast<Address>(0xC000U | reg), static_cast<Byte>(0x80U + reg));
    }
    mapper->write_prg(0xC00CU, 2U);
    mapper->write_prg(0xC008U, 1U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_prg(0xC00FU, 0xFEU);
    mapper->write_prg(0xC00DU, 7U);
    mapper->clock_cpu();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x2100UZ);
    REQUIRE(saved[0U] == 0x80U);
    REQUIRE(saved[1U] == 0x52U);
    REQUIRE(saved[kGameOffset] == 1U);
    REQUIRE(saved[kPrgBanksOffset] == 0xF1U);
    REQUIRE(saved[kPrgBanksOffset + 1U] == 0x82U);
    REQUIRE(saved[kChrBanksOffset] == 0x80U);
    REQUIRE(saved[kChrBanksOffset + 7U] == 0x87U);
    REQUIRE(saved[kLowPrgBankOffset] == 1U);
    REQUIRE(saved[kMirrorOffset] == 2U);
    REQUIRE(saved[kIrqCounterOffset] == 0xFFU);
    REQUIRE(saved[kIrqPendingOffset] == 0U);
    REQUIRE(saved[kRamOffset + 0x0123U] == 0xA5U);

    auto copy = board(MirrorMode::Vertical, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(copy->read_prg(0x6123U) == 0xA5U);
    require_prg(*copy, 0x8123U, 33U * kPrgBankSize + 0x0123U, 0xA0U);
    require_prg(*copy, 0xA123U, 18U * kPrgBankSize + 0x0123U, 0xA0U);
    require_chr(*copy, 0x0234U, 0x020000UZ + 0x0234U, 0xA0U);
    require_chr(*copy, 0x1E34U, 0x020000UZ + 7U * kChrBankSize + 0x0234U, 0xA0U);
    copy->clock_cpu();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE_FALSE(copy->irq_pending());
}

TEST_CASE("Mapper 528 rejects truncated damaged and mismatched states atomically",
          "[mapper528][state][atomic][geometry][irq][ram]") {
    auto mapper = board();
    select_game(*mapper, 1U);
    mapper->write_prg(0xC009U, 0xF1U);
    mapper->write_prg(0xC003U, 0x81U);
    mapper->write_prg(0xC00CU, 3U);
    mapper->write_prg(0xC008U, 1U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_prg(0xC00FU, 0xFEU);
    mapper->write_prg(0xC00DU, 7U);
    mapper->clock_cpu();
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    auto trailing = saved;
    trailing.resize(0x4000UZ, 0U);
    load_rejected(*mapper, trailing);
    for (std::size_t offset = 0U; offset < kGameOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        load_rejected(*mapper, invalid);
    }
    for (const auto& [offset, value] : std::array<std::pair<std::size_t, Byte>, 8>{{
             {kGameOffset, 2U},
             {kMirrorOffset, 4U},
             {kIrqPrescalerOffset, 0U},
             {kIrqControlOffset, 8U},
             {kIrqEnabledOffset, 2U},
             {kIrqPendingOffset, 2U},
             {kIrqEnabledOffset, 0U},
             {kIrqPendingOffset, 1U},
         }}) {
        auto invalid = saved;
        invalid[offset] = value;
        if (offset == kIrqPrescalerOffset) {
            invalid[kIrqPrescalerOffset + 1U] = 0U;
        } else if (offset == kIrqEnabledOffset && value == 0U) {
            invalid[kIrqPendingOffset] = 1U;
        } else if (offset == kIrqPendingOffset && value == 1U) {
            invalid[kIrqEnabledOffset] = 0U;
        }
        CAPTURE(offset, value);
        load_rejected(*mapper, invalid);
    }
    auto excessive_prescaler = saved;
    excessive_prescaler[kIrqPrescalerOffset] = 0x56U;
    excessive_prescaler[kIrqPrescalerOffset + 1U] = 0x01U;
    load_rejected(*mapper, excessive_prescaler);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    load_rejected(*wrong_mirror, saved);

    auto changed = saved;
    changed[kGameOffset] = 0U;
    changed[kPrgBanksOffset] = 0x0EU;
    changed[kMirrorOffset] = 1U;
    changed[kIrqPendingOffset] = 1U;
    changed[kRamOffset + 0x0123U] = 0x5AU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->irq_pending());
    require_prg(*mapper, 0x8123U, 14U * kPrgBankSize + 0x0123U, 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5AU);
}

} // namespace
