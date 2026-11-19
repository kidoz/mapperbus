#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/mappers/mapper527.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kDefaultPrgSize = 0x040000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kDefaultChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kStateSize = 21UZ;
constexpr std::size_t kPrgBanksOffset = 10UZ;
constexpr std::size_t kChrBanksOffset = 12UZ;
constexpr std::size_t kMicrowireOffset = 20UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 527U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kDefaultPrgSize;
    result.chr_rom_size = kDefaultChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 37U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper527> board(std::size_t prg_size = kDefaultPrgSize,
                                 std::size_t chr_size = kDefaultChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper527::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper527>(h, rom(prg_size, salt), rom(chr_size, salt ^ 0x5AU));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper527& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper527& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0234U);
    const auto latch = mapper.read_prg(0x6123U);
    const auto mirror = mapper.mirror_mode();
    const auto nametable = mapper.nametable_address(0x2C34U);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0234U) == chr);
    REQUIRE(mapper.read_prg(0x6123U) == latch);
    REQUIRE(mapper.mirror_mode() == mirror);
    REQUIRE(mapper.nametable_address(0x2C34U) == nametable);
}

void require_prg(
    Mapper527& mapper, Address addr, std::size_t physical, std::size_t prg_size, Byte salt) {
    const auto offset = physical % prg_size;
    CAPTURE(addr, physical, offset, prg_size, salt);
    REQUIRE(mapper.read_prg(addr) == cell(offset, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(
    Mapper527& mapper, Address addr, std::size_t physical, std::size_t chr_size, Byte salt) {
    const auto offset = physical % chr_size;
    CAPTURE(addr, physical, offset, chr_size, salt);
    REQUIRE(mapper.read_chr(addr) == cell(offset, salt ^ 0x5AU));
}

TEST_CASE("Mapper 527 validates UNL-AX-40G geometry and bounded initial state",
          "[mapper527][geometry][ram][ciram][state]") {
    for (const auto prg : {kMinPrgSize, 0x010000UZ, 0x020000UZ, kDefaultPrgSize}) {
        for (const auto chr : {kMinChrSize, 0x008000UZ, 0x020000UZ, kDefaultChrSize}) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(prg, chr, mirror);
                REQUIRE(Mapper527::validate_geometry(header(mirror), prg, chr));
                auto mapper = board(prg, chr, mirror, 0x35U);
                auto different_rom = board(prg, chr, mirror, 0xA0U);
                REQUIRE(state(*mapper) == state(*different_rom));
                REQUIRE(state(*mapper).size() == kStateSize);
                REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
                REQUIRE(mapper->battery_ram().empty());
                REQUIRE(mapper->shared_nvram().empty());
                REQUIRE_FALSE(mapper->has_flash_storage());
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
                require_prg(*mapper, 0x8123U, 0x0123U, prg, 0x35U);
                require_prg(*mapper, 0xA123U, kPrgBankSize + 0x0123U, prg, 0x35U);
                require_prg(*mapper, 0xC123U, prg - 2U * kPrgBankSize + 0x0123U, prg, 0x35U);
                require_prg(*mapper, 0xE123U, prg - kPrgBankSize + 0x0123U, prg, 0x35U);
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    require_chr(*mapper,
                                static_cast<Address>(slot * kChrBankSize + 0x234U),
                                0x234U,
                                chr,
                                0x35U);
                }
                for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
                    REQUIRE(mapper->nametable_address(addr) == (addr & 0x03FFU));
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
        REQUIRE_FALSE(Mapper527::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 22U, 23U, 25U, 207U, 526U, 4095U}) {
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

TEST_CASE("Mapper 527 exhaustively decodes VRC2b A0 A1 and the low D0 latch",
          "[mapper527][decoding][banking][bus][ram]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = initial;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x6000U && raw < 0x7000U) {
            expected[kMicrowireOffset] = value & 1U;
        } else if (raw >= 0x8000U) {
            const auto group = raw >> 12U;
            const auto port = raw & 3U;
            if (group == 8U || group == 10U) {
                expected[kPrgBanksOffset + (group == 8U ? 0U : 1U)] = value & 0x1FU;
            } else if (group >= 11U && group <= 14U) {
                const auto index = (group - 11U) * 2U + (port >> 1U);
                auto& bank = expected[kChrBanksOffset + index];
                if ((port & 1U) == 0U) {
                    bank = (bank & 0xF0U) | (value & 0x0FU);
                } else {
                    bank = (bank & 0x0FU) | ((value & 0x0FU) << 4U);
                }
            }
        }

        CAPTURE(raw, value);
        mapper->write_prg(addr, value);
        REQUIRE(state(*mapper) == expected);
        const bool low_latch = raw >= 0x6000U && raw < 0x7000U;
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U || low_latch));
        REQUIRE(mapper->prg_read_mask(addr) ==
                (raw >= 0x8000U ? 0xFFU : (low_latch ? 0x01U : 0x00U)));
        if (raw < 0x6000U || (raw >= 0x7000U && raw < 0x8000U)) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
}

TEST_CASE("Mapper 527 selects two five-bit PRG banks and two fixed final banks",
          "[mapper527][banking][prg][bus]") {
    for (const std::size_t size : {kMinPrgSize, 0x010000UZ, 0x020000UZ, kDefaultPrgSize}) {
        auto mapper = board(size, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
        std::array<Byte, 2> banks{0U, 1U};
        for (unsigned reg = 0U; reg < 2U; ++reg) {
            for (unsigned raw = 0U; raw < 256U; ++raw) {
                const auto value = static_cast<Byte>(raw);
                const auto alias = static_cast<Address>((raw + reg) & 0xF0U);
                mapper->write_prg(
                    static_cast<Address>((reg == 0U ? 0x8000U : 0xA000U) | alias | (raw & 3U)),
                    value);
                banks[reg] = value & 0x1FU;
                CAPTURE(size, reg, raw, alias);
                require_prg(*mapper,
                            static_cast<Address>((reg == 0U ? 0x8123U : 0xA123U)),
                            static_cast<std::size_t>(banks[reg]) * kPrgBankSize + 0x0123U,
                            size,
                            0x35U);
                require_prg(*mapper, 0xC123U, size - 2U * kPrgBankSize + 0x0123U, size, 0x35U);
                require_prg(*mapper, 0xF123U, size - kPrgBankSize + 0x1123U, size, 0x35U);
            }
        }
    }

    auto mapper = board(kMinPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0xA0U);
    mapper->write_prg(0x8000U, 0xFFU);
    mapper->write_prg(0xA000U, 0xFEU);
    for (Address addr = 0x8000U;; ++addr) {
        const auto slot = static_cast<unsigned>((addr - 0x8000U) >> 13U);
        constexpr std::array<Byte, 4> kBanks{0x1FU, 0x1EU, 0x02U, 0x03U};
        require_prg(*mapper,
                    addr,
                    static_cast<std::size_t>(kBanks[slot]) * kPrgBankSize + (addr & 0x1FFFU),
                    kMinPrgSize,
                    0xA0U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
}

TEST_CASE("Mapper 527 combines eight-bit VRC2b CHR selectors and protects ROM",
          "[mapper527][banking][chr][bus][ciram]") {
    for (const std::size_t size : {kMinChrSize, 0x008000UZ, 0x020000UZ, kDefaultChrSize}) {
        auto mapper = board(kDefaultPrgSize, size, MirrorMode::Vertical, 0x35U);
        std::array<Byte, 8> banks{};
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            for (unsigned half = 0U; half < 2U; ++half) {
                for (unsigned raw = 0U; raw < 256U; ++raw) {
                    const auto value = static_cast<Byte>(raw ^ (slot * 0x33U));
                    const auto group = 11U + slot / 2U;
                    const auto port = (slot & 1U) * 2U + half;
                    mapper->write_prg(static_cast<Address>((group << 12U) | port | (raw & 0xF0U)),
                                      value);
                    if (half == 0U) {
                        banks[slot] = (banks[slot] & 0xF0U) | (value & 0x0FU);
                    } else {
                        banks[slot] = (banks[slot] & 0x0FU) | ((value & 0x0FU) << 4U);
                    }
                    CAPTURE(size, slot, half, raw, value);
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
    }

    auto mapper = board(kMinPrgSize, kMinChrSize, MirrorMode::Vertical, 0xA0U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto group = 11U + slot / 2U;
        const auto port = (slot & 1U) * 2U;
        mapper->write_prg(static_cast<Address>((group << 12U) | port), 0x08U);
        mapper->write_prg(static_cast<Address>((group << 12U) | port | 1U), 0x0FU);
    }
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        require_chr(*mapper, addr, 0xF8U * kChrBankSize + (addr & 0x3FFU), kMinChrSize, 0xA0U);
    }
    const auto before = state(*mapper);
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xA5U);
    mapper->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 527 wires CHR A17 to CIRAM A10 for all four arrangements",
          "[mapper527][ciram][banking][bus]") {
    auto mapper = board();
    for (unsigned bits = 0U; bits < 4U; ++bits) {
        mapper->reset();
        mapper->write_prg(0xB000U, 0x05U);
        mapper->write_prg(0xB002U, 0x0AU);
        mapper->write_prg(0xB001U, static_cast<Byte>((bits & 1U) != 0U ? 0x08U : 0x07U));
        mapper->write_prg(0xB003U, static_cast<Byte>((bits & 2U) != 0U ? 0x08U : 0x07U));
        CAPTURE(bits);
        const auto expected_mirror = bits == 0U   ? MirrorMode::SingleLower
                                     : bits == 1U ? MirrorMode::Horizontal
                                     : bits == 2U ? MirrorMode::Vertical
                                                  : MirrorMode::SingleUpper;
        REQUIRE(mapper->mirror_mode() == expected_mirror);
        for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
            const auto pair = (addr >> 11U) & 1U;
            const auto selected = pair == 0U ? (bits & 1U) : ((bits >> 1U) & 1U);
            const auto expected =
                static_cast<Address>((selected != 0U ? 0x400U : 0U) | (addr & 0x03FFU));
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE(mapper->nametable_address(addr) == expected);
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        require_chr(*mapper,
                    0x0234U,
                    ((bits & 1U) != 0U ? 0x85U : 0x75U) * kChrBankSize + 0x234U,
                    kDefaultChrSize,
                    0U);
        require_chr(*mapper,
                    0x0634U,
                    ((bits & 2U) != 0U ? 0x8AU : 0x7AU) * kChrBankSize + 0x234U,
                    kDefaultChrSize,
                    0U);

        const auto before = state(*mapper);
        for (unsigned port = 0U; port < 4U; ++port) {
            for (const Byte value : {Byte{0U}, Byte{1U}, Byte{2U}, Byte{3U}, Byte{0xFFU}}) {
                mapper->write_prg(static_cast<Address>(0x9000U | port), value);
                REQUIRE(state(*mapper) == before);
                REQUIRE(mapper->mirror_mode() == expected_mirror);
            }
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 527 low CPU window drives only D0 and exposes no RAM",
          "[mapper527][ram][bus][state]") {
    auto mapper = board();
    for (Address addr = 0x6000U; addr < 0x7000U; ++addr) {
        for (const Byte value :
             {Byte{0U}, Byte{1U}, Byte{2U}, Byte{3U}, Byte{0xFEU}, Byte{0xFFU}}) {
            mapper->write_prg(addr, value);
            CAPTURE(addr, value);
            REQUIRE(mapper->read_prg(addr) == (value & 1U));
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0x01U);
        }
    }
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        if (addr >= 0x6000U && addr < 0x7000U) {
            continue;
        }
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0x00U);
    }

    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 527 has no IRQ or expansion audio and ignores unrelated clocks",
          "[mapper527][irq][audio][bus][state]") {
    auto mapper = board();
    for (unsigned port = 0U; port < 4U; ++port) {
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            mapper->write_prg(static_cast<Address>(0xF000U | (raw & 0xF0U) | port),
                              static_cast<Byte>(raw));
        }
    }
    const auto before = state(*mapper);
    for (unsigned cycle = 0U; cycle < 100000U; ++cycle) {
        mapper->clock_cpu();
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

TEST_CASE("Mapper 527 reset restores VRC banks single-lower CIRAM and a zero latch",
          "[mapper527][reset][banking][ciram][ram]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Horizontal, 0x35U);
    const auto cold = state(*mapper);
    mapper->write_prg(0x8000U, 0x1FU);
    mapper->write_prg(0xA000U, 0x1EU);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto group = 11U + slot / 2U;
        const auto port = (slot & 1U) * 2U;
        mapper->write_prg(static_cast<Address>((group << 12U) | port), 0x0FU);
        mapper->write_prg(static_cast<Address>((group << 12U) | port | 1U), 0x0FU);
    }
    mapper->write_prg(0x6FFFU, 0xFFU);
    REQUIRE(state(*mapper) != cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);

    mapper->reset();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    require_prg(*mapper, 0x8123U, 0x0123U, kDefaultPrgSize, 0x35U);
    require_prg(*mapper, 0xA123U, kPrgBankSize + 0x0123U, kDefaultPrgSize, 0x35U);
    require_prg(
        *mapper, 0xC123U, kDefaultPrgSize - 2U * kPrgBankSize + 0x0123U, kDefaultPrgSize, 0x35U);
    require_prg(*mapper, 0xE123U, kDefaultPrgSize - kPrgBankSize + 0x0123U, kDefaultPrgSize, 0x35U);

    mapper->write_prg(0x8000U, 0x1FU);
    mapper->write_prg(0xB001U, 0x80U);
    mapper->write_prg(0x6000U, 1U);
    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
}

TEST_CASE("Mapper 527 bounded snapshots replay without ROM bytes",
          "[mapper527][state][atomic][banking][ciram][ram]") {
    auto mapper = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0x8000U, 0xF1U);
    mapper->write_prg(0xA000U, 0x82U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto group = 11U + slot / 2U;
        const auto port = (slot & 1U) * 2U;
        mapper->write_prg(static_cast<Address>((group << 12U) | port),
                          static_cast<Byte>(slot + 1U));
        mapper->write_prg(static_cast<Address>((group << 12U) | port | 1U),
                          static_cast<Byte>(0x08U + slot));
    }
    mapper->write_prg(0xB003U, 0U);
    mapper->write_prg(0x6123U, 0xFFU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x20UZ);
    REQUIRE(saved[0U] == 0x70U);
    REQUIRE(saved[1U] == 0x52U);
    REQUIRE(saved[kPrgBanksOffset] == 0x11U);
    REQUIRE(saved[kPrgBanksOffset + 1U] == 0x02U);
    REQUIRE(saved[kChrBanksOffset] == 0x81U);
    REQUIRE(saved[kChrBanksOffset + 7U] == 0xF8U);
    REQUIRE(saved[kMicrowireOffset] == 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    auto copy = board(kDefaultPrgSize, kDefaultChrSize, MirrorMode::Horizontal, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->nametable_address(0x2123U) == 0x0523U);
    REQUIRE(copy->nametable_address(0x2D23U) == 0x0123U);
    require_prg(*copy, 0x8123U, 0x11U * kPrgBankSize + 0x0123U, kDefaultPrgSize, 0xA0U);
    require_prg(*copy, 0xA123U, 0x02U * kPrgBankSize + 0x0123U, kDefaultPrgSize, 0xA0U);
    require_chr(*copy, 0x0234U, 0x81U * kChrBankSize + 0x234U, kDefaultChrSize, 0xA0U);
    require_chr(*copy, 0x1E34U, 0xF8U * kChrBankSize + 0x234U, kDefaultChrSize, 0xA0U);
    REQUIRE(copy->read_prg(0x6123U) == 1U);
}

TEST_CASE("Mapper 527 rejects truncated damaged and mismatched states atomically",
          "[mapper527][state][atomic][banking][ciram][ram]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x11U);
    mapper->write_prg(0xB001U, 0x80U);
    mapper->write_prg(0xB003U, 0x70U);
    mapper->write_prg(0x6000U, 1U);
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
    for (const auto offset : {kPrgBanksOffset, kPrgBanksOffset + 1U}) {
        auto invalid = saved;
        invalid[offset] = 0x20U;
        load_rejected(*mapper, invalid);
    }
    auto invalid_latch = saved;
    invalid_latch[kMicrowireOffset] = 2U;
    load_rejected(*mapper, invalid_latch);

    auto wrong_prg = board(kDefaultPrgSize / 2U);
    load_rejected(*wrong_prg, saved);
    auto wrong_chr = board(kDefaultPrgSize, kDefaultChrSize / 2U);
    load_rejected(*wrong_chr, saved);

    auto changed = saved;
    changed[kPrgBanksOffset] = 0x1FU;
    changed[kChrBanksOffset] ^= 0xFFU;
    changed[kMicrowireOffset] = 0U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0x8123U, 0x1FU * kPrgBankSize + 0x0123U, kDefaultPrgSize, 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
}

} // namespace
