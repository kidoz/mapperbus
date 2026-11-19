#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper424.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kKiB = 1024UZ;
constexpr std::size_t kMiB = 1024UZ * kKiB;
constexpr std::size_t kMaxRomSize = 64UZ * kMiB;
constexpr std::size_t kStateSize = 35UZ;
constexpr std::size_t kOuterOffset = 14UZ;
constexpr std::size_t kControlOffset = 15UZ;
constexpr std::size_t kMirrorOffset = 16UZ;
constexpr std::size_t kPrgOffset = 17UZ;
constexpr std::size_t kMiddleOffset = 20UZ;
constexpr std::size_t kModeOffset = 21UZ;
constexpr std::size_t kChrOffset = 22UZ;
constexpr std::size_t kIntermediateOffset = 28UZ;
constexpr std::size_t kChrMiddleOffset = 29UZ;
constexpr std::size_t kLatchOffset = 30UZ;
constexpr std::size_t kCounterOffset = 31UZ;
constexpr std::size_t kIrqOffset = 32UZ;
constexpr std::size_t kPinsOffset = 33UZ;
constexpr std::size_t kExtensionOffset = 34UZ;
constexpr std::array<unsigned, 8> kPrgMasks{63U, 31U, 15U, 7U, 3U, 1U, 0U, 255U};
constexpr std::array<unsigned, 8> kChrMasks{255U, 127U, 63U, 0U, 31U, 15U, 7U, 0U};

