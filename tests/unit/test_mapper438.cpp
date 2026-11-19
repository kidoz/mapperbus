#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper438.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x040000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kAddressOffset = 10UZ;
constexpr std::size_t kDataOffset = 11UZ;
constexpr std::size_t kStateSize = 12UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 438U;
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

std::unique_ptr<Mapper438> board(std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper438::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper438>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x55U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper438& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(Address addr, Byte latch, std::size_t prg_size = kMaxPrgSize) {
    unsigned bank = (latch & 0x3FU) >> 1U;
    if ((latch & 1U) != 0U) {
        bank = (bank & ~1U) | ((addr >> 14U) & 1U);
    }
    return (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU)) % prg_size;
}

std::size_t expected_chr(Address addr, Byte latch, std::size_t chr_size = kMaxChrSize) {
    const unsigned bank = (latch & 0x3FU) >> 1U;
    return (static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x1FFFU)) % chr_size;
}

TEST_CASE("Mapper 438 validates NES 2.0 ROM-only H/V geometry", "[mapper438][geometry][ram]") {
    for (std::size_t prg = kMinPrgSize; prg <= kMaxPrgSize; prg += kPrgBankSize) {
        for (std::size_t chr = kMinChrSize; chr <= kMaxChrSize; chr += kChrBankSize) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper438::validate_geometry(header(), prg, chr));
        }
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, kMaxChrSize, mirror);
        auto different_rom = board(kMaxPrgSize, kMaxChrSize, mirror, 0x80U);
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
        REQUIRE_FALSE(Mapper438::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 202U, 430U, 437U, 439U, 4095U}) {
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
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x004000UZ,
                                   kMinPrgSize - 1U,
                                   kMinPrgSize + 1U,
                                   0x009000UZ,
                                   kMaxPrgSize + 1U,
                                   0x100000UZ}) {
        reject(header(), size, kMinChrSize);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x001000UZ,
                                   kMinChrSize - 1U,
                                   kMinChrSize + 1U,
                                   0x003000UZ,
                                   kMaxChrSize + 1U,
                                   0x080000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, kMinChrSize);
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
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, kMinChrSize);
    }
}

