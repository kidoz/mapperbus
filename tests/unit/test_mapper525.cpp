#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper525.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x004000UZ;
constexpr std::size_t kDefaultPrgSize = 0x020000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kDefaultChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kStateSize = 20UZ;
constexpr std::size_t kPrgBankOffset = 10UZ;
constexpr std::size_t kChrBanksOffset = 11UZ;
constexpr std::size_t kMirrorOffset = 19UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 525U;
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
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper525> board(std::size_t prg_size = kDefaultPrgSize,
                                 std::size_t chr_size = kDefaultChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper525::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper525>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x5AU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper525& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper525& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0234U);
    const auto mirror = mapper.mirror_mode();
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0234U) == chr);
    REQUIRE(mapper.mirror_mode() == mirror);
}

void require_prg(
    Mapper525& mapper, Address addr, std::size_t physical, std::size_t prg_size, Byte salt) {
    const auto offset = physical % prg_size;
    CAPTURE(addr, physical, offset, prg_size, salt);
    REQUIRE(mapper.read_prg(addr) == cell(offset, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(
    Mapper525& mapper, Address addr, std::size_t physical, std::size_t chr_size, Byte salt) {
    const auto offset = physical % chr_size;
    CAPTURE(addr, physical, offset, chr_size, salt);
    REQUIRE(mapper.read_chr(addr) == cell(offset, salt ^ 0x5AU));
}

TEST_CASE("Mapper 525 validates KS7021A ROM geometry and bounded initial state",
          "[mapper525][geometry][ram][ciram][state]") {
    for (const auto prg : {kMinPrgSize, 0x008000UZ, 0x010000UZ, kDefaultPrgSize}) {
        for (const auto chr : {kMinChrSize, 0x004000UZ, 0x020000UZ, kDefaultChrSize}) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(prg, chr, mirror);
                REQUIRE(Mapper525::validate_geometry(header(mirror), prg, chr));
                auto mapper = board(prg, chr, mirror, 0x35U);
                auto different_rom = board(prg, chr, mirror, 0xA0U);
                REQUIRE(state(*mapper) == state(*different_rom));
                REQUIRE(state(*mapper).size() == kStateSize);
                REQUIRE(mapper->mirror_mode() == mirror);
                REQUIRE(mapper->battery_ram().empty());
                REQUIRE(mapper->shared_nvram().empty());
                REQUIRE_FALSE(mapper->has_flash_storage());
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
                for (const Address addr : {Address{0x8000U}, Address{0xA000U}}) {
                    require_prg(*mapper, addr, addr & 0x3FFFU, prg, 0x35U);
                }
                for (const Address addr : {Address{0xC000U}, Address{0xE000U}}) {
                    require_prg(*mapper, addr, prg - kPrgBankSize + (addr & 0x3FFFU), prg, 0x35U);
                }
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    require_chr(*mapper,
                                static_cast<Address>(slot * kChrBankSize + 0x234U),
                                0x234U,
                                chr,
                                0x35U);
                }
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
        REQUIRE_FALSE(Mapper525::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 22U, 23U, 25U, 524U, 526U, 4095U}) {
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
         {0UZ, 1UZ, 0x2000UZ, 0x4001UZ, 0x6000UZ, 0xC000UZ, kDefaultPrgSize * 2UZ}) {
        reject(header(), size, kDefaultChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x0400UZ, 0x1000UZ, 0x2001UZ, 0x3000UZ, kDefaultChrSize * 2UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
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

TEST_CASE("Mapper 525 exhaustively decodes only the three documented register ranges",
          "[mapper525][decoding][banking][bus]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = initial;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const auto group = raw >> 12U;
        if (group == 8U) {
            expected[kPrgBankOffset] = (value >> 1U) & 7U;
        } else if (group == 9U) {
            expected[kMirrorOffset] = value & 1U;
        } else if (group == 11U) {
            expected[kChrBanksOffset + (raw & 7U)] = value;
        }

        CAPTURE(raw, value, group);
        mapper->write_prg(addr, value);
        REQUIRE(state(*mapper) == expected);
        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
}

TEST_CASE("Mapper 525 selects shifted 16 KiB PRG below a fixed final bank",
          "[mapper525][banking][prg][bus]") {
    for (const std::size_t size : {kMinPrgSize, 0x008000UZ, 0x010000UZ, kDefaultPrgSize}) {
        auto mapper = board(size, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto value = static_cast<Byte>(raw);
            mapper->write_prg(0x8000U | (raw & 0x0FFFU), value);
            const std::size_t bank = (value >> 1U) & 7U;
            CAPTURE(size, raw, bank);
            for (const Address offset : {Address{0U}, Address{0x123U}, Address{0x3FFFU}}) {
                require_prg(*mapper,
                            static_cast<Address>(0x8000U + offset),
                            bank * kPrgBankSize + offset,
                            size,
                            0x35U);
                require_prg(*mapper,
                            static_cast<Address>(0xC000U + offset),
                            size - kPrgBankSize + offset,
                            size,
                            0x35U);
            }
        }
    }

    auto mapper = board(kMinPrgSize, kDefaultChrSize, MirrorMode::Horizontal, 0xA0U);
    mapper->write_prg(0x8FFFU, 0x0FU);
    for (Address addr = 0x8000U;; ++addr) {
        require_prg(*mapper,
                    addr,
                    addr < 0xC000U ? 7U * kPrgBankSize + (addr & 0x3FFFU) : addr & 0x3FFFU,
                    kMinPrgSize,
                    0xA0U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
}

TEST_CASE("Mapper 525 selects eight independent full-byte CHR banks and protects ROM",
          "[mapper525][banking][chr][bus]") {
    for (const std::size_t size : {kMinChrSize, 0x008000UZ, 0x020000UZ, kDefaultChrSize}) {
        auto mapper = board(kDefaultPrgSize, size, MirrorMode::Vertical, 0x35U);
        std::array<Byte, 8> banks{};
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto value = static_cast<Byte>(raw);
                mapper->write_prg(static_cast<Address>(0xB000U + slot), value);
                banks[slot] = value;
                CAPTURE(size, slot, raw);
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

    auto mapper = board();
    const auto before = state(*mapper);
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xA5U);
    mapper->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 525 exposes only PRG ROM on its CPU and expansion buses",
          "[mapper525][bus][ram][decoding]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        const auto before = state(*mapper);
        const auto addr = static_cast<Address>(raw);
        mapper->write_prg(addr, static_cast<Byte>(raw));
        mapper->write_expansion(addr, static_cast<Byte>(raw ^ 0xFFU));
        CAPTURE(raw);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(state(*mapper) == before);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
}

TEST_CASE("Mapper 525 switches only horizontal and vertical CIRAM",
          "[mapper525][ciram][bus][reset]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, kDefaultChrSize, initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x9000U | (value << 4U), static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
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
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == initial);
    }
}

TEST_CASE("Mapper 525 has no IRQ audio RAM or unrelated observer side effects",
          "[mapper525][irq][audio][ram][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x0EU);
    mapper->write_prg(0xB007U, 0xA5U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x2012U);
    mapper->observe_ppu_read(0x2012U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_cpu_falling_edge();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    mapper->acknowledge_irq();
    mapper->set_region(Region::PAL);
    mapper->write_chr(0x0123U, 0xFFU);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->led_outputs() == std::nullopt);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 525 reset restores deterministic bank and CIRAM defaults",
          "[mapper525][reset][banking][ciram][ram][irq]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, kDefaultChrSize, mirror, 0x35U);
        const auto cold = state(*mapper);
        mapper->write_prg(0x8000U, 0x0FU);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            mapper->write_prg(static_cast<Address>(0xB000U + slot),
                              static_cast<Byte>(0x80U + slot));
        }
        mapper->write_prg(0x9000U, static_cast<Byte>(mirror == MirrorMode::Horizontal ? 0U : 1U));
        REQUIRE(state(*mapper) != cold);

        mapper->reset();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        require_prg(*mapper, 0x8123U, 0x123U, kDefaultPrgSize, 0x35U);
        require_prg(
            *mapper, 0xF123U, kDefaultPrgSize - kPrgBankSize + 0x3123U, kDefaultPrgSize, 0x35U);
        require_chr(*mapper, 0x1E34U, 0x0234U, kDefaultChrSize, 0x35U);

        mapper->write_prg(0x8000U, 0x0EU);
        mapper->write_prg(0xB003U, 0xFFU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
    }
}

TEST_CASE("Mapper 525 bounded snapshots replay without ROM bytes",
          "[mapper525][state][atomic][banking][ciram]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0x8000U, 0x0BU);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0xB000U + slot),
                          static_cast<Byte>(0x20U + slot * 17U));
    }
    mapper->write_prg(0x9000U, 1U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[0U] == 0x51U);
    REQUIRE(saved[1U] == 0x52U);
    REQUIRE(saved[kPrgBankOffset] == 5U);
    REQUIRE(saved[kChrBanksOffset] == 0x20U);
    REQUIRE(saved[kChrBanksOffset + 7U] == 0x97U);
    REQUIRE(saved[kMirrorOffset] == 1U);

    auto copy = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    require_prg(*copy, 0x8123U, 5U * kPrgBankSize + 0x123U, kDefaultPrgSize, 0xA0U);
    require_prg(*copy, 0xF123U, kDefaultPrgSize - kPrgBankSize + 0x3123U, kDefaultPrgSize, 0xA0U);
    require_chr(*copy, 0x0234U, 0x20U * kChrBankSize + 0x234U, kDefaultChrSize, 0xA0U);
    require_chr(*copy, 0x1E34U, 0x97U * kChrBankSize + 0x234U, kDefaultChrSize, 0xA0U);
}

TEST_CASE("Mapper 525 rejects truncated damaged and mismatched states atomically",
          "[mapper525][state][atomic][banking][ciram]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x0BU);
    mapper->write_prg(0xB000U, 0xA5U);
    mapper->write_prg(0xB007U, 0x5AU);
    mapper->write_prg(0x9000U, 1U);
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    auto trailing = saved;
    trailing.push_back(0U);
    load_rejected(*mapper, trailing);
    for (std::size_t offset = 0U; offset < kPrgBankOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        load_rejected(*mapper, invalid);
    }
    const std::array<std::pair<std::size_t, Byte>, 4> bad_fields = {{
        {kPrgBankOffset, 8U},
        {kPrgBankOffset, 0xFFU},
        {kMirrorOffset, 2U},
        {kMirrorOffset, 0xFFU},
    }};
    for (const auto& [offset, value] : bad_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        load_rejected(*mapper, invalid);
    }

    auto wrong_prg = board(kDefaultPrgSize / 2U);
    load_rejected(*wrong_prg, saved);
    auto wrong_chr = board(kDefaultPrgSize, kDefaultChrSize / 2U);
    load_rejected(*wrong_chr, saved);
    auto wrong_mirror = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Horizontal);
    load_rejected(*wrong_mirror, saved);

    auto changed = saved;
    changed[kPrgBankOffset] = 7U;
    changed[kChrBanksOffset] ^= 0xFFU;
    changed[kMirrorOffset] = 0U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0x8123U, 7U * kPrgBankSize + 0x123U, kDefaultPrgSize, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
}

} // namespace
