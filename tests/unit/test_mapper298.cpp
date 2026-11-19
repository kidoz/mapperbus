#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/unl_tf1201.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

enum class RamKind { None, Volatile, NonVolatile };

INesHeader make_header(RamKind ram = RamKind::None,
                       MirrorMode mirror = MirrorMode::Vertical,
                       std::size_t prg_size = 262144U,
                       std::size_t chr_size = 524288U) {
    INesHeader header{};
    header.mapper_number = 298U;
    header.is_nes2 = true;
    header.submapper = 0U;
    header.mirror_mode = mirror;
    header.nametable_bits = mirror == MirrorMode::Vertical ? 1U : 0U;
    header.prg_rom_size = prg_size;
    header.chr_rom_size = chr_size;
    header.prg_ram_size = ram == RamKind::Volatile ? 2048U : 0U;
    header.prg_nvram_size = ram == RamKind::NonVolatile ? 2048U : 0U;
    header.has_battery = ram == RamKind::NonVolatile;
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 13U + offset * 7U + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + (bank >> 8U) * 71U + offset * 11U + (offset >> 8U));
}

std::vector<Byte> make_prg(std::size_t size) {
    std::vector<Byte> rom(size);
    for (std::size_t i = 0U; i < size; ++i) {
        rom[i] = prg_cell(static_cast<unsigned>(i / 8192U), static_cast<unsigned>(i % 8192U));
    }
    return rom;
}

std::vector<Byte> make_chr(std::size_t size) {
    std::vector<Byte> rom(size);
    for (std::size_t i = 0U; i < size; ++i) {
        rom[i] = chr_cell(static_cast<unsigned>(i / 1024U), static_cast<unsigned>(i % 1024U));
    }
    return rom;
}

std::unique_ptr<UnlTf1201> make_board(RamKind ram = RamKind::None,
                                      MirrorMode mirror = MirrorMode::Vertical,
                                      std::size_t prg_size = 262144U,
                                      std::size_t chr_size = 524288U,
                                      bool patterned = true) {
    const auto header = make_header(ram, mirror, prg_size, chr_size);
    REQUIRE(UnlTf1201::validate_geometry(header, prg_size, chr_size));
    auto prg = patterned ? make_prg(prg_size) : std::vector<Byte>(prg_size);
    auto chr = patterned ? make_chr(chr_size) : std::vector<Byte>(chr_size);
    return std::make_unique<UnlTf1201>(header, std::move(prg), std::move(chr));
}

void select_chr_bank(Mapper& mapper, unsigned slot, unsigned bank) {
    const auto base = static_cast<Address>(0xB000U + (slot / 2U) * 0x1000U + (slot & 1U));
    mapper.write_prg(base, static_cast<Byte>(bank & 15U));
    mapper.write_prg(static_cast<Address>(base + 2U), static_cast<Byte>(bank >> 4U));
}

std::vector<Byte> mapper_state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 298 validates the documented NES 2.0 geometry envelope", "[mapper298]") {
    for (const std::size_t prg : {32768U, 65536U, 131072U, 262144U}) {
        for (const std::size_t chr : {8192U, 16384U, 32768U, 65536U, 131072U, 262144U, 524288U}) {
            for (const RamKind ram : {RamKind::None, RamKind::Volatile, RamKind::NonVolatile}) {
                for (const MirrorMode mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                    INFO("prg=" << prg << " chr=" << chr << " ram=" << static_cast<int>(ram));
                    REQUIRE(
                        UnlTf1201::validate_geometry(make_header(ram, mirror, prg, chr), prg, chr));
                }
            }
        }
    }

    for (const std::size_t size : {0U, 8192U, 16384U, 32767U, 32769U, 49152U, 524288U}) {
        REQUIRE_FALSE(UnlTf1201::validate_geometry(make_header(), size, 524288U));
    }
    for (const std::size_t size : {0U, 4096U, 8191U, 8193U, 12288U, 1048576U}) {
        REQUIRE_FALSE(UnlTf1201::validate_geometry(make_header(), 262144U, size));
    }

    auto header = make_header();
    header.mapper_number = 25U;
    REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
    header = make_header();
    header.is_nes2 = false;
    REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        header = make_header();
        header.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
    }
    for (const MirrorMode mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(
            UnlTf1201::validate_geometry(make_header(RamKind::None, mirror), 262144U, 524288U));
    }

    for (const std::size_t size : {1U, 1024U, 4096U, 8192U}) {
        header = make_header();
        header.prg_ram_size = size;
        REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
        header = make_header(RamKind::NonVolatile);
        header.prg_nvram_size = size;
        REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
    }
    header = make_header(RamKind::Volatile);
    header.prg_nvram_size = 2048U;
    header.has_battery = true;
    REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
    header = make_header(RamKind::NonVolatile);
    header.has_battery = false;
    REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
    header = make_header();
    header.has_battery = true;
    REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
    header = make_header();
    header.chr_ram_size = 8192U;
    REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
    header = make_header();
    header.chr_nvram_size = 8192U;
    REQUIRE_FALSE(UnlTf1201::validate_geometry(header, 262144U, 524288U));
}

