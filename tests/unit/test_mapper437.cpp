#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper437.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = Mapper437::kPrgSize;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = Mapper437::kChrRamSize;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kInnerOffset = 11UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 437U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 14U) * 47U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper437> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper437::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper437>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper437& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void select_inner(Mapper437& mapper, Byte value) {
    for (Address addr = 0x8000U; addr < 0xC000U; ++addr) {
        if ((mapper.read_prg(addr) & 7U) == 7U) {
            mapper.write_prg(addr, static_cast<Byte>(value | 0xF8U));
            return;
        }
    }
    FAIL("Synthetic PRG ROM has no conflict-free inner-bank write address");
}

std::size_t expected_prg(Address addr, Byte outer, Byte inner) {
    const unsigned selected_inner = (addr & 0x4000U) == 0U ? inner & 7U : 7U;
    const unsigned bank = ((outer & 7U) << 3U) | selected_inner;
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
}

TEST_CASE("Mapper 437 validates the documented TH2348 NES 2.0 population",
          "[mapper437][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        CAPTURE(mirror);
        REQUIRE(Mapper437::validate_geometry(header(mirror), kPrgSize, 0U));
        auto mapper = board(mirror);
        auto different_rom = board(mirror, 0x80U);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8204U);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    const auto reject = [](INesHeader h, std::size_t prg, std::size_t chr) {
        CAPTURE(h.mapper_number,
                h.is_nes2,
                h.submapper,
                h.has_battery,
                h.has_trainer,
                h.prg_ram_size,
                h.prg_nvram_size,
                h.chr_ram_size,
                h.chr_nvram_size,
                h.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper437::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 294U, 434U, 436U, 438U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, 0U);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x004000UZ,
                                   0x008000UZ,
                                   0x020000UZ,
                                   0x080000UZ,
                                   kPrgSize - 1U,
                                   kPrgSize + 1U,
                                   kPrgSize * 2U}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, kPrgSize}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0x2000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, 0U);
    }
}

TEST_CASE("Mapper 437 decodes address-latched outer and UNROM inner CPU writes",
          "[mapper437][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        const auto driven = raw >= 0x8000U ? mapper->read_prg(addr) : Byte{0U};
        if (raw < 0x8000U) {
            mapper->write_expansion(addr, value);
            mapper->write_prg(addr, static_cast<Byte>(value ^ 0xFFU));
        } else {
            mapper->write_prg(addr, value);
        }
        const bool outer = raw >= 0x5000U && raw < 0x6000U;
        const auto bytes = state(*mapper);
        CAPTURE(raw, value, driven, outer);
        REQUIRE(bytes[kOuterOffset] == (outer ? raw & 0x0FU : 0U));
        REQUIRE(bytes[kInnerOffset] == (raw >= 0x8000U ? driven & value & 7U : 0U));
        REQUIRE(mapper->maps_expansion(addr) == outer);
        REQUIRE(mapper->expansion_read_mask(addr) == (outer ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x5AEDU, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kOuterOffset] == 0x0DU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
    mapper->write_prg_cpu(0xFFFFU, 0U, false);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x0DU);
}

TEST_CASE("Mapper 437 composes every outer bank with UNROM banking and local fixed bank 7",
          "[mapper437][decoding][banking][prg]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 8U; ++outer) {
        const auto outer_address = static_cast<Address>(0x5000U | outer | ((outer & 1U) << 3U));
        mapper->write_expansion(outer_address, 0xA5U);
        for (Byte inner = 0U; inner < 8U; ++inner) {
            select_inner(*mapper, inner);
            REQUIRE(state(*mapper)[kInnerOffset] == inner);
            for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
                const auto addr = static_cast<Address>(raw);
                const auto physical = expected_prg(addr, outer, inner);
                CAPTURE(outer, inner, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            }
            REQUIRE(mapper->read_prg(0xFFFFU) ==
                    cell((static_cast<std::size_t>(outer) + 1U) * 0x20000UZ - 1U));
        }
    }
}

