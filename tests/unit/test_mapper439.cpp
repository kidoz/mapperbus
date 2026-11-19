#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper439.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = Mapper439::kChrRamSize;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kOuterOffset = 11UZ;
constexpr std::size_t kMaskOffset = 12UZ;
constexpr std::size_t kChrRamOffset = 13UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 439U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 15U) * 43U +
                             (offset >> 16U) * 59U + (offset >> 17U) * 61U + (offset >> 18U) * 71U +
                             (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper439> board(std::size_t prg_size = kMaxPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper439::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper439>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper439& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(
    Address addr, Byte latch, Byte outer, Byte mask, std::size_t prg_size = kMaxPrgSize) {
    unsigned bank = (addr & 0x4000U) == 0U ? latch & 0x3FU : 0x3FU;
    const unsigned pin_mask = (mask & 0x70U) >> 1U;
    const unsigned pins = (outer & 0x70U) >> 1U;
    bank = (bank & ~pin_mask) | (pins & pin_mask);
    return (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU)) % prg_size;
}

TEST_CASE("Mapper 439 validates NES 2.0 PRG ROM and volatile CHR RAM geometry",
          "[mapper439][geometry][ram]") {
    for (std::size_t prg = kMinPrgSize; prg <= kMaxPrgSize; prg += kPrgBankSize) {
        CAPTURE(prg);
        REQUIRE(Mapper439::validate_geometry(header(), prg, 0U));
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, mirror);
        auto different_rom = board(kMaxPrgSize, mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

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
        REQUIRE_FALSE(Mapper439::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 437U, 438U, 440U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, 0U);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x004000UZ,
                                   kMinPrgSize - 1U,
                                   kMinPrgSize + 1U,
                                   0x009000UZ,
                                   kMaxPrgSize + 1U,
                                   0x200000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, kMaxPrgSize}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 7U; ++field) {
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
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, 0U);
    }
}

TEST_CASE("Mapper 439 decodes the complete CPU write space without bus conflicts",
          "[mapper439][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw < 0x8000U) {
            mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        }
        mapper->write_prg(addr, value);
        const auto bytes = state(*mapper);
        const bool low = raw >= 0x6000U && raw < 0x8000U;
        const bool outer = low && (raw & 1U) == 0U;
        const bool mask = low && (raw & 1U) != 0U;
        CAPTURE(raw, value, outer, mask);
        REQUIRE(bytes[kLatchOffset] == (raw >= 0x8000U ? value & 0x7FU : 0U));
        REQUIRE(bytes[kOuterOffset] == (outer ? value & 0x70U : 0U));
        REQUIRE(bytes[kMaskOffset] == (mask ? value & 0xF0U : 0U));
        REQUIRE(mapper->mirror_mode() == (raw >= 0x8000U && (value & 0x40U) != 0U
                                              ? MirrorMode::Horizontal
                                              : MirrorMode::Vertical));
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0x7FFEU, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x70U));
        mapper->write_prg(0x7FFFU, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kMaskOffset] == (value & 0xF0U));
        mapper->reset();
        mapper->write_prg(0xFFFFU, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kLatchOffset] == (value & 0x7FU));
    }
    mapper->write_prg_cpu(0x8000U, 0x35U, true);
    REQUIRE(state(*mapper)[kLatchOffset] == 0x35U);
    mapper->write_prg_cpu(0x8000U, 0x7AU, false);
    REQUIRE(state(*mapper)[kLatchOffset] == 0x7AU);
}

TEST_CASE("Mapper 439 combines latch fixed-bank and independent outer A17-A19 pins",
          "[mapper439][decoding][banking][prg]") {
    auto mapper = board();
    for (Byte latch = 0U; latch < 0x40U; ++latch) {
        for (Byte outer = 0U; outer < 8U; ++outer) {
            for (Byte mask = 0U; mask < 8U; ++mask) {
                mapper->write_prg(0x6000U, static_cast<Byte>(outer << 4U));
                mapper->write_prg(0x6001U, static_cast<Byte>(mask << 4U));
                mapper->write_prg(0x8000U, latch);
                for (const auto addr : kPrgAddresses) {
                    const auto physical = expected_prg(addr, latch, outer << 4U, mask << 4U);
                    CAPTURE(latch, outer, mask, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
            }
        }
    }

    for (const auto config : {std::array<Byte, 3>{0U, 0U, 0U},
                              std::array<Byte, 3>{0x2AU, 0x70U, 0x70U},
                              std::array<Byte, 3>{0x15U, 0x20U, 0x40U},
                              std::array<Byte, 3>{0x3FU, 0x50U, 0x30U}}) {
        mapper->write_prg(0x6000U, config[1]);
        mapper->write_prg(0x6001U, config[2]);
        mapper->write_prg(0x8000U, config[0]);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const auto physical = expected_prg(addr, config[0], config[1], config[2]);
            CAPTURE(config[0], config[1], config[2], addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x00C000UZ, 0x014000UZ, 0x030000UZ}) {
        auto fitted = board(size);
        for (const auto config : {std::array<Byte, 3>{0x02U, 0x70U, 0x70U},
                                  std::array<Byte, 3>{0x3FU, 0x50U, 0x30U},
                                  std::array<Byte, 3>{0x1BU, 0x20U, 0x50U}}) {
            fitted->write_prg(0x6000U, config[1]);
            fitted->write_prg(0x6001U, config[2]);
            fitted->write_prg(0x8000U, config[0]);
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, config[0], config[1], config[2], size);
                CAPTURE(size, config[0], config[1], config[2], addr, physical);
                REQUIRE(fitted->read_prg(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 439 owns flat volatile CHR RAM with ordinary 8 KiB aliasing",
          "[mapper439][ram][banking]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == 0U);
        mapper->write_chr(addr, cell(addr, 0x39U));
    }
    mapper->write_prg(0x6000U, 0x70U);
    mapper->write_prg(0x6001U, 0x70U);
    mapper->write_prg(0x8000U, 0x3FU);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x39U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0x39U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFFU)) ==
                cell((static_cast<std::size_t>(addr) + 0x3FFFU) & 0x1FFFU, 0x39U));
    }
}

