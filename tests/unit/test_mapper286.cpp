#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/mappers/mapper286_benshieng.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::size_t kPrgSize = 0x20000U;
constexpr std::size_t kChrSize = 0x8000U;

INesHeader header(bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 286U;
    h.is_nes2 = !legacy;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

Byte prg_byte(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + (offset & 0xFFU) + (offset >> 8U) * 3U);
}

Byte chr_byte(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 53U + (offset & 0xFFU) + (offset >> 8U) * 5U);
}

std::vector<Byte> prg_rom(std::size_t size = kPrgSize) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        bytes[offset] = prg_byte(static_cast<unsigned>(offset / 0x2000U),
                                 static_cast<unsigned>(offset % 0x2000U));
    return bytes;
}

std::vector<Byte> chr_rom(std::size_t size = kChrSize) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset)
        bytes[offset] = chr_byte(static_cast<unsigned>(offset / 0x800U),
                                 static_cast<unsigned>(offset % 0x800U));
    return bytes;
}

std::unique_ptr<BenshiengBs5> board(Byte pads = 0U,
                                    std::size_t prg_size = kPrgSize,
                                    std::size_t chr_size = kChrSize,
                                    bool legacy = false) {
    const auto h = header(legacy);
    REQUIRE(BenshiengBs5::validate_geometry(h, prg_size, chr_size));
    auto mapper = std::make_unique<BenshiengBs5>(h, prg_rom(prg_size), chr_rom(chr_size));
    REQUIRE(mapper->configure_board_jumpers(pads));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

unsigned expected_prg_bank(Address addr, Byte pads) {
    const unsigned bank = addr & 0x0FU;
    const unsigned mask = ((addr >> 4U) & 0x0FU) | (~pads & 0x0FU);
    return bank & mask;
}

void require_prg_bank(Mapper& mapper,
                      unsigned slot,
                      unsigned bank,
                      unsigned offset = 0x155U,
                      unsigned bank_count = 16U) {
    const auto addr = static_cast<Address>(0x8000U + slot * 0x2000U + offset);
    REQUIRE(mapper.read_prg(addr) == prg_byte(bank % bank_count, offset));
}

void require_chr_bank(Mapper& mapper,
                      unsigned slot,
                      unsigned bank,
                      unsigned offset = 0x255U,
                      unsigned bank_count = 16U) {
    const auto addr = static_cast<Address>(slot * 0x800U + offset);
    REQUIRE(mapper.read_chr(addr) == chr_byte(bank % bank_count, offset));
}

TEST_CASE("Mapper 286 powers up with documented PRG banks and deterministic zero controls",
          "[mapper286]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        require_prg_bank(*mapper, slot, 15U);
        require_chr_bank(*mapper, slot, 0U);
    }
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->maps_prg(0x4020U));
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE_FALSE(mapper->maps_prg(0x7FFFU));
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->clock_cpu();
    mapper->clock_irq_counter();
    mapper->clock_audio();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper).size() == 20U);
}

TEST_CASE("Mapper 286 decodes every CHR register alias and ignores write data", "[mapper286]") {
    auto mapper = board();
    std::array<unsigned, 4> banks{};
    for (unsigned raw = 0x8000U; raw < 0xA000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        mapper->write_prg(addr, static_cast<Byte>(~raw));
        banks[(raw >> 8U) & 3U] = raw & 0x0FU;
        for (unsigned slot = 0U; slot < 4U; ++slot)
            require_chr_bank(*mapper, slot, banks[slot]);
    }
    for (Address addr : {0x8000U, 0x81A5U, 0x923FU, 0x9FFFU}) {
        mapper->write_prg(addr, 0U);
        const auto saved = state(*mapper);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(addr, static_cast<Byte>(value));
            REQUIRE(state(*mapper) == saved);
        }
    }
}

TEST_CASE("Mapper 286 applies each enabled solder-pad AND mask to all PRG windows", "[mapper286]") {
    for (unsigned pads = 0U; pads < 16U; ++pads) {
        auto mapper = board(static_cast<Byte>(pads));
        for (unsigned slot = 0U; slot < 4U; ++slot)
            for (unsigned mask = 0U; mask < 16U; ++mask)
                for (unsigned bank = 0U; bank < 16U; ++bank) {
                    CAPTURE(pads, slot, mask, bank);
                    const auto addr =
                        static_cast<Address>(0xA000U + slot * 0x100U + mask * 0x10U + bank);
                    mapper->write_prg(addr, static_cast<Byte>(addr ^ 0xA5U));
                    for (unsigned window = 0U; window < 4U; ++window) {
                        const auto expected =
                            window == slot ? expected_prg_bank(addr, static_cast<Byte>(pads)) : 15U;
                        require_prg_bank(*mapper, window, expected);
                    }
                }
    }
}

