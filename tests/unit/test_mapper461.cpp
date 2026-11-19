#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper461.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x040000UZ;
constexpr std::size_t kLargeChrSize = 0x040000UZ;
constexpr std::size_t kSmallChrSize = 0x008000UZ;
constexpr std::size_t kChrSize = kLargeChrSize + kSmallChrSize;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kShiftOffset = 11UZ;
constexpr std::size_t kWriteCountOffset = 12UZ;
constexpr std::size_t kControlOffset = 13UZ;
constexpr std::size_t kChrBank0Offset = 14UZ;
constexpr std::size_t kChrBank1Offset = 15UZ;
constexpr std::size_t kPrgBankOffset = 16UZ;
constexpr std::size_t kStateSize = 17UZ;
constexpr std::array<Address, 10> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xC001U, 0xD234U, 0xE000U, 0xFFFEU, 0xFFFFU};
constexpr std::array<Address, 12> kChrAddresses{0x0000U,
                                                0x0001U,
                                                0x0123U,
                                                0x0FFFU,
                                                0x1000U,
                                                0x1001U,
                                                0x1123U,
                                                0x1FFFU,
                                                0x0555U,
                                                0x0AAAU,
                                                0x1555U,
                                                0x1AAAU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 461U;
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

std::unique_ptr<Mapper461> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper461::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper461>(
        h, rom(kPrgSize, salt), rom(kChrSize, static_cast<Byte>(salt ^ 0x55U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper461& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void commit(Mapper461& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
    }
}

std::size_t expected_prg(Address addr, Byte outer, Byte control, Byte prg_bank) {
    const std::size_t outer_base =
        ((outer >> 3U) & 1U) * 0x020000UZ + ((outer >> 2U) & 1U) * 0x010000UZ;
    if ((outer & 0x04U) == 0U) {
        return outer_base + (outer & 3U) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    }

    const unsigned bank_count = (outer & 0x08U) != 0U ? 4U : 8U;
    const unsigned selected = prg_bank & 0x0FU;
    const unsigned mode = (control >> 2U) & 3U;
    unsigned bank = 0U;
    if (mode <= 1U) {
        bank = (selected & 0x0EU) | (addr >= 0xC000U ? 1U : 0U);
    } else if (mode == 2U) {
        bank = addr < 0xC000U ? 0U : selected;
    } else {
        bank = addr < 0xC000U ? selected : bank_count - 1U;
    }
    return outer_base + (bank & (bank_count - 1U)) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t expected_chr(Address addr, Byte outer, Byte control, Byte chr0, Byte chr1) {
    const bool upper = (addr & 0x1000U) != 0U;
    const unsigned bank =
        (control & 0x10U) != 0U ? (upper ? chr1 : chr0) : ((chr0 & 0x1EU) | (upper ? 1U : 0U));
    const std::size_t inner = bank * kChrBankSize + (addr & (kChrBankSize - 1U));
    if ((outer & 0x04U) == 0U) {
        return kLargeChrSize + inner % kSmallChrSize;
    }
    return ((outer >> 3U) & 1U) * 0x020000UZ + inner;
}

void require_prg(
    Mapper461& mapper, Address addr, Byte outer, Byte control, Byte prg_bank, Byte salt = 0U) {
    const auto physical = expected_prg(addr, outer, control, prg_bank);
    CAPTURE(addr, outer, control, prg_bank, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper461& mapper,
                 Address addr,
                 Byte outer,
                 Byte control,
                 Byte chr0,
                 Byte chr1,
                 Byte salt = 0U) {
    const auto physical = expected_chr(addr, outer, control, chr0, chr1);
    CAPTURE(addr, outer, control, chr0, chr1, physical, salt);
    REQUIRE(mapper.read_chr(addr) == cell(physical, static_cast<Byte>(salt ^ 0x55U)));
}

TEST_CASE("Mapper 461 validates the documented NES 2.0 ROM-only geometry",
          "[mapper461][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper461::validate_geometry(header(mirror), kPrgSize, kChrSize));
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8000U, 0U, 0x0CU, 0U, 0x35U);
        require_prg(*mapper, 0xC000U, 0U, 0x0CU, 0U, 0x35U);
        require_chr(*mapper, 0U, 0U, 0x0CU, 0U, 0U, 0x35U);
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
        REQUIRE_FALSE(Mapper461::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 1U, 155U, 460U, 462U, 4095U}) {
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
         {0UZ, 1UZ, 0x4000UZ, 0x020000UZ, kPrgSize - 1U, kPrgSize + 1U, kPrgSize * 2U}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x2000UZ,
                                   kLargeChrSize,
                                   kSmallChrSize,
                                   kChrSize - 1U,
                                   kChrSize + 1U,
                                   kChrSize * 2U}) {
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

TEST_CASE("Mapper 461 outer register covers the low CPU window and floats reads",
          "[mapper461][decoding][bus][ram]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->power_on();
        auto expected = state(*mapper);
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x6000U) {
            expected[kOuterOffset] = value & 0x0FU;
        }
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }

    for (const Byte prg_bank : {Byte{0U}, Byte{0x10U}, Byte{0x1FU}}) {
        mapper->power_on();
        commit(*mapper, 0xE000U, prg_bank);
        for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
            const auto value = static_cast<Byte>(raw * 17U + prg_bank);
            mapper->write_prg(static_cast<Address>(raw), value);
            CAPTURE(prg_bank, raw, value);
            REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x0FU));
            REQUIRE(state(*mapper)[kPrgBankOffset] == prg_bank);
        }
    }
}

