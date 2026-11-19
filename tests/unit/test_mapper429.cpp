#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper429.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kDiyPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kStateSize = 15UZ + Mapper429::kChrRamSize;
constexpr std::size_t kLatchOffset = 14UZ;
constexpr std::size_t kChrRamOffset = 15UZ;

INesHeader make_header(Byte submapper = 0U,
                       bool battery = false,
                       MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader header{};
    header.mapper_number = 429U;
    header.is_nes2 = true;
    header.submapper = submapper;
    header.mirror_mode = mirror;
    header.has_battery = battery;
    header.chr_ram_size = battery ? 0U : Mapper429::kChrRamSize;
    header.chr_nvram_size = battery ? Mapper429::kChrRamSize : 0U;
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset, Byte salt = 0U) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U) * 17U + salt);
}

Byte chr_cell(unsigned bank, unsigned offset, Byte salt = 0U) {
    return static_cast<Byte>(bank * 53U + offset + (offset >> 8U) * 29U + salt);
}

unsigned initial_bank(Byte submapper) {
    return submapper == 2U ? 0U : 1U;
}

std::unique_ptr<Mapper429> make_board(std::size_t prg_size = kPrgSize,
                                      Byte submapper = 0U,
                                      bool battery = false,
                                      MirrorMode mirror = MirrorMode::Vertical,
                                      Byte salt = 0U) {
    std::vector<Byte> prg(prg_size);
    for (std::size_t index = 0U; index < prg.size(); ++index) {
        prg[index] = prg_cell(static_cast<unsigned>(index / kPrgBankSize),
                              static_cast<unsigned>(index % kPrgBankSize),
                              salt);
    }
    const auto header = make_header(submapper, battery, mirror);
    REQUIRE(Mapper429::validate_geometry(header, prg_size, 0U));
    return std::make_unique<Mapper429>(header, std::move(prg), std::vector<Byte>{});
}

std::unique_ptr<Mapper429> make_ff_board(Byte submapper = 2U) {
    const auto header = make_header(submapper);
    REQUIRE(Mapper429::validate_geometry(header, kPrgSize, 0U));
    return std::make_unique<Mapper429>(
        header, std::vector<Byte>(kPrgSize, 0xFFU), std::vector<Byte>{});
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

TEST_CASE("Mapper 429 validates ROM submapper RAM and CIRAM geometry",
          "[mapper429][geometry][ram]") {
    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        for (bool battery : {false, true}) {
            for (std::size_t prg_size : {kPrgSize, kDiyPrgSize}) {
                for (auto mirror : {MirrorMode::Horizontal,
                                    MirrorMode::Vertical,
                                    MirrorMode::SingleLower,
                                    MirrorMode::SingleUpper}) {
                    CAPTURE(submapper, battery, prg_size, mirror);
                    REQUIRE(Mapper429::validate_geometry(
                        make_header(submapper, battery, mirror), prg_size, 0U));
                }
            }
        }
    }

    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        auto mapper = make_board(kPrgSize, submapper, false, MirrorMode::Horizontal);
        CAPTURE(submapper);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(initial_bank(submapper), 0U));
        REQUIRE(mapper->mirror_mode() ==
                (submapper == 1U ? MirrorMode::SingleLower : MirrorMode::Horizontal));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(save(*mapper).size() == kStateSize);
        REQUIRE(save(*mapper).size() < kPrgSize);
    }
    auto persistent = make_board(kPrgSize, 0U, true);
    REQUIRE(persistent->battery_ram().size() == Mapper429::kChrRamSize);

    const auto reject = [](INesHeader header, std::size_t prg, std::size_t chr = 0U) {
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
        REQUIRE_FALSE(Mapper429::validate_geometry(header, prg, chr));
    };
    for (std::size_t prg :
         {0UZ, 1UZ, 0x8000UZ, kPrgSize - 1U, kPrgSize + 1U, 0x0C0000UZ, 2U * kDiyPrgSize}) {
        reject(make_header(), prg);
    }
    for (std::size_t chr : {1UZ, 0x2000UZ, Mapper429::kChrRamSize, kPrgSize}) {
        reject(make_header(), kPrgSize, chr);
    }
    for (Byte submapper = 3U; submapper < 16U; ++submapper) {
        reject(make_header(submapper), kPrgSize);
    }
    auto wrong_id = make_header();
    wrong_id.mapper_number = 428U;
    reject(wrong_id, kPrgSize);
    auto legacy = make_header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize);
    reject(make_header(0U, false, MirrorMode::FourScreen), kPrgSize);
    auto trainer = make_header();
    trainer.has_trainer = true;
    reject(trainer, kPrgSize);
    auto prg_ram = make_header();
    prg_ram.prg_ram_size = 0x2000U;
    reject(prg_ram, kPrgSize);
    auto prg_nvram = make_header();
    prg_nvram.prg_nvram_size = 0x2000U;
    reject(prg_nvram, kPrgSize);
    auto no_chr = make_header();
    no_chr.chr_ram_size = 0U;
    reject(no_chr, kPrgSize);
    auto wrong_chr = make_header();
    wrong_chr.chr_ram_size = 0x2000U;
    reject(wrong_chr, kPrgSize);
    auto wrong_nvram = make_header(0U, true);
    wrong_nvram.chr_nvram_size = 0x2000U;
    reject(wrong_nvram, kPrgSize);
    auto both_chr = make_header();
    both_chr.chr_nvram_size = Mapper429::kChrRamSize;
    reject(both_chr, kPrgSize);
    auto battery_volatile = make_header();
    battery_volatile.has_battery = true;
    reject(battery_volatile, kPrgSize);
    auto unflagged_nvram = make_header(0U, true);
    unflagged_nvram.has_battery = false;
    reject(unflagged_nvram, kPrgSize);
}

