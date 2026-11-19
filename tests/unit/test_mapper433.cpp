#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper433.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x004000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = Mapper433::kChrRamSize;
constexpr std::size_t kControlOffset = 10UZ;
constexpr std::size_t kAuxiliaryOffset = 11UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 6> kPrgAddresses{
    0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 433U;
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

std::unique_ptr<Mapper433> board(std::size_t prg_size = kMaxPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper433::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper433>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper433& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(Address addr, Byte control, std::size_t prg_size = kMaxPrgSize) {
    unsigned bank = control & 0x1FU;
    if ((control & 0x20U) == 0U) {
        bank = (bank & 0x1EU) | ((addr >> 14U) & 1U);
    }
    bank |= static_cast<unsigned>((control >> 2U) & 0x20U);
    return (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU)) % prg_size;
}

TEST_CASE("Mapper 433 validates NES 2.0 ROM RAM trainer and CIRAM geometry",
          "[mapper433][geometry][ram]") {
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper433::validate_geometry(header(), size, 0U));
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, mirror);
        auto identical_state = board(kMaxPrgSize, mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8204U);
        REQUIRE(state(*mapper) == state(*identical_state));
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
        REQUIRE_FALSE(Mapper433::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 233U, 430U, 431U, 432U, 434U, 4095U}) {
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
         {0UZ, 1UZ, 0x2000UZ, 0x3FFFUZ, 0x4001UZ, 0x5000UZ, 0x100001UZ, 0x104000UZ}) {
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

TEST_CASE("Mapper 433 decodes every even control and odd auxiliary alias",
          "[mapper433][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->write_prg(0x8000U, 0x55U);
        mapper->write_prg(0x8001U, 0U);
        const auto value = static_cast<Byte>(raw ^ (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper)[kControlOffset] == ((raw & 1U) == 0U ? value : 0x55U));
        REQUIRE(state(*mapper)[kAuxiliaryOffset] == ((raw & 1U) != 0U ? value & 2U : 0U));
        REQUIRE(mapper->maps_prg(static_cast<Address>(raw)));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(raw)) == 0xFFU);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xFFFEU, static_cast<Byte>(value));
        mapper->write_prg(0xFFFFU, static_cast<Byte>(value ^ 0xFFU));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kControlOffset] == value);
        REQUIRE(state(*mapper)[kAuxiliaryOffset] == ((value ^ 0xFFU) & 2U));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
}

TEST_CASE("Mapper 433 composes inner outer and PRG mode bits before fitted-ROM wrapping",
          "[mapper433][decoding][banking][prg]") {
    for (const std::size_t size : {kMinPrgSize, 0x00C000UZ, 0x080000UZ, kMaxPrgSize}) {
        auto mapper = board(size);
        for (unsigned value = 0U; value < 256U; ++value) {
            const auto control = static_cast<Byte>(value);
            mapper->write_prg(0x8000U, control);
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, control, size);
                CAPTURE(size, control, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 433 uses control bit 6 for ordinary CIRAM arrangement",
          "[mapper433][decoding][ciram]") {
    auto mapper = board();
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
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

TEST_CASE("Mapper 433 owns every CHR RAM cell and odd writes control protection",
          "[mapper433][ram][reset]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        const auto value = cell(addr, 0x39U);
        mapper->write_chr(addr, value);
        REQUIRE(mapper->read_chr(addr) == value);
    }
    mapper->write_prg(0x8001U, 0xFFU);
    REQUIRE(state(*mapper)[kAuxiliaryOffset] == 2U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, static_cast<Byte>(cell(addr, 0x39U) ^ 0xFFU));
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x39U));
    }
    mapper->reset();
    REQUIRE(state(*mapper)[kAuxiliaryOffset] == 0U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x39U));
        mapper->write_chr(addr, cell(addr, 0xA7U));
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA7U));
    }
    mapper->power_on();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 433 exposes no low CPU RAM expansion device IRQ or audio",
          "[mapper433][bus][ram][irq][audio]") {
    auto mapper = board();
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
        mapper->write_prg(addr, static_cast<Byte>(addr));
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
    REQUIRE_FALSE(mapper->configure_board_profile("reset-42"));
}

TEST_CASE("Mapper 433 soft reset toggles only A19 and cold power restores deterministic zero",
          "[mapper433][reset][banking][ciram][ram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, initial);
        const auto cold = state(*mapper);
        mapper->write_chr(0x0123U, 0xA5U);
        for (unsigned phase = 0U; phase < 16U; ++phase) {
            mapper->write_prg(0xFFFEU, (phase & 1U) == 0U ? 0xFFU : 0x7FU);
            mapper->write_prg(0xFFFFU, 0xFFU);
            mapper->reset();
            const auto control = static_cast<Byte>((phase & 1U) == 0U ? 0U : 0x80U);
            CAPTURE(initial, phase, control);
            REQUIRE(state(*mapper)[kControlOffset] == control);
            REQUIRE(state(*mapper)[kAuxiliaryOffset] == 0U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            for (const auto addr : kPrgAddresses) {
                REQUIRE(mapper->read_prg(addr) == cell(expected_prg(addr, control)));
            }
            REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
        }
        mapper->write_prg(0x8000U, 0xFFU);
        mapper->write_prg(0x8001U, 0xFFU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, 0U)));
        REQUIRE(mapper->read_prg(0xC000U) == cell(expected_prg(0xC000U, 0U)));
    }
}

TEST_CASE("Mapper 433 bounded snapshots replay registers and RAM without either ROM",
          "[mapper433][state][banking][ram]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xFEU);
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    mapper->write_prg(0x8001U, 2U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kControlOffset] == 0xFEU);
    REQUIRE(saved[kAuxiliaryOffset] == 2U);

    auto copy = board(kMaxPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, 0xFEU);
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

TEST_CASE("Mapper 433 rejects truncated damaged and mismatched states atomically",
          "[mapper433][state][ram]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xFEU);
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_prg(0x8001U, 2U);
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
    for (std::size_t offset = 0U; offset < kControlOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte auxiliary : {Byte{1U}, Byte{3U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kAuxiliaryOffset] = auxiliary;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(auxiliary);
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
    changed[kControlOffset] = 0x7FU;
    changed[kAuxiliaryOffset] = 0U;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8000U) == cell(expected_prg(0x8000U, 0x7FU)));
}

} // namespace
