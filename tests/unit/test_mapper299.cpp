#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper299.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kRomSize = 128U * 1024U;

INesHeader make_header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader header{};
    header.mapper_number = 299U;
    header.mirror_mode = mirror;
    header.is_nes2 = true;
    header.prg_rom_banks = 8U;
    header.chr_rom_banks = 16U;
    header.prg_rom_size = kRomSize;
    header.chr_rom_size = kRomSize;
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 61U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 43U + offset + (offset >> 9U));
}

std::unique_ptr<Mapper299> make_board(MirrorMode mirror = MirrorMode::Vertical) {
    std::vector<Byte> prg(kRomSize);
    std::vector<Byte> chr(kRomSize);
    for (std::size_t offset = 0U; offset < kRomSize; ++offset) {
        prg[offset] = prg_cell(offset / 0x8000U, offset % 0x8000U);
        chr[offset] = chr_cell(offset / 0x2000U, offset % 0x2000U);
    }
    return std::make_unique<Mapper299>(make_header(mirror), std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load_valid(Mapper299& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

TEST_CASE("Mapper 299 validates the complete decoded ROM-only geometry", "[mapper299]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical})
        REQUIRE(Mapper299::validate_geometry(make_header(mirror), kRomSize, kRomSize));

    auto invalid = make_header();
    invalid.mapper_number = 298U;
    REQUIRE_FALSE(Mapper299::validate_geometry(invalid, kRomSize, kRomSize));
    invalid = make_header();
    invalid.is_nes2 = false;
    REQUIRE_FALSE(Mapper299::validate_geometry(invalid, kRomSize, kRomSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        invalid = make_header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper299::validate_geometry(invalid, kRomSize, kRomSize));
    }
    for (const std::size_t size : {0U, 1U, 8192U, 32768U, 65536U, 131071U, 131073U, 262144U}) {
        REQUIRE_FALSE(Mapper299::validate_geometry(make_header(), size, kRomSize));
        REQUIRE_FALSE(Mapper299::validate_geometry(make_header(), kRomSize, size));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        invalid = make_header();
        switch (field) {
        case 0:
            invalid.has_battery = true;
            break;
        case 1:
            invalid.prg_ram_size = 8192U;
            break;
        case 2:
            invalid.prg_nvram_size = 8192U;
            break;
        case 3:
            invalid.chr_ram_size = 8192U;
            break;
        default:
            invalid.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper299::validate_geometry(invalid, kRomSize, kRomSize));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        invalid = make_header();
        invalid.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper299::validate_geometry(invalid, kRomSize, kRomSize));
    }
}

TEST_CASE("Mapper 299 decodes every latch bit and all CPU address aliases", "[mapper299]") {
    auto mapper = make_board();
    for (unsigned value = 0U; value < 256U; ++value) {
        const auto address = static_cast<Address>(0x8000U | ((value * 193U) & 0x7FFFU));
        mapper->write_prg(address, static_cast<Byte>(value));
        const unsigned outer = (value >> 4U) & 3U;
        const unsigned inner = value & 3U;
        CAPTURE(value, address, outer, inner);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x80U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        for (const unsigned offset : {0U, 1U, 0x1234U, 0x7FFFU}) {
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    prg_cell(outer, offset));
        }
        for (const unsigned offset : {0U, 1U, 0x0555U, 0x1FFFU}) {
            REQUIRE(mapper->read_chr(static_cast<Address>(offset)) ==
                    chr_cell(outer * 4U + inner, offset));
        }
        REQUIRE(state(*mapper)[10U] == (value & 0xB3U));
    }

    for (unsigned address = 0U; address <= 0xFFFFU; ++address) {
        mapper->power_on();
        mapper->write_prg(static_cast<Address>(address), 0xB3U);
        const bool selected = address >= 0x8000U;
        CAPTURE(address, selected);
        REQUIRE(state(*mapper)[10U] == (selected ? 0xB3U : 0U));
        REQUIRE(mapper->read_prg(0x8123U) == prg_cell(selected ? 3U : 0U, 0x123U));
    }
}

TEST_CASE("Mapper 299 combines outer and inner CHR banks without writable ROM", "[mapper299]") {
    auto mapper = make_board();
    for (unsigned outer = 0U; outer < 4U; ++outer) {
        for (unsigned inner = 0U; inner < 4U; ++inner) {
            mapper->write_prg(0xFFFFU,
                              static_cast<Byte>((outer << 4U) | inner | ((inner & 1U) << 7U)));
            for (unsigned offset = 0U; offset < 0x2000U; ++offset) {
                CAPTURE(outer, inner, offset);
                REQUIRE(mapper->read_chr(static_cast<Address>(offset)) ==
                        chr_cell(outer * 4U + inner, offset));
            }
        }
    }

    const auto before = state(*mapper);
    const auto rom_before = std::vector<Byte>(
        {mapper->read_chr(0U), mapper->read_chr(0x123U), mapper->read_chr(0x1FFFU)});
    for (unsigned offset = 0U; offset < 0x2000U; ++offset)
        mapper->write_chr(static_cast<Address>(offset), static_cast<Byte>(offset ^ 0xA5U));
    REQUIRE(state(*mapper) == before);
    REQUIRE(std::vector<Byte>{mapper->read_chr(0U),
                              mapper->read_chr(0x123U),
                              mapper->read_chr(0x1FFFU)} == rom_before);
}

TEST_CASE("Mapper 299 leaves undriven CPU windows open and exposes no extra hardware",
          "[mapper299]") {
    auto mapper = make_board();
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->maps_expansion(0x5000U));

    const auto initial = state(*mapper);
    for (unsigned address = 0U; address <= 0xFFFFU; ++address) {
        REQUIRE(mapper->maps_prg(static_cast<Address>(address)) == (address >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(address)) == 0xFFU);
    }
    for (const Address address : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
        REQUIRE(mapper->read_prg(address) == 0U);
        mapper->write_prg(address, 0xFFU);
        mapper->write_expansion(address, 0xFFU);
    }
    mapper->set_battery_ram(std::vector<Byte>(kRomSize, 0xA7U));
    mapper->clock_cpu();
    mapper->clock_audio();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->acknowledge_irq();
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 299 distinguishes cold power from an undocumented soft reset", "[mapper299]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = make_board(initial);
        const auto cold = state(*mapper);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->write_prg(0x8000U, 0xF3U);
        const auto selected = state(*mapper);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->reset();
        REQUIRE(state(*mapper) == selected);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U, 0U));
        REQUIRE(mapper->read_chr(0U) == chr_cell(0U, 0U));
    }
}