TEST_CASE("Mapper 286 decodes every PRG alias with all mask pads populated", "[mapper286]") {
    auto mapper = board(15U);
    std::array<unsigned, 4> banks{15U, 15U, 15U, 15U};
    for (unsigned raw = 0xA000U; raw < 0xC000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        mapper->write_prg(addr, static_cast<Byte>(raw));
        banks[(raw >> 8U) & 3U] = expected_prg_bank(addr, 15U);
        for (unsigned slot = 0U; slot < 4U; ++slot)
            require_prg_bank(*mapper, slot, banks[slot]);
    }
}

TEST_CASE("Mapper 286 nametable arrangement uses only C000-DFFF address bit zero", "[mapper286]") {
    auto mapper = board();
    for (unsigned raw = 0xC000U; raw < 0xE000U; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(~raw));
        REQUIRE(mapper->mirror_mode() ==
                ((raw & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }
    mapper->write_prg(0xC001U, 0U);
    const auto saved = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        if (raw >= 0x8000U && raw < 0xE000U)
            continue;
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
        REQUIRE(state(*mapper) == saved);
    }
}

TEST_CASE("Mapper 286 CHR ROM is read-only and all fitted sizes wrap after bank decoding",
          "[mapper286]") {
    constexpr std::size_t prg_size = 9U * 0x2000U;
    constexpr std::size_t chr_size = 10U * 0x800U;
    auto mapper = board(0U, prg_size, chr_size);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_prg(static_cast<Address>(0xA00FU + slot * 0x100U), 0U);
        mapper->write_prg(static_cast<Address>(0x800FU + slot * 0x100U), 0U);
        require_prg_bank(*mapper, slot, 15U, 0x155U, 9U);
        require_chr_bank(*mapper, slot, 15U, 0x255U, 10U);
    }
    for (unsigned bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(static_cast<Address>(0x8000U + bank), 0U);
        for (unsigned offset = 0U; offset < 0x800U; ++offset) {
            const auto addr = static_cast<Address>(offset);
            const auto expected = chr_byte(bank % 10U, offset);
            REQUIRE(mapper->read_chr(addr) == expected);
            mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == expected);
        }
    }
}

TEST_CASE("Mapper 286 reset restores initial banks and CIRAM without changing ROM or pads",
          "[mapper286]") {
    auto mapper = board(10U);
    mapper->write_prg(0x8007U, 0U);
    mapper->write_prg(0xA3C9U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->reset();
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        require_prg_bank(*mapper, slot, 15U);
        require_chr_bank(*mapper, slot, 0U);
    }
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0x92F3U, 0U);
    mapper->write_prg(0xA7F5U, 0U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->power_on();
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        require_prg_bank(*mapper, slot, 15U);
        require_chr_bank(*mapper, slot, 0U);
    }
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0xA005U, 0U);
    require_prg_bank(*mapper, 0U, expected_prg_bank(0xA005U, 10U));
}

TEST_CASE("Mapper 286 pad profiles validate atomically", "[mapper286]") {
    auto mapper = board();
    mapper->write_prg(0x8E6BU, 0U);
    mapper->write_prg(0xB57DU, 0U);
    mapper->write_prg(0xC001U, 0U);
    for (unsigned pads = 0U; pads < 16U; ++pads) {
        REQUIRE(mapper->configure_board_profile("mask-" + std::to_string(pads)));
        mapper->write_prg(0xAF7DU, 0U);
        require_prg_bank(*mapper, 3U, expected_prg_bank(0xAF7DU, static_cast<Byte>(pads)));
    }
    const auto configured = state(*mapper);
    for (unsigned invalid = 16U; invalid < 256U; ++invalid) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(static_cast<Byte>(invalid)));
        REQUIRE(state(*mapper) == configured);
    }
    for (std::string_view bad : {"",
                                 "mask",
                                 "mask-",
                                 "mask--1",
                                 "mask-+1",
                                 "mask-16",
                                 "mask-1x",
                                 "menu-1",
                                 "mask-42949672960"}) {
        REQUIRE_FALSE(mapper->configure_board_profile(bad));
        REQUIRE(state(*mapper) == configured);
    }
}