INesHeader make_header(bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 424U;
    header.is_nes2 = true;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 37U + (bank >> 8U) * 53U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 41U + (bank >> 8U) * 73U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper424> make_board(std::size_t prg_size = 128UZ * kKiB,
                                      std::size_t chr_size = 128UZ * kKiB,
                                      bool vertical = true) {
    std::vector<Byte> prg(prg_size);
    std::vector<Byte> chr(chr_size);
    for (std::size_t index = 0U; index < prg.size(); ++index) {
        prg[index] =
            prg_cell(static_cast<unsigned>(index / 8192U), static_cast<unsigned>(index % 8192U));
    }
    for (std::size_t index = 0U; index < chr.size(); ++index) {
        chr[index] =
            chr_cell(static_cast<unsigned>(index / 1024U), static_cast<unsigned>(index % 1024U));
    }
    const auto header = make_header(vertical);
    REQUIRE(Mapper424::validate_geometry(header, prg_size, chr_size));
    return std::make_unique<Mapper424>(header, std::move(prg), std::move(chr));
}

std::vector<Byte> save(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void edge(Mapper424& mapper, unsigned low_cycles = 3U) {
    mapper.observe_ppu_a12(false);
    for (unsigned cycle = 0U; cycle < low_cycles; ++cycle) {
        mapper.clock_cpu();
    }
    mapper.observe_ppu_a12(true);
}

TEST_CASE("Mapper 424 validates NES 2.0 ROM RAM and CIRAM geometry", "[mapper424][geometry]") {
    for (std::size_t prg = 32UZ * kKiB; prg <= kMaxRomSize; prg <<= 1U) {
        REQUIRE(Mapper424::validate_geometry(make_header(), prg, 8192U));
    }
    REQUIRE(Mapper424::validate_geometry(make_header(), 32768U, 0U));
    for (std::size_t chr = 8UZ * kKiB; chr <= kMaxRomSize; chr <<= 1U) {
        REQUIRE(Mapper424::validate_geometry(make_header(), 32768U, chr));
    }
    REQUIRE(Mapper424::validate_geometry(make_header(false), 32768U, 8192U));

    const auto reject = [](INesHeader header, std::size_t prg, std::size_t chr) {
        CAPTURE(header.mapper_number,
                header.is_nes2,
                header.submapper,
                header.has_battery,
                header.has_trainer,
                header.prg_ram_size,
                header.prg_nvram_size,
                header.chr_ram_size,
                header.chr_nvram_size,
                header.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper424::validate_geometry(header, prg, chr));
    };
    for (std::size_t prg : {0UZ, 16384UZ, 32767UZ, 49152UZ, 2U * kMaxRomSize}) {
        reject(make_header(), prg, 8192U);
    }
    for (std::size_t chr : {1UZ, 4096UZ, 12288UZ, 2U * kMaxRomSize}) {
        reject(make_header(), 32768U, chr);
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto header = make_header();
        header.submapper = submapper;
        reject(header, 32768U, 8192U);
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto header = make_header();
        header.mirror_mode = mirror;
        reject(header, 32768U, 8192U);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.mapper_number = 423U;
            break;
        case 1U:
            header.is_nes2 = false;
            break;
        case 2U:
            header.has_battery = true;
            break;
        case 3U:
            header.prg_ram_size = 8192U;
            break;
        case 4U:
            header.prg_nvram_size = 8192U;
            break;
        case 5U:
            header.chr_ram_size = 8192U;
            break;
        case 6U:
            header.chr_nvram_size = 8192U;
            break;
        default:
            header.has_trainer = true;
            break;
        }
        reject(header, 32768U, 8192U);
    }

    auto mapper = make_board();
    REQUIRE(save(*mapper).size() == kStateSize);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 424 decodes exact write-only expansion and native PPU apertures",
          "[mapper424][decoding][bus]") {
    auto mapper = make_board();
    const auto initial = save(*mapper);
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        mapper->reset();
        mapper->write_expansion(static_cast<Address>(addr), 0xD5U);
        auto expected = initial;
        if (addr == 0x4100U) {
            expected[kOuterOffset] = 0xD5U;
        } else if (addr == 0x4101U) {
            expected[kLatchOffset] = 0xD5U;
        } else if (addr == 0x4104U) {
            expected[kIrqOffset] = 1U;
        } else if (addr == 0x4105U) {
            expected[kControlOffset] = 0xC5U;
        } else if (addr == 0x4106U) {
            expected[kMirrorOffset] = 1U;
        } else if (addr >= 0x4107U && addr <= 0x4109U) {
            expected[kPrgOffset + addr - 0x4107U] = 0xD5U;
        } else if (addr == 0x410AU) {
            expected[kMiddleOffset] = 0xD5U;
        } else if (addr == 0x410BU) {
            expected[kModeOffset] = 0xC5U;
        } else if (addr == 0x411EU) {
            expected[kExtensionOffset] = 1U;
        }
        CAPTURE(addr);
        REQUIRE(save(*mapper) == expected);
        const bool mapped = (addr >= 0x4100U && addr <= 0x410BU) || addr == 0x411EU;
        REQUIRE(mapper->maps_expansion(static_cast<Address>(addr)) == mapped);
        REQUIRE(mapper->expansion_read_mask(static_cast<Address>(addr)) == (mapped ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(static_cast<Address>(addr)) == 0U);
    }

    for (unsigned addr = 0x2000U; addr < 0x4000U; ++addr) {
        mapper->reset();
        const bool native = addr >= 0x2010U && addr <= 0x201FU;
        REQUIRE(mapper->write_ppu_register(static_cast<Address>(addr), 0xD5U) == native);
        REQUIRE(mapper->read_ppu_register(static_cast<Address>(addr), 0xA7U) ==
                (native ? std::optional<Byte>(0xA7U) : std::nullopt));
        auto expected = initial;
        if (addr >= 0x2012U && addr <= 0x2017U) {
            expected[kChrOffset + addr - 0x2012U] = 0xD5U;
        } else if (addr == 0x2018U) {
            expected[kIntermediateOffset] = 0x50U;
        } else if (addr == 0x201AU) {
            expected[kChrMiddleOffset] = 0xD5U;
        }
        CAPTURE(addr);
        REQUIRE(save(*mapper) == expected);
    }
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
}

TEST_CASE("Mapper 424 forwards MMC3 aliases and FWEN without creating WRAM",
          "[mapper424][decoding][bus][ram]") {
    auto mapper = make_board();
    mapper->write_expansion(0x4105U, 0x20U);
    const auto initial = save(*mapper);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_expansion(0x4105U, 0x20U);
        mapper->write_prg(static_cast<Address>(addr), 0xA3U);
        auto expected = initial;
        if (addr >= 0x8000U) {
            switch (addr & 0xE001U) {
            case 0x8000U:
                expected[kControlOffset] = 0xA3U;
                break;
            case 0x8001U:
                expected[kChrOffset + 4U] = 0xA3U;
                break;
            case 0xA000U:
                expected[kMirrorOffset] = 1U;
                break;
            case 0xC000U:
                expected[kLatchOffset] = 0xA3U;
                break;
            case 0xE001U:
                expected[kIrqOffset] = 1U;
                break;
            default:
                break;
            }
        }
        CAPTURE(addr);
        REQUIRE(save(*mapper) == expected);
    }
    for (unsigned index = 0U; index < 8U; ++index) {
        mapper->reset();
        mapper->write_expansion(0x4105U, static_cast<Byte>(index));
        mapper->write_prg(0x9FFFU, 0xFFU);
        const auto bytes = save(*mapper);
        REQUIRE(bytes[index < 6U ? (index < 2U ? kChrOffset + 4U + index : kChrOffset + index - 2U)
                                 : kPrgOffset + index - 6U] == 0xFFU);
    }

    mapper->write_expansion(0x410BU, 8U);
    const auto disabled = save(*mapper);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        REQUIRE(save(*mapper) == disabled);
    }
    mapper->write_expansion(0x4107U, 9U);
    mapper->write_expansion(0x411EU, 1U);
    REQUIRE(save(*mapper)[kPrgOffset] == 9U);
    REQUIRE(save(*mapper)[kExtensionOffset] == 1U);
}

