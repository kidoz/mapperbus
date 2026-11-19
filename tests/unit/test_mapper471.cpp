#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper471.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x010000UZ;
constexpr std::size_t kChrSize = 0x004000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kFlagsOffset = 10UZ;
constexpr std::size_t kStateSize = 11UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};
constexpr std::array<Address, 8> kChrAddresses{
    0x0000U, 0x0001U, 0x0123U, 0x0FFFU, 0x1000U, 0x1234U, 0x1FFEU, 0x1FFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 471U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kPrgSize;
    result.chr_rom_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper471> board(MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper471::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper471>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x55U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper471& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void select_bank(Mapper471& mapper, Byte bank) {
    mapper.write_prg(static_cast<Address>(0x8000U | (bank & 1U)), 0xA5U);
}

void require_prg(
    Mapper471& mapper, Address addr, Byte bank, Byte salt = 0U, std::size_t prg_size = kPrgSize) {
    const std::size_t physical =
        (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x7FFFU)) % prg_size;
    CAPTURE(addr, bank, physical, salt, prg_size);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(
    Mapper471& mapper, Address addr, Byte bank, Byte salt = 0U, std::size_t chr_size = kChrSize) {
    const std::size_t physical =
        (static_cast<std::size_t>(bank) * kChrBankSize + (addr & 0x1FFFU)) % chr_size;
    CAPTURE(addr, bank, physical, salt, chr_size);
    REQUIRE(mapper.read_chr(addr) == cell(physical, static_cast<Byte>(salt ^ 0x55U)));
}

TEST_CASE("Mapper 471 validates bounded NES 2.0 ROM geometry and absent memory",
          "[mapper471][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg : {0x008000UZ, kPrgSize}) {
            for (const std::size_t chr : {0x002000UZ, kChrSize}) {
                auto h = header(mirror);
                h.prg_rom_size = prg;
                h.chr_rom_size = chr;
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper471::validate_geometry(h, prg, chr));
            }
        }

        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8000U, 0U, 0x35U);
        require_prg(*mapper, 0xFFFFU, 0U, 0x35U);
        require_chr(*mapper, 0x0000U, 0U, 0x35U);
        require_chr(*mapper, 0x1FFFU, 0U, 0x35U);
    }

    const auto reject = [](const INesHeader& h, std::size_t prg, std::size_t chr) {
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
        REQUIRE_FALSE(Mapper471::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 201U, 470U, 472U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, kChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, kChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, kChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x00C000UZ, 0x018000UZ, 0x020000UZ}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x1000UZ, 0x1800UZ, 0x3000UZ, 0x6000UZ, 0x008000UZ}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, kChrSize);
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, kChrSize);
    }
}

TEST_CASE("Mapper 471 decodes all latch aliases acknowledges IRQs and floats low CPU reads",
          "[mapper471][decoding][bus][irq]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        mapper->observe_ppu_a12(true);
        mapper->observe_ppu_a12(false);
        auto expected = cold;
        expected[kFlagsOffset] = 4U;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x8000U) {
            expected[kFlagsOffset] = raw & 1U;
        }

        mapper->write_expansion(addr, value);
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->irq_pending() == (raw < 0x8000U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(false);
    mapper->write_prg_cpu(0xFFFFU, 0U, true);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kFlagsOffset] == 1U);
}

TEST_CASE("Mapper 471 shares address bit zero between full PRG and CHR windows",
          "[mapper471][banking][prg][chr][bus]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    for (Byte bank = 0U; bank < 2U; ++bank) {
        select_bank(*mapper, bank);
        for (Address addr = 0x8000U; addr != 0U; ++addr) {
            require_prg(*mapper, addr, bank, 0x35U);
        }
        for (Address addr = 0U; addr < kChrBankSize; ++addr) {
            require_chr(*mapper, addr, bank, 0x35U);
        }
        for (const auto addr : kPrgAddresses) {
            mapper->write_prg(addr, static_cast<Byte>(addr ^ 0xA5U));
            const auto selected = static_cast<Byte>(addr & 1U);
            require_prg(*mapper, addr, selected, 0x35U);
            require_chr(*mapper, static_cast<Address>(addr & 0x1FFFU), selected, 0x35U);
        }
    }

    auto small = board(MirrorMode::Horizontal, 0x80U, kPrgBankSize, kChrBankSize);
    for (Byte bank = 0U; bank < 2U; ++bank) {
        select_bank(*small, bank);
        for (const auto addr : kPrgAddresses) {
            require_prg(*small, addr, bank, 0x80U, kPrgBankSize);
        }
        for (const auto addr : kChrAddresses) {
            require_chr(*small, addr, bank, 0x80U, kChrBankSize);
        }
    }
}

