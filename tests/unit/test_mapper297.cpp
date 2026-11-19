#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper297.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 256U * 1024U;
constexpr std::size_t kChrSize = 256U * 1024U;

INesHeader header() {
    INesHeader h{};
    h.mapper_number = 297U;
    h.is_nes2 = true;
    h.submapper = 0U;
    h.mirror_mode = MirrorMode::Vertical;
    return h;
}

std::vector<Byte> make_prg() {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i >> 14U) ^ (i >> 7U) ^ (i * 29U));
    return data;
}

std::vector<Byte> make_chr() {
    std::vector<Byte> data(kChrSize);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i >> 12U) ^ (i >> 5U) ^ (i * 43U));
    return data;
}

std::unique_ptr<Mapper297> board() {
    REQUIRE(Mapper297::validate_geometry(header(), kPrgSize, kChrSize));
    return std::make_unique<Mapper297>(header(), make_prg(), make_chr());
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void serial_write(Mapper297& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit)
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
}

Byte prg_cell(std::size_t bank, Address addr) {
    const auto i = bank * 16384U + (addr & 0x3FFFU);
    return static_cast<Byte>((i >> 14U) ^ (i >> 7U) ^ (i * 29U));
}

Byte chr_cell(std::size_t bank, Address addr) {
    const auto i = bank * 4096U + (addr & 0x0FFFU);
    return static_cast<Byte>((i >> 12U) ^ (i >> 5U) ^ (i * 43U));
}

TEST_CASE("Mapper 297 accepts only the documented ROM-only NES 2.0 population", "[mapper297]") {
    auto mapper = board();
    REQUIRE(state(*mapper).size() == 20U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    for (std::size_t prg : {0U, 32768U, 131072U, 262143U, 262145U, 524288U})
        REQUIRE_FALSE(Mapper297::validate_geometry(header(), prg, kChrSize));
    for (std::size_t chr : {0U, 8192U, 131072U, 262143U, 262145U, 524288U})
        REQUIRE_FALSE(Mapper297::validate_geometry(header(), kPrgSize, chr));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper297::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.mapper_number = 70U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = 8192U;
            break;
        case 4U:
            h.prg_nvram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 8192U;
            break;
        case 6U:
            h.chr_nvram_size = 8192U;
            break;
        case 7U:
            h.mirror_mode = MirrorMode::Horizontal;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            FAIL("unexpected field");
        }
        REQUIRE_FALSE(Mapper297::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 297 mode register uses the complete E100 decode and floats on reads",
          "[mapper297]") {
    auto mapper = board();
    for (Address addr = 0x4020U; addr < 0x6000U; ++addr) {
        mapper->write_expansion(0x4120U, 0U);
        const auto before = state(*mapper);
        mapper->write_expansion(addr, 0xFDU);
        const bool decoded = (addr & 0xE100U) == 0x4100U;
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(decoded ? 8U : 0U, 0x8000U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        if (!decoded)
            REQUIRE(state(*mapper) == before);
    }
    for (Address addr : {0x4020U, 0x40FFU, 0x4120U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_prg(addr, 0xFFU);
    }
}

TEST_CASE("Mapper 297 Mapper 70 aliases bank PRG and CHR with wired AND conflicts", "[mapper297]") {
    auto mapper = board();
    for (Address addr = 0x8000U;; ++addr) {
        mapper->write_prg(0x8000U, 0U);
        const auto value = static_cast<Byte>(static_cast<unsigned>(addr) * 37U ^
                                             (static_cast<unsigned>(addr) >> 8U));
        const auto effective = static_cast<Byte>(value & mapper->read_prg(addr));
        mapper->write_prg(addr, value);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell((effective >> 4U) & 3U, 0x8000U));
        REQUIRE(mapper->read_prg(0xBFFFU) == prg_cell((effective >> 4U) & 3U, 0xBFFFU));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(3U, 0xC000U));
        REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(3U, 0xFFFFU));
        REQUIRE(mapper->read_chr(0x1234U) == chr_cell((effective & 0x0FU) * 2U + 1U, 0x0234U));
        if (addr == 0xFFFFU)
            break;
    }
}

