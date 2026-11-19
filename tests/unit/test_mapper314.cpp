#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper314.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgBankSize = 16384U;
constexpr std::size_t kChrBankSize = 8192U;
constexpr std::size_t kMaxPrgSize = 4U * 1024U * 1024U;
constexpr std::size_t kMaxChrRomSize = 64U * kChrBankSize;
constexpr std::size_t kRomStateSize = 14U;

INesHeader header(std::size_t chr_rom_size = kChrBankSize,
                  std::size_t chr_ram_size = 0U,
                  MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 314U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = chr_ram_size;
    if (chr_rom_size == 0U && chr_ram_size == 0U) {
        result.chr_ram_size = kChrBankSize;
    }
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 13U + (offset >> 8U) * 29U + (offset >> 14U) * 47U +
                             (offset >> 19U) * 61U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = pattern(offset);
    }
    return data;
}

std::unique_ptr<Mapper314> board(std::size_t prg_size = 1024U * 1024U,
                                 std::size_t chr_rom_size = kMaxChrRomSize,
                                 std::size_t chr_ram_size = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(chr_rom_size, chr_ram_size, mirror);
    REQUIRE(Mapper314::validate_geometry(h, prg_size, chr_rom_size));
    return std::make_unique<Mapper314>(h, rom(prg_size), rom(chr_rom_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(
    std::size_t prg_size, Byte miscellaneous, Byte prg_bank, Byte unrom_latch, Address addr) {
    const std::size_t base = (static_cast<std::size_t>(miscellaneous & 1U) << 7U) |
                             (static_cast<std::size_t>(prg_bank & 0x3FU) << 1U);
    std::size_t bank = base;
    if ((miscellaneous & 0x20U) == 0U) {
        const std::size_t inner = addr >= 0xC000U ? 7UZ : unrom_latch & 7U;
        bank = (base & 0xF8U) | inner;
    } else if ((prg_bank & 0x80U) != 0U) {
        bank = (base & 0xFEU) | ((addr >> 14U) & 1U);
    } else {
        bank |= (prg_bank >> 6U) & 1U;
    }
    return pattern((bank * kPrgBankSize + (addr & 0x3FFFU)) % prg_size);
}

TEST_CASE("Mapper 314 validates the complete documented memory envelope", "[mapper314]") {
    for (std::size_t prg_size = 32768U; prg_size <= kMaxPrgSize; prg_size *= 2U) {
        for (std::size_t chr_size = kChrBankSize; chr_size <= kMaxChrRomSize; chr_size *= 2U) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                REQUIRE(
                    Mapper314::validate_geometry(header(chr_size, 0U, mirror), prg_size, chr_size));
            }
        }
        for (const std::size_t ram_size : {kChrBankSize, 4U * kChrBankSize}) {
            REQUIRE(Mapper314::validate_geometry(header(0U, ram_size), prg_size, 0U));
        }
    }

    auto h = header();
    h.mapper_number = 313U;
    REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, kChrBankSize));
    h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, kChrBankSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, kChrBankSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 8192UZ, 16384UZ, 32767UZ, 32769UZ, 49152UZ, 3145728UZ, 4194305UZ, 8388608UZ}) {
        REQUIRE_FALSE(Mapper314::validate_geometry(header(), size, kChrBankSize));
    }
    for (const std::size_t size : {1UZ, 4096UZ, 8191UZ, 8193UZ, 12288UZ, 524289UZ, 1048576UZ}) {
        REQUIRE_FALSE(Mapper314::validate_geometry(header(), kMaxPrgSize, size));
    }
    for (const std::size_t ram_size : {1UZ, 4096UZ, 16384UZ, 65536UZ}) {
        REQUIRE_FALSE(Mapper314::validate_geometry(header(0U, ram_size), kMaxPrgSize, 0U));
    }
    h = header(0U);
    h.chr_ram_size = 0U;
    REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, 0U));

    h = header();
    h.has_battery = true;
    REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, kChrBankSize));
    h = header();
    h.prg_ram_size = 8192U;
    REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, kChrBankSize));
    h = header();
    h.prg_nvram_size = 8192U;
    REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, kChrBankSize));
    h = header();
    h.chr_ram_size = 8192U;
    REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, kChrBankSize));
    h = header(0U);
    h.chr_nvram_size = 8192U;
    REQUIRE_FALSE(Mapper314::validate_geometry(h, kMaxPrgSize, 0U));
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper314::validate_geometry(
            header(kChrBankSize, 0U, mirror), kMaxPrgSize, kChrBankSize));
    }
}

