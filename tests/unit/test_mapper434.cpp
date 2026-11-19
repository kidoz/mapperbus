#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper434.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = Mapper434::kChrRamSize;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kInnerOffset = 11UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 6> kPrgAddresses{
    0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 434U;
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

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper434> board(std::size_t prg_size = kMaxPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper434::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper434>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper434& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void select_inner(Mapper434& mapper, Byte value) {
    for (Address addr = 0x8000U; addr < 0xC000U; ++addr) {
        if ((mapper.read_prg(addr) & 7U) == 7U) {
            mapper.write_prg(addr, static_cast<Byte>(value | 0xF8U));
            return;
        }
    }
    FAIL("Synthetic PRG ROM has no conflict-free inner-bank write address");
}

std::size_t expected_prg(Address addr, Byte outer, Byte inner, std::size_t prg_size = kMaxPrgSize) {
    const unsigned selected_inner = (addr & 0x4000U) == 0U ? inner & 7U : 7U;
    const unsigned bank = ((outer & 7U) << 3U) | selected_inner;
    return (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU)) % prg_size;
}

TEST_CASE("Mapper 434 validates NES 2.0 ROM RAM trainer and CIRAM geometry",
          "[mapper434][geometry][ram]") {
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper434::validate_geometry(header(), size, 0U));
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, mirror);
        auto identical_state = board(kMaxPrgSize, mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8204U);
        REQUIRE(state(*mapper) == state(*identical_state));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
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
        REQUIRE_FALSE(Mapper434::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 432U, 433U, 435U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x100001UZ, 0x104000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x100000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, 0U);
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
        reject(h, kMinPrgSize, 0U);
    }
}

TEST_CASE("Mapper 434 decodes every outer and inner CPU address", "[mapper434][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        mapper->write_prg(addr, 0xA7U);
        CAPTURE(raw);
        REQUIRE(state(*mapper)[kOuterOffset] == (raw >= 0x6000U ? 0x27U : 0U));
        REQUIRE(state(*mapper)[kInnerOffset] == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw ^ (raw >> 8U));
        const auto driven = mapper->read_prg(addr);
        mapper->write_prg(addr, value);
        CAPTURE(raw, value, driven);
        REQUIRE(state(*mapper)[kOuterOffset] == 0U);
        REQUIRE(state(*mapper)[kInnerOffset] == (driven & value & 7U));
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x6000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x27U));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x20U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
        mapper->write_prg(0x7FFFU, static_cast<Byte>(value ^ 0xFFU));
        REQUIRE(state(*mapper)[kOuterOffset] == ((value ^ 0xFFU) & 0x27U));
    }
}

TEST_CASE("Mapper 434 composes outer and UNROM inner banks before fitted-ROM wrapping",
          "[mapper434][decoding][banking][prg]") {
    for (const std::size_t size : {kMinPrgSize, 0x00C000UZ, 0x020000UZ, kMaxPrgSize}) {
        auto mapper = board(size);
        for (Byte outer = 0U; outer < 8U; ++outer) {
            mapper->write_prg(0x6000U, static_cast<Byte>(outer | (outer & 1U) << 5U | 0xD8U));
            for (Byte inner = 0U; inner < 8U; ++inner) {
                select_inner(*mapper, inner);
                REQUIRE(state(*mapper)[kInnerOffset] == inner);
                for (const auto addr : kPrgAddresses) {
                    const auto physical = expected_prg(addr, outer, inner, size);
                    CAPTURE(size, outer, inner, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }
}

TEST_CASE("Mapper 434 inner writes AND data with the old bank's driven ROM byte",
          "[mapper434][decoding][banking][bus]") {
    auto mapper = board();
    for (const Byte outer : {Byte{0U}, Byte{5U}}) {
        mapper->write_prg(0x6000U, outer);
        for (const Byte initial : {Byte{0U}, Byte{3U}, Byte{7U}}) {
            for (unsigned mask = 0U; mask < 256U; ++mask) {
                const auto addr = static_cast<Address>(0x8100U + mask);
                for (unsigned data = 0U; data < 256U; ++data) {
                    select_inner(*mapper, initial);
                    const auto driven = mapper->read_prg(addr);
                    const auto bus = static_cast<Byte>(data & driven & 7U);
                    mapper->write_prg(addr, static_cast<Byte>(data));
                    const auto physical = expected_prg(0x8000U, outer, bus);
                    CAPTURE(outer, initial, mask, data, driven, bus);
                    REQUIRE(mapper->read_prg(0x8000U) == cell(physical));
                }
            }
        }
    }
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg_cpu(0x8000U, 0U, false);
    REQUIRE(state(*mapper)[kInnerOffset] == 0U);
}

TEST_CASE("Mapper 434 uses outer bit 5 for ordinary CIRAM arrangement",
          "[mapper434][decoding][ciram]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x6000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x20U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
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
}

TEST_CASE("Mapper 434 owns every CHR RAM cell and retains it across soft reset",
          "[mapper434][ram][reset]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        const auto value = cell(addr, 0x39U);
        mapper->write_chr(addr, value);
        REQUIRE(mapper->read_chr(addr) == value);
    }
    mapper->write_prg(0x6000U, 0x27U);
    select_inner(*mapper, 5U);
    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    REQUIRE(state(*mapper)[kInnerOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x39U));
    }
    for (const auto addr : {Address{0U}, Address{0x0123U}, Address{0x1FFFU}}) {
        mapper->write_chr(addr, 0xA5U);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == 0xA5U);
    }
    mapper->power_on();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 434 exposes no low CPU RAM expansion device IRQ or audio",
          "[mapper434][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x27U);
    select_inner(*mapper, 2U);
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        if (addr < 0x6000U) {
            mapper->write_prg(addr, static_cast<Byte>(addr));
        }
        mapper->write_expansion(addr, static_cast<Byte>(addr ^ 0xFFU));
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

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
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 434 soft reset clears latches and cold power also clears CHR RAM",
          "[mapper434][reset][banking][ciram][ram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, initial);
        const auto cold = state(*mapper);
        mapper->write_chr(0x0123U, 0xA5U);
        mapper->write_prg(0x7FFFU, 0xFFU);
        select_inner(*mapper, 6U);
        mapper->reset();
        CAPTURE(initial);
        REQUIRE(state(*mapper)[kOuterOffset] == 0U);
        REQUIRE(state(*mapper)[kInnerOffset] == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
        for (const auto addr : kPrgAddresses) {
            REQUIRE(mapper->read_prg(addr) == cell(expected_prg(addr, 0U, 0U)));
        }
        mapper->write_prg(0x6000U, 0x27U);
        select_inner(*mapper, 4U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
    }
}

TEST_CASE("Mapper 434 bounded snapshots replay registers and RAM without either ROM",
          "[mapper434][state][banking][ram]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x25U);
    select_inner(*mapper, 3U);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == 0x25U);
    REQUIRE(saved[kInnerOffset] == 3U);

    auto copy = board(kMaxPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0x25U, 3U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
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

TEST_CASE("Mapper 434 rejects truncated damaged and mismatched states atomically",
          "[mapper434][state][ram]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0x25U);
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
    for (std::size_t offset = 0U; offset < kOuterOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte outer : {Byte{0x08U}, Byte{0x40U}, Byte{0x80U}, Byte{0xFFU}}) {
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

    auto wrong_prg = board(kMaxPrgSize / 2U);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_mirror = board(kMaxPrgSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto changed = saved;
    changed[kOuterOffset] = 0x22U;
    changed[kInnerOffset] = 6U;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, 0x22U, 6U)));
}

} // namespace