TEST_CASE("Mapper 299 state is bounded, ROM-free, replayable and atomic on rejection",
          "[mapper299][state]") {
    auto mapper = make_board();
    mapper->write_prg(0x8000U, 0x93U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 11U);

    auto replay = make_board(MirrorMode::Horizontal);
    load_valid(*replay, saved);
    REQUIRE(state(*replay) == saved);
    REQUIRE(replay->mirror_mode() == MirrorMode::Vertical);
    for (const Byte value : {0x00U, 0x31U, 0xA2U, 0xB3U, 0xFFU}) {
        mapper->write_prg(0xFFFFU, value);
        replay->write_prg(0x8000U, value);
        REQUIRE(state(*replay) == state(*mapper));
    }
    load_valid(*replay, saved);
    REQUIRE(state(*replay) == saved);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        replay->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*replay) == saved);
    }
    for (unsigned offset = 0U; offset < 10U; ++offset) {
        auto damaged = saved;
        damaged[offset] ^= 1U;
        StateReader reader(damaged);
        replay->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*replay) == saved);
    }
    for (const Byte reserved : {0x04U, 0x08U, 0x40U}) {
        auto damaged = saved;
        damaged[10U] |= reserved;
        StateReader reader(damaged);
        replay->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*replay) == saved);
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        auto canonical = saved;
        canonical[10U] = static_cast<Byte>(value & 0xB3U);
        load_valid(*replay, canonical);
        REQUIRE(state(*replay) == canonical);
    }
    load_valid(*replay, saved);
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {0xAAU, 0xBBU, 0xCCU});
    StateReader tail_reader(with_tail);
    replay->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*replay) == saved);

    INesHeader other_header = make_header();
    auto other = std::make_unique<Mapper299>(
        other_header, std::vector<Byte>(kRomSize, 0x5AU), std::vector<Byte>(kRomSize, 0xC3U));
    REQUIRE(state(*other) == state(*make_board()));
}

} // namespace
