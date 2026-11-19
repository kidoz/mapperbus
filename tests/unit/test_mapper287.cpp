#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper287.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::size_t kPrgSize = 512U * 1024U;
constexpr std::size_t kChrSize = 512U * 1024U;

INesHeader header(bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 287U;
    h.is_nes2 = true;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 37U + (offset >> 8U) + (offset >> 15U));
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 19U + (offset >> 7U) + (offset >> 13U));
}

std::vector<Byte> prg_image(std::size_t size = kPrgSize) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = prg_cell(offset);
    return data;
}

std::vector<Byte> chr_image(std::size_t size = kChrSize) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        data[offset] = chr_cell(offset);
    return data;
}

std::unique_ptr<Mapper287> board(bool vertical = true,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    const auto h = header(vertical);
    REQUIRE(Mapper287::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper287>(h, prg_image(prg_size), chr_image(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void select_bank(Mapper287& mapper, unsigned index, Byte value, bool prg_mode = false) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | (prg_mode ? 0x40U : 0U)));
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 287 powers on with bounded ROM-only MMC3 state", "[mapper287]") {
    for (bool vertical : {false, true}) {
        auto mapper = board(vertical);
        REQUIRE(state(*mapper).size() == 25U);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(mapper->read_prg(0x8123U) == prg_cell(0x123U));
        REQUIRE(mapper->read_prg(0xA123U) == prg_cell(0x123U));
        REQUIRE(mapper->read_prg(0xC123U) == prg_cell(14U * 8192U + 0x123U));
        REQUIRE(mapper->read_prg(0xE123U) == prg_cell(15U * 8192U + 0x123U));
        for (Address addr = 0U; addr < 8192U; ++addr) {
            INFO("CHR address=" << addr);
            REQUIRE(mapper->read_chr(addr) ==
                    chr_cell(addr < 0x1000U ? addr & 0x7FFU : addr & 0x3FFU));
        }
        REQUIRE_FALSE(mapper->configure_board_jumpers(2U));
        REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    }
}

TEST_CASE("Mapper 287 decodes every CPU register alias exactly", "[mapper287]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->power_on();
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        const auto actual = state(*mapper);

        mapper->power_on();
        if (addr >= 0x8000U)
            mapper->write_prg(static_cast<Address>(addr & 0xE001U), 0xA5U);
        else if (addr >= 0x6000U)
            mapper->write_prg(0x6000U, 0xA5U);
        const auto expected = state(*mapper);
        INFO("address=" << addr);
        REQUIRE(actual == expected);
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x8000U));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 287 outer register obeys both MMC3 WRAM gates", "[mapper287]") {
    auto mapper = board();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->power_on();
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(static_cast<Address>(addr), 0x3FU);
        REQUIRE(state(*mapper)[10U] == 0x3FU);
        mapper->write_prg(static_cast<Address>(addr), 0x15U);
        REQUIRE(state(*mapper)[10U] == 0x15U);

        mapper->write_prg(0xA001U, 0xC0U);
        mapper->write_prg(static_cast<Address>(addr), 0x2AU);
        REQUIRE(state(*mapper)[10U] == 0x15U);
        mapper->write_prg(0xA001U, 0x40U);
        mapper->write_prg(static_cast<Address>(addr), 0x2AU);
        REQUIRE(state(*mapper)[10U] == 0x15U);
        mapper->write_prg(0xA001U, 0x00U);
        mapper->write_prg(static_cast<Address>(addr), 0x2AU);
        REQUIRE(state(*mapper)[10U] == 0x15U);
        mapper->write_prg(0xA001U, 0x80U);
        mapper->write_prg(static_cast<Address>(addr), 0x2AU);
        REQUIRE(state(*mapper)[10U] == 0x2AU);
    }
}

