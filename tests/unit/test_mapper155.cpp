#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mmc1a.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t k16K = 16384U;
constexpr std::size_t k8K = 8192U;

INesHeader native_header(std::size_t ram = 0, std::size_t nvram = 0) {
    INesHeader header{};
    header.mapper_number = 155U;
    header.mirror_mode = MirrorMode::Vertical;
    header.region = Region::NTSC;
    header.is_nes2 = true;
    header.has_battery = nvram != 0U;
    header.prg_ram_size = ram;
    header.prg_nvram_size = nvram;
    return header;
}

std::vector<Byte> patterned_prg(std::size_t size) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0; offset < size; ++offset)
        rom[offset] = static_cast<Byte>(offset / k16K);
    return rom;
}

std::vector<Byte> patterned_chr(std::size_t size) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0; offset < size; ++offset)
        rom[offset] = static_cast<Byte>(offset / 4096U);
    return rom;
}

std::unique_ptr<Mmc1a> make_board(INesHeader header, std::size_t prg, std::size_t chr) {
    header.prg_rom_size = prg;
    header.chr_rom_size = chr;
    header.chr_ram_size = chr == 0U ? k8K : 0U;
    REQUIRE(Mmc1a::validate_geometry(header, prg, chr));
    return std::make_unique<Mmc1a>(header, patterned_prg(prg), patterned_chr(chr));
}

std::unique_ptr<Mmc1a> board(std::size_t prg = 262144U,
                             std::size_t chr = 131072U,
                             std::size_t ram = 0,
                             std::size_t nvram = 0) {
    return make_board(native_header(ram, nvram), prg, chr);
}

std::unique_ptr<Mmc1a> legacy_board(std::size_t prg, std::size_t chr, bool battery = false) {
    INesHeader header{};
    header.mapper_number = 155U;
    header.mirror_mode = MirrorMode::Horizontal;
    header.has_battery = battery;
    header.prg_ram_size = battery ? 0U : k8K;
    header.prg_nvram_size = battery ? k8K : 0U;
    return make_board(header, prg, chr);
}

void serial_write(Mmc1a& mapper, Address addr, Byte value) {
    for (unsigned bit = 0; bit < 5U; ++bit)
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
}

std::vector<Byte> state(const Mmc1a& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 155 validates documented SxROM geometry", "[mapper155]") {
    struct Population {
        std::size_t prg;
        std::size_t chr;
        std::size_t ram;
        std::size_t nvram;
    };
    for (const auto population : std::array{
             Population{32768U, 16384U, 0U, 0U},
             Population{32768U, 65536U, k8K, 0U},
             Population{65536U, 131072U, 0U, 0U},
             Population{131072U, 0U, 0U, 0U},
             Population{131072U, k8K, 0U, k8K},
             Population{131072U, k8K, 16384U, 0U},
             Population{131072U, k8K, k8K, k8K},
             Population{262144U, 32768U, k8K, k8K},
             Population{131072U, k8K, 32768U, 0U},
             Population{524288U, k8K, 0U, k8K},
             Population{524288U, k8K, 0U, 32768U},
         }) {
        auto mapper = board(population.prg, population.chr, population.ram, population.nvram);
        REQUIRE(mapper->battery_ram().size() == population.nvram);
    }
    auto sub5 = native_header();
    sub5.submapper = 5U;
    REQUIRE(make_board(sub5, 32768U, 131072U));

    const auto reject = [](INesHeader header, std::size_t prg, std::size_t chr) {
        header.chr_ram_size = chr == 0U ? k8K : 0U;
        INFO("prg=" << prg << " chr=" << chr << " ram=" << header.prg_ram_size
                    << " nvram=" << header.prg_nvram_size << " submapper=" << header.submapper);
        REQUIRE_FALSE(Mmc1a::validate_geometry(header, prg, chr));
    };
    for (Byte submapper : {1U, 2U, 3U, 4U, 6U, 7U, 8U, 15U}) {
        auto header = native_header();
        header.submapper = submapper;
        reject(header, 131072U, 131072U);
    }
    for (std::size_t size : std::array<std::size_t, 4>{0U, k16K, 49152U, 1048576U})
        reject(native_header(), size, 131072U);
    for (std::size_t size : std::array<std::size_t, 3>{4096U, 49152U, 1048576U})
        reject(native_header(), 131072U, size);
    reject(native_header(), 32768U, k8K);
    reject(native_header(k8K), 65536U, 131072U);
    reject(native_header(16384U), 32768U, 65536U);
    reject(native_header(32768U), 131072U, 131072U);
    reject(native_header(), 524288U, k8K);
    reject(native_header(16384U), 524288U, k8K);
    reject(native_header(0U, k8K), 65536U, 131072U);

    auto bad = native_header();
    bad.mirror_mode = MirrorMode::FourScreen;
    reject(bad, 131072U, 131072U);
    bad = native_header();
    bad.has_battery = true;
    reject(bad, 131072U, 131072U);
    bad = native_header(k8K);
    bad.has_battery = true;
    reject(bad, 131072U, 131072U);
    bad = native_header(16384U, 0U);
    bad.chr_nvram_size = k8K;
    reject(bad, 131072U, k8K);
    bad = native_header();
    bad.submapper = 5U;
    reject(bad, 131072U, 131072U);

    for (const auto population : {Population{32768U, 65536U, 0U, 0U},
                                  Population{131072U, 131072U, 0U, 0U},
                                  Population{262144U, 0U, 0U, 0U},
                                  Population{524288U, k8K, 0U, 0U}})
        REQUIRE(legacy_board(population.prg, population.chr, true));
}