TEST_CASE("Mapper 429 decodes every CPU write and applies only submapper 2 conflicts",
          "[mapper429][decoding][banking][bus][ciram]") {
    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        auto mapper = make_board(kDiyPrgSize, submapper);
        for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
            mapper->reset();
            const auto addr = static_cast<Address>(raw);
            const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0x5BU);
            const auto rom = prg_cell(initial_bank(submapper), raw & 0x7FFFU);
            const auto effective = static_cast<Byte>(submapper == 2U ? value & rom : value);
            const auto bank_mask = submapper == 2U ? 0x03U : 0x1FU;
            const auto expected_bank =
                raw < 0x8000U ? initial_bank(submapper) : (effective >> 2U) & bank_mask;
            mapper->write_prg(addr, value);
            CAPTURE(submapper, raw, value, rom, effective, expected_bank);
            REQUIRE(mapper->read_prg(0x8123U) == prg_cell(expected_bank, 0x0123U));
            REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(expected_bank, 0x7FFFU));
            const auto expected_mirror =
                submapper == 1U && raw >= 0x8000U && (effective & 0x80U) != 0U
                    ? MirrorMode::SingleUpper
                : submapper == 1U ? MirrorMode::SingleLower
                                  : MirrorMode::Vertical;
            REQUIRE(mapper->mirror_mode() == expected_mirror);
        }
    }
}

TEST_CASE("Mapper 429 regular and AxROM variants select complete PRG and CHR windows",
          "[mapper429][banking][ram][ciram]") {
    for (Byte submapper = 0U; submapper < 2U; ++submapper) {
        auto mapper = make_board(kDiyPrgSize, submapper, false, MirrorMode::Horizontal);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(0xF000U, static_cast<Byte>(value));
            const unsigned bank = (value >> 2U) & 0x1FU;
            for (Address addr : {0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
                CAPTURE(submapper, value, bank, addr);
                REQUIRE(mapper->read_prg(addr) == prg_cell(bank, addr & 0x7FFFU));
            }
            REQUIRE(mapper->mirror_mode() ==
                    (submapper == 1U ? ((value & 0x80U) != 0U ? MirrorMode::SingleUpper
                                                              : MirrorMode::SingleLower)
                                     : MirrorMode::Horizontal));
        }

        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0x8000U, static_cast<Byte>(bank));
            for (Address offset : {0x0000U, 0x0123U, 0x1FFFU}) {
                mapper->write_chr(offset, chr_cell(bank, offset, 0x35U));
            }
        }
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0xFFFFU, static_cast<Byte>(value));
            const unsigned bank = value & 3U;
            for (Address offset : {0x0000U, 0x0123U, 0x1FFFU, 0x2123U, 0x3FFFU}) {
                CAPTURE(submapper, value, bank, offset);
                REQUIRE(mapper->read_chr(offset) == chr_cell(bank, offset & 0x1FFFU, 0x35U));
            }
        }
    }
}