TEST_CASE("Mapper 287 MMC3 PRG banking stays inside each 128 KiB outer bank", "[mapper287]") {
    auto mapper = board();
    constexpr std::array<Byte, 8> values{0U, 1U, 14U, 15U, 16U, 31U, 63U, 255U};
    for (unsigned outer = 0U; outer < 4U; ++outer)
        for (bool mode : {false, true})
            for (Byte r6 : values)
                for (Byte r7 : values) {
                    mapper->write_prg(0x6000U, static_cast<Byte>(outer));
                    select_bank(*mapper, 6U, r6, mode);
                    select_bank(*mapper, 7U, r7, mode);
                    const std::size_t base = outer * 128U * 1024U;
                    const std::array<unsigned, 4> banks{
                        mode ? 14U : static_cast<unsigned>(r6 & 15U),
                        static_cast<unsigned>(r7 & 15U),
                        mode ? static_cast<unsigned>(r6 & 15U) : 14U,
                        15U,
                    };
                    for (unsigned slot = 0U; slot < 4U; ++slot) {
                        const std::size_t offset = base + banks[slot] * 8192U + 0x1A5U;
                        REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * 8192U +
                                                                      0x1A5U)) == prg_cell(offset));
                    }
                }
}

TEST_CASE("Mapper 287 32 KiB mode uses inner bits and DIP-qualified mode aliases", "[mapper287]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 4U; ++outer)
        for (unsigned inner = 0U; inner < 4U; ++inner) {
            select_bank(*mapper, 6U, 7U);
            select_bank(*mapper, 7U, 9U);
            mapper->write_prg(0x6000U, static_cast<Byte>(0x08U | (inner << 4U) | outer));
            const std::size_t base = outer * 128U * 1024U + inner * 32768U;
            for (unsigned offset = 0U; offset < 32768U; offset += 997U)
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                        prg_cell(base + offset));
        }

    REQUIRE(mapper->configure_board_jumpers(0U));
    mapper->write_prg(0x6000U, 0x14U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(7U * 8192U));
    REQUIRE(mapper->configure_board_jumpers(1U));
    mapper->write_prg(0x6000U, 0x14U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(32768U));
    mapper->write_prg(0x6000U, 0x1CU);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(32768U));
    mapper->write_prg(0x6000U, 0x10U);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(7U * 8192U));
}

TEST_CASE("Mapper 287 wraps disconnected ROM pins only after outer decoding", "[mapper287]") {
    auto mapper = board(true, 32768U, 8192U);
    mapper->write_prg(0x6000U, 0x3BU);
    for (Address addr = 0x8000U; addr != 0U; ++addr)
        REQUIRE(mapper->read_prg(addr) ==
                prg_cell((3U * 128U * 1024U + 32768U + (addr & 0x7FFFU)) % 32768U));
    mapper->write_prg(0x6000U, 0x03U);
    select_bank(*mapper, 0U, 0xFFU);
    for (Address addr = 0U; addr < 1024U; ++addr)
        REQUIRE(mapper->read_chr(addr) ==
                chr_cell((3U * 128U * 1024U + 0x7EU * 1024U + (addr & 0x3FFU)) % 8192U));
}

TEST_CASE("Mapper 287 implements all CHR registers pairing inversion and outer banks",
          "[mapper287]") {
    auto mapper = board();
    constexpr std::array<Address, 6> addresses{0U, 0x800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
    for (unsigned reg = 0U; reg < 6U; ++reg)
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x6000U, 2U);
            select_bank(*mapper, reg, static_cast<Byte>(value));
            const unsigned bank = reg < 2U ? (value & 0x7EU) : (value & 0x7FU);
            const std::size_t base = 2U * 128U * 1024U + bank * 1024U;
            for (Address offset = 0U; offset < 1024U; ++offset)
                REQUIRE(mapper->read_chr(addresses[reg] + offset) == chr_cell(base + offset));
        }

    mapper->write_prg(0x6000U, 3U);
    for (unsigned reg = 0U; reg < 6U; ++reg)
        select_bank(*mapper, reg, static_cast<Byte>(4U + reg * 4U + 1U));
    constexpr std::array<unsigned, 8> normal{4U, 5U, 8U, 9U, 13U, 17U, 21U, 25U};
    constexpr std::array<unsigned, 8> inverted{13U, 17U, 21U, 25U, 4U, 5U, 8U, 9U};
    for (bool invert : {false, true}) {
        mapper->write_prg(0x8000U, invert ? 0x80U : 0U);
        for (Address addr = 0U; addr < 8192U; ++addr) {
            const auto bank = (invert ? inverted : normal)[addr >> 10U];
            const std::size_t offset = 3U * 128U * 1024U + bank * 1024U + (addr & 0x3FFU);
            const auto before = mapper->read_chr(addr);
            mapper->write_chr(addr, static_cast<Byte>(before ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == chr_cell(offset));
        }
    }
}