TEST_CASE("Mapper 314 decodes every PRG pin combination before physical wrapping",
          "[mapper314][prg]") {
    auto mapper = board(kMaxPrgSize);
    for (unsigned miscellaneous = 0U; miscellaneous < 256U; ++miscellaneous) {
        mapper->write_expansion(0x5000U, static_cast<Byte>(miscellaneous));
        for (unsigned prg_bank = 0U; prg_bank < 256U; ++prg_bank) {
            mapper->write_expansion(0x5001U, static_cast<Byte>(prg_bank));
            for (const Byte latch : {0U, 3U, 7U}) {
                mapper->write_prg(0x8000U, latch);
                for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
                    CAPTURE(miscellaneous, prg_bank, latch, addr);
                    REQUIRE(mapper->read_prg(addr) == expected_prg(kMaxPrgSize,
                                                                   static_cast<Byte>(miscellaneous),
                                                                   static_cast<Byte>(prg_bank),
                                                                   latch,
                                                                   addr));
                }
            }
        }
    }

    auto small = board(32768U);
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        small->write_expansion(0x5000U, 0x21U);
        small->write_expansion(0x5001U, static_cast<Byte>(bank));
        REQUIRE(small->read_prg(0x8000U) ==
                expected_prg(32768U, 0x21U, static_cast<Byte>(bank), 0U, 0x8000U));
        REQUIRE(small->read_prg(0xC000U) ==
                expected_prg(32768U, 0x21U, static_cast<Byte>(bank), 0U, 0xC000U));
    }
}

TEST_CASE("Mapper 314 UNROM latch covers every upper CPU alias and ignores high data bits",
          "[mapper314][prg]") {
    auto mapper = board(kMaxPrgSize);
    mapper->write_expansion(0x5000U, 0U);
    mapper->write_expansion(0x5001U, 0U);
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        const auto addr = static_cast<Address>(raw_addr);
        for (const Byte latch : {0U, 1U, 7U, 0xF8U, 0xFFU}) {
            mapper->write_prg(addr, latch);
            CAPTURE(raw_addr, latch);
            REQUIRE(mapper->read_prg(0x8000U) == expected_prg(kMaxPrgSize, 0U, 0U, latch, 0x8000U));
            REQUIRE(mapper->read_prg(0xC000U) == pattern(7U * kPrgBankSize));
        }
    }

    mapper->write_expansion(0x5000U, 0xFFU);
    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr < 0x8000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(addr)));
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == 0U);
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
}