TEST_CASE("Mapper 424 applies normal PRG masks middle substitution and COMR6 swap",
          "[mapper424][banking][bus]") {
    auto mapper = make_board(8UZ * kMiB, 8192U);
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (unsigned value : {0U, 1U, 0x55U, 0xFEU, 0xFFU}) {
            for (unsigned outer : {0U, 3U, 15U}) {
                for (Byte middle : {0U, 0x55U, 0xAAU, 0xFFU}) {
                    for (bool third : {false, true}) {
                        for (bool swap : {false, true}) {
                            mapper->write_expansion(0x4100U, static_cast<Byte>(outer << 4U));
                            mapper->write_expansion(0x4105U, swap ? 0x40U : 0U);
                            mapper->write_expansion(0x4107U, static_cast<Byte>(value));
                            mapper->write_expansion(0x4108U, 0x59U);
                            mapper->write_expansion(0x4109U, 0xA6U);
                            mapper->write_expansion(0x410AU, middle);
                            mapper->write_expansion(0x410BU,
                                                    static_cast<Byte>(mode | (third ? 0x40U : 0U)));
                            const std::array<unsigned, 4> pages{swap ? (third ? 0xA6U : 254U)
                                                                     : value,
                                                                0x59U,
                                                                swap    ? value
                                                                : third ? 0xA6U
                                                                        : 254U,
                                                                255U};
                            for (unsigned slot = 0U; slot < 4U; ++slot) {
                                const unsigned bank =
                                    ((pages[slot] & kPrgMasks[mode]) |
                                     (middle & (255U ^ kPrgMasks[mode])) | (outer << 8U)) %
                                    1024U;
                                const auto addr =
                                    static_cast<Address>(0x8000U + slot * 8192U + 0x123U);
                                CAPTURE(mode, value, outer, middle, third, swap, slot, bank);
                                REQUIRE(mapper->read_prg(addr) == prg_cell(bank, 0x123U));
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 424 applies normal CHR masks pairing inversion and shared ROM storage",
          "[mapper424][banking][chr]") {
    auto mapper = make_board(8UZ * kMiB, 4UZ * kMiB);
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (unsigned value : {0U, 1U, 0x55U, 0xFEU, 0xFFU}) {
            for (unsigned intermediate : {0U, 5U, 7U}) {
                for (unsigned outer : {0U, 7U, 15U}) {
                    for (bool flip : {false, true}) {
                        mapper->write_expansion(0x4100U, static_cast<Byte>(outer));
                        mapper->write_expansion(0x4105U, flip ? 0x80U : 0U);
                        const auto middle = static_cast<Byte>((value & 248U) | mode);
                        mapper->write_ppu_register(0x201AU, middle);
                        mapper->write_ppu_register(0x2018U, static_cast<Byte>(intermediate << 4U));
                        std::array<unsigned, 6> banks{};
                        for (unsigned reg = 0U; reg < 6U; ++reg) {
                            banks[reg] = (value + reg * 17U) & 255U;
                            mapper->write_ppu_register(static_cast<Address>(0x2012U + reg),
                                                       static_cast<Byte>(banks[reg]));
                        }
                        const std::array<unsigned, 8> pages{banks[4] & 254U,
                                                            banks[4] | 1U,
                                                            banks[5] & 254U,
                                                            banks[5] | 1U,
                                                            banks[0],
                                                            banks[1],
                                                            banks[2],
                                                            banks[3]};
                        for (unsigned slot = 0U; slot < 8U; ++slot) {
                            const unsigned bank =
                                ((pages[slot ^ (flip ? 4U : 0U)] & kChrMasks[mode]) |
                                 (middle & (255U ^ kChrMasks[mode]) & 248U) | (intermediate << 8U) |
                                 (outer << 11U)) %
                                4096U;
                            const auto addr = static_cast<Address>(slot * 1024U + 0x123U);
                            CAPTURE(mode, value, intermediate, outer, flip, slot, bank);
                            REQUIRE(mapper->read_chr(addr) == chr_cell(bank, 0x123U));
                        }
                    }
                }
            }
        }
    }

    auto shared = make_board(128UZ * kKiB, 0U);
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        shared->write_ppu_register(0x2012U, static_cast<Byte>(bank));
        const unsigned physical = (bank * 1024U + 0x123U) % (128U * 1024U);
        REQUIRE(shared->read_chr(0x1123U) == prg_cell(physical / 8192U, physical % 8192U));
    }
}

TEST_CASE("Mapper 424 outer extension reaches independent high PRG and CHR halves",
          "[mapper424][banking][decoding]") {
    auto mapper = make_board(kMaxRomSize, kMaxRomSize);
    mapper->write_expansion(0x4100U, 0xAFU);
    mapper->write_expansion(0x4107U, 0x12U);
    mapper->write_expansion(0x4108U, 0x34U);
    mapper->write_expansion(0x4109U, 0x56U);
    mapper->write_expansion(0x410AU, 0U);
    mapper->write_expansion(0x410BU, 7U);
    mapper->write_expansion(0x411EU, 0xFFU);
    for (bool swap : {false, true}) {
        mapper->write_expansion(0x4105U, swap ? 0x40U : 0U);
        const std::array<unsigned, 4> pages{swap ? 254U : 0x12U, 0x34U, swap ? 0x12U : 254U, 255U};
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto addr = static_cast<Address>(0x8000U + slot * 8192U + 0x123U);
            REQUIRE(mapper->read_prg(addr) == prg_cell(0x1A00U + pages[slot], 0x123U));
        }
    }

    const std::array<Byte, 6> banks{0x12U, 0x34U, 0x56U, 0x78U, 0x9AU, 0xBCU};
    for (unsigned reg = 0U; reg < banks.size(); ++reg) {
        mapper->write_ppu_register(static_cast<Address>(0x2012U + reg), banks[reg]);
    }
    mapper->write_ppu_register(0x2018U, 0x70U);
    mapper->write_ppu_register(0x201AU, 0U);
    for (bool flip : {false, true}) {
        mapper->write_expansion(0x4105U, flip ? 0x80U : 0U);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const unsigned logical = slot ^ (flip ? 4U : 0U);
            const unsigned page = logical < 4U ? (banks[4U + logical / 2U] & 254U) | (logical & 1U)
                                               : banks[logical - 4U];
            const auto addr = static_cast<Address>(slot * 1024U + 0x123U);
            const auto actual = mapper->read_chr(addr);
            const auto expected = chr_cell(0xFF00U | page, 0x123U);
            CAPTURE(flip, slot, page, +actual, +expected);
            REQUIRE(actual == expected);
        }
    }
    mapper->write_expansion(0x411EU, 2U);
    REQUIRE(save(*mapper)[kExtensionOffset] == 0U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0x0A12U, 0U));
    REQUIRE(save(*mapper).size() == kStateSize);
}

TEST_CASE("Mapper 424 preserves console CIRAM routing and immutable pattern ROM",
          "[mapper424][ciram][ram][reset]") {
    auto mapper = make_board(128UZ * kKiB, 8192U, false);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_expansion(0x4106U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xA000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xBFFEU, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    for (Address addr : {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1FFFU, 0x2000U, 0x3EFFU}) {
        REQUIRE_FALSE(mapper->ciram_address(addr));
    }
    mapper->write_expansion(0x4105U, 0x20U);
    for (Address addr : {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1FFFU}) {
        REQUIRE(mapper->ciram_address(addr) == static_cast<Address>(addr & 0x07FFU));
    }
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->unmirrored_nametables());

    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto before = mapper->read_chr(static_cast<Address>(addr));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(before ^ 0xFFU));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == before);
    }
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(save(*mapper) == save(*make_board(128UZ * kKiB, 8192U)));
}

