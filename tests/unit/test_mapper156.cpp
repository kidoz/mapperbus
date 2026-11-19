#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper156_daou.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kPrgBytes = 256 * 1024;
constexpr std::size_t kChrBytes = 512 * 1024;
constexpr std::size_t kStateBytes = 8212U;

INesHeader header(bool legacy = false, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader h{};
    h.mapper_number = 156U;
    h.is_nes2 = !legacy;
    h.mirror_mode = mirror;
    h.prg_ram_size = 8192U;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 41U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset = 0U) {
    return static_cast<Byte>(bank * 73U + (bank >> 8U) * 19U + offset + (offset >> 4U));
}

std::vector<Byte> patterned_rom(std::size_t size, std::size_t bank_size, bool chr) {
    std::vector<Byte> rom(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        const auto bank = static_cast<unsigned>(offset / bank_size);
        const auto within = static_cast<unsigned>(offset % bank_size);
        rom[offset] = chr ? chr_cell(bank, within) : prg_cell(bank, within);
    }
    return rom;
}

std::unique_ptr<DaouMapper156> board(const INesHeader& h = header(),
                                     std::size_t prg_size = kPrgBytes,
                                     std::size_t chr_size = kChrBytes) {
    REQUIRE(DaouMapper156::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<DaouMapper156>(
        h, patterned_rom(prg_size, 16384U, false), patterned_rom(chr_size, 1024U, true));
}

std::vector<Byte> state(const DaouMapper156& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Address chr_register(unsigned slot, bool high = false) {
    return static_cast<Address>(0xC000U + (slot & 3U) + ((slot & 4U) << 1U) + (high ? 4U : 0U));
}

void set_chr_bank(DaouMapper156& mapper, unsigned slot, unsigned bank) {
    mapper.write_prg(chr_register(slot), static_cast<Byte>(bank));
    mapper.write_prg(chr_register(slot, true), static_cast<Byte>(bank >> 8U));
}

bool is_register(Address addr) {
    return (addr >= 0xC000U && addr <= 0xC010U) || addr == 0xC014U;
}

TEST_CASE("Mapper 156 maps volatile WRAM and switchable PRG with a fixed last bank",
          "[mapper156]") {
    auto mapper = board();
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
    REQUIRE(mapper->read_prg(0xBFFFU) == prg_cell(0U, 0x3FFFU));
    REQUIRE(mapper->read_prg(0xC000U) == prg_cell(15U));
    REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(15U, 0x3FFFU));
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned addr = 0U; addr < 0x10000U; addr += 0x101U) {
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(addr)) == 0xFFU);
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xC010U, static_cast<Byte>(value));
        const unsigned bank = value & 15U;
        for (unsigned offset : {0U, 1U, 0x1234U, 0x3FFFU})
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    prg_cell(bank, offset));
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(15U));
    }

    mapper->write_prg(0xC010U, 7U);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        const auto bank = addr < 0xC000U ? 7U : 15U;
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == prg_cell(bank, addr & 0x3FFFU));
    }

    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 13U));
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == static_cast<Byte>(addr * 13U));
    }
}

TEST_CASE("Mapper 156 applies bank pins before wrapping smaller fitted ROMs", "[mapper156]") {
    auto mapper = board(header(), 49152U, 12288U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xC010U, static_cast<Byte>(value));
        REQUIRE(mapper->read_prg(0x8123U) == prg_cell((value & 15U) % 3U, 0x123U));
        REQUIRE(mapper->read_prg(0xE123U) == prg_cell(2U, 0x2123U));
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        set_chr_bank(*mapper, 0U, value);
        REQUIRE(mapper->read_chr(0x23U) == chr_cell(value % 12U, 0x23U));
    }
    set_chr_bank(*mapper, 0U, 0x1FFU);
    REQUIRE(mapper->read_chr(0x23U) == chr_cell(7U, 0x23U));
}

TEST_CASE("Mapper 156 decodes exactly the eighteen documented register addresses", "[mapper156]") {
    auto mapper = board();
    mapper->write_prg(0xC010U, 9U);
    mapper->write_prg(0xC014U, 1U);
    for (unsigned slot = 0U; slot < 8U; ++slot)
        set_chr_bank(*mapper, slot, 0x100U + slot);
    const auto before = state(*mapper);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        if (!is_register(static_cast<Address>(addr)))
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 29U));
    }
    REQUIRE(state(*mapper) == before);

    for (Address addr : {0xC011U, 0xC012U, 0xC013U, 0xC015U, 0xD000U, 0x8000U}) {
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 156 CHR registers retain all nine bits in every 1 KiB slot", "[mapper156]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const unsigned bank = 0x120U + slot;
        set_chr_bank(*mapper, slot, bank);
        const auto base = static_cast<Address>(slot * 1024U);
        REQUIRE(mapper->read_chr(base) == chr_cell(bank));
        REQUIRE(mapper->read_chr(static_cast<Address>(base + 0x3FFU)) == chr_cell(bank, 0x3FFU));
    }
    for (unsigned slot = 0U; slot < 8U; ++slot)
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U)) == chr_cell(0x120U + slot));

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xC004U, static_cast<Byte>(value));
        mapper->write_prg(0xC000U, static_cast<Byte>(value ^ 0xA5U));
        const unsigned bank = (value & 1U) * 256U + (value ^ 0xA5U);
        REQUIRE(mapper->read_chr(0U) == chr_cell(bank));
        mapper->write_prg(0xC004U, static_cast<Byte>(value));
        REQUIRE(mapper->read_chr(0U) == chr_cell(bank));
    }

    const auto before = state(*mapper);
    mapper->write_chr(0U, 0xA7U);
    mapper->write_chr(0x1FFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 156 mirroring uses two data bits and one-screen page zero", "[mapper156]") {
    auto mapper = board();
    constexpr std::array kModes = {MirrorMode::Vertical,
                                   MirrorMode::Horizontal,
                                   MirrorMode::SingleLower,
                                   MirrorMode::SingleLower};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xC014U, static_cast<Byte>(value));
        REQUIRE(mapper->mirror_mode() == kModes[value & 3U]);
        REQUIRE_FALSE(mapper->ciram_address(0x2000U));
        REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    }
}