TEST_CASE("Mapper 314 low ports decode only their three literal addresses and reads float",
          "[mapper314][registers][bus]") {
    auto mapper = board(32768U, kChrBankSize);
    REQUIRE_FALSE(mapper->maps_expansion(0x5000U));
    REQUIRE_FALSE(mapper->maps_expansion(0x5001U));
    REQUIRE_FALSE(mapper->maps_expansion(0x5002U));

    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        mapper->power_on();
        const auto initial = state(*mapper);
        mapper->write_expansion(static_cast<Address>(addr), 0xA5U);
        auto expected = initial;
        if (addr == 0x5000U) {
            expected[10] = 0xA5U;
        } else if (addr == 0x5001U) {
            expected[11] = 0xA5U;
        } else if (addr == 0x5002U) {
            expected[12] = 5U;
        }
        CAPTURE(addr);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 314 selects all CHR banks and provides bounded banked CHR RAM",
          "[mapper314][chr]") {
    auto rom_board = board(kMaxPrgSize, kMaxChrRomSize);
    for (unsigned miscellaneous = 0U; miscellaneous < 256U; ++miscellaneous) {
        rom_board->write_expansion(0x5000U, static_cast<Byte>(miscellaneous));
        for (unsigned chr_bank = 0U; chr_bank < 256U; ++chr_bank) {
            rom_board->write_expansion(0x5002U, static_cast<Byte>(chr_bank));
            const std::size_t bank = ((chr_bank & 0x0FU) << 2U) | ((miscellaneous >> 1U) & 3U);
            for (const Address addr : {0U, 0x0123U, 0x1FFFU, 0x2123U, 0x3FFFU}) {
                const auto offset = (bank * kChrBankSize + (addr & 0x1FFFU)) % kMaxChrRomSize;
                rom_board->write_chr(addr, static_cast<Byte>(offset ^ 0xA5U));
                CAPTURE(miscellaneous, chr_bank, addr);
                REQUIRE(rom_board->read_chr(addr) == pattern(offset));
            }
        }
    }

    for (const std::size_t ram_size : {kChrBankSize, 4U * kChrBankSize}) {
        auto ram_board = board(32768U, 0U, ram_size);
        for (unsigned bank = 0U; bank < 64U; ++bank) {
            ram_board->write_expansion(0x5000U, static_cast<Byte>(bank & 3U) << 1U);
            ram_board->write_expansion(0x5002U, static_cast<Byte>(bank >> 2U));
            for (std::size_t addr = 0U; addr < kChrBankSize; ++addr) {
                ram_board->write_chr(static_cast<Address>(addr),
                                     static_cast<Byte>(bank * 17U + addr * 3U + (addr >> 8U)));
            }
        }
        for (unsigned bank = 0U; bank < 64U; ++bank) {
            ram_board->write_expansion(0x5000U, static_cast<Byte>(bank & 3U) << 1U);
            ram_board->write_expansion(0x5002U, static_cast<Byte>(bank >> 2U));
            const unsigned page_count = ram_size / kChrBankSize;
            const unsigned last_writer = 64U - page_count + bank % page_count;
            for (std::size_t addr = 0U; addr < kChrBankSize; ++addr) {
                CAPTURE(ram_size, bank, addr);
                REQUIRE(ram_board->read_chr(static_cast<Address>(addr)) ==
                        static_cast<Byte>(last_writer * 17U + addr * 3U + (addr >> 8U)));
            }
        }
        REQUIRE(ram_board->battery_ram().empty());
        ram_board->set_battery_ram(std::array<Byte, 2>{0xA5U, 0x5AU});
        REQUIRE(ram_board->battery_ram().empty());
    }
}

TEST_CASE("Mapper 314 mirroring uses only miscellaneous D5", "[mapper314][ciram]") {
    auto mapper = board(32768U, kChrBankSize);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x5000U, static_cast<Byte>(value));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE_FALSE(mapper->ciram_address(0x2000U).has_value());
        REQUIRE_FALSE(mapper->nametable_address(0x2000U).has_value());
    }
}

