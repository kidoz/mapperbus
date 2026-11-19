#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/mappers/mapper371.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kFirstChipSize = 0x010000UZ;
constexpr std::size_t kPrgSize = 0x090000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kCartridgeRamSize = 0x002000UZ;
constexpr std::size_t kMainOffset = 10UZ;
constexpr std::size_t kAuxiliaryOffset = 11UZ;
constexpr std::size_t kPinsOffset = 12UZ;
constexpr std::size_t kTapeOffset = 13UZ;
constexpr std::size_t kPrgRamOffset = 14UZ;
constexpr std::size_t kChrRamOffset = kPrgRamOffset + kCartridgeRamSize;
constexpr std::size_t kStateSize = kChrRamOffset + kCartridgeRamSize + 16U;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 371U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kCartridgeRamSize;
    result.chr_ram_size = kCartridgeRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 13U) * 37U +
                             (offset >> 16U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper371> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper371::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper371>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_main(Mapper371& mapper, Byte value) {
    mapper.write_expansion(0x5000U, value);
}

void write_auxiliary(Mapper371& mapper, Byte value) {
    mapper.write_expansion(0x5100U, value);
}

std::optional<std::size_t> expected_prg_offset(Byte main, Byte auxiliary, Address addr) {
    const auto source = (main >> 4U) & 7U;
    const auto within_bank = static_cast<std::size_t>(addr & 0x3FFFU);
    if (source == 0U) {
        const unsigned bank = addr < 0xC000U ? main & 3U : 3U;
        return bank * kPrgBankSize + within_bank;
    }
    if (source == 5U) {
        const unsigned bank = (main & 0x0FU) | ((auxiliary & 1U) != 0U ? 0x10U : 0U);
        return kFirstChipSize + bank * kPrgBankSize + within_bank;
    }
    return std::nullopt;
}

Address expected_chr_address(Byte main, Byte pins, Address addr) {
    addr &= 0x1FFFU;
    if ((main & 0x80U) == 0U) {
        return addr;
    }
    return static_cast<Address>((addr & 0x0FF7U) | ((pins & 1U) != 0U ? 0x0008U : 0U) |
                                ((pins & 2U) != 0U ? 0x1000U : 0U));
}

TEST_CASE("Mapper 371 validates exact NES 2.0 ROM RAM and CIRAM geometry", "[mapper371]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper371::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (const unsigned id : {0U, 4U, 257U, 369U, 370U, 372U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper371::validate_geometry(h, kPrgSize, 0U));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper371::validate_geometry(h, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper371::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, kFirstChipSize - 1, kPrgSize - 1U, kPrgSize + 1U, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper371::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, kPrgSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper371::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper371::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 9U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0U;
            break;
        case 2U:
            h.prg_ram_size = 0x4000U;
            break;
        case 3U:
            h.prg_nvram_size = kCartridgeRamSize;
            break;
        case 4U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = kCartridgeRamSize;
            h.has_battery = true;
            break;
        case 5U:
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 0x4000U;
            break;
        case 7U:
            h.chr_nvram_size = kCartridgeRamSize;
            break;
        default:
            h.chr_ram_size = 0U;
            h.chr_nvram_size = kCartridgeRamSize;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper371::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 371 decodes FF00 register aliases and drives tape input only on D2",
          "[mapper371][bus]") {
    auto mapper = board();
    mapper->set_tape_input(true);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        const auto decoded = raw & 0xFF00U;
        const bool main = decoded == 0x5000U;
        const bool auxiliary = decoded == 0x5100U;
        const bool tape = decoded == 0x5500U;
        mapper->reset();
        mapper->write_expansion(addr, 0xA5U);
        CAPTURE(raw, main, auxiliary, tape);
        REQUIRE(state(*mapper)[kMainOffset] == (main ? 0xA5U : 0U));
        REQUIRE(mapper->maps_expansion(addr) == (main || auxiliary || tape));
        REQUIRE(mapper->expansion_read_mask(addr) == (tape ? 0x04U : 0U));
        REQUIRE(mapper->read_expansion(addr) == (tape ? 0x04U : 0U));

        mapper->reset();
        mapper->write_expansion(addr, 0xA5U);
        REQUIRE(state(*mapper)[kAuxiliaryOffset] == (auxiliary ? 1U : 3U));
    }

    mapper->reset();
    mapper->write_prg(0x5000U, 0xA5U);
    mapper->write_prg_cpu(0x5100U, 0xA5U, false);
    mapper->write_expansion(0x5500U, 0xA5U);
    REQUIRE(state(*mapper)[kMainOffset] == 0U);
    REQUIRE(state(*mapper)[kAuxiliaryOffset] == 3U);
    mapper->set_tape_input(false);
    REQUIRE(mapper->read_expansion(0x55FFU) == 0U);
    mapper->set_tape_input(true);
    REQUIRE(mapper->read_expansion(0x5500U) == 0x04U);
}