TEST_CASE("Mapper 461 decodes every MMC1 register alias through five serial writes",
          "[mapper461][decoding][banking]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        const auto value = static_cast<Byte>((raw * 37U + (raw >> 8U) + 0x15U) & 0x1FU);
        commit(*mapper, static_cast<Address>(raw), value);
        auto expected = cold;
        const std::size_t offset = raw < 0xA000U   ? kControlOffset
                                   : raw < 0xC000U ? kChrBank0Offset
                                   : raw < 0xE000U ? kChrBank1Offset
                                                   : kPrgBankOffset;
        expected[offset] = value;
        CAPTURE(raw, value, offset);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 461 MMC1 reset writes and consecutive-cycle filtering preserve outer state",
          "[mapper461][decoding][bus][reset]") {
    auto mapper = board();
    mapper->write_prg(0x7FFFU, 0x0DU);
    commit(*mapper, 0x8000U, 0x12U);
    commit(*mapper, 0xA000U, 0x13U);
    commit(*mapper, 0xC000U, 0x14U);
    commit(*mapper, 0xE000U, 0x15U);
    mapper->write_prg(0x8000U, 1U);
    mapper->write_prg(0x8000U, 0U);
    auto expected = state(*mapper);
    expected[kShiftOffset] = 0x10U;
    expected[kWriteCountOffset] = 0U;
    expected[kControlOffset] = 0x1EU;

    for (const Address addr :
         {Address{0x8000U}, Address{0xBFFFU}, Address{0xDFFFU}, Address{0xFFFFU}}) {
        mapper->write_prg(addr, 0x80U);
        CAPTURE(addr);
        REQUIRE(state(*mapper) == expected);
        mapper->write_prg(0x8000U, 1U);
        mapper->write_prg(0x8000U, 0U);
    }

    const auto before_filtered = state(*mapper);
    mapper->write_prg_cpu(0x8000U, 1U, true);
    REQUIRE(state(*mapper) == before_filtered);
    mapper->write_prg_cpu(0x8000U, 1U, false);
    REQUIRE(state(*mapper) != before_filtered);
    mapper->write_prg_cpu(0xFFFFU, 0x80U, true);
    REQUIRE(state(*mapper) == expected);

    mapper->write_prg_cpu(0x6000U, 0x02U, true);
    REQUIRE(state(*mapper)[kOuterOffset] == 2U);
    mapper->write_prg_cpu(0x7FFFU, 0x03U, true);
    REQUIRE(state(*mapper)[kOuterOffset] == 3U);
}

