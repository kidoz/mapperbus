#include <array>
#include <catch2/catch_test_macros.hpp>
#include <span>
#include <vector>

#include "core/mappers/mapper120_lh15.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgSize = 0x18000U;

INesHeader make_header(bool legacy = false) {
    INesHeader header{};
    header.prg_rom_banks = 6U;
    header.mapper_number = 120U;
    header.is_nes2 = !legacy;
    header.mirror_mode = MirrorMode::Vertical;
    header.prg_ram_size = legacy ? 8192U : 0U;
    header.chr_ram_size = 8192U;
    return header;
}

std::vector<Byte> make_prg() {
    std::vector<Byte> prg(kPrgSize);
    for (std::size_t addr = 0U; addr < prg.size(); ++addr)
        prg[addr] = static_cast<Byte>(addr ^ (addr >> 8U) ^ (addr >> 16U));
    return prg;
}

Lh15 make_board(bool legacy = false) {
    return Lh15(make_header(legacy), make_prg(), {});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 120 initializes the LH15 memory map without extra hardware", "[mapper120]") {
    const auto prg = make_prg();
    for (bool legacy : {false, true}) {
        auto mapper = make_board(legacy);
        REQUIRE(Lh15::validate_geometry(make_header(legacy), kPrgSize, 0U));
        REQUIRE(mapper.read_prg(0x6000U) == prg[0U]);
        REQUIRE(mapper.read_prg(0x8000U) == prg[0x10000U]);
        REQUIRE(mapper.read_prg(0xFFFFU) == prg[0x17FFFU]);
        REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
        REQUIRE(state(mapper).size() == 8195U);
        REQUIRE(mapper.battery_ram().empty());
        REQUIRE_FALSE(mapper.has_flash_storage());
        REQUIRE_FALSE(mapper.has_expansion_audio());
        REQUIRE_FALSE(mapper.irq_pending());
        REQUIRE(mapper.audio_output() == 0.0F);
        mapper.clock_cpu();
        mapper.clock_audio();
        mapper.clock_irq_counter();
        REQUIRE_FALSE(mapper.irq_pending());
    }
}

TEST_CASE("Mapper 120 selects all eight 8 KiB banks and masks unused data bits", "[mapper120]") {
    auto mapper = make_board();
    const auto prg = make_prg();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper.write_expansion(0x41FFU, static_cast<Byte>(value));
        const std::size_t base = (value & 7U) * 8192U;
        REQUIRE(mapper.read_prg(0x6000U) == prg[base]);
        REQUIRE(mapper.read_prg(0x7FFFU) == prg[base + 8191U]);
        REQUIRE(mapper.read_prg(0x8000U) == prg[0x10000U]);
        REQUIRE(mapper.read_prg(0xFFFFU) == prg[0x17FFFU]);
    }
    for (std::size_t bank = 0U; bank < 8U; ++bank) {
        mapper.write_expansion(0x41FFU, static_cast<Byte>(bank));
        for (std::size_t offset = 0U; offset < 8192U; ++offset)
            REQUIRE(mapper.read_prg(static_cast<Address>(0x6000U + offset)) ==
                    prg[bank * 8192U + offset]);
    }
    for (std::size_t offset = 0U; offset < 32768U; ++offset)
        REQUIRE(mapper.read_prg(static_cast<Address>(0x8000U + offset)) == prg[0x10000U + offset]);
}

TEST_CASE("Mapper 120 decodes every $41FF alias and leaves other writes inert", "[mapper120]") {
    auto mapper = make_board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper.reset();
        const bool selected = (addr & 0xE100U) == 0x4100U;
        REQUIRE(mapper.maps_expansion(static_cast<Address>(addr)) == selected);
        REQUIRE(mapper.expansion_read_mask(static_cast<Address>(addr)) == (selected ? 0U : 0xFFU));
        REQUIRE(mapper.read_expansion(static_cast<Address>(addr)) == 0U);
        mapper.write_expansion(static_cast<Address>(addr), 5U);
        REQUIRE(state(mapper)[2U] == (selected ? 5U : 0U));
        mapper.write_prg(static_cast<Address>(addr), 7U);
        REQUIRE(state(mapper)[2U] == (selected ? 7U : 0U));
        mapper.write_prg_cpu(static_cast<Address>(addr), 6U, true);
        REQUIRE(state(mapper)[2U] == (selected ? 6U : 0U));
    }
}

TEST_CASE("Mapper 120 maps only PRG ROM and leaves unmapped CPU bits floating", "[mapper120]") {
    auto mapper = make_board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        REQUIRE(mapper.maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
        REQUIRE(mapper.prg_read_mask(static_cast<Address>(addr)) == 0xFFU);
        REQUIRE(mapper.cpu_open_bus_mask() == 0xFFU);
    }
}