TEST_CASE("Mapper 314 power-on values are exact and soft reset is non-invented",
          "[mapper314][reset]") {
    auto mapper = board(32768U, 0U, 4U * kChrBankSize, MirrorMode::Horizontal);
    auto initial = state(*mapper);
    REQUIRE(initial.size() == 32790U);
    REQUIRE(initial[10] == 0x80U);
    REQUIRE(initial[11] == 0x43U);
    REQUIRE(initial[12] == 0U);
    REQUIRE(initial[13] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(32768U, 0x80U, 0x43U, 0U, 0x8000U));
    REQUIRE(mapper->read_prg(0xC000U) == expected_prg(32768U, 0x80U, 0x43U, 0U, 0xC000U));

    mapper->write_expansion(0x5000U, 0xE7U);
    mapper->write_expansion(0x5001U, 0xBDU);
    mapper->write_expansion(0x5002U, 0xFEU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    mapper->write_chr(0x1234U, 0xA7U);
    const auto programmed = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == programmed);
    REQUIRE(mapper->read_chr(0x1234U) == 0xA7U);

    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x1234U) == 0U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 314 snapshots contain only geometry latches and actual CHR RAM",
          "[mapper314][state]") {
    for (const std::size_t chr_ram_size : {0UZ, kChrBankSize, 4U * kChrBankSize}) {
        const std::size_t chr_rom_size = chr_ram_size == 0U ? kChrBankSize : 0U;
        auto source = board(32768U, chr_rom_size, chr_ram_size);
        source->write_expansion(0x5000U, 0xD7U);
        source->write_expansion(0x5001U, 0xB9U);
        source->write_expansion(0x5002U, 0xAEU);
        source->write_prg(0xFFFFU, 0xF6U);
        if (chr_ram_size != 0U) {
            for (unsigned bank = 0U; bank < 64U; ++bank) {
                source->write_expansion(0x5000U, static_cast<Byte>(bank & 3U) << 1U);
                source->write_expansion(0x5002U, static_cast<Byte>(bank >> 2U));
                for (std::size_t addr = 0U; addr < kChrBankSize; ++addr) {
                    source->write_chr(static_cast<Address>(addr),
                                      static_cast<Byte>(bank * 11U + addr * 7U));
                }
            }
            source->write_expansion(0x5000U, 0xD7U);
            source->write_expansion(0x5002U, 0xAEU);
        }
        const auto saved = state(*source);
        REQUIRE(saved.size() ==
                kRomStateSize + (chr_ram_size == 0U ? 0UZ : sizeof(std::uint64_t) + chr_ram_size));

        auto target = board(32768U, chr_rom_size, chr_ram_size);
        StateReader reader(saved);
        target->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*target) == saved);
        for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
            REQUIRE(target->read_prg(addr) == source->read_prg(addr));
        }
        for (const Address addr : {0U, 0x123U, 0x1FFFU}) {
            REQUIRE(target->read_chr(addr) == source->read_chr(addr));
        }

        auto followed = saved;
        followed.push_back(0xA7U);
        StateReader trailing(followed);
        target->load_state(trailing);
        REQUIRE(trailing.ok());
        REQUIRE(trailing.remaining() == 1U);
        REQUIRE(state(*target) == saved);
    }
}

TEST_CASE("Mapper 314 malformed states reject atomically with bounded RAM reads",
          "[mapper314][state][atomic]") {
    auto mapper = board(32768U, 0U, kChrBankSize);
    mapper->write_expansion(0x5000U, 0xD7U);
    mapper->write_expansion(0x5001U, 0xB9U);
    mapper->write_expansion(0x5002U, 0xAEU);
    mapper->write_prg(0xFFFFU, 0xF6U);
    for (std::size_t addr = 0U; addr < kChrBankSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 19U + 7U));
    }
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 80U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        reject(bad);
    }
    for (const Byte value : {0x10U, 0x80U, 0xF0U, 0xFFU}) {
        auto bad = saved;
        bad[12] = value;
        reject(bad);
    }
    for (const Byte value : {8U, 0x80U, 0xF8U, 0xFFU}) {
        auto bad = saved;
        bad[13] = value;
        reject(bad);
    }
    for (unsigned bit = 0U; bit < 64U; ++bit) {
        auto bad = saved;
        bad[14U + bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        reject(bad);
    }

    const auto cross_geometry = [&](Mapper314& target) {
        const auto before = state(target);
        StateReader reader(saved);
        target.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(target) == before);
    };
    auto other_prg = board(65536U, 0U, kChrBankSize);
    cross_geometry(*other_prg);
    auto other_ram = board(32768U, 0U, 4U * kChrBankSize);
    cross_geometry(*other_ram);
    auto rom_board = board(32768U, kChrBankSize);
    cross_geometry(*rom_board);
    auto other_chr_rom = board(32768U, 2U * kChrBankSize);
    const auto rom_saved = state(*rom_board);
    const auto before = state(*other_chr_rom);
    StateReader wrong_rom_size(rom_saved);
    other_chr_rom->load_state(wrong_rom_size);
    REQUIRE_FALSE(wrong_rom_size.ok());
    REQUIRE(state(*other_chr_rom) == before);
}

} // namespace