TEST_CASE("Mapper 298 implements complete VRC4b PRG banking and physical-size aliases",
          "[mapper298]") {
    auto mapper = make_board();
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->read_prg(0xA000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->read_prg(0xC000U) == prg_cell(30U, 0U));
    REQUIRE(mapper->read_prg(0xE000U) == prg_cell(31U, 0U));

    for (unsigned value = 0U; value < 256U; ++value) {
        for (const Address addr : {Address{0x8000U},
                                   Address{0x8001U},
                                   Address{0x8002U},
                                   Address{0x8003U},
                                   Address{0x8FFCU}}) {
            mapper->write_prg(addr, static_cast<Byte>(value));
        }
        for (const Address addr : {Address{0xA000U},
                                   Address{0xA001U},
                                   Address{0xA002U},
                                   Address{0xA003U},
                                   Address{0xAFFCU}}) {
            mapper->write_prg(addr, static_cast<Byte>(value));
        }
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(value & 31U, 0U));
        REQUIRE(mapper->read_prg(0xA000U) == prg_cell(value & 31U, 0U));
    }

    for (unsigned bank = 0U; bank < 32U; ++bank) {
        mapper->write_prg(0x8000U, static_cast<Byte>(bank));
        mapper->write_prg(0xA000U, static_cast<Byte>(31U - bank));
        for (const Byte swap : {0U, 2U}) {
            mapper->write_prg(0x9001U, swap);
            const std::array banks = {
                swap == 0U ? bank : 30U, 31U - bank, swap == 0U ? 30U : bank, 31U};
            for (unsigned offset = 0U; offset < 8192U; ++offset) {
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto addr = static_cast<Address>(0x8000U + slot * 8192U + offset);
                    REQUIRE(mapper->read_prg(addr) == prg_cell(banks[slot], offset));
                }
            }
        }
    }

    auto small = make_board(RamKind::None, MirrorMode::Vertical, 32768U, 8192U);
    small->write_prg(0x8000U, 31U);
    small->write_prg(0xA000U, 31U);
    REQUIRE(small->read_prg(0x8000U) == prg_cell(3U, 0U));
    REQUIRE(small->read_prg(0xA000U) == prg_cell(3U, 0U));
    REQUIRE(small->read_prg(0xC000U) == prg_cell(2U, 0U));
    REQUIRE(small->read_prg(0xE000U) == prg_cell(3U, 0U));
}

TEST_CASE("Mapper 298 implements all nine-bit CHR banks and protects CHR ROM", "[mapper298]") {
    auto mapper = make_board();
    for (unsigned bank = 0U; bank < 512U; ++bank) {
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            select_chr_bank(*mapper, slot, bank);
        }
        for (unsigned addr = 0U; addr < 8192U; ++addr) {
            INFO("bank=" << bank << " addr=" << addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr_cell(bank, addr & 1023U));
        }
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xB000U, static_cast<Byte>(value));
        mapper->write_prg(0xB002U, static_cast<Byte>(value));
        const unsigned bank = (value & 15U) | ((value & 31U) << 4U);
        REQUIRE(mapper->read_chr(0U) == chr_cell(bank, 0U));
    }

    select_chr_bank(*mapper, 0U, 17U);
    const auto before = mapper_state(*mapper);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto data = static_cast<Byte>(addr ^ (addr >> 8U));
        mapper->write_chr(static_cast<Address>(addr), data);
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const unsigned bank = addr < 1024U ? 17U : 511U;
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == chr_cell(bank, addr & 1023U));
    }
    REQUIRE(mapper_state(*mapper) == before);

    auto small = make_board(RamKind::None, MirrorMode::Vertical, 32768U, 8192U);
    select_chr_bank(*small, 7U, 511U);
    REQUIRE(small->read_chr(0x1FFFU) == chr_cell(7U, 1023U));
}

TEST_CASE("Mapper 298 decodes VRC4b mirroring swap and external-select ports independently",
          "[mapper298]") {
    auto mapper = make_board(RamKind::Volatile);
    constexpr std::array kMirrors = {MirrorMode::Vertical,
                                     MirrorMode::Horizontal,
                                     MirrorMode::SingleLower,
                                     MirrorMode::SingleUpper};
    for (const Address addr : {Address{0x9000U}, Address{0x9004U}, Address{0x9FFCU}}) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(addr, static_cast<Byte>(value));
            REQUIRE(mapper->mirror_mode() == kMirrors[value & 3U]);
        }
    }

    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0x9002U, 2U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(7U, 0U));
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x9003U, 3U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(7U, 0U));
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x9001U, 1U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(7U, 0U));
    mapper->write_prg(0x9005U, 2U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(30U, 0U));
    REQUIRE(mapper->read_prg(0xC000U) == prg_cell(7U, 0U));
}