TEST_CASE("Mapper 439 latch bit six selects H/V CIRAM and mask bit seven protects it",
          "[mapper439][decoding][ciram]") {
    auto mapper = board(kMaxPrgSize, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x6001U, 0U);
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(state(*mapper)[kLatchOffset] == (value & 0x7FU));
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
    }

    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        mapper->reset();
        mapper->write_prg(0x8000U, initial == MirrorMode::Horizontal ? 0x40U : 0U);
        mapper->write_prg(0x6001U, 0x80U);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0xFFFFU, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() == initial);
            REQUIRE(state(*mapper)[kLatchOffset] ==
                    ((initial == MirrorMode::Horizontal ? 0x40U : 0U) | (value & 0x3FU)));
        }
    }
    mapper->write_prg(0x6001U, 0U);
    mapper->write_prg(0x8000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 439 exposes no CPU RAM IRQ expansion audio or hidden side effects",
          "[mapper439][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x50U);
    mapper->write_prg(0x8000U, 0x6DU);
    mapper->write_prg(0x6001U, 0xB0U);
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->prg_read_mask(0x6001U) == 0U);
    REQUIRE(mapper->read_prg(0x6001U) == 0U);

    const auto before = state(*mapper);
    for (Address addr = 0x4020U; addr < 0x8000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, static_cast<Byte>(addr ^ 0xFFU));
    }
    mapper->observe_cpu_read(0xFFFDU, 0xA5U);
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

TEST_CASE("Mapper 439 reset clears every register and cold power also clears CHR RAM",
          "[mapper439][reset][banking][ciram][ram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, initial, 0x35U);
        const auto cold = state(*mapper);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            mapper->write_chr(addr, cell(addr, 0xA5U));
        }
        mapper->write_prg(0x6000U, 0x70U);
        mapper->write_prg(0x8000U, 0x7FU);
        mapper->write_prg(0x6001U, 0xF0U);
        const auto programmed = state(*mapper);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

        mapper->reset();
        const auto reset = state(*mapper);
        CAPTURE(initial);
        REQUIRE(reset[kLatchOffset] == 0U);
        REQUIRE(reset[kOuterOffset] == 0U);
        REQUIRE(reset[kMaskOffset] == 0U);
        REQUIRE(std::ranges::equal(std::span(reset).subspan(kChrRamOffset),
                                   std::span(programmed).subspan(kChrRamOffset)));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        for (const auto addr : kPrgAddresses) {
            REQUIRE(mapper->read_prg(addr) == cell(expected_prg(addr, 0U, 0U, 0U), 0x35U));
        }
        mapper->write_chr(0x0123U, 0x67U);
        REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

        mapper->write_prg(0x6000U, 0x70U);
        mapper->write_prg(0x8000U, 0x7FU);
        mapper->write_prg(0x6001U, 0xF0U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 439 bounded snapshots replay registers and CHR RAM without PRG ROM",
          "[mapper439][state][banking][ram]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x50U);
    mapper->write_prg(0x8000U, 0x6DU);
    mapper->write_prg(0x6001U, 0x30U);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 8205U);
    REQUIRE(saved.size() < kMinPrgSize);
    REQUIRE(saved[kLatchOffset] == 0x6DU);
    REQUIRE(saved[kOuterOffset] == 0x50U);
    REQUIRE(saved[kMaskOffset] == 0x30U);

    auto copy = board(kMaxPrgSize, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0x6DU, 0x50U, 0x30U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x5AU));
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

TEST_CASE("Mapper 439 rejects truncated damaged and mismatched states atomically",
          "[mapper439][state][ram]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x50U);
    mapper->write_prg(0x8000U, 0x6DU);
    mapper->write_prg(0x6001U, 0xB0U);
    mapper->write_chr(0x0123U, 0xA5U);
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
    for (unsigned bit = 0U; bit < kLatchOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte latch : {Byte{0x80U}, Byte{0xC0U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kLatchOffset] = latch;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(latch);
        reject(invalid);
    }
    for (const Byte outer : {Byte{0x80U}, Byte{0x0FU}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kOuterOffset] = outer;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(outer);
        reject(invalid);
    }
    for (const Byte mask : {Byte{0x01U}, Byte{0x0FU}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kMaskOffset] = mask;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(mask);
        reject(invalid);
    }

    auto wrong_prg = std::make_unique<Mapper439>(header(), rom(kMinPrgSize), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto changed = saved;
    changed[kLatchOffset] = 0x2AU;
    changed[kOuterOffset] = 0x60U;
    changed[kMaskOffset] = 0x50U;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, 0x2AU, 0x60U, 0x50U)));
    REQUIRE(mapper->read_chr(0x0123U) == static_cast<Byte>(0xA5U ^ 0xFFU));
}

} // namespace
