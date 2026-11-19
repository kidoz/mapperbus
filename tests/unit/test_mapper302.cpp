#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper302_ks7057.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 128U * 1024U;
constexpr std::size_t kBankSize = 2U * 1024U;
constexpr std::size_t kStateSize = 8207U;
constexpr std::array<Address, 8> kWindowBases = {
    0x8000U,
    0x8800U,
    0x9000U,
    0x9800U,
    0x6000U,
    0x6800U,
    0x7000U,
    0x7800U,
};
constexpr std::array<Address, 8> kLowRegisters = {
    0xB000U,
    0xB002U,
    0xC000U,
    0xC002U,
    0xD000U,
    0xD002U,
    0xE000U,
    0xE002U,
};

INesHeader header(bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 302U;
    result.is_nes2 = true;
    result.chr_ram_size = 8192U;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + offset / 251U * 17U + offset / 2048U * 7U);
}

std::vector<Byte> rom() {
    std::vector<Byte> bytes(kPrgSize);
    for (std::size_t offset = 0U; offset < bytes.size(); ++offset) {
        bytes[offset] = pattern(offset);
    }
    return bytes;
}

std::unique_ptr<KaiserKs7057> board(bool vertical = true) {
    REQUIRE(KaiserKs7057::validate_geometry(header(vertical), kPrgSize, 0U));
    return std::make_unique<KaiserKs7057>(header(vertical), rom(), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_bank(KaiserKs7057& mapper, unsigned slot, Byte bank) {
    mapper.write_prg(kLowRegisters[slot], static_cast<Byte>(0xC0U | (bank & 0x0FU)));
    mapper.write_prg(static_cast<Address>(kLowRegisters[slot] + 1U),
                     static_cast<Byte>(0xFCU | (bank >> 4U)));
}

int decoded_slot(Address addr) {
    switch (addr & 0xF002U) {
    case 0xB000U:
        return 0;
    case 0xB002U:
        return 1;
    case 0xC000U:
        return 2;
    case 0xC002U:
        return 3;
    case 0xD000U:
        return 4;
    case 0xD002U:
        return 5;
    case 0xE000U:
        return 6;
    case 0xE002U:
        return 7;
    default:
        return -1;
    }
}

TEST_CASE("Mapper 302 cold state has documented fixed windows and no unrelated hardware",
          "[mapper302]") {
    for (bool vertical : {false, true}) {
        auto mapper = board(vertical);
        for (Address base : kWindowBases) {
            REQUIRE(mapper->read_prg(base) == pattern(0U));
            REQUIRE(mapper->read_prg(static_cast<Address>(base + 0x7FFU)) == pattern(0x7FFU));
        }
        REQUIRE(mapper->read_prg(0xA000U) == pattern(13U * 8192U));
        REQUIRE(mapper->read_prg(0xBFFFU) == pattern(14U * 8192U - 1U));
        REQUIRE(mapper->read_prg(0xC000U) == pattern(7U * 16384U));
        REQUIRE(mapper->read_prg(0xFFFFU) == pattern(8U * 16384U - 1U));
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        for (Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU, 0x8000U, 0xFFFFU}) {
            CAPTURE(addr);
            REQUIRE(mapper->maps_prg(addr) == (addr >= 0x6000U));
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            if (addr >= 0x6000U) {
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            }
        }
    }
}

TEST_CASE("Mapper 302 maps every six-bit bank through every 2 KiB window", "[mapper302]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned bank = 0U; bank < 64U; ++bank) {
            CAPTURE(slot, bank);
            set_bank(*mapper, slot, static_cast<Byte>(bank));
            for (unsigned offset = 0U; offset < kBankSize; ++offset) {
                REQUIRE(mapper->read_prg(static_cast<Address>(kWindowBases[slot] + offset)) ==
                        pattern(bank * kBankSize + offset));
            }
            REQUIRE(mapper->read_prg(0xA123U) == pattern(13U * 8192U + 0x123U));
            REQUIRE(mapper->read_prg(0xF123U) == pattern(7U * 16384U + 0x3123U));
        }
    }
}