TEST_CASE("Mapper 438 decodes the full CPU write aperture without bus conflicts",
          "[mapper438][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw < 0x8000U) {
            mapper->write_expansion(addr, value);
            mapper->write_prg(addr, static_cast<Byte>(value ^ 0xFFU));
        } else {
            mapper->write_prg(addr, value);
        }
        const auto bytes = state(*mapper);
        CAPTURE(raw, value);
        REQUIRE(bytes[kAddressOffset] == (raw >= 0x8000U ? raw & 0x3FU : 0U));
        REQUIRE(bytes[kDataOffset] == (raw >= 0x8000U ? value & 0x3FU : 0U));
        REQUIRE(
            mapper->mirror_mode() ==
            (raw >= 0x8000U && (value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE(mapper->maps_expansion(addr) == false);
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    mapper->write_prg(0xFFFFU, 0xFFU);
    mapper->write_prg_cpu(0x8000U, 0x00U, true);
    REQUIRE(state(*mapper)[kAddressOffset] == 0U);
    REQUIRE(state(*mapper)[kDataOffset] == 0U);
}

TEST_CASE("Mapper 438 selects NROM-128 mirrors and aligned NROM-256 pairs",
          "[mapper438][decoding][banking][prg]") {
    auto mapper = board();
    for (Byte latch = 0U; latch < 0x40U; ++latch) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0xA5U);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const auto physical = expected_prg(addr, latch);
            CAPTURE(latch, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
        const auto lower = expected_prg(0x8123U, latch);
        const auto upper = expected_prg(0xC123U, latch);
        if ((latch & 1U) == 0U) {
            REQUIRE(lower == upper);
        } else {
            REQUIRE(upper - lower == kPrgBankSize);
        }
    }

    auto fitted = board(0x00C000UZ, 0x006000UZ);
    for (const Byte latch : {Byte{0x02U}, Byte{0x1BU}, Byte{0x3EU}, Byte{0x3FU}}) {
        fitted->write_prg(static_cast<Address>(0xF000U | latch), 0U);
        for (const auto addr : kPrgAddresses) {
            const auto physical = expected_prg(addr, latch, 0x00C000UZ);
            CAPTURE(latch, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 438 selects every 8 KiB CHR bank and keeps ROM immutable",
          "[mapper438][decoding][banking][chr][ram]") {
    auto mapper = board();
    for (Byte latch = 0U; latch < 0x40U; ++latch) {
        mapper->write_prg(0xFFFFU, static_cast<Byte>(latch | 0xC0U));
        for (Address addr = 0U; addr < kChrBankSize; ++addr) {
            const auto physical = expected_chr(addr, latch);
            CAPTURE(latch, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
        }
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x2000U, 0x2123U, 0x3FFFU}) {
            const auto before = mapper->read_chr(addr);
            mapper->write_chr(addr, static_cast<Byte>(before ^ 0xFFU));
            CAPTURE(latch, addr);
            REQUIRE(mapper->read_chr(addr) == before);
            REQUIRE(mapper->read_chr(addr) == cell(expected_chr(addr, latch), 0x55U));
        }
        REQUIRE(state(*mapper)[kDataOffset] == latch);
    }

    auto fitted = board(kMinPrgSize, 0x006000UZ);
    for (const Byte latch : {Byte{0x04U}, Byte{0x16U}, Byte{0x3EU}}) {
        fitted->write_prg(0x8000U, latch);
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x2123U, 0x3FFFU}) {
            const auto physical = expected_chr(addr, latch, 0x006000UZ);
            CAPTURE(latch, addr, physical);
            REQUIRE(fitted->read_chr(addr) == cell(physical, 0x55U));
        }
    }
}

TEST_CASE("Mapper 438 data bit zero selects ordinary H/V CIRAM", "[mapper438][decoding][ciram]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Byte latch = 0U; latch < 0x40U; ++latch) {
        mapper->write_prg(0x8000U, latch);
        CAPTURE(latch);
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 438 exposes no RAM IRQ expansion audio or hidden side effects",
          "[mapper438][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0xABCDU, 0xEFU);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    const auto before = state(*mapper);
    for (Address addr = 0x4020U; addr < 0x6000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, static_cast<Byte>(addr));
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

TEST_CASE("Mapper 438 reset and power-on restore bank zero and vertical mirroring",
          "[mapper438][reset][banking][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, kMaxChrSize, initial, 0x35U);
        const auto cold = state(*mapper);
        mapper->write_prg(0xFFFFU, 0xFFU);
        REQUIRE(state(*mapper) != cold);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

        mapper->reset();
        CAPTURE(initial);
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        for (const auto addr : kPrgAddresses) {
            REQUIRE(mapper->read_prg(addr) == cell(expected_prg(addr, 0U), 0x35U));
        }
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x3FFFU}) {
            REQUIRE(mapper->read_chr(addr) == cell(expected_chr(addr, 0U), 0x60U));
        }

        mapper->write_prg(0xABCDU, 0xEFU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
    }
}

TEST_CASE("Mapper 438 bounded snapshots replay latches without PRG or CHR ROM",
          "[mapper438][state][banking]") {
    auto mapper = board();
    mapper->write_prg(0xFFFFU, 0xFFU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kMinChrSize);
    REQUIRE(saved[kAddressOffset] == 0x3FU);
    REQUIRE(saved[kDataOffset] == 0x3FU);

    auto copy = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0x3FU);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x2123U, 0x3FFFU}) {
        const auto physical = expected_chr(addr, 0x3FU);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0xD5U));
        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
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

TEST_CASE("Mapper 438 rejects truncated damaged and mismatched states atomically",
          "[mapper438][state]") {
    auto mapper = board();
    mapper->write_prg(0xABCDU, 0xEFU);
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
    for (unsigned bit = 0U; bit < kAddressOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        invalid[kDataOffset] ^= 1U;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte reserved : {Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}, Byte{0xFFU}}) {
        auto invalid_address = saved;
        invalid_address[kAddressOffset] = reserved;
        reject(invalid_address);
        auto invalid_data = saved;
        invalid_data[kDataOffset] = reserved;
        reject(invalid_data);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper438>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto prg = board(kMinPrgSize, kMaxChrSize);
    mismatch(prg);
    auto chr = board(kMaxPrgSize, kMinChrSize);
    mismatch(chr);
    auto both = board(0x00C000UZ, 0x006000UZ);
    mismatch(both);

    auto changed = saved;
    changed[kAddressOffset] = 0x3FU;
    changed[kDataOffset] = 0x3EU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, 0x3FU)));
    REQUIRE(mapper->read_chr(0x0123U) == cell(expected_chr(0x0123U, 0x3EU), 0x55U));
}

} // namespace