TEST_CASE("Mapper 429 submapper 2 combines BA with CC and wraps fitted CHR RAM",
          "[mapper429][banking][bus][ram]") {
    auto mapper = make_board(kPrgSize, 2U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        const auto old_rom = prg_cell(0U, 0x7FFFU);
        const auto effective = static_cast<Byte>(value & old_rom);
        mapper->write_prg(0xFFFFU, static_cast<Byte>(value));
        const unsigned bank = (effective >> 2U) & 0x03U;
        CAPTURE(value, old_rom, effective, bank);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(bank, 0U));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(bank, 0x4000U));
    }

    auto chr = make_ff_board();
    for (unsigned value = 0U; value < 256U; ++value) {
        chr->write_prg(0xFFFFU, static_cast<Byte>(value));
        const unsigned bank = (value & 3U) | (((value & 0x0CU) == 0x0CU) ? 4U : 0U);
        chr->write_chr(0x0123U, static_cast<Byte>(0x80U + (bank & 3U)));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        chr->write_prg(0x8000U, static_cast<Byte>(value));
        const unsigned bank = (value & 3U) | (((value & 0x0CU) == 0x0CU) ? 4U : 0U);
        CAPTURE(value, bank);
        REQUIRE(chr->read_chr(0x0123U) == static_cast<Byte>(0x80U + (bank & 3U)));
        REQUIRE(chr->read_chr(0x2123U) == static_cast<Byte>(0x80U + (bank & 3U)));
        REQUIRE(chr->read_chr(0x3FFFU) == 0U);
    }
}

TEST_CASE("Mapper 429 CHR RAM persists across soft reset and NVRAM across cold power",
          "[mapper429][ram][reset]") {
    for (bool battery : {false, true}) {
        auto mapper = make_board(kPrgSize, 0U, battery);
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0x8000U, static_cast<Byte>(bank));
            for (Address offset = 0U; offset < kChrBankSize; ++offset) {
                mapper->write_chr(offset, chr_cell(bank, offset, 0x35U));
            }
        }
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0xFFFFU, static_cast<Byte>(bank));
            for (Address offset = 0U; offset < kChrBankSize; ++offset) {
                CAPTURE(battery, bank, offset);
                REQUIRE(mapper->read_chr(offset) == chr_cell(bank, offset, 0x35U));
                REQUIRE(mapper->read_chr(static_cast<Address>(offset + 0x2000U)) ==
                        chr_cell(bank, offset, 0x35U));
            }
        }

        mapper->write_prg(0x8000U, 0xFFU);
        mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(1U, 0U));
        REQUIRE(mapper->read_chr(0x0123U) == chr_cell(0U, 0x0123U, 0x35U));
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0x8000U, static_cast<Byte>(bank));
            REQUIRE(mapper->read_chr(0x0123U) == chr_cell(bank, 0x0123U, 0x35U));
        }

        mapper->power_on();
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0x8000U, static_cast<Byte>(bank));
            CAPTURE(battery, bank);
            REQUIRE(mapper->read_chr(0x0123U) == (battery ? chr_cell(bank, 0x0123U, 0x35U) : 0U));
        }
    }

    auto imported = make_board(kPrgSize, 0U, true);
    std::array<Byte, Mapper429::kChrRamSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = static_cast<Byte>(index * 17U + (index >> 8U) + 3U);
    }
    imported->set_battery_ram(bytes);
    REQUIRE(std::ranges::equal(imported->battery_ram(), bytes));
    const auto before = save(*imported);
    imported->set_battery_ram(std::span<const Byte>(bytes).first(bytes.size() - 1U));
    REQUIRE(save(*imported) == before);
    std::vector<Byte> oversized(bytes.begin(), bytes.end());
    oversized.push_back(0U);
    imported->set_battery_ram(oversized);
    REQUIRE(save(*imported) == before);
    REQUIRE(imported->read_chr(0x0123U) == bytes[0x0123U]);
}