TEST_CASE("Mapper 297 Mapper 70 A16 selects each 64 KiB fixed-bank block", "[mapper297]") {
    auto mapper = board();
    for (Byte outer : {0U, 2U}) {
        mapper->write_expansion(0x4120U, outer);
        for (Byte bank = 0U; bank < 4U; ++bank) {
            const auto value = static_cast<Byte>(bank << 4U);
            const auto effective = static_cast<Byte>(value & mapper->read_prg(0x8000U));
            mapper->write_prg(0x8000U, value);
            const auto expected = (effective >> 4U) & 3U;
            const auto base = outer != 0U ? 4U : 0U;
            const auto fixed = base + 3U;
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(base + expected, 0x8000U));
            REQUIRE(mapper->read_prg(0xC000U) == prg_cell(fixed, 0xC000U));
            REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(fixed, 0xFFFFU));
        }
    }
}

TEST_CASE("Mapper 297 MMC1 serial protocol covers all PRG modes and outer banking", "[mapper297]") {
    auto mapper = board();
    mapper->write_expansion(0x4120U, 1U);
    for (Byte mode = 0U; mode < 4U; ++mode) {
        serial_write(*mapper, 0x8000U, static_cast<Byte>(mode << 2U));
        for (Byte bank = 0U; bank < 32U; ++bank) {
            serial_write(*mapper, 0xE000U, bank);
            const unsigned selected = bank & 7U;
            unsigned low = 0U;
            unsigned high = 0U;
            if (mode <= 1U) {
                low = selected & 6U;
                high = low + 1U;
            } else if (mode == 2U) {
                low = 0U;
                high = selected;
            } else {
                low = selected;
                high = 7U;
            }
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(8U + low, 0x8000U));
            REQUIRE(mapper->read_prg(0xBFFFU) == prg_cell(8U + low, 0xBFFFU));
            REQUIRE(mapper->read_prg(0xC000U) == prg_cell(8U + high, 0xC000U));
            REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(8U + high, 0xFFFFU));
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        }
    }
}

TEST_CASE("Mapper 297 MMC1 serial protocol covers both CHR modes and all bank pins",
          "[mapper297]") {
    auto mapper = board();
    mapper->write_expansion(0x4120U, 1U);
    for (Byte bank0 = 0U; bank0 < 32U; ++bank0) {
        serial_write(*mapper, 0x8000U, 0x0CU);
        serial_write(*mapper, 0xA000U, bank0);
        const unsigned low = 32U + (bank0 & 0x1EU);
        REQUIRE(mapper->read_chr(0x0000U) == chr_cell(low, 0U));
        REQUIRE(mapper->read_chr(0x0FFFU) == chr_cell(low, 0x0FFFU));
        REQUIRE(mapper->read_chr(0x1000U) == chr_cell(low + 1U, 0U));
        REQUIRE(mapper->read_chr(0x1FFFU) == chr_cell(low + 1U, 0x0FFFU));
        serial_write(*mapper, 0x8000U, 0x1CU);
        for (Byte bank1 = 0U; bank1 < 32U; ++bank1) {
            serial_write(*mapper, 0xA000U, bank0);
            serial_write(*mapper, 0xC000U, bank1);
            REQUIRE(mapper->read_chr(0x0123U) == chr_cell(32U + bank0, 0x0123U));
            REQUIRE(mapper->read_chr(0x1456U) == chr_cell(32U + bank1, 0x0456U));
        }
    }
}

TEST_CASE("Mapper 297 MMC1 commits only on the fifth write and filters RMW writes", "[mapper297]") {
    auto mapper = board();
    mapper->write_expansion(0x4120U, 1U);
    const auto initial = mapper->read_prg(0x8000U);
    const std::array<Byte, 4> partial{1U, 1U, 0U, 1U};
    for (const auto bit : partial) {
        mapper->write_prg(0x8000U, bit);
        REQUIRE(mapper->read_prg(0x8000U) == initial);
    }
    mapper->write_prg(0xE000U, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(8U + 3U, 0x8000U));

    mapper->write_prg(0x8000U, 0x80U);
    mapper->write_prg_cpu(0xE000U, 0U, false);
    mapper->write_prg_cpu(0xE000U, 1U, true);
    for (Byte bit : {1U, 0U, 0U, 0U})
        mapper->write_prg_cpu(0xE000U, bit, false);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(8U + 2U, 0x8000U));

    mapper->write_prg(0x8000U, 0x80U);
    serial_write(*mapper, 0x8000U, 0U);
    mapper->write_prg_cpu(0xE000U, 1U, false);
    mapper->write_prg_cpu(0x8000U, 0x80U, true);
    for (Byte bit : {1U, 0U, 0U, 0U, 0U})
        mapper->write_prg_cpu(0xE000U, bit, false);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(8U + 1U, 0x8000U));
    REQUIRE(mapper->read_prg(0xC000U) == prg_cell(8U + 7U, 0xC000U));
}