TEST_CASE("Mapper 437 inner writes AND data with the old mapping's driven ROM byte",
          "[mapper437][decoding][banking][bus]") {
    auto mapper = board();
    for (const Byte outer : {Byte{0U}, Byte{5U}}) {
        mapper->write_expansion(static_cast<Address>(0x5000U | outer), 0U);
        for (const Byte initial : {Byte{0U}, Byte{3U}, Byte{7U}}) {
            for (unsigned mask = 0U; mask < 256U; ++mask) {
                const auto addr = static_cast<Address>(0x8100U + mask);
                for (unsigned data = 0U; data < 256U; ++data) {
                    select_inner(*mapper, initial);
                    const auto driven = mapper->read_prg(addr);
                    const auto inner = static_cast<Byte>(data & driven & 7U);
                    mapper->write_prg(addr, static_cast<Byte>(data));
                    const auto physical = expected_prg(0x8000U, outer, inner);
                    CAPTURE(outer, initial, mask, data, driven, inner, physical);
                    REQUIRE(mapper->read_prg(0x8000U) == cell(physical));
                }
            }
        }
    }

    mapper->reset();
    mapper->write_expansion(0x5003U, 0U);
    select_inner(*mapper, 2U);
    const auto fixed_driven = mapper->read_prg(0xFFFFU);
    mapper->write_prg(0xFFFFU, 0xFFU);
    REQUIRE(state(*mapper)[kInnerOffset] == (fixed_driven & 7U));
    REQUIRE(state(*mapper)[kOuterOffset] == 3U);
}

TEST_CASE("Mapper 437 owns every flat CHR RAM cell with ordinary PPU aliasing",
          "[mapper437][ram][banking]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == 0U);
        mapper->write_chr(addr, cell(addr, 0x39U));
    }
    mapper->write_expansion(0x500FU, 0U);
    select_inner(*mapper, 6U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x39U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0x39U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFFU)) ==
                cell((static_cast<std::size_t>(addr) + 0x3FFFU) & 0x1FFFU, 0x39U));
    }
}

TEST_CASE("Mapper 437 outer address bit 3 selects ordinary H/V CIRAM",
          "[mapper437][decoding][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Address outer = 0U; outer < 16U; ++outer) {
        mapper->write_expansion(static_cast<Address>(0x5000U | outer), 0xFFU);
        CAPTURE(outer);
        REQUIRE(mapper->mirror_mode() ==
                ((outer & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 437 exposes no CPU RAM IRQ expansion audio or hidden side effects",
          "[mapper437][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_expansion(0x500DU, 0U);
    select_inner(*mapper, 4U);
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    const auto before = state(*mapper);
    mapper->observe_cpu_read(0xFFFDU, 0xA5U);
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
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 437 soft reset clears latches and cold power also clears CHR RAM",
          "[mapper437][reset][banking][ciram][ram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial, 0x35U);
        const auto cold = state(*mapper);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            mapper->write_chr(addr, cell(addr, 0xA5U));
        }
        mapper->write_expansion(0x500FU, 0U);
        select_inner(*mapper, 6U);
        const auto programmed = state(*mapper);

        mapper->reset();
        const auto reset = state(*mapper);
        CAPTURE(initial);
        REQUIRE(reset[kOuterOffset] == 0U);
        REQUIRE(reset[kInnerOffset] == 0U);
        REQUIRE(std::ranges::equal(std::span(reset).subspan(kChrRamOffset),
                                   std::span(programmed).subspan(kChrRamOffset)));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        for (const auto addr : kPrgAddresses) {
            REQUIRE(mapper->read_prg(addr) == cell(expected_prg(addr, 0U, 0U), 0x35U));
        }
        mapper->write_chr(0x0123U, 0x67U);
        REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

        mapper->write_expansion(0x500FU, 0U);
        select_inner(*mapper, 4U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 437 bounded snapshots replay registers and RAM without PRG ROM",
          "[mapper437][state][banking][ram]") {
    auto mapper = board();
    mapper->write_expansion(0x500DU, 0xFFU);
    select_inner(*mapper, 3U);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kOuterOffset] == 0x0DU);
    REQUIRE(saved[kInnerOffset] == 3U);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0x0DU, 3U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x5AU));
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

TEST_CASE("Mapper 437 rejects truncated damaged and mismatched states atomically",
          "[mapper437][state][ram]") {
    auto mapper = board();
    mapper->write_expansion(0x500DU, 0U);
    select_inner(*mapper, 3U);
    mapper->write_chr(0x0123U, 0xA5U);
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
    for (unsigned bit = 0U; bit < kOuterOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte outer : {Byte{0x10U}, Byte{0x80U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kOuterOffset] = outer;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(outer);
        reject(invalid);
    }
    for (const Byte inner : {Byte{0x08U}, Byte{0x80U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kInnerOffset] = inner;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(inner);
        reject(invalid);
    }

    auto wrong_prg = std::make_unique<Mapper437>(header(), rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto changed = saved;
    changed[kOuterOffset] = 0x06U;
    changed[kInnerOffset] = 5U;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, 0x06U, 5U)));
}

} // namespace