TEST_CASE("Mapper 298 RAM open-bus persistence and reset policies are population-aware",
          "[mapper298]") {
    for (const RamKind ram : {RamKind::None, RamKind::Volatile, RamKind::NonVolatile}) {
        auto mapper = make_board(ram, MirrorMode::Horizontal, 32768U, 8192U);
        for (const Address addr : {Address{0x4020U},
                                   Address{0x5FFFU},
                                   Address{0x6000U},
                                   Address{0x6FFFU},
                                   Address{0x7000U},
                                   Address{0x7FFFU}}) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
        }
        REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
        REQUIRE(mapper->battery_ram().size() == (ram == RamKind::NonVolatile ? 2048U : 0U));
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);

        mapper->write_prg(0x9001U, 1U);
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
            REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) ==
                    (ram != RamKind::None && addr < 0x7000U));
        }
        mapper->write_prg(0x6000U, 0xA7U);
        mapper->write_prg(0x6801U, 0x5AU);
        mapper->write_prg(0x7000U, 0xCCU);
        if (ram != RamKind::None) {
            REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
            REQUIRE(mapper->read_prg(0x6800U) == 0xA7U);
            REQUIRE(mapper->read_prg(0x6001U) == 0x5AU);
        }

        mapper->write_prg(0x9001U, 0U);
        mapper->write_prg(0x6000U, 0U);
        mapper->write_prg(0x9001U, 1U);
        if (ram != RamKind::None) {
            REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
        }
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->write_prg(0x9001U, 1U);
        if (ram != RamKind::None) {
            REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
        }
        mapper->power_on();
        mapper->write_prg(0x9001U, 1U);
        if (ram != RamKind::None) {
            REQUIRE(mapper->read_prg(0x6000U) == (ram == RamKind::NonVolatile ? 0xA7U : 0U));
        }
    }

    auto persistent = make_board(RamKind::NonVolatile, MirrorMode::Vertical, 32768U, 8192U);
    std::vector<Byte> imported(2048U);
    for (std::size_t i = 0U; i < imported.size(); ++i) {
        imported[i] = static_cast<Byte>(i * 19U + 3U);
    }
    persistent->set_battery_ram(imported);
    REQUIRE(std::ranges::equal(persistent->battery_ram(), imported));
    const auto before = mapper_state(*persistent);
    persistent->set_battery_ram(std::span(imported).first(2047U));
    REQUIRE(mapper_state(*persistent) == before);
    std::vector<Byte> oversized(2049U, 0xFFU);
    persistent->set_battery_ram(oversized);
    REQUIRE(mapper_state(*persistent) == before);
    persistent->power_on();
    REQUIRE(std::ranges::equal(persistent->battery_ram(), imported));
}

TEST_CASE("Mapper 298 TF1201 acknowledgement does not transfer A to E", "[mapper298]") {
    auto mapper = make_board(RamKind::None, MirrorMode::Vertical, 32768U, 8192U);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->reset();
        mapper->write_prg(0xF000U, static_cast<Byte>(latch & 15U));
        mapper->write_prg(0xF002U, static_cast<Byte>(latch >> 4U));
        mapper->write_prg(0xF001U, 6U);
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF003U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        for (unsigned cycle = 0U; cycle < 255U - latch; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        INFO("latch=" << latch);
        REQUIRE(mapper->irq_pending());
    }

    mapper->write_prg(0xF001U, 1U);
    mapper->write_prg(0xF003U, 0U);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xF001U, 6U);
    mapper->clock_cpu();
    mapper->write_prg(0xF003U, 0U);
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 298 VRC IRQ scanline prescaler and control-write state match pinned references",
          "[mapper298]") {
    auto mapper = make_board(RamKind::None, MirrorMode::Vertical, 32768U, 8192U);
    mapper->write_prg(0xF000U, 15U);
    mapper->write_prg(0xF002U, 15U);
    mapper->write_prg(0xF001U, 3U);
    for (const unsigned period : {114U, 114U, 113U, 114U, 114U, 113U}) {
        for (unsigned cycle = 1U; cycle < period; ++cycle) {
            mapper->clock_cpu();
        }
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_cpu();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xF003U, 0U);
    }

    mapper->reset();
    mapper->write_prg(0xF001U, 2U);
    for (unsigned cycle = 0U; cycle < 50U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto partial = mapper_state(*mapper);
    mapper->write_prg(0xF001U, 0U);
    const auto disabled = mapper_state(*mapper);
    REQUIRE(disabled[32U] == partial[32U]);
    REQUIRE(disabled[33U] == partial[33U]);
    REQUIRE(disabled[34U] == partial[34U]);
    REQUIRE(disabled[35U] == partial[35U]);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper_state(*mapper) == disabled);

    mapper->write_prg(0xF001U, 2U);
    const auto reenabled = mapper_state(*mapper);
    REQUIRE(reenabled[33U] == 0U);
    std::uint16_t reset_prescaler = 0U;
    std::memcpy(&reset_prescaler, reenabled.data() + 34U, sizeof(reset_prescaler));
    REQUIRE(reset_prescaler == 341U);
    const auto before_ack = mapper_state(*mapper);
    mapper->write_prg(0xF003U, 0U);
    REQUIRE(mapper_state(*mapper) == before_ack);
}

