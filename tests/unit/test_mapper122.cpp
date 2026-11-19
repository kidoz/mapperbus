#include <array>
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <vector>

#include "core/mappers/mapper122.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 122U;
    result.is_nes2 = !legacy;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    result.prg_ram_size = legacy ? 8192U : 0U;
    return result;
}

std::vector<Byte> prg_rom() {
    std::vector<Byte> data(32768U);
    for (std::size_t i = 0U; i < data.size(); ++i) {
        data[i] = static_cast<Byte>((i * 37U) ^ (i >> 8U));
    }
    return data;
}

std::vector<Byte> chr_rom() {
    std::vector<Byte> data(32768U);
    for (std::size_t i = 0U; i < data.size(); ++i) {
        data[i] = static_cast<Byte>((i / 4096U) ^ (i * 29U) ^ (i >> 8U));
    }
    return data;
}

Jy043 board(bool legacy = false, bool vertical = true) {
    return Jy043(header(legacy, vertical), prg_rom(), chr_rom());
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 122 maps fixed PRG ROM and fixed solder-pad CIRAM", "[mapper122]") {
    const auto expected_prg = prg_rom();
    for (bool legacy : {false, true}) {
        for (bool vertical : {false, true}) {
            auto mapper = board(legacy, vertical);
            for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
                REQUIRE(mapper.read_prg(static_cast<Address>(addr)) ==
                        expected_prg[addr & 0x7FFFU]);
            }
            REQUIRE(mapper.mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            REQUIRE(mapper.battery_ram().empty());
            REQUIRE_FALSE(mapper.has_flash_storage());
            REQUIRE_FALSE(mapper.has_expansion_audio());
            REQUIRE_FALSE(mapper.irq_pending());
            REQUIRE(mapper.audio_output() == 0.0F);
            REQUIRE(mapper.cpu_open_bus_mask() == 0xFFU);
            REQUIRE(mapper.prg_read_mask(0x8000U) == 0xFFU);
            REQUIRE(mapper.prg_read_mask(0xFFFFU) == 0xFFU);
            for (Address addr : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
                REQUIRE_FALSE(mapper.maps_prg(addr));
                REQUIRE_FALSE(mapper.maps_expansion(addr));
            }
        }
    }
}

TEST_CASE("Mapper 122 decodes even and odd writes across the entire CPU ROM range", "[mapper122]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper.reset();
        const Byte value = static_cast<Byte>(addr ^ (addr >> 8U));
        mapper.write_prg(static_cast<Address>(addr), value);
        const Byte expected = addr >= 0x8000U ? static_cast<Byte>(value & 7U) : 0U;
        REQUIRE(state(mapper)[2U] == ((addr & 1U) == 0U ? expected : 0U));
        REQUIRE(state(mapper)[3U] == ((addr & 1U) != 0U ? expected : 0U));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper.write_prg(0xFFFEU, static_cast<Byte>(value));
        REQUIRE(state(mapper)[2U] == (value & 7U));
        mapper.write_prg(0xFFFFU, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(mapper)[3U] == ((value ^ 0xFFU) & 7U));
    }
}

TEST_CASE("Mapper 122 selects independent 4 KiB CHR windows and protects ROM", "[mapper122]") {
    auto mapper = board();
    const auto expected_chr = chr_rom();
    for (Byte low = 0U; low < 8U; ++low) {
        mapper.write_prg(0x8000U, low);
        for (Byte high = 0U; high < 8U; ++high) {
            mapper.write_prg(0x8001U, high);
            for (Address addr : {0x0000U, 0x0123U, 0x0FFFU, 0x1000U, 0x1123U, 0x1FFFU}) {
                mapper.write_chr(addr, static_cast<Byte>(addr ^ 0xA7U));
                const std::size_t bank = (addr & 0x1000U) == 0U ? low : high;
                REQUIRE(mapper.read_chr(addr) == expected_chr[bank * 4096U + (addr & 0x0FFFU)]);
            }
        }
    }
}

TEST_CASE("Mapper 122 reset and cold power select both CHR banks at zero", "[mapper122]") {
    auto mapper = board();
    const auto initial = state(mapper);
    mapper.write_prg(0x9A00U, 7U);
    mapper.write_prg(0xC001U, 6U);
    REQUIRE(state(mapper) != initial);
    mapper.reset();
    REQUIRE(state(mapper) == initial);
    mapper.write_prg(0xFFFFU, 5U);
    mapper.write_prg(0xFFFEU, 4U);
    mapper.power_on();
    REQUIRE(state(mapper) == initial);
    REQUIRE(mapper.read_chr(0U) == chr_rom()[0U]);
    REQUIRE(mapper.read_chr(0x1000U) == chr_rom()[0U]);
    mapper.set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper.battery_ram().empty());
}