TEST_CASE("Mapper 461 NROM mode mirrors the selected 16 KiB page in both CPU windows",
          "[mapper461][banking][prg]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 16U; outer += 4U) {
        mapper->write_prg(0x6000U, outer);
        commit(*mapper, 0x8000U, 0x1CU);
        commit(*mapper, 0xE000U, 0x1FU);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, outer, 0x1CU, 0x1FU);
        }
        const auto lower = expected_prg(0x8123U, outer, 0x1CU, 0x1FU);
        const auto upper = expected_prg(0xC123U, outer, 0x1CU, 0x1FU);
        REQUIRE(lower == upper);
    }

    mapper->power_on();
    for (Address addr = 0x8000U; addr != 0U; ++addr) {
        require_prg(*mapper, addr, 0U, 0x0CU, 0U);
    }
}

TEST_CASE("Mapper 461 MMC1 PRG banking wraps inside the outer 128 or 64 KiB region",
          "[mapper461][banking][prg]") {
    auto mapper = board();
    for (const Byte outer : {Byte{0x04U}, Byte{0x07U}, Byte{0x0CU}, Byte{0x0FU}}) {
        mapper->write_prg(0x6000U, outer);
        for (Byte mode = 0U; mode < 4U; ++mode) {
            const auto control = static_cast<Byte>((mode << 2U) | (mode & 1U) | 0x10U);
            commit(*mapper, 0x8000U, control);
            for (Byte prg = 0U; prg < 32U; ++prg) {
                commit(*mapper, 0xE000U, prg);
                for (const auto addr : kPrgAddresses) {
                    require_prg(*mapper, addr, outer, control, prg);
                }
            }
        }
    }

    mapper->write_prg(0x6000U, 0x04U);
    commit(*mapper, 0x8000U, 0x0CU);
    commit(*mapper, 0xE000U, 0x07U);
    const auto first = state(*mapper);
    for (const Byte alias : {Byte{0x05U}, Byte{0x06U}, Byte{0x07U}}) {
        mapper->write_prg(0x6000U, alias);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, alias, 0x0CU, 0x07U);
        }
    }
    mapper->write_prg(0x6000U, 0x04U);
    REQUIRE(state(*mapper) == first);
}

TEST_CASE("Mapper 461 selects the packed CHR chips and applies MMC1 banking before small-chip wrap",
          "[mapper461][banking][chr]") {
    auto mapper = board();
    for (const Byte outer : {Byte{0x00U},
                             Byte{0x03U},
                             Byte{0x08U},
                             Byte{0x0BU},
                             Byte{0x04U},
                             Byte{0x07U},
                             Byte{0x0CU},
                             Byte{0x0FU}}) {
        mapper->write_prg(0x6000U, outer);
        for (const Byte control : {Byte{0x00U}, Byte{0x10U}}) {
            commit(*mapper, 0x8000U, control);
            for (const Byte chr0 : {Byte{0x00U},
                                    Byte{0x01U},
                                    Byte{0x02U},
                                    Byte{0x07U},
                                    Byte{0x08U},
                                    Byte{0x0FU},
                                    Byte{0x10U},
                                    Byte{0x1FU}}) {
                commit(*mapper, 0xA000U, chr0);
                for (const Byte chr1 : {Byte{0x00U}, Byte{0x03U}, Byte{0x08U}, Byte{0x1EU}}) {
                    commit(*mapper, 0xC000U, chr1);
                    for (const auto addr : kChrAddresses) {
                        require_chr(*mapper, addr, outer, control, chr0, chr1);
                        const auto before = mapper->read_chr(addr);
                        mapper->write_chr(addr, static_cast<Byte>(~before));
                        REQUIRE(mapper->read_chr(addr) == before);
                    }
                }
            }
        }
    }

    mapper->power_on();
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        require_chr(*mapper, addr, 0U, 0x0CU, 0U, 0U);
    }
}