TEST_CASE("Mapper 156 reset restores registers while cold power clears volatile WRAM",
          "[mapper156]") {
    for (bool legacy : {false, true})
        for (MirrorMode initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto mapper = board(header(legacy, initial));
            mapper->write_prg(0x6000U, 0xA7U);
            mapper->write_prg(0x7FFFU, 0x5AU);
            mapper->write_prg(0xC010U, 12U);
            mapper->write_prg(0xC014U, 1U);
            set_chr_bank(*mapper, 3U, 0x1ABU);
            mapper->reset();
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
            REQUIRE(mapper->read_prg(0xC000U) == prg_cell(15U));
            REQUIRE(mapper->read_chr(0x0C00U) == chr_cell(0U));
            REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
            REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
            REQUIRE(mapper->read_prg(0x7FFFU) == 0x5AU);
            mapper->power_on();
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_prg(0x7FFFU) == 0U);
        }
}

TEST_CASE("Mapper 156 geometry validation enforces ROM and native memory declarations",
          "[mapper156]") {
    for (bool legacy : {false, true})
        for (std::size_t prg_size : {32768U, 49152U, 262144U})
            for (std::size_t chr_size : {8192U, 12288U, 524288U})
                REQUIRE(DaouMapper156::validate_geometry(header(legacy), prg_size, chr_size));

    for (std::size_t size : {0U, 8192U, 16384U, 32767U, 32769U, 262145U, 524288U})
        REQUIRE_FALSE(DaouMapper156::validate_geometry(header(), size, kChrBytes));
    for (std::size_t size : {0U, 1U, 1024U, 8191U, 8193U, 524289U, 1048576U})
        REQUIRE_FALSE(DaouMapper156::validate_geometry(header(), kPrgBytes, size));

    for (unsigned kind = 0U; kind < 10U; ++kind) {
        auto h = header();
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 4096U;
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
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        }
        INFO(kind);
        REQUIRE_FALSE(DaouMapper156::validate_geometry(h, kPrgBytes, kChrBytes));
    }
    auto legacy = header(true);
    legacy.has_battery = true;
    REQUIRE_FALSE(DaouMapper156::validate_geometry(legacy, kPrgBytes, kChrBytes));
    legacy = header(true);
    legacy.mirror_mode = MirrorMode::FourScreen;
    REQUIRE_FALSE(DaouMapper156::validate_geometry(legacy, kPrgBytes, kChrBytes));
}

TEST_CASE("Mapper 156 snapshots replay all mutable state without serializing ROM",
          "[mapper156][state]") {
    auto mapper = board();
    mapper->write_prg(0xC010U, 0xABU);
    mapper->write_prg(0xC014U, 0xFEU);
    for (unsigned slot = 0U; slot < 8U; ++slot)
        set_chr_bank(*mapper, slot, 0x100U + slot * 7U);
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr)
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 7U + 3U));
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateBytes);

    auto copy = board();
    StateReader copy_reader(saved);
    copy->load_state(copy_reader);
    REQUIRE(copy_reader.ok());
    REQUIRE(copy_reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8123U) == prg_cell(11U, 0x123U));
    REQUIRE(copy->read_prg(0xE123U) == prg_cell(15U, 0x2123U));
    REQUIRE(copy->mirror_mode() == MirrorMode::SingleLower);
    for (unsigned slot = 0U; slot < 8U; ++slot)
        REQUIRE(copy->read_chr(static_cast<Address>(slot * 1024U + 0x23U)) ==
                chr_cell(0x100U + slot * 7U, 0x23U));

    mapper->power_on();
    mapper->write_prg(0x6123U, 0xFFU);
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0x6123U * 7U + 3U));
}

TEST_CASE("Mapper 156 rejects truncated and invalid snapshots atomically", "[mapper156][state]") {
    auto mapper = board();
    mapper->write_prg(0xC010U, 10U);
    mapper->write_prg(0xC014U, 1U);
    set_chr_bank(*mapper, 0U, 0x123U);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0x7FFFU, 0x5AU);
    const auto saved = state(*mapper);

    for (std::size_t length : {0UZ, 1UZ, 2UZ, 17UZ, 18UZ, 19UZ, 20UZ, 100UZ, saved.size() - 1U}) {
        StateReader reader{std::span(saved).first(length)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (const auto& [offset, value] : std::array<std::pair<std::size_t, Byte>, 5>{{
             {1U, 0U},
             {3U, 2U},
             {18U, 16U},
             {19U, 4U},
             {19U, 0xFFU},
         }}) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
}

} // namespace