TEST_CASE("Mapper 471 preserves CHR ROM and hard-wired CIRAM across bank writes",
          "[mapper471][chr][ciram][ram][bus]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        for (Byte bank = 0U; bank < 2U; ++bank) {
            select_bank(*mapper, bank);
            const auto banked = state(*mapper);
            for (const auto addr : kChrAddresses) {
                const auto original = mapper->read_chr(addr);
                mapper->write_chr(addr, static_cast<Byte>(original ^ 0xFFU));
                CAPTURE(mirror, bank, addr);
                REQUIRE(mapper->read_chr(addr) == original);
                REQUIRE(mapper->mirror_mode() == mirror);
            }
            REQUIRE(state(*mapper) == banked);
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
}

TEST_CASE("Mapper 471 raises sticky IRQs on every unfiltered falling A12 edge",
          "[mapper471][irq]") {
    auto mapper = board();
    REQUIRE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());

    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(false);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(true);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(false);
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kFlagsOffset] == 4U);

    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(false);
    REQUIRE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kFlagsOffset] == 4U);
    mapper->observe_ppu_a12(true);
    REQUIRE(state(*mapper)[kFlagsOffset] == 6U);

    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kFlagsOffset] == 2U);
    mapper->observe_ppu_a12(false);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x7FFFU, 0xA5U);
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0x8000U, 0xA5U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(state(*mapper)[kFlagsOffset] == 0U);

    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(false);
    const auto pending = state(*mapper);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_irq_counter();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == pending);
    REQUIRE(mapper->irq_pending());
}

TEST_CASE("Mapper 471 has no RAM or expansion audio and rejects board options",
          "[mapper471][ram][audio][bus]") {
    auto mapper = board();
    select_bank(*mapper, 1U);
    mapper->observe_ppu_a12(true);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->write_expansion(0x5000U, 0xA5U);
    mapper->write_expansion(0x7FFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 471 reset and cold power clear latch pin history and pending IRQ",
          "[mapper471][reset][irq][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        const auto cold = state(*mapper);
        select_bank(*mapper, 1U);
        mapper->observe_ppu_a12(true);
        mapper->observe_ppu_a12(false);
        mapper->observe_ppu_a12(true);
        REQUIRE(state(*mapper)[kFlagsOffset] == 7U);
        REQUIRE(mapper->irq_pending());

        mapper->reset();
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE_FALSE(mapper->irq_pending());
        require_prg(*mapper, 0xFFFFU, 0U);
        require_chr(*mapper, 0x1FFFU, 0U);

        select_bank(*mapper, 1U);
        mapper->observe_ppu_a12(true);
        mapper->observe_ppu_a12(false);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
    }
}

TEST_CASE("Mapper 471 bounded snapshots replay banking and partial IRQ pins without ROM",
          "[mapper471][state][banking][irq]") {
    auto mapper = board();
    select_bank(*mapper, 1U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kFlagsOffset] == 7U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->irq_pending());
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 1U, 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, 1U, 0x80U);
    }
    copy->observe_ppu_a12(false);
    REQUIRE(copy->irq_pending());
    REQUIRE(state(*copy)[kFlagsOffset] == 5U);

    mapper->power_on();
    load(*mapper, saved);
    REQUIRE(state(*mapper) == saved);
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    mapper->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*mapper) == saved);
}

TEST_CASE("Mapper 471 rejects truncated damaged and mismatched states atomically",
          "[mapper471][state][irq][ciram]") {
    auto mapper = board();
    select_bank(*mapper, 1U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
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
    for (unsigned offset = 0U; offset < kFlagsOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 0x01U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte flags : {Byte{0x08U}, Byte{0x80U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kFlagsOffset] = flags;
        CAPTURE(flags);
        reject(invalid);
    }

    auto wrong_prg = board(MirrorMode::Vertical, 0U, kPrgBankSize, kChrSize);
    const auto before_prg = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == before_prg);

    auto wrong_chr = board(MirrorMode::Vertical, 0U, kPrgSize, kChrBankSize);
    const auto before_chr = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == before_chr);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto before_mirror = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before_mirror);

    auto changed = saved;
    changed[kFlagsOffset] = 4U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->irq_pending());
    require_prg(*mapper, 0xFFFFU, 0U);
    require_chr(*mapper, 0x1FFFU, 0U);
}

} // namespace