TEST_CASE("Mapper 424 clocks qualified A12 and HSYNC IRQs without expansion audio",
          "[mapper424][irq][audio]") {
    auto mapper = make_board();
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_expansion(0x4101U, static_cast<Byte>(latch));
        mapper->write_expansion(0x4102U, 0U);
        mapper->write_expansion(0x4104U, 0U);
        mapper->on_ppu_hblank();
        REQUIRE_FALSE(mapper->irq_pending());
        edge(*mapper);
        CAPTURE(latch);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        REQUIRE(save(*mapper)[kCounterOffset] == latch);
        for (unsigned count = 0U; count < latch; ++count) {
            edge(*mapper);
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->write_expansion(0x4103U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    for (unsigned low = 0U; low <= 3U; ++low) {
        mapper->reset();
        mapper->write_expansion(0x4104U, 0U);
        edge(*mapper, low);
        REQUIRE(mapper->irq_pending() == (low == 3U));
    }

    mapper->reset();
    mapper->write_expansion(0x410BU, 0x80U);
    mapper->write_expansion(0x4101U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->on_ppu_hblank();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4103U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    const auto before = save(*mapper);
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_frame_start();
    REQUIRE(save(*mapper) == before);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 424 reset restores bounded defaults and clears extension and IRQ",
          "[mapper424][reset][irq]") {
    auto mapper = make_board();
    mapper->write_expansion(0x4100U, 0xA7U);
    mapper->write_expansion(0x4101U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    mapper->write_expansion(0x4105U, 0xE7U);
    mapper->write_expansion(0x4106U, 1U);
    mapper->write_expansion(0x4107U, 0x12U);
    mapper->write_expansion(0x410AU, 0x34U);
    mapper->write_expansion(0x410BU, 0x4FU);
    mapper->write_expansion(0x411EU, 1U);
    mapper->write_ppu_register(0x2012U, 0x56U);
    mapper->write_ppu_register(0x2018U, 0x70U);
    mapper->write_ppu_register(0x201AU, 0x78U);
    edge(*mapper);
    REQUIRE(mapper->irq_pending());
    REQUIRE_FALSE(save(*mapper) == save(*make_board()));

    mapper->reset();
    REQUIRE(save(*mapper) == save(*make_board()));
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(save(*mapper)[kExtensionOffset] == 0U);

    mapper->write_expansion(0x411EU, 1U);
    mapper->power_on();
    REQUIRE(save(*mapper) == save(*make_board()));
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 424 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper424][state][irq]") {
    auto mapper = make_board();
    mapper->write_expansion(0x4100U, 0xA7U);
    mapper->write_expansion(0x4105U, 0xA5U);
    mapper->write_expansion(0x4106U, 1U);
    mapper->write_expansion(0x4107U, 0x95U);
    mapper->write_expansion(0x4108U, 0x59U);
    mapper->write_expansion(0x4109U, 0xA6U);
    mapper->write_expansion(0x410AU, 0x5AU);
    mapper->write_expansion(0x410BU, 0x47U);
    for (unsigned reg = 0U; reg < 6U; ++reg) {
        mapper->write_ppu_register(static_cast<Address>(0x2012U + reg),
                                   static_cast<Byte>(0x31U + reg * 19U));
    }
    mapper->write_ppu_register(0x2018U, 0x50U);
    mapper->write_ppu_register(0x201AU, 0xABU);
    mapper->write_expansion(0x4101U, 2U);
    mapper->write_expansion(0x4102U, 0U);
    mapper->write_expansion(0x4104U, 0U);
    edge(*mapper);
    mapper->observe_ppu_a12(false);
    mapper->clock_cpu();
    mapper->clock_cpu();
    mapper->write_expansion(0x411EU, 1U);
    const auto expected = save(*mapper);
    REQUIRE(expected.size() == kStateSize);
    REQUIRE(expected[kIrqOffset] == 1U);
    REQUIRE(expected[kPinsOffset] == 4U);
    REQUIRE(expected[kExtensionOffset] == 1U);

    mapper->reset();
    load(*mapper, expected);
    REQUIRE(save(*mapper) == expected);
    mapper->clock_cpu();
    mapper->observe_ppu_a12(true);
    edge(*mapper);
    REQUIRE(mapper->irq_pending());
    load(*mapper, expected);

    auto with_tail = expected;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    mapper->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(save(*mapper) == expected);

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == expected);
    };
    for (std::size_t length = 0U; length < expected.size(); ++length) {
        CAPTURE(length);
        reject(std::span(expected).first(length));
    }
    const std::array<std::pair<std::size_t, Byte>, 14> bad_fields{{
        {0U, 0U},
        {2U, 1U},
        {6U, 1U},
        {10U, 1U},
        {kControlOffset, 0x08U},
        {kMirrorOffset, 2U},
        {kModeOffset, 0x10U},
        {kIntermediateOffset, 0x80U},
        {kIrqOffset, 2U},
        {kIrqOffset, 4U},
        {kPinsOffset, 3U},
        {kPinsOffset, 7U},
        {kExtensionOffset, 2U},
        {kExtensionOffset, 0xFFU},
    }};
    for (const auto& [offset, value] : bad_fields) {
        auto bad = expected;
        bad[offset] = value;
        CAPTURE(offset, value);
        reject(bad);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper424>& other) {
        const auto before = save(*other);
        StateReader reader(expected);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*other) == before);
    };
    auto prg = make_board(256UZ * kKiB);
    mismatch(prg);
    auto chr = make_board(128UZ * kKiB, 256UZ * kKiB);
    mismatch(chr);
    auto shared = make_board(128UZ * kKiB, 0U);
    mismatch(shared);

    auto changed = expected;
    changed[kOuterOffset] ^= 0xFFU;
    changed[kExtensionOffset] = 0U;
    load(*mapper, changed);
    REQUIRE(save(*mapper) == changed);
}

} // namespace