TEST_CASE("Mapper 297 mode changes retain independent latch and MMC1 state", "[mapper297]") {
    auto mapper = board();
    mapper->write_expansion(0x4120U, 1U);
    serial_write(*mapper, 0x8000U, 0x1CU);
    serial_write(*mapper, 0xA000U, 7U);
    serial_write(*mapper, 0xC000U, 19U);
    serial_write(*mapper, 0xE000U, 5U);
    const auto mmc1_prg = mapper->read_prg(0x8000U);
    const auto mmc1_chr0 = mapper->read_chr(0U);
    const auto mmc1_chr1 = mapper->read_chr(0x1000U);
    mapper->write_expansion(0x4120U, 2U);
    mapper->write_prg(0x8000U, 0U);
    const auto mapper70_prg = mapper->read_prg(0x8000U);
    const auto mapper70_chr = mapper->read_chr(0U);
    mapper->write_expansion(0x4120U, 1U);
    REQUIRE(mapper->read_prg(0x8000U) == mmc1_prg);
    REQUIRE(mapper->read_chr(0U) == mmc1_chr0);
    REQUIRE(mapper->read_chr(0x1000U) == mmc1_chr1);
    mapper->write_expansion(0x4120U, 2U);
    REQUIRE(mapper->read_prg(0x8000U) == mapper70_prg);
    REQUIRE(mapper->read_chr(0U) == mapper70_chr);
}

TEST_CASE("Mapper 297 reset and cold power use deterministic defaults without RAM", "[mapper297]") {
    auto mapper = board();
    mapper->write_expansion(0x4120U, 1U);
    serial_write(*mapper, 0x8000U, 0x1FU);
    serial_write(*mapper, 0xA000U, 31U);
    serial_write(*mapper, 0xC000U, 29U);
    serial_write(*mapper, 0xE000U, 7U);
    for (bool cold : {false, true}) {
        if (cold)
            mapper->power_on();
        else
            mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0x8000U));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(3U, 0xC000U));
        REQUIRE(mapper->read_chr(0x1234U) == chr_cell(1U, 0x0234U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
    }
}

TEST_CASE("Mapper 297 snapshots replay partial serial and bank state without ROM bytes",
          "[mapper297]") {
    auto mapper = board();
    mapper->write_expansion(0x4120U, 1U);
    serial_write(*mapper, 0x8000U, 0x1CU);
    serial_write(*mapper, 0xA000U, 13U);
    serial_write(*mapper, 0xC000U, 21U);
    mapper->write_prg(0xE000U, 1U);
    mapper->write_prg(0xE000U, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 20U);
    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (Byte bit : {1U, 0U, 0U}) {
        mapper->write_prg(0xE000U, bit);
        copy->write_prg(0xE000U, bit);
    }
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(8U + 5U, 0x8000U));
    REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));
    REQUIRE(copy->read_chr(0U) == mapper->read_chr(0U));
    REQUIRE(copy->read_chr(0x1000U) == mapper->read_chr(0x1000U));
}

TEST_CASE("Mapper 297 malformed snapshots reject atomically", "[mapper297]") {
    auto mapper = board();
    mapper->write_expansion(0x4120U, 1U);
    serial_write(*mapper, 0x8000U, 0x1CU);
    serial_write(*mapper, 0xA000U, 13U);
    serial_write(*mapper, 0xE000U, 5U);
    mapper->write_prg(0xE000U, 1U);
    mapper->write_prg(0xE000U, 0U);
    const auto saved = state(*mapper);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset : {0UZ, 4UZ, 8UZ}) {
        auto bad = saved;
        bad[offset] ^= 0x80U;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    const std::array<std::pair<std::size_t, Byte>, 7> invalid{{
        {12U, 4U},
        {14U, 4U},
        {15U, 5U},
        {16U, 0x20U},
        {17U, 0x20U},
        {18U, 0x20U},
        {19U, 0x20U},
    }};
    for (const auto& [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
}

} // namespace