TEST_CASE("Mapper 286 state replay is deterministic and rejection is atomic",
          "[mapper286][state]") {
    auto mapper = board(13U);
    mapper->write_prg(0x8123U, 0U);
    mapper->write_prg(0x9E4AU, 0U);
    mapper->write_prg(0xA179U, 0U);
    mapper->write_prg(0xBCE5U, 0U);
    mapper->write_prg(0xD123U, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 20U);
    auto copy = board(13U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (Address addr : {0x8765U, 0x9F09U, 0xA2D7U, 0xB431U, 0xC000U, 0xDFFFU}) {
        mapper->write_prg(addr, 0x5AU);
        copy->write_prg(addr, 0xA5U);
        REQUIRE(state(*mapper) == state(*copy));
        for (Address read : {0x8000U, 0xA155U, 0xC355U, 0xFFFFU})
            REQUIRE(mapper->read_prg(read) == copy->read_prg(read));
        for (Address read : {0x0000U, 0x0855U, 0x1255U, 0x1FFFU})
            REQUIRE(mapper->read_chr(read) == copy->read_chr(read));
    }
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        const auto before = state(*mapper);
        StateReader truncated{std::span(saved).first(size)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == before);
    }
    auto stable = board(13U);
    StateReader stable_reader(saved);
    stable->load_state(stable_reader);
    REQUIRE(stable_reader.ok());
    std::vector<std::pair<std::size_t, Byte>> corruptions{{0U, 0U},
                                                          {1U, 0U},
                                                          {2U, 12U},
                                                          {3U, 2U},
                                                          {4U, 16U},
                                                          {7U, 255U},
                                                          {8U, 16U},
                                                          {11U, 255U},
                                                          {12U, 1U},
                                                          {15U, 255U},
                                                          {16U, 1U},
                                                          {19U, 255U}};
    for (const auto [offset, value] : corruptions) {
        auto bad = saved;
        bad[offset] = value;
        CAPTURE(offset, value);
        StateReader invalid(bad);
        stable->load_state(invalid);
        REQUIRE_FALSE(invalid.ok());
        REQUIRE(state(*stable) == saved);
    }
    auto wrong_pads = board(12U);
    const auto pads_before = state(*wrong_pads);
    StateReader pad_reader(saved);
    wrong_pads->load_state(pad_reader);
    REQUIRE_FALSE(pad_reader.ok());
    REQUIRE(state(*wrong_pads) == pads_before);
    auto wrong_geometry = board(13U, 0x10000U, 0x4000U);
    const auto geometry_before = state(*wrong_geometry);
    StateReader geometry_reader(saved);
    wrong_geometry->load_state(geometry_reader);
    REQUIRE_FALSE(geometry_reader.ok());
    REQUIRE(state(*wrong_geometry) == geometry_before);
}

TEST_CASE("Mapper 286 rejects unsupported geometry memory and submappers", "[mapper286]") {
    for (unsigned kind = 0U; kind < 22U; ++kind) {
        auto h = header();
        std::size_t p = kPrgSize, c = kChrSize;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.submapper = 15U;
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
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 9U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 10U:
            p = 0U;
            break;
        case 11U:
            p = 0x4000U;
            break;
        case 12U:
            p = 0x22000U;
            break;
        case 13U:
            p = 0x21000U;
            break;
        case 14U:
            --p;
            break;
        case 15U:
            c = 0U;
            break;
        case 16U:
            c = 0x1000U;
            break;
        case 17U:
            c = 0x8800U;
            break;
        case 18U:
            c = 0x8100U;
            break;
        case 19U:
            --c;
            break;
        case 20U:
            h = header(true);
            h.has_battery = true;
            break;
        case 21U:
            h = header(true);
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(BenshiengBs5::validate_geometry(h, p, c));
    }
    for (std::size_t p = 0x8000U; p <= 0x20000U; p += 0x2000U)
        for (std::size_t c = 0x2000U; c <= 0x8000U; c += 0x800U)
            REQUIRE(BenshiengBs5::validate_geometry(header(), p, c));
    REQUIRE(BenshiengBs5::validate_geometry(header(true), kPrgSize, kChrSize));
}

} // namespace