TEST_CASE("Mapper 120 provides volatile unbanked CHR RAM and fixed vertical CIRAM", "[mapper120]") {
    auto mapper = make_board();
    for (std::size_t addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(mapper.read_chr(static_cast<Address>(addr)) == 0U);
        mapper.write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    }
    for (std::size_t addr = 0U; addr < 8192U; ++addr) {
        const auto expected = static_cast<Byte>(addr ^ (addr >> 8U));
        REQUIRE(mapper.read_chr(static_cast<Address>(addr)) == expected);
        REQUIRE(mapper.read_chr(static_cast<Address>(addr + 0x2000U)) == expected);
    }
    mapper.write_expansion(0x41FFU, 7U);
    mapper.reset();
    REQUIRE(state(mapper)[2U] == 0U);
    REQUIRE(mapper.read_chr(0x1234U) == 0x26U);
    REQUIRE(mapper.mirror_mode() == MirrorMode::Vertical);
    mapper.power_on();
    for (std::size_t addr = 0U; addr < 8192U; ++addr)
        REQUIRE(mapper.read_chr(static_cast<Address>(addr)) == 0U);
    mapper.set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper.battery_ram().empty());
}

TEST_CASE("Mapper 120 rejects unsupported geometry memory and metadata", "[mapper120]") {
    const auto reject =
        [](const INesHeader& header, std::size_t prg_size = kPrgSize, std::size_t chr_size = 0U) {
            REQUIRE_FALSE(Lh15::validate_geometry(header, prg_size, chr_size));
        };
    for (std::size_t size : {0UZ, 8192UZ, 32768UZ, 65536UZ, 98303UZ, 98305UZ, 131072UZ})
        reject(make_header(), size);
    for (std::size_t size : {1UZ, 4096UZ, 8192UZ, 16384UZ})
        reject(make_header(), kPrgSize, size);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto header = make_header();
        header.submapper = submapper;
        reject(header);
    }
    for (auto mode : {MirrorMode::Horizontal,
                      MirrorMode::FourScreen,
                      MirrorMode::SingleLower,
                      MirrorMode::SingleUpper}) {
        auto header = make_header();
        header.mirror_mode = mode;
        reject(header);
    }
    for (bool legacy : {false, true}) {
        auto header = make_header(legacy);
        header.has_battery = true;
        reject(header);
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto header = make_header();
        switch (field) {
        case 0U:
            header.prg_ram_size = 8192U;
            break;
        case 1U:
            header.prg_nvram_size = 8192U;
            break;
        case 2U:
            header.chr_ram_size = 0U;
            break;
        default:
            header.chr_nvram_size = 8192U;
            break;
        }
        reject(header);
    }
    for (std::size_t size : {1UZ, 4096UZ, 16384UZ}) {
        auto header = make_header();
        header.chr_ram_size = size;
        reject(header);
    }
}

TEST_CASE("Mapper 120 snapshots replay bank and CHR RAM without ROM data", "[mapper120][state]") {
    auto mapper = make_board();
    auto copy = make_board();
    mapper.write_expansion(0x41FFU, 0xFEU);
    for (std::size_t addr = 0U; addr < 8192U; ++addr)
        mapper.write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 17U + addr / 256U));
    const auto saved = state(mapper);
    REQUIRE(saved.size() == 8195U);
    StateReader reader(saved);
    copy.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(copy) == saved);
    REQUIRE(copy.read_prg(0x6000U) == mapper.read_prg(0x6000U));
    REQUIRE(copy.read_chr(0x1555U) == mapper.read_chr(0x1555U));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {0x12U, 0x34U, 0x56U});
    StateReader tail_reader(with_tail);
    copy.load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
}

TEST_CASE("Mapper 120 rejects malformed snapshots atomically", "[mapper120][state]") {
    auto mapper = make_board();
    mapper.write_expansion(0x41FFU, 6U);
    mapper.write_chr(0x123U, 0xA7U);
    const auto saved = state(mapper);
    for (std::size_t size : {0UZ, 1UZ, 2UZ, 3UZ, 4UZ, 4096UZ, 8194UZ}) {
        StateReader reader{std::span(saved).first(size)};
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
    for (uint16_t tag : {0U, 1U, 0x77FFU, 0x7801U, 0xFFFFU}) {
        auto bad = saved;
        bad[0U] = static_cast<Byte>(tag);
        bad[1U] = static_cast<Byte>(tag >> 8U);
        bad[3U + 0x123U] ^= 0xFFU;
        StateReader reader(bad);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
    for (unsigned bank = 8U; bank < 256U; ++bank) {
        auto bad = saved;
        bad[2U] = static_cast<Byte>(bank);
        bad[3U + 0x123U] ^= 0xFFU;
        StateReader reader(bad);
        mapper.load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(mapper) == saved);
    }
    StateReader valid(saved);
    mapper.load_state(valid);
    REQUIRE(valid.ok());
    REQUIRE(state(mapper) == saved);
}

} // namespace