TEST_CASE("Mapper 122 rejects unsupported ROM geometry memory and metadata", "[mapper122]") {
    for (bool legacy : {false, true}) {
        for (auto mode : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto valid = header(legacy, mode == MirrorMode::Vertical);
            REQUIRE(Jy043::validate_geometry(valid, 32768U, 32768U));
        }
        for (std::size_t size : {0U, 8192U, 16384U, 32767U, 32769U, 65536U, 131072U}) {
            INFO(size);
            REQUIRE_FALSE(Jy043::validate_geometry(header(legacy), size, 32768U));
            REQUIRE_FALSE(Jy043::validate_geometry(header(legacy), 32768U, size));
        }
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            auto invalid = header(legacy);
            invalid.mirror_mode = mode;
            REQUIRE_FALSE(Jy043::validate_geometry(invalid, 32768U, 32768U));
        }
        auto battery = header(legacy);
        battery.has_battery = true;
        REQUIRE_FALSE(Jy043::validate_geometry(battery, 32768U, 32768U));
        for (unsigned field = 0U; field < 2U; ++field) {
            auto invalid = header(legacy);
            if (field == 0U) {
                invalid.chr_ram_size = 8192U;
            } else {
                invalid.chr_nvram_size = 8192U;
            }
            REQUIRE_FALSE(Jy043::validate_geometry(invalid, 32768U, 32768U));
        }
    }
    for (unsigned field = 0U; field < 2U; ++field) {
        auto invalid = header();
        if (field == 0U) {
            invalid.prg_ram_size = 8192U;
        } else {
            invalid.prg_nvram_size = 8192U;
        }
        REQUIRE_FALSE(Jy043::validate_geometry(invalid, 32768U, 32768U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Jy043::validate_geometry(invalid, 32768U, 32768U));
    }
}

TEST_CASE("Mapper 122 snapshots replay both banks without ROM data", "[mapper122][state]") {
    const auto expected_chr = chr_rom();
    for (bool legacy : {false, true}) {
        for (bool vertical : {false, true}) {
            for (Byte low = 0U; low < 8U; ++low) {
                for (Byte high = 0U; high < 8U; ++high) {
                    auto mapper = board(legacy, vertical);
                    mapper.write_prg(0x8000U, static_cast<Byte>(0xF8U | low));
                    mapper.write_prg(0x8001U, static_cast<Byte>(0xF8U | high));
                    const auto saved = state(mapper);
                    REQUIRE(saved.size() == 4U);

                    mapper.reset();
                    StateReader reader(saved);
                    mapper.load_state(reader);
                    REQUIRE(reader.ok());
                    REQUIRE(reader.remaining() == 0U);
                    REQUIRE(state(mapper) == saved);
                    REQUIRE(mapper.read_chr(0U) == expected_chr[low * 4096U]);
                    REQUIRE(mapper.read_chr(0x1000U) ==
                            expected_chr[static_cast<std::size_t>(high) * 4096U]);
                }
            }
        }
    }
}

TEST_CASE("Mapper 122 rejects malformed and mismatched snapshots atomically",
          "[mapper122][state]") {
    auto mapper = board();
    mapper.write_prg(0x8000U, 3U);
    mapper.write_prg(0x8001U, 6U);
    const auto saved = state(mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader{std::span(saved).first(size)};
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        StateReader reader(invalid);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
    for (std::size_t field : {2UZ, 3UZ}) {
        for (unsigned bank = 0U; bank < 256U; ++bank) {
            auto candidate = saved;
            candidate[field] = static_cast<Byte>(bank);
            StateReader reader(candidate);
            mapper.load_state(reader);
            if (bank < 8U) {
                REQUIRE(reader.ok());
                REQUIRE(state(mapper) == candidate);
                StateReader restore(saved);
                mapper.load_state(restore);
                REQUIRE(restore.ok());
            } else {
                REQUIRE_FALSE(reader.ok());
                REQUIRE(state(mapper) == saved);
            }
        }
    }

    auto other = board(false, false);
    const auto before = state(other);
    StateReader mismatch(saved);
    other.load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(other) == before);
}

} // namespace