TEST_CASE("Mapper 287 implements MMC3 mirroring and scanline IRQ controls", "[mapper287]") {
    auto mapper = board();
    for (unsigned addr = 0xA000U; addr < 0xC000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->write_prg(static_cast<Address>(addr), 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }

    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_prg(0xC001U, 0xFFU);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 287 reset restores defaults while retaining immutable DIP wiring",
          "[mapper287]") {
    auto mapper = board(false);
    REQUIRE(mapper->configure_board_jumpers(1U));
    mapper->write_prg(0x6000U, 0x3FU);
    select_bank(*mapper, 6U, 9U, true);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    for (bool cold : {false, true}) {
        cold ? mapper->power_on() : mapper->reset();
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
        REQUIRE(mapper->read_chr(0U) == chr_cell(0U));
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0x6000U, 0x04U);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
        const auto before = state(*mapper);
        REQUIRE_FALSE(mapper->configure_board_jumpers(255U));
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 287 rejects invalid geometry and memory declarations", "[mapper287]") {
    for (auto prg : {32768U, 40960U, 131072U, 524288U})
        for (auto chr : {8192U, 9216U, 131072U, 524288U})
            REQUIRE(Mapper287::validate_geometry(header(), prg, chr));

    for (auto prg : {0U, 8192U, 16384U, 32767U, 33792U, 532480U, 1048576U})
        REQUIRE_FALSE(Mapper287::validate_geometry(header(), prg, kChrSize));
    for (auto chr : {0U, 1024U, 8191U, 9000U, 524289U, 1048576U})
        REQUIRE_FALSE(Mapper287::validate_geometry(header(), kPrgSize, chr));

    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper287::validate_geometry(h, kPrgSize, kChrSize));
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper287::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        case 4U:
            h.chr_nvram_size = 8192U;
            break;
        case 5U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        }
        REQUIRE_FALSE(Mapper287::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 287 snapshots replay all latches and reject corruption atomically",
          "[mapper287]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x35U);
    for (unsigned reg = 0U; reg < 8U; ++reg)
        select_bank(*mapper, reg, static_cast<Byte>(reg * 9U + 3U), true);
    mapper->write_prg(0x8000U, 0xC7U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 25U);
    REQUIRE(mapper->irq_pending());

    mapper->power_on();
    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(restore.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_prg(0x8000U) == prg_cell(128U * 1024U + 14U * 8192U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->irq_pending());

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    std::vector<std::pair<std::size_t, Byte>> invalid{
        {0U, saved[0U] ^ 1U},
        {1U, saved[1U] ^ 1U},
        {2U, saved[2U] ^ 1U},
        {6U, saved[6U] ^ 1U},
        {10U, 0x40U},
        {11U, 0x08U},
        {12U, 0x80U},
        {18U, 0x10U},
        {19U, 0x10U},
        {20U, 2U},
        {23U, 0x08U},
        {23U, 0x04U},
        {24U, 0x01U},
    };
    for (const auto& [offset, value] : invalid) {
        auto corrupt = saved;
        corrupt[offset] = value;
        StateReader reader(corrupt);
        mapper->load_state(reader);
        INFO("offset=" << offset << " value=" << value);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto other_dip = board();
    REQUIRE(other_dip->configure_board_jumpers(1U));
    const auto dip_before = state(*other_dip);
    StateReader wrong_dip(saved);
    other_dip->load_state(wrong_dip);
    REQUIRE_FALSE(wrong_dip.ok());
    REQUIRE(state(*other_dip) == dip_before);

    auto other_mirror = board(false);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);

    auto other_geometry = board(true, 32768U, 8192U);
    const auto geometry_before = state(*other_geometry);
    StateReader wrong_geometry(saved);
    other_geometry->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*other_geometry) == geometry_before);
}
} // namespace