TEST_CASE("MMC1A implements the measured fixed-bank A17 matrix", "[mapper155]") {
    auto mapper = board();
    for (unsigned mode = 0; mode < 4U; ++mode) {
        for (unsigned bank = 0; bank < 32U; ++bank) {
            serial_write(*mapper, 0x8000U, static_cast<Byte>(mode << 2U));
            serial_write(*mapper, 0xE000U, static_cast<Byte>(bank));
            for (unsigned high = 0; high < 2U; ++high) {
                unsigned expected = 0;
                bool fixed = false;
                if (mode <= 1U) {
                    expected = (bank & 0x0EU) | high;
                } else if (mode == 2U) {
                    fixed = high == 0U;
                    expected = fixed ? 0U : bank & 0x0FU;
                } else {
                    fixed = high != 0U;
                    expected = fixed ? 0x0FU : bank & 0x0FU;
                }
                if (fixed && (bank & 0x10U) != 0U)
                    expected = (expected & 7U) | (bank & 8U);
                INFO("mode=" << mode << " bank=" << bank << " high=" << high);
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + high * 0x4000U)) ==
                        expected);
            }
        }
    }
}

TEST_CASE("MMC1 serial aliases reset semantics and RMW filtering match hardware", "[mapper155]") {
    auto mapper = board(262144U, 131072U, k8K);
    for (const auto& reg : std::array{std::pair{0x8000U, 0x9FFFU},
                                      std::pair{0xA000U, 0xBFFFU},
                                      std::pair{0xC000U, 0xDFFFU},
                                      std::pair{0xE000U, 0xFFFFU}}) {
        mapper->write_prg(0x8000U, 0x80U);
        serial_write(*mapper, reg.first, 0U);
        mapper->write_prg(0x8123U, 1U);
        mapper->write_prg(0xA456U, 0U);
        mapper->write_prg(0xC789U, 1U);
        mapper->write_prg(0xEABCU, 0U);
        mapper->write_prg(reg.second, 1U);
    }
    serial_write(*mapper, 0x8000U, 0x11U);
    serial_write(*mapper, 0xA000U, 0x07U);
    serial_write(*mapper, 0xC000U, 0x09U);
    serial_write(*mapper, 0xE000U, 0x12U);
    mapper->write_prg(0x9000U, 1U);
    mapper->write_prg(0x9001U, 0U);
    mapper->write_prg(0xFFFFU, 0x80U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    REQUIRE(mapper->read_prg(0xC000U) == 0x07U);
    REQUIRE(mapper->read_chr(0U) == 0x07U);
    REQUIRE(mapper->read_chr(0x1000U) == 0x09U);
    REQUIRE(mapper->maps_prg(0x6000U));

    serial_write(*mapper, 0xE000U, 0x03U);
    const auto before = state(*mapper);
    for (unsigned bit = 0; bit < 5U; ++bit)
        mapper->write_prg_cpu(0xE000U, static_cast<Byte>((0x05U >> bit) & 1U), true);
    REQUIRE(state(*mapper) == before);
    mapper->write_prg_cpu(0xE000U, 0x80U, true);
    REQUIRE(mapper->read_prg(0xC000U) == 0x0FU);
    mapper->write_prg_cpu(0x6000U, 0xA7U, true);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
}

TEST_CASE("MMC1 CHR modes nametables and CHR RAM are fully decoded", "[mapper155]") {
    auto mapper = board();
    serial_write(*mapper, 0x8000U, 0x10U);
    for (Byte bank = 0; bank < 32U; ++bank) {
        serial_write(*mapper, 0xA000U, bank);
        serial_write(*mapper, 0xC000U, static_cast<Byte>(31U - bank));
        REQUIRE(mapper->read_chr(0U) == bank);
        REQUIRE(mapper->read_chr(0x1000U) == static_cast<Byte>(31U - bank));
    }
    serial_write(*mapper, 0x8000U, 0x00U);
    for (Byte bank = 0; bank < 32U; ++bank) {
        serial_write(*mapper, 0xA000U, bank);
        REQUIRE(mapper->read_chr(0U) == (bank & 0x1EU));
        REQUIRE(mapper->read_chr(0x1000U) == ((bank & 0x1EU) + 1U));
    }
    for (Byte mode = 0; mode < 4U; ++mode) {
        serial_write(*mapper, 0x8000U, static_cast<Byte>(0x0CU | mode));
        REQUIRE(mapper->mirror_mode() ==
                static_cast<MirrorMode>(mode == 0U   ? MirrorMode::SingleLower
                                        : mode == 1U ? MirrorMode::SingleUpper
                                        : mode == 2U ? MirrorMode::Vertical
                                                     : MirrorMode::Horizontal));
    }

    auto ram = board(131072U, 0U);
    ram->write_chr(0x0123U, 0xA7U);
    REQUIRE(ram->read_chr(0x0123U) == 0xA7U);
    serial_write(*ram, 0x8000U, 0x10U);
    serial_write(*ram, 0xA000U, 0x1FU);
    ram->write_chr(0x0123U, 0x39U);
    REQUIRE(ram->read_chr(0x0123U) == 0x39U);
    REQUIRE(ram->read_chr(0x1123U) == 0xA7U);
}

TEST_CASE("SUROM outer PRG and all banked WRAM variants follow CHR pins", "[mapper155]") {
    auto surom = board(524288U, k8K, k8K);
    serial_write(*surom, 0xE000U, 0x03U);
    serial_write(*surom, 0xA000U, 0x10U);
    REQUIRE(surom->read_prg(0x8000U) == 19U);
    REQUIRE(surom->read_prg(0xC000U) == 31U);
    serial_write(*surom, 0xE000U, 0x10U);
    REQUIRE(surom->read_prg(0xC000U) == 23U);
    serial_write(*surom, 0x8000U, 0x1CU);
    serial_write(*surom, 0xA000U, 0x00U);
    serial_write(*surom, 0xC000U, 0x10U);
    surom->observe_ppu_a12(false);
    REQUIRE(surom->read_prg(0xC000U) == 7U);
    surom->observe_ppu_a12(true);
    REQUIRE(surom->read_prg(0xC000U) == 23U);
    REQUIRE(surom->observes_ppu_a12());

    auto sorom = board(131072U, k8K, k8K, k8K);
    sorom->write_prg(0x6000U, 0x11U);
    serial_write(*sorom, 0xA000U, 0x08U);
    sorom->write_prg(0x6000U, 0x22U);
    serial_write(*sorom, 0xA000U, 0x00U);
    REQUIRE(sorom->read_prg(0x6000U) == 0x11U);
    serial_write(*sorom, 0xA000U, 0x08U);
    REQUIRE(sorom->read_prg(0x6000U) == 0x22U);
    REQUIRE(sorom->battery_ram().size() == k8K);

    auto szrom = board(131072U, 32768U, 16384U);
    serial_write(*szrom, 0xA000U, 0x00U);
    szrom->write_prg(0x6000U, 0x33U);
    serial_write(*szrom, 0xA000U, 0x10U);
    szrom->write_prg(0x6000U, 0x44U);
    serial_write(*szrom, 0xA000U, 0x00U);
    REQUIRE(szrom->read_prg(0x6000U) == 0x33U);

    auto sxrom = board(262144U, k8K, 32768U);
    for (Byte bank = 0; bank < 4U; ++bank) {
        serial_write(*sxrom, 0xA000U, static_cast<Byte>(bank << 2U));
        sxrom->write_prg(0x6000U, static_cast<Byte>(0x50U + bank));
    }
    for (Byte bank = 0; bank < 4U; ++bank) {
        serial_write(*sxrom, 0xA000U, static_cast<Byte>(bank << 2U));
        REQUIRE(sxrom->read_prg(0x6000U) == static_cast<Byte>(0x50U + bank));
    }
}

TEST_CASE("MMC1A never disables WRAM through R and SNROM uses CHR enable", "[mapper155]") {
    auto mapper = board(262144U, k8K, k8K);
    mapper->write_prg(0x6000U, 0xA7U);
    serial_write(*mapper, 0xE000U, 0x1FU);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    serial_write(*mapper, 0xA000U, 0x10U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x6000U, 0x39U);
    serial_write(*mapper, 0xA000U, 0x00U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    serial_write(*mapper, 0x8000U, 0x1CU);
    serial_write(*mapper, 0xC000U, 0x10U);
    mapper->observe_ppu_a12(true);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->observe_ppu_a12(false);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
}

TEST_CASE("Mapper 155 reset power and battery retention are deterministic", "[mapper155]") {
    auto mapper = board(131072U, 0U, k8K, k8K);
    REQUIRE(mapper->battery_ram().size() == k8K);
    mapper->set_battery_ram(std::vector<Byte>(k8K, 0xA7U));
    serial_write(*mapper, 0xA000U, 0x08U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    serial_write(*mapper, 0xA000U, 0x00U);
    mapper->write_prg(0x6000U, 0x39U);
    mapper->write_chr(0x123U, 0x55U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6000U) == 0x39U);
    REQUIRE(mapper->read_chr(0x123U) == 0x55U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->read_chr(0x123U) == 0U);
    serial_write(*mapper, 0xA000U, 0x08U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);

    auto legacy = legacy_board(262144U, k8K, true);
    REQUIRE(legacy->battery_ram().size() == 32768U);
    legacy->set_battery_ram(std::vector<Byte>(65536U, 0x5CU));
    REQUIRE(std::ranges::all_of(legacy->battery_ram(), [](Byte value) { return value == 0x5CU; }));
}

TEST_CASE("Mapper 155 state replay is complete and rejection is atomic", "[mapper155]") {
    auto mapper = board();
    serial_write(*mapper, 0x8000U, 0x1AU);
    serial_write(*mapper, 0xA000U, 0x07U);
    serial_write(*mapper, 0xC000U, 0x09U);
    serial_write(*mapper, 0xE000U, 0x15U);
    mapper->write_prg(0xE000U, 1U);
    mapper->write_prg(0xE000U, 0U);
    mapper->observe_ppu_a12(true);
    const auto saved = state(*mapper);
    mapper->power_on();
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_prg(0xC000U) == 5U);

    for (std::size_t length = 0; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    const auto reject = [&](std::size_t offset, Byte value) {
        auto corrupt = saved;
        corrupt[offset] = value;
        StateReader reader(corrupt);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    reject(0U, 0U);
    reject(4U, 1U);
    reject(8U, 1U);
    reject(12U, 1U);
    reject(16U, 1U);
    reject(21U, 5U);
    reject(22U, 0x20U);
    reject(23U, 0x20U);
    reject(24U, 0x20U);
    reject(25U, 0x20U);
    reject(26U, 2U);

    auto wrong = board(131072U, 131072U);
    const auto before = state(*wrong);
    StateReader mismatch(saved);
    wrong->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*wrong) == before);
}

TEST_CASE("Mapper 155 large RAM snapshots preserve volatile and persistent bytes", "[mapper155]") {
    auto mapper = board(131072U, 0U, k8K, k8K);
    for (std::size_t offset = 0; offset < 16384U; ++offset) {
        serial_write(*mapper, 0xA000U, static_cast<Byte>((offset / k8K) << 3U));
        mapper->write_prg(static_cast<Address>(0x6000U + offset % k8K),
                          static_cast<Byte>(offset * 17U));
    }
    for (std::size_t offset = 0; offset < k8K; ++offset)
        mapper->write_chr(static_cast<Address>(offset), static_cast<Byte>(offset * 29U));
    serial_write(*mapper, 0xA000U, 0x08U);
    const auto saved = state(*mapper);
    mapper->power_on();
    REQUIRE(state(*mapper) != saved);
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(state(*mapper) == saved);
    for (std::size_t length : {saved.size() - 1U, saved.size() - k8K, std::size_t{27U}}) {
        StateReader truncated{std::span(saved).first(length)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    auto corrupt = saved;
    corrupt[27U] = 0xFFU;
    StateReader bad_length(corrupt);
    mapper->load_state(bad_length);
    REQUIRE_FALSE(bad_length.ok());
    REQUIRE(state(*mapper) == saved);
}

} // namespace