TEST_CASE("Mapper 298 snapshots are bounded ROM-free and reject malformed loads atomically",
          "[mapper298]") {
    auto mapper = make_board(RamKind::NonVolatile);
    mapper->write_prg(0x9001U, 1U);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0x8000U, 9U);
    mapper->write_prg(0xA000U, 13U);
    select_chr_bank(*mapper, 3U, 321U);
    mapper->write_prg(0x9000U, 3U);
    mapper->write_prg(0xF000U, 3U);
    mapper->write_prg(0xF002U, 10U);
    mapper->write_prg(0xF001U, 2U);
    for (unsigned cycle = 0U; cycle < 50U; ++cycle) {
        mapper->clock_cpu();
    }
    const auto saved = mapper_state(*mapper);
    REQUIRE(saved.size() == 2086U);

    auto without_ram = make_board();
    REQUIRE(mapper_state(*without_ram).size() == 38U);
    auto different_rom = make_board(RamKind::None, MirrorMode::Vertical, 262144U, 524288U, false);
    REQUIRE(mapper_state(*without_ram) == mapper_state(*different_rom));

    mapper->power_on();
    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(mapper_state(*mapper) == saved);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(*mapper) == saved);
    }

    for (const auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 13>{{{0U, 0U},
                                                                                    {4U, 0U},
                                                                                    {8U, 0U},
                                                                                    {10U, 3U},
                                                                                    {11U, 2U},
                                                                                    {12U, 32U},
                                                                                    {13U, 32U},
                                                                                    {30U, 2U},
                                                                                    {30U, 5U},
                                                                                    {31U, 4U},
                                                                                    {36U, 8U},
                                                                                    {37U, 2U},
                                                                                    {37U, 1U}}}) {
        auto bad = saved;
        if (offset == 37U && value == 1U) {
            bad[36U] = 0U;
        }
        bad[offset] = value;
        INFO("offset=" << offset << " value=" << static_cast<unsigned>(value));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(*mapper) == saved);
    }

    auto bad_chr = saved;
    const std::uint16_t invalid_chr = 512U;
    std::memcpy(bad_chr.data() + 14U, &invalid_chr, sizeof(invalid_chr));
    StateReader chr_reader(bad_chr);
    mapper->load_state(chr_reader);
    REQUIRE_FALSE(chr_reader.ok());
    REQUIRE(mapper_state(*mapper) == saved);

    for (const std::uint16_t prescaler : {0U, 342U, 65535U}) {
        auto bad = saved;
        std::memcpy(bad.data() + 34U, &prescaler, sizeof(prescaler));
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(*mapper) == saved);
    }

    for (const RamKind ram : {RamKind::None, RamKind::Volatile, RamKind::NonVolatile}) {
        auto other = make_board(ram, MirrorMode::Horizontal, 32768U, 8192U);
        const auto before = mapper_state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(mapper_state(*other) == before);
    }
}

TEST_CASE("Mapper 298 reset restores deterministic defaults without clearing soft RAM",
          "[mapper298]") {
    auto mapper = make_board(RamKind::Volatile);
    mapper->write_prg(0x9001U, 1U);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0x8000U, 9U);
    mapper->write_prg(0xA000U, 11U);
    select_chr_bank(*mapper, 5U, 300U);
    mapper->write_prg(0x9000U, 2U);
    mapper->write_prg(0xF001U, 6U);
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
    }
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->read_prg(0xA000U) == prg_cell(0U, 0U));
    REQUIRE(mapper->read_prg(0xC000U) == prg_cell(30U, 0U));
    REQUIRE(mapper->read_prg(0xE000U) == prg_cell(31U, 0U));
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) == chr_cell(0U, 0U));
    }
    mapper->write_prg(0x9001U, 1U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
}

} // namespace
