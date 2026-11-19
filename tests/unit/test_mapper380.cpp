#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper380.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x800FU, 0x8010U, 0x9A05U, 0xBFFFU, 0xC000U, 0xC123U, 0xFFFFU};

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 380U;
    result.is_nes2 = true;
    result.submapper = submapper;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + (offset >> 18U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper380> board(Byte submapper = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte pads = 0U,
                                 Byte salt = 0U) {
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper380::validate_geometry(h, prg_size, 0U));
    auto mapper = std::make_unique<Mapper380>(h, rom(prg_size, salt), std::vector<Byte>{});
    if (submapper == 0U) {
        REQUIRE(mapper->configure_board_jumpers(pads));
    } else {
        REQUIRE_FALSE(mapper->configure_board_jumpers(pads));
    }
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::size_t expected_offset(Address addr, Address latch, Byte submapper, Byte pads) {
    const bool upper = (addr & 0x4000U) != 0U;
    unsigned bank = (latch >> 2U) & 0x1FU;
    if ((latch & 0x0200U) == 0U) {
        if (upper) {
            bank = (bank & 0x18U) | 7U;
        }
    } else if (!upper && (latch & 1U) == 0U) {
        bank &= 0x1EU;
    }
    if (submapper == 1U && upper && (latch & 0x0100U) != 0U) {
        bank |= 0x08U;
    }

    auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    if (submapper == 0U && (latch & 0x0100U) != 0U) {
        offset = (offset & ~0x0FU) | pads;
    }
    return offset;
}

TEST_CASE("Mapper 380 validates both documented NES 2.0 board populations", "[mapper380]") {
    for (const Byte submapper : {0U, 1U}) {
        for (const std::size_t prg_size : {0x040000UZ, kPrgSize}) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(submapper, prg_size, mirror);
                REQUIRE(Mapper380::validate_geometry(header(submapper, mirror), prg_size, 0U));
                auto mapper = board(submapper, prg_size, mirror);
                REQUIRE(state(*mapper).size() == kStateSize);
                REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
                REQUIRE(mapper->battery_ram().empty());
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
            }
        }
    }

    for (const unsigned id : {0U, 2U, 227U, 242U, 379U, 381U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper380::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper380::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 2U; submapper < 16U; ++submapper) {
        REQUIRE_FALSE(Mapper380::validate_geometry(header(submapper), kPrgSize, 0U));
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x004000UZ,
                                   0x020000UZ,
                                   0x03FFFFUZ,
                                   0x040001UZ,
                                   0x060000UZ,
                                   0x07FFFFUZ,
                                   0x080001UZ,
                                   0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper380::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper380::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper380::validate_geometry(header(0U, mirror), kPrgSize, 0U));
        REQUIRE_FALSE(Mapper380::validate_geometry(header(1U, mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = kChrRamSize;
            break;
        case 2U:
            h.prg_nvram_size = kChrRamSize;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper380::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 380 decodes only address A9-A0 across the full PRG write range",
          "[mapper380][bus]") {
    auto actual = board();
    auto expected = board();
    const auto prepare = [](Mapper380& mapper) {
        mapper.reset();
        mapper.write_prg(0x8000U | 0x02A5U, 0x5AU);
    };

    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        prepare(*actual);
        prepare(*expected);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(0x8000U | (raw & 0x03FFU)), 0U);
        }
        CAPTURE(raw, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }

    for (Address latch = 0U; latch <= 0x03FFU; ++latch) {
        actual->write_prg(static_cast<Address>(0xFC00U | latch), 0xFFU);
        CAPTURE(latch);
        REQUIRE(actual->read_prg(0x9A05U) ==
                cell(expected_offset(0x9A05U, latch, 0U, 0U) % kPrgSize));
        REQUIRE(actual->mirror_mode() ==
                ((latch & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    actual->write_prg_cpu(0xFFFFU, 0U, true);
    REQUIRE(actual->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 380 applies every PRG pin before wrapping to fitted ROM sizes",
          "[mapper380][banking]") {
    for (const Byte submapper : {0U, 1U}) {
        const Byte pads = submapper == 0U ? 0x0BU : 0U;
        for (const std::size_t prg_size : {0x040000UZ, kPrgSize}) {
            auto mapper = board(submapper, prg_size, MirrorMode::Vertical, pads);
            for (Address latch = 0U; latch <= 0x03FFU; ++latch) {
                mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
                for (const auto addr : kPrgAddresses) {
                    const auto physical = expected_offset(addr, latch, submapper, pads);
                    CAPTURE(submapper, pads, prg_size, latch, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical % prg_size));
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
            }
        }
    }

    for (const Byte submapper : {0U, 1U}) {
        const Byte pads = submapper == 0U ? 0x0BU : 0U;
        auto mapper = board(submapper, kPrgSize, MirrorMode::Vertical, pads);
        for (const Address latch : {0x0000U,
                                    0x0001U,
                                    0x003FU,
                                    0x0100U,
                                    0x013FU,
                                    0x0200U,
                                    0x0201U,
                                    0x023FU,
                                    0x0300U,
                                    0x03FFU}) {
            mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
            for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
                const auto addr = static_cast<Address>(raw);
                const auto physical = expected_offset(addr, latch, submapper, pads);
                CAPTURE(submapper, latch, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 380 uses flat 8 KiB CHR RAM with latch-controlled write protection",
          "[mapper380][ram][banking]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFFU)) ==
                cell((static_cast<std::size_t>(addr) + 0x3FFFU) & 0x1FFFU, 0xA5U));
    }

    for (const Byte submapper : {0U, 1U}) {
        mapper = board(submapper);
        for (Address latch = 0U; latch <= 0x03FFU; ++latch) {
            mapper->write_prg(0x8000U, 0U);
            mapper->write_chr(0x1234U, 0x5AU);
            mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
            mapper->write_chr(0x1234U, 0xA7U);
            CAPTURE(submapper, latch);
            REQUIRE(mapper->read_chr(0x1234U) == ((latch & 0x0080U) == 0U ? 0xA7U : 0x5AU));
        }
    }
}

TEST_CASE("Mapper 380 wires CIRAM A10 from M and starts vertical regardless of metadata",
          "[mapper380][ciram]") {
    for (const Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper, kPrgSize, MirrorMode::Horizontal);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        for (Address latch = 0U; latch <= 0x03FFU; ++latch) {
            mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
            CAPTURE(submapper, latch);
            REQUIRE(mapper->mirror_mode() ==
                    ((latch & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
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
}

TEST_CASE("Mapper 380 leaves CPU RAM expansion IRQ and audio absent",
          "[mapper380][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0xFFFFU, 0U);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, static_cast<Byte>(raw * 41U));
        mapper->write_expansion(addr, static_cast<Byte>(raw * 37U));
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));

    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 380 exposes only submapper-zero pads and preserves them across reset",
          "[mapper380][bus][reset]") {
    auto mapper = board();
    for (Byte pads = 0U; pads < 16U; ++pads) {
        REQUIRE(mapper->configure_board_jumpers(pads));
        mapper->write_prg(0x8100U, 0U);
        CAPTURE(pads);
        REQUIRE(mapper->read_prg(0x8000U) == cell(pads));
        REQUIRE(state(*mapper)[0] == pads);
    }
    auto saved = state(*mapper);
    for (const Byte pads : {16U, 31U, 255U}) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(pads));
        REQUIRE(state(*mapper) == saved);
    }
    REQUIRE_FALSE(mapper->configure_board_profile("menu-1"));
    REQUIRE(state(*mapper) == saved);

    auto kn35a = board(1U);
    saved = state(*kn35a);
    for (const Byte pads : {0U, 1U, 15U, 16U, 255U}) {
        REQUIRE_FALSE(kn35a->configure_board_jumpers(pads));
        REQUIRE(state(*kn35a) == saved);
    }
}

TEST_CASE("Mapper 380 reset restores bank zero and seven while cold power clears CHR RAM",
          "[mapper380][reset][ram]") {
    for (const Byte submapper : {0U, 1U}) {
        const Byte pads = submapper == 0U ? 13U : 0U;
        auto mapper = board(submapper, kPrgSize, MirrorMode::Horizontal, pads, 0x35U);
        const auto initial = state(*mapper);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            mapper->write_chr(addr, cell(addr, 0xA5U));
        }
        mapper->write_prg(0xFFFFU, 0U);
        const auto programmed = state(*mapper);
        const auto programmed_chr = std::span(programmed).subspan(kChrRamOffset, kChrRamSize);

        mapper->reset();
        const auto reset = state(*mapper);
        REQUIRE(reset[kLatchOffset] == 0U);
        REQUIRE(reset[kLatchOffset + 1U] == 0U);
        REQUIRE(std::ranges::equal(std::span(reset).subspan(kChrRamOffset, kChrRamSize),
                                   programmed_chr));
        REQUIRE(reset[0] == initial[0]);
        REQUIRE(reset[1] == initial[1]);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(7UZ * kPrgBankSize + 0x3FFFU, 0x35U));
        mapper->write_chr(0x0123U, 0x67U);
        REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

        mapper->write_prg(0xFFFFU, 0U);
        mapper->power_on();
        REQUIRE(state(*mapper) == initial);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 380 bounded snapshots replay latch and CHR RAM without serializing ROM",
          "[mapper380][state]") {
    for (const Byte submapper : {0U, 1U}) {
        const Byte pads = submapper == 0U ? 9U : 0U;
        auto mapper = board(submapper, kPrgSize, MirrorMode::Vertical, pads);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            mapper->write_chr(addr, cell(addr, 0xA5U));
        }
        mapper->write_prg(0x8325U, 0xFFU);
        const auto saved = state(*mapper);
        REQUIRE(saved.size() == kStateSize);
        REQUIRE(saved.size() == 8204U);
        REQUIRE(saved.size() < kPrgSize);

        auto copy = board(submapper, kPrgSize, MirrorMode::Horizontal, pads, 0x80U);
        StateReader reader(saved);
        copy->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*copy) == saved);
        REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
        for (const auto addr : kPrgAddresses) {
            CAPTURE(submapper, addr);
            REQUIRE(copy->read_prg(addr) == static_cast<Byte>(mapper->read_prg(addr) + 0x80U));
        }
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
        }

        auto with_tail = saved;
        with_tail.insert(with_tail.end(), {1U, 2U, 3U});
        copy->power_on();
        StateReader tail_reader(with_tail);
        copy->load_state(tail_reader);
        REQUIRE(tail_reader.ok());
        REQUIRE(tail_reader.remaining() == 3U);
        REQUIRE(state(*copy) == saved);
    }
}

TEST_CASE("Mapper 380 rejects damaged truncated and incompatible states atomically",
          "[mapper380][state]") {
    auto mapper = board(0U, kPrgSize, MirrorMode::Vertical, 9U);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0x8325U, 0U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < kLatchOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte reserved : {0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U, 0xFCU}) {
        auto invalid = saved;
        invalid[kLatchOffset + 1U] |= reserved;
        CAPTURE(reserved);
        reject(invalid);
    }

    auto wrong_prg = board(0U, kPrgSize / 2U, MirrorMode::Vertical, 9U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_pads = board(0U, kPrgSize, MirrorMode::Vertical, 8U);
    const auto wrong_pads_before = state(*wrong_pads);
    StateReader wrong_pads_reader(saved);
    wrong_pads->load_state(wrong_pads_reader);
    REQUIRE_FALSE(wrong_pads_reader.ok());
    REQUIRE(state(*wrong_pads) == wrong_pads_before);

    auto wrong_submapper = board(1U);
    const auto wrong_submapper_before = state(*wrong_submapper);
    StateReader wrong_submapper_reader(saved);
    wrong_submapper->load_state(wrong_submapper_reader);
    REQUIRE_FALSE(wrong_submapper_reader.ok());
    REQUIRE(state(*wrong_submapper) == wrong_submapper_before);
}

} // namespace