TEST_CASE("Mapper 429 exposes no CPU RAM IRQ audio or special nametable aperture",
          "[mapper429][bus][ciram][ram][irq][audio]") {
    for (Byte submapper = 0U; submapper < 3U; ++submapper) {
        auto mapper = make_board(kPrgSize, submapper, false, MirrorMode::SingleUpper);
        for (Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            CAPTURE(submapper, addr);
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0U);
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        for (Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE(mapper->mirror_mode() ==
                (submapper == 1U ? MirrorMode::SingleLower : MirrorMode::SingleUpper));

        const auto before = save(*mapper);
        for (Address addr = 0x4020U; addr < 0x6000U; ++addr) {
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            mapper->write_expansion(addr, static_cast<Byte>(addr));
            REQUIRE(mapper->read_expansion(addr) == 0U);
        }
        mapper->observe_cpu_read(0x8000U, 0xA5U);
        mapper->observe_cpu_write(0x5000U, 0xA5U);
        mapper->observe_ppu_a12(true);
        mapper->observe_ppu_address(0x1FFFU);
        mapper->observe_ppu_read(0x1FFFU);
        mapper->clock_irq_counter();
        mapper->acknowledge_irq();
        mapper->clock_cpu();
        mapper->clock_cpu_falling_edge();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
        mapper->clock_audio();
        mapper->set_region(Region::PAL);
        REQUIRE(save(*mapper) == before);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        REQUIRE(mapper->expansion_read_mask(0x5000U) == 0xFFU);
    }
}

TEST_CASE("Mapper 429 snapshots replay latch and RAM without cloning ROM",
          "[mapper429][state][ram][reset]") {
    auto mapper = make_board(kDiyPrgSize, 1U, true, MirrorMode::Horizontal);
    mapper->write_prg(0x8000U, 0xD6U);
    for (Address offset = 0U; offset < kChrBankSize; offset += 0x101U) {
        mapper->write_chr(offset, chr_cell(2U, offset, 0x35U));
    }
    const auto saved = save(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kLatchOffset] == 0xD6U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);

    auto copy = make_board(kDiyPrgSize, 1U, true, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(save(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleUpper);
    for (Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        CAPTURE(addr);
        REQUIRE(copy->read_prg(addr) == prg_cell(21U, addr & 0x7FFFU, 0x80U));
    }
    for (Address offset = 0U; offset < kChrBankSize; offset += 0x101U) {
        REQUIRE(copy->read_chr(offset) == chr_cell(2U, offset, 0x35U));
    }
    mapper->write_prg(0xFFFFU, 0x27U);
    copy->write_prg(0xFFFFU, 0x27U);
    mapper->write_chr(0x0100U, 0xA5U);
    copy->write_chr(0x0100U, 0xA5U);
    REQUIRE(save(*copy) == save(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(save(*copy) == saved);
}

TEST_CASE("Mapper 429 rejects truncated malformed and incompatible states atomically",
          "[mapper429][state][ram]") {
    auto mapper = make_board(kPrgSize, 2U, true, MirrorMode::SingleUpper);
    mapper->write_prg(0x8000U, 0x3FU);
    for (Address offset = 0U; offset < kChrBankSize; offset += 0x101U) {
        mapper->write_chr(offset, chr_cell(3U, offset, 0x35U));
    }
    const auto saved = save(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kLatchOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (Byte reserved : {0x10U, 0x20U, 0x40U, 0x80U, 0xF0U}) {
        auto invalid = saved;
        invalid[kLatchOffset] |= reserved;
        CAPTURE(reserved);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper429>& other) {
        const auto before = save(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*other) == before);
    };
    auto submapper = make_board(kPrgSize, 1U, true, MirrorMode::SingleUpper);
    mismatch(submapper);
    auto prg = make_board(kDiyPrgSize, 2U, true, MirrorMode::SingleUpper);
    mismatch(prg);
    auto battery = make_board(kPrgSize, 2U, false, MirrorMode::SingleUpper);
    mismatch(battery);
    auto mirror = make_board(kPrgSize, 2U, true, MirrorMode::Vertical);
    mismatch(mirror);

    auto changed = saved;
    changed[kLatchOffset] ^= 1U;
    changed[kChrRamOffset + 0x0123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(save(*mapper) == changed);
}

} // namespace