TEST_CASE("Mapper 371 maps both physical PRG chips and leaves other sources undriven",
          "[mapper371][banking][bus]") {
    auto mapper = board();
    constexpr std::array addresses{
        0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xC123U, 0xD0FFU, 0xD200U, 0xFFFFU};
    for (unsigned value = 0U; value < 256U; ++value) {
        const auto main = static_cast<Byte>(value);
        for (const Byte auxiliary : {0U, 1U}) {
            write_main(*mapper, main);
            write_auxiliary(*mapper, auxiliary);
            REQUIRE(state(*mapper)[kMainOffset] == main);
            REQUIRE(state(*mapper)[kAuxiliaryOffset] == auxiliary);
            for (const auto raw : addresses) {
                const auto addr = static_cast<Address>(raw);
                const auto expected = expected_prg_offset(main, auxiliary, addr);
                CAPTURE(value, auxiliary, addr, expected);
                REQUIRE(mapper->maps_prg(addr) == expected.has_value());
                REQUIRE(mapper->prg_read_mask(addr) == (expected ? 0xFFU : 0U));
                REQUIRE(mapper->read_prg(addr) == (expected ? cell(*expected) : 0U));
            }
        }
    }
    const auto before = state(*mapper);
    mapper->write_prg(0x8000U, 0xA5U);
    mapper->write_prg(0xFFFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 371 provides unbanked volatile PRG and CHR RAM", "[mapper371][ram][bus]") {
    auto mapper = board();
    for (std::size_t offset = 0U; offset < kCartridgeRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), cell(offset, 0x37U));
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0xA6U));
    }
    for (std::size_t offset = 0U; offset < kCartridgeRamSize; ++offset) {
        const auto prg_addr = static_cast<Address>(0x6000U + offset);
        const auto chr_addr = static_cast<Address>(offset);
        CAPTURE(offset);
        REQUIRE(mapper->read_prg(prg_addr) == cell(offset, 0x37U));
        REQUIRE(mapper->maps_prg(prg_addr));
        REQUIRE(mapper->prg_read_mask(prg_addr) == 0xFFU);
        REQUIRE(mapper->read_chr(chr_addr) == cell(offset, 0xA6U));
        REQUIRE(mapper->read_chr(chr_addr + 0x2000U) == cell(offset, 0xA6U));
    }
    for (const Address addr : {0U, 0x4020U, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 371 auxiliary D1 controls ordinary CIRAM mirroring", "[mapper371][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        write_auxiliary(*mapper, static_cast<Byte>(value));
        const auto expected = (value & 2U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        CAPTURE(value, expected);
        REQUIRE(mapper->mirror_mode() == expected);
        REQUIRE(state(*mapper)[kAuxiliaryOffset] == (value & 3U));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 371 latches A0 and A9 only from nametable reads for 1bpp CHR",
          "[mapper371][banking][ppu]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        mapper->write_chr(addr, cell(addr));
    }
    write_main(*mapper, 0x80U);
    Byte expected_pins = 0U;
    for (unsigned raw = 0U; raw <= 0x3FFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        if (raw >= 0x2000U && raw < 0x3F00U && (raw & 0x03FFU) < 0x03C0U) {
            expected_pins = static_cast<Byte>((raw & 1U) | ((raw >> 8U) & 2U));
        }
        mapper->observe_ppu_read(addr);
        CAPTURE(raw, expected_pins);
        REQUIRE(state(*mapper)[kPinsOffset] == expected_pins);
    }

    for (Byte pins = 0U; pins < 4U; ++pins) {
        const auto nametable_addr =
            static_cast<Address>(0x2000U | ((pins & 2U) != 0U ? 0x0200U : 0U) | (pins & 1U));
        mapper->observe_ppu_read(nametable_addr);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            const auto mapped = expected_chr_address(0x80U, pins, addr);
            CAPTURE(pins, addr, mapped);
            REQUIRE(mapper->read_chr(addr) == cell(mapped));
        }
    }
    mapper->write_chr(0U, 0x39U);
    REQUIRE(mapper->read_chr(0x1008U) == 0x39U);
    write_main(*mapper, 0U);
    REQUIRE(mapper->read_chr(0x1008U) == 0x39U);
    REQUIRE(mapper->read_chr(0U) == cell(0U));
    REQUIRE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    const auto before = state(*mapper);
    mapper->observe_ppu_address(0x2201U);
    mapper->observe_ppu_a12(true);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 371 tape output is meaningful only for the first chip", "[mapper371][bus]") {
    auto mapper = board();
    REQUIRE(mapper->tape_output());
    write_auxiliary(*mapper, 2U);
    REQUIRE_FALSE(mapper->tape_output());
    write_auxiliary(*mapper, 3U);
    write_main(*mapper, 0x50U);
    REQUIRE_FALSE(mapper->tape_output());
    write_main(*mapper, 0x70U);
    REQUIRE_FALSE(mapper->tape_output());
    write_main(*mapper, 0U);
    REQUIRE(mapper->tape_output());
}

TEST_CASE("Mapper 371 protection performs the cartridge-local reset only in its decode",
          "[mapper371][reset][bus]") {
    auto mapper = board();
    mapper->set_tape_input(true);
    for (Address offset = 0U; offset < 0x100U; ++offset) {
        mapper->power_on();
        mapper->set_tape_input(true);
        write_main(*mapper, 0xD5U);
        write_auxiliary(*mapper, 0U);
        mapper->observe_ppu_read(0x2201U);
        mapper->write_prg(0x6123U, 0xA7U);
        mapper->write_chr(0x1234U, 0x5CU);
        const auto addr = static_cast<Address>(0xD100U + offset);
        const auto value = mapper->read_prg(addr);
        auto expected = board();
        expected->set_tape_input(true);
        write_main(*expected, 0xD5U);
        expected->observe_ppu_read(0x2201U);
        expected->write_prg(0x6123U, 0xA7U);
        expected->write_chr(0x1234U, 0x5CU);
        expected->reset();
        CAPTURE(offset, value);
        REQUIRE(state(*mapper) == state(*expected));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }

    const auto unchanged = [&](Byte main, Byte auxiliary, Address addr) {
        mapper->power_on();
        write_main(*mapper, main);
        write_auxiliary(*mapper, auxiliary);
        const auto before = state(*mapper);
        (void)mapper->read_prg(addr);
        CAPTURE(main, auxiliary, addr);
        REQUIRE(state(*mapper) == before);
    };
    for (const Address addr : {0xD0FFU, 0xD200U}) {
        unchanged(0xD5U, 0U, addr);
    }
    unchanged(0xD5U, 1U, 0xD100U);
    unchanged(0xD5U, 1U, 0xD1FFU);
    unchanged(0x05U, 0U, 0xD100U);
    unchanged(0x75U, 0U, 0xD100U);
    unchanged(0x15U, 0U, 0xD100U);
}

TEST_CASE("Mapper 371 reset restores registers retains RAM and has no IRQ or audio",
          "[mapper371][reset][irq][audio]") {
    auto mapper = board(MirrorMode::Horizontal);
    mapper->set_tape_input(true);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_chr(0x1234U, 0x5CU);
    write_main(*mapper, 0xFFU);
    write_auxiliary(*mapper, 0U);
    mapper->observe_ppu_read(0x2201U);

    mapper->reset();
    REQUIRE(state(*mapper)[kMainOffset] == 0U);
    REQUIRE(state(*mapper)[kAuxiliaryOffset] == 3U);
    REQUIRE(state(*mapper)[kPinsOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA7U);
    REQUIRE(mapper->read_chr(0x1234U) == 0x5CU);
    REQUIRE(mapper->read_expansion(0x5500U) == 0x04U);
    REQUIRE(mapper->tape_output());

    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->read_chr(0x1234U) == 0U);
    REQUIRE(mapper->read_expansion(0x5500U) == 0x04U);

    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->observe_cpu_read(0xD100U, 0xA5U);
    mapper->observe_cpu_write(0x5000U, 0xFFU);
    mapper->write_prg_cpu(0x5000U, 0xFFU, true);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 371 fixed bounded snapshots replay all mutable state without ROM",
          "[mapper371][state]") {
    auto mapper = board(MirrorMode::Vertical, 0U);
    write_main(*mapper, 0xD5U);
    write_auxiliary(*mapper, 1U);
    mapper->observe_ppu_read(0x2201U);
    mapper->set_tape_input(true);
    for (std::size_t offset = 0U; offset < kCartridgeRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), cell(offset, 0x37U));
    }
    write_main(*mapper, 0x55U);
    for (std::size_t offset = 0U; offset < kCartridgeRamSize; ++offset) {
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0xA6U));
    }
    write_main(*mapper, 0xD5U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 16414U);
    REQUIRE(saved.size() < kPrgSize);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->read_expansion(0x5500U) == 0x04U);
    for (const Address addr : {0x8000U, 0x8123U, 0xC000U, 0xD200U, 0xFFFFU}) {
        const auto expected = expected_prg_offset(0xD5U, 1U, addr);
        REQUIRE(expected);
        CAPTURE(addr, *expected);
        REQUIRE(copy->read_prg(addr) == cell(*expected, 0x80U));
    }
    for (std::size_t offset = 0U; offset < kCartridgeRamSize; ++offset) {
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + offset)) == cell(offset, 0x37U));
        REQUIRE(copy->read_chr(static_cast<Address>(offset)) ==
                cell(expected_chr_address(0xD5U, 3U, static_cast<Address>(offset)), 0xA6U));
    }

    mapper->observe_ppu_read(0x2000U);
    copy->observe_ppu_read(0x2000U);
    write_main(*mapper, 0U);
    write_main(*copy, 0U);
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 371 rejects truncated malformed and incompatible states atomically",
          "[mapper371][state]") {
    auto mapper = board();
    write_main(*mapper, 0xD5U);
    write_auxiliary(*mapper, 1U);
    mapper->observe_ppu_read(0x2201U);
    mapper->set_tape_input(true);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_chr(0x1234U, 0x5CU);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    const auto replace = [&](std::size_t offset, Byte value) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kMainOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte value : {4U, 0x80U, 0xFFU}) {
        replace(kAuxiliaryOffset, value);
        replace(kPinsOffset, value);
    }
    for (const Byte value : {2U, 3U, 0x80U, 0xFFU}) {
        replace(kTapeOffset, value);
    }

    Mapper371 wrong_prg(header(), rom(kFirstChipSize), std::vector<Byte>{});
    const auto before = state(wrong_prg);
    StateReader wrong_reader(saved);
    wrong_prg.load_state(wrong_reader);
    REQUIRE_FALSE(wrong_reader.ok());
    REQUIRE(state(wrong_prg) == before);
}

} // namespace
