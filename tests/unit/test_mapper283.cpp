#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper283.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 283U;
    result.is_nes2 = !legacy;
    result.chr_ram_size = legacy ? 0U : 8192U;
    result.prg_ram_size = legacy ? 8192U : 0U;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + offset / 251U * 17U + offset / 8192U * 7U);
}

std::vector<Byte> rom(std::size_t size, bool repeated_auxiliary = true) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0; offset < size; ++offset) {
        bytes[offset] = pattern(offset);
    }
    if (repeated_auxiliary && size >= 49152U && std::has_single_bit(size - 16384U)) {
        for (std::size_t offset = 0; offset < 8192U; ++offset) {
            bytes[size - 16384U + offset] = bytes[size - 8192U + offset];
        }
    }
    return bytes;
}

std::unique_ptr<BlockFamily283> board(std::size_t prg_size = 262144U,
                                      bool legacy = false,
                                      bool vertical = true) {
    REQUIRE(BlockFamily283::validate_geometry(header(legacy, vertical), prg_size, 0U));
    return std::make_unique<BlockFamily283>(
        header(legacy, vertical), rom(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 283 powers up with bank zero fixed metadata and no unrelated hardware",
          "[mapper283]") {
    for (bool legacy : {false, true}) {
        for (bool vertical : {false, true}) {
            auto mapper = board(262144U, legacy, vertical);
            REQUIRE(mapper->read_prg(0x8000U) == pattern(0U));
            REQUIRE(mapper->read_prg(0xFFFFU) == pattern(32767U));
            REQUIRE(mapper->read_prg(0x6000U) == pattern(253952U));
            REQUIRE(mapper->read_prg(0x7FFFU) == pattern(262143U));
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(state(*mapper).size() == 8199U);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
        }
    }
}

TEST_CASE("Mapper 283 latches every data byte at every upper CPU address without conflicts",
          "[mapper283]") {
    auto mapper = board(131072U);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->power_on();
        const auto value = static_cast<Byte>(addr * 37U + addr / 256U);
        mapper->write_prg(static_cast<Address>(addr), value);
        const auto expected_bank = addr >= 0x8000U ? value : 0U;
        REQUIRE(state(*mapper)[6U] == expected_bank);
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
    }
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        mapper->write_prg(0xFFFFU, static_cast<Byte>(bank));
        const std::size_t selected = bank * 32768U % 131072U;
        for (Address addr : {0x8000U, 0x8001U, 0x9ABCu, 0xBFFFU, 0xC000U, 0xFFFEU, 0xFFFFU}) {
            REQUIRE(mapper->read_prg(addr) == pattern(selected + (addr & 0x7FFFU)));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
    }
}

TEST_CASE("Mapper 283 maps the fixed low ROM bank and optional auxiliary chip", "[mapper283]") {
    for (std::size_t main_size : {32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ, 8388608UZ}) {
        CAPTURE(main_size);
        auto mapper = board(main_size);
        const std::size_t fixed = std::min(main_size, 262144UZ) - 8192U;
        for (Address addr : {0x6000U, 0x6001U, 0x6ABCu, 0x7FFFU}) {
            REQUIRE(mapper->read_prg(addr) == pattern(fixed + (addr & 0x1FFFU)));
        }
        mapper->write_prg(0x8000U, 0xFFU);
        REQUIRE(mapper->read_prg(0x6000U) == pattern(fixed));

        auto auxiliary = board(main_size + 16384U);
        for (Address addr : {0x6000U, 0x6001U, 0x6ABCu, 0x7FFFU}) {
            REQUIRE(auxiliary->read_prg(addr) == pattern(main_size + 8192U + (addr & 0x1FFFU)));
        }
        for (unsigned bank = 0U; bank < 256U; ++bank) {
            auxiliary->write_prg(0xFFFFU, static_cast<Byte>(bank));
            REQUIRE(auxiliary->read_prg(0x8000U) == pattern(bank * 32768U % main_size));
            REQUIRE(auxiliary->read_prg(0x6000U) == pattern(main_size + 8192U));
        }
    }
}

TEST_CASE("Mapper 283 owns writable volatile CHR RAM across all address aliases and resets",
          "[mapper283]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ addr / 256U));
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 8192U)) ==
                static_cast<Byte>(addr ^ addr / 256U));
        mapper->write_chr(static_cast<Address>(addr + 16384U), static_cast<Byte>(addr + 91U));
    }
    mapper->write_prg(0xFFFFU, 0xA7U);
    mapper->reset();
    REQUIRE(state(*mapper)[6U] == 0U);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr + 91U));
    }
    mapper->power_on();
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
    }
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 283 validates both documented ROM shapes and board metadata", "[mapper283]") {
    for (bool legacy : {false, true}) {
        for (std::size_t main_size : {32768UZ, 65536UZ, 131072UZ, 262144UZ, 1048576UZ, 8388608UZ}) {
            REQUIRE(BlockFamily283::validate_geometry(header(legacy), main_size, 0U));
            REQUIRE(BlockFamily283::validate_geometry(header(legacy), main_size + 16384U, 0U));
        }
        for (std::size_t prg_size : {0UZ,
                                     1UZ,
                                     16384UZ,
                                     32767UZ,
                                     32769UZ,
                                     49151UZ,
                                     98304UZ,
                                     114688UZ,
                                     163840UZ,
                                     16777216UZ}) {
            CAPTURE(legacy, prg_size);
            REQUIRE_FALSE(BlockFamily283::validate_geometry(header(legacy), prg_size, 0U));
        }
        for (std::size_t chr_size : {1UZ, 8192UZ, 16384UZ}) {
            REQUIRE_FALSE(BlockFamily283::validate_geometry(header(legacy), 262144U, chr_size));
        }
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            auto invalid = header(legacy);
            invalid.mirror_mode = mode;
            REQUIRE_FALSE(BlockFamily283::validate_geometry(invalid, 262144U, 0U));
        }
        auto invalid = header(legacy);
        invalid.has_battery = true;
        REQUIRE_FALSE(BlockFamily283::validate_geometry(invalid, 262144U, 0U));
        invalid = header(legacy);
        invalid.prg_nvram_size = 8192U;
        REQUIRE_FALSE(BlockFamily283::validate_geometry(invalid, 262144U, 0U));
        invalid = header(legacy);
        invalid.chr_nvram_size = 8192U;
        REQUIRE_FALSE(BlockFamily283::validate_geometry(invalid, 262144U, 0U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(BlockFamily283::validate_geometry(invalid, 262144U, 0U));
    }
    for (unsigned field = 0U; field < 2U; ++field) {
        auto invalid = header();
        if (field == 0U) {
            invalid.prg_ram_size = 8192U;
        } else {
            invalid.chr_ram_size = 0U;
        }
        REQUIRE_FALSE(BlockFamily283::validate_geometry(invalid, 262144U, 0U));
    }
}

TEST_CASE("Mapper 283 snapshots replay latch and CHR state and reject atomically",
          "[mapper283][state]") {
    auto mapper = board(278528U);
    auto copy = board(278528U);
    mapper->write_prg(0xF123U, 0xDBU);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 13U + addr / 64U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved[6U] == 0xDBU);
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated(std::span(saved).first(size));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset = 0U; offset < 6U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        invalid[100U] ^= 0xFFU;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t size : {32768UZ, 262144UZ, 278528UZ, 1048576UZ}) {
        auto other = board(size, false, size != 32768UZ && size != 278528UZ);
        const auto before = state(*other);
        StateReader mismatched(saved);
        other->load_state(mismatched);
        REQUIRE_FALSE(mismatched.ok());
        REQUIRE(state(*other) == before);
    }
    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*mapper) == saved);
}

} // namespace