TEST_CASE("Mapper 302 register aliases and data masks match the KS204 wiring", "[mapper302]") {
    auto mapper = board(false);
    std::array<Byte, 8> banks{};
    auto mirror = MirrorMode::Horizontal;
    for (unsigned raw_addr = 0U; raw_addr <= 0xFFFFU; ++raw_addr) {
        const auto addr = static_cast<Address>(raw_addr);
        constexpr Byte value = 0xA5U;
        mapper->write_prg(addr, value);
        if ((addr & 0xE000U) == 0x8000U) {
            mirror = MirrorMode::Vertical;
        } else if (addr >= 0xB000U && addr <= 0xEFFFU) {
            const int slot = decoded_slot(addr);
            if (slot >= 0) {
                if ((addr & 1U) == 0U) {
                    banks[static_cast<unsigned>(slot)] =
                        static_cast<Byte>((banks[static_cast<unsigned>(slot)] & 0x30U) | 5U);
                } else {
                    banks[static_cast<unsigned>(slot)] =
                        static_cast<Byte>((banks[static_cast<unsigned>(slot)] & 0x0FU) | 0x10U);
                }
            }
        }
        CAPTURE(addr);
        REQUIRE(mapper->mirror_mode() == mirror);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            REQUIRE(mapper->read_prg(kWindowBases[slot]) == pattern(banks[slot] * kBankSize));
        }
    }

    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_bank(*mapper, slot, 0x35U);
        unsigned selected = 0x35U;
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(kLowRegisters[slot], static_cast<Byte>(value));
            selected = (selected & 0x30U) | (value & 0x0FU);
            REQUIRE(mapper->read_prg(kWindowBases[slot]) == pattern(selected * kBankSize));
            mapper->write_prg(static_cast<Address>(kLowRegisters[slot] + 1U),
                              static_cast<Byte>(value));
            selected = (selected & 0x0FU) | ((value & 3U) << 4U);
            REQUIRE(mapper->read_prg(kWindowBases[slot]) == pattern(selected * kBankSize));
        }
    }
}

TEST_CASE("Mapper 302 mirroring uses D0 throughout $8000-$9FFF only", "[mapper302]") {
    auto mapper = board(false);
    for (unsigned addr = 0x8000U; addr <= 0x9FFFU; ++addr) {
        const auto value = static_cast<Byte>(addr * 37U);
        mapper->write_prg(static_cast<Address>(addr), value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }
    for (Address addr : {0x8000U, 0x9FFFU}) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(addr, static_cast<Byte>(value));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        }
    }
}

TEST_CASE("Mapper 302 CHR RAM is volatile and soft reset leaves all board state intact",
          "[mapper302]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 8192U)) ==
                static_cast<Byte>(addr ^ (addr >> 8U)));
        mapper->write_chr(static_cast<Address>(addr + 16384U), static_cast<Byte>(addr + 91U));
    }
    set_bank(*mapper, 3U, 0x2BU);
    mapper->write_prg(0x8000U, 0U);
    const auto before_reset = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) == before_reset);
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(state(*mapper) == before_reset);
    mapper->power_on();
    REQUIRE(state(*mapper) == state(*board()));
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
    }
}

TEST_CASE("Mapper 302 accepts only the documented NES 2.0 geometry", "[mapper302]") {
    for (bool vertical : {false, true}) {
        REQUIRE(KaiserKs7057::validate_geometry(header(vertical), kPrgSize, 0U));
    }
    for (std::size_t size :
         {0UZ, 1UZ, 16384UZ, 32768UZ, 65536UZ, 131071UZ, 131073UZ, 262144UZ, 524288UZ}) {
        REQUIRE_FALSE(KaiserKs7057::validate_geometry(header(), size, 0U));
    }
    for (std::size_t size : {1UZ, 4096UZ, 8192UZ, 16384UZ}) {
        REQUIRE_FALSE(KaiserKs7057::validate_geometry(header(), kPrgSize, size));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    legacy.chr_ram_size = 0U;
    REQUIRE_FALSE(KaiserKs7057::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(KaiserKs7057::validate_geometry(invalid, kPrgSize, 0U));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto invalid = header();
        invalid.mirror_mode = mode;
        REQUIRE_FALSE(KaiserKs7057::validate_geometry(invalid, kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto invalid = header();
        if (field == 0U) {
            invalid.has_battery = true;
        } else if (field == 1U) {
            invalid.prg_ram_size = 8192U;
        } else if (field == 2U) {
            invalid.prg_nvram_size = 8192U;
        } else if (field == 3U) {
            invalid.chr_ram_size = 0U;
        } else {
            invalid.chr_nvram_size = 8192U;
        }
        REQUIRE_FALSE(KaiserKs7057::validate_geometry(invalid, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 302 snapshots are bounded, replayable, and atomic on rejection",
          "[mapper302][state]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        set_bank(*mapper, slot, static_cast<Byte>(slot * 9U + 7U));
    }
    mapper->write_prg(0x9FFFU, 0U);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U + addr / 64U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board();
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        REQUIRE(copy->read_prg(kWindowBases[slot]) == mapper->read_prg(kWindowBases[slot]));
    }
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader truncated(std::span(saved).first(length));
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (unsigned offset = 0U; offset < 6U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (unsigned offset = 6U; offset < 14U; ++offset) {
        auto invalid = saved;
        invalid[offset] = 0x40U;
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (Byte invalid_mirror : {2U, 3U, 4U, 0xFFU}) {
        auto invalid = saved;
        invalid[14] = invalid_mirror;
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    auto other = board(false);
    const auto before = state(*other);
    StateReader mismatched(saved);
    other->load_state(mismatched);
    REQUIRE_FALSE(mismatched.ok());
    REQUIRE(state(*other) == before);

    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*mapper) == saved);
}

} // namespace