TEST_CASE("Mapper 461 MMC1 controls all four CIRAM arrangements without cartridge RAM",
          "[mapper461][ciram][ram][audio][bus]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
        for (Byte control = 0U; control < 4U; ++control) {
            commit(*mapper, 0x8000U, control);
            const auto expected = control == 0U   ? MirrorMode::SingleLower
                                  : control == 1U ? MirrorMode::SingleUpper
                                  : control == 2U ? MirrorMode::Vertical
                                                  : MirrorMode::Horizontal;
            CAPTURE(initial, control, expected);
            REQUIRE(mapper->mirror_mode() == expected);
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
    REQUIRE(mapper->expansion_read_mask(0x5000U) == 0xFFU);
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    mapper->write_expansion(0x5000U, 0xFFU);
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
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 461 soft reset resets only the MMC1 serial port while power-on clears latches",
          "[mapper461][reset]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    mapper->write_prg(0x6000U, 0x0DU);
    commit(*mapper, 0x8000U, 0x12U);
    commit(*mapper, 0xA000U, 0x13U);
    commit(*mapper, 0xC000U, 0x14U);
    commit(*mapper, 0xE000U, 0x15U);
    mapper->write_prg(0x8000U, 1U);
    mapper->write_prg(0x8000U, 0U);
    auto expected = state(*mapper);
    expected[kShiftOffset] = 0x10U;
    expected[kWriteCountOffset] = 0U;
    expected[kControlOffset] = 0x1EU;

    mapper->reset();
    REQUIRE(state(*mapper) == expected);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0DU);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->reset();
    REQUIRE(state(*mapper) == expected);

    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    require_prg(*mapper, 0x8000U, 0U, 0x0CU, 0U);
    require_chr(*mapper, 0U, 0U, 0x0CU, 0U, 0U);
}

TEST_CASE("Mapper 461 bounded snapshots replay banks and partial serial state without ROM",
          "[mapper461][state][banking]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x0CU);
    commit(*mapper, 0x8000U, 0x1AU);
    commit(*mapper, 0xA000U, 0x13U);
    commit(*mapper, 0xC000U, 0x1CU);
    commit(*mapper, 0xE000U, 0x15U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA000U, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kSmallChrSize);
    REQUIRE(saved[kOuterOffset] == 0x0CU);
    REQUIRE(saved[kShiftOffset] == 0x0CU);
    REQUIRE(saved[kWriteCountOffset] == 2U);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x0CU, 0x1AU, 0x15U, 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, 0x0CU, 0x1AU, 0x13U, 0x1CU, 0x80U);
    }
    for (unsigned bit = 2U; bit < 5U; ++bit) {
        copy->write_prg(0xA000U, static_cast<Byte>((0x15U >> bit) & 1U));
    }
    REQUIRE(state(*copy)[kChrBank0Offset] == 0x15U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 461 rejects truncated damaged and mismatched states atomically",
          "[mapper461][state]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x0DU);
    commit(*mapper, 0x8000U, 0x16U);
    commit(*mapper, 0xA000U, 0x13U);
    commit(*mapper, 0xC000U, 0x14U);
    commit(*mapper, 0xE000U, 0x15U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA000U, 0U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 10> invalid_fields = {
        {{0U, 1U},
         {2U, 1U},
         {6U, 1U},
         {kOuterOffset, 0x10U},
         {kShiftOffset, 0x20U},
         {kWriteCountOffset, 5U},
         {kControlOffset, 0x20U},
         {kChrBank0Offset, 0x20U},
         {kChrBank1Offset, 0x20U},
         {kPrgBankOffset, 0x20U}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    for (const Byte shift : {Byte{0x00U}, Byte{0x08U}, Byte{0x0FU}, Byte{0x1FU}}) {
        auto invalid = saved;
        invalid[kShiftOffset] = shift;
        CAPTURE(shift);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper461>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg = std::make_unique<Mapper461>(header(), rom(kPrgSize / 2U), rom(kChrSize));
    mismatch(wrong_prg);
    auto wrong_chr = std::make_unique<Mapper461>(header(), rom(kPrgSize), rom(kChrSize / 2U));
    mismatch(wrong_chr);

    auto changed = saved;
    changed[kOuterOffset] = 0x04U;
    changed[kControlOffset] = 0x0CU;
    changed[kPrgBankOffset] = 0x06U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0x8000U, 0x04U, 0x0CU, 0x06U);
}

} // namespace
