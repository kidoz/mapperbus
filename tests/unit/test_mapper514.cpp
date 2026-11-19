#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper514.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kDefaultPrgSize = 0x020000UZ;
constexpr std::size_t kMaxPrgSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x008000UZ;
constexpr std::size_t kRamSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x001000UZ;
constexpr std::size_t kModeStateOffset = 10UZ;
constexpr std::size_t kNametableStateOffset = 11UZ;
constexpr std::size_t kWramStateOffset = 12UZ;
constexpr std::size_t kChrRamStateOffset = kWramStateOffset + kRamSize;
constexpr std::size_t kStateSize = kChrRamStateOffset + kRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};
constexpr std::array<Address, 8> kChrAddresses{
    0x0000U, 0x0001U, 0x0123U, 0x0FFFU, 0x1000U, 0x1234U, 0x1FFEU, 0x1FFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 514U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::Dendy;
    result.prg_rom_size = kDefaultPrgSize;
    result.prg_ram_size = kRamSize;
    result.chr_ram_size = kRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 12U) * 47U +
                             (offset >> 15U) * 59U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper514> board(std::size_t prg_size = kDefaultPrgSize,
                                 MirrorMode mirror = MirrorMode::Horizontal,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper514::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper514>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, kWramStateOffset> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    REQUIRE(bytes.size() == kStateSize);
    std::array<Byte, kWramStateOffset> result{};
    std::copy_n(bytes.begin(), kWramStateOffset, result.begin());
    return result;
}

void load(Mapper514& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

Byte initial_mode(MirrorMode mirror) {
    return mirror == MirrorMode::Vertical ? 0x40U : 0U;
}

MirrorMode expected_mirror(Byte mode) {
    return (mode & 0x40U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
}

unsigned expected_second_ciram(Byte mode, Byte nametable) {
    return (mode & 0x40U) != 0U ? (nametable & 1U) : ((nametable >> 1U) & 1U);
}

void observe_nametable(Mapper514& mapper, Byte nametable) {
    mapper.observe_ppu_address(static_cast<Address>(0x205AU | ((nametable & 3U) << 10U)));
}

std::size_t expected_chr_offset(Address addr, Byte mode, Byte nametable) {
    if (addr >= kChrBankSize || (mode & 0x80U) == 0U) {
        return addr;
    }
    return expected_second_ciram(mode, nametable) * kChrBankSize + addr;
}

void require_prg(Mapper514& mapper, Address addr, Byte mode, std::size_t prg_size, Byte salt) {
    const auto physical =
        (static_cast<std::size_t>(mode & 0x3FU) * kPrgBankSize + (addr & 0x7FFFU)) % prg_size;
    CAPTURE(addr, mode, prg_size, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper514& mapper, Address addr, Byte mode, Byte nametable, Byte salt = 0x60U) {
    const auto physical = expected_chr_offset(addr, mode, nametable);
    CAPTURE(addr, mode, nametable, physical, salt);
    REQUIRE(mapper.read_chr(addr) == cell(physical, salt));
}

TEST_CASE("Mapper 514 validates NES 2.0 geometry and bounded volatile memory",
          "[mapper514][geometry][ram][ciram]") {
    for (std::size_t size = kPrgBankSize; size <= kMaxPrgSize; size *= 2U) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(size, mirror);
            REQUIRE(Mapper514::validate_geometry(header(mirror), size, 0U));
            auto mapper = board(size, mirror, 0x35U);
            auto different_rom = board(size, mirror, 0x80U);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(state(*mapper).size() == 16396U);
            REQUIRE(register_state(*mapper)[kModeStateOffset] == initial_mode(mirror));
            REQUIRE(register_state(*mapper)[kNametableStateOffset] == 0U);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            require_prg(*mapper, 0x8000U, initial_mode(mirror), size, 0x35U);
            require_prg(*mapper, 0xFFFFU, initial_mode(mirror), size, 0x35U);
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_prg(0x7FFFU) == 0U);
            REQUIRE(mapper->read_chr(0U) == 0U);
            REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
        }
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
        REQUIRE_FALSE(Mapper514::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 512U, 513U, 515U, 518U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kDefaultPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kDefaultPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kDefaultPrgSize, 0U);
    }
    for (const std::size_t size : {0UZ,
                                   1UZ,
                                   0x4000UZ,
                                   0x7FFFUZ,
                                   0x8001UZ,
                                   0xC000UZ,
                                   0x18000UZ,
                                   kMaxPrgSize + kPrgBankSize,
                                   0x400000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ, 0x100000UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 10U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 0x1000U;
            break;
        case 4U:
            h.prg_ram_size = 0x4000U;
            break;
        case 5U:
            h.prg_nvram_size = kRamSize;
            break;
        case 6U:
            h.chr_ram_size = 0U;
            break;
        case 7U:
            h.chr_ram_size = 0x1000U;
            break;
        case 8U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kRamSize;
            break;
        }
        CAPTURE(field);
        reject(h, kDefaultPrgSize, 0U);
    }
}

TEST_CASE("Mapper 514 decodes only the documented register and maps WRAM and PRG",
          "[mapper514][decoding][bus][ram]") {
    auto mapper = board();
    const auto cold = register_state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw == 0x8000U) {
            expected[kModeStateOffset] = value;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(register_state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x6000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x6000U ? 0xFFU : 0U));
        if (raw < 0x6000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        } else if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == value);
        }
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    mapper->write_prg_cpu(0x8000U, 0xFFU, true);
    REQUIRE(register_state(*mapper)[kModeStateOffset] == 0xFFU);
    mapper->write_prg_cpu(0x8001U, 0U, false);
    REQUIRE(register_state(*mapper)[kModeStateOffset] == 0xFFU);
}

TEST_CASE("Mapper 514 selects all six-bit 32 KiB PRG banks before fitted-ROM wrapping",
          "[mapper514][banking][prg][bus]") {
    for (std::size_t size = kPrgBankSize; size <= kMaxPrgSize; size *= 2U) {
        auto mapper = board(size, MirrorMode::Horizontal, 0x35U);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto mode = static_cast<Byte>(raw);
            mapper->write_prg(0x8000U, mode);
            REQUIRE(register_state(*mapper)[kModeStateOffset] == mode);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, mode, size, 0x35U);
            }
        }
        mapper->write_prg(0x8000U, 0xFFU);
        for (Address addr = 0x8000U;; ++addr) {
            require_prg(*mapper, addr, 0xFFU, size, 0x35U);
            if (addr == 0xFFFFU) {
                break;
            }
        }
    }
}

TEST_CASE("Mapper 514 provides one independent volatile 8 KiB WRAM window",
          "[mapper514][ram][bus][reset]") {
    auto mapper = board();
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(addr, cell(addr, 0x45U));
    }
    for (const Byte mode : {Byte{0U}, Byte{0x3FU}, Byte{0x80U}, Byte{0xFFU}}) {
        mapper->write_prg(0x8000U, mode);
        for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
            CAPTURE(mode, addr);
            REQUIRE(mapper->read_prg(addr) == cell(addr, 0x45U));
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
    }
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6123U) == cell(0x6123U, 0x45U));
    mapper->power_on();
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        REQUIRE(mapper->read_prg(addr) == 0U);
    }
}

TEST_CASE("Mapper 514 switches only lower CHR reads and never banks CHR writes",
          "[mapper514][banking][chr][ram][ciram]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x60U));
    }
    REQUIRE(mapper->observes_ppu_address());
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        const auto mode = static_cast<Byte>(raw);
        mapper->write_prg(0x8000U, mode);
        for (Byte nametable = 0U; nametable < 4U; ++nametable) {
            observe_nametable(*mapper, nametable);
            REQUIRE(mapper->mirror_mode() == expected_mirror(mode));
            for (const auto addr : kChrAddresses) {
                require_chr(*mapper, addr, mode, nametable);
            }
        }
    }

    mapper->write_prg(0x8000U, 0xC0U);
    observe_nametable(*mapper, 1U);
    for (Address addr = 0U; addr < kRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0x8000U, 0U);
    for (Address addr = 0U; addr < kRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
    }

    const auto before = state(*mapper);
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0x3FFFU) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xA5U);
    mapper->write_chr(0x3FFFU, 0x5AU);
    mapper->write_chr(0xFFFFU, 0xFFU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 514 tracks raw nametable offsets without read side effects",
          "[mapper514][decoding][banking][chr][ciram][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0x3FFFU; ++raw) {
        mapper->reset();
        mapper->write_prg(0x8000U, 0xC0U);
        observe_nametable(*mapper, 1U);
        auto expected = register_state(*mapper);
        const auto addr = static_cast<Address>(raw);
        if (addr >= 0x2000U && addr < 0x3F00U) {
            expected[kNametableStateOffset] = static_cast<Byte>((addr >> 10U) & 3U);
        }

        const auto before = register_state(*mapper);
        if (addr < 0x2000U) {
            (void)mapper->read_chr(addr);
            REQUIRE(register_state(*mapper) == before);
        }
        mapper->observe_ppu_address(addr);
        CAPTURE(raw, expected[kNametableStateOffset]);
        REQUIRE(register_state(*mapper) == expected);
        mapper->observe_ppu_address(addr);
        REQUIRE(register_state(*mapper) == expected);
    }
    const auto before = register_state(*mapper);
    mapper->observe_ppu_address(0xFFFFU);
    REQUIRE(register_state(*mapper) == before);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 514 exposes the documented CIRAM arrangement without board VRAM",
          "[mapper514][ciram][bus]") {
    auto mapper = board();
    for (const Byte mode : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
        mapper->write_prg(0x8000U, mode);
        REQUIRE(mapper->mirror_mode() == expected_mirror(mode));
        for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
            CAPTURE(mode, addr);
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

TEST_CASE("Mapper 514 soft reset restores arrangement and retains RAM while power clears it",
          "[mapper514][reset][ram][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, mirror);
        const auto cold = state(*mapper);
        mapper->write_prg(0x6123U, 0xA5U);
        mapper->write_chr(0x0321U, 0x5AU);
        mapper->write_chr(0x1321U, 0x6BU);
        mapper->write_prg(0x8000U, 0xFFU);
        observe_nametable(*mapper, 3U);
        REQUIRE(state(*mapper) != cold);

        mapper->reset();
        REQUIRE(register_state(*mapper)[kModeStateOffset] == initial_mode(mirror));
        REQUIRE(register_state(*mapper)[kNametableStateOffset] == 0U);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
        mapper->write_prg(0x8000U, 0U);
        REQUIRE(mapper->read_chr(0x0321U) == 0x5AU);
        REQUIRE(mapper->read_chr(0x1321U) == 0x6BU);

        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(std::ranges::all_of(std::span(cold).subspan(kWramStateOffset),
                                    [](Byte value) { return value == 0U; }));
    }
}

TEST_CASE("Mapper 514 has no IRQ audio battery or unrelated observer effects",
          "[mapper514][irq][audio][ram][bus]") {
    auto mapper = board();
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_chr(0x0123U, 0x5AU);
    mapper->write_prg(0x8000U, 0xFFU);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_read(0x2012U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 514 bounded snapshots replay latches and RAM without PRG ROM",
          "[mapper514][state][banking][ram][ciram]") {
    auto mapper = board(kDefaultPrgSize, MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0x6123U, 0xA6U);
    mapper->write_chr(0x0321U, 0x5AU);
    mapper->write_chr(0x1432U, 0x6BU);
    mapper->write_prg(0x8000U, 0xDFU);
    observe_nametable(*mapper, 2U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kModeStateOffset] == 0xDFU);
    REQUIRE(saved[kNametableStateOffset] == 2U);

    auto copy = board(kDefaultPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0xDFU, kDefaultPrgSize, 0x80U);
    }
    REQUIRE(copy->read_prg(0x6123U) == 0xA6U);
    REQUIRE(copy->read_chr(0x0321U) == 0x5AU);
    REQUIRE(copy->read_chr(0x1432U) == 0x6BU);

    copy->power_on();
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 514 rejects truncated damaged and mismatched states atomically",
          "[mapper514][state][ram][ciram]") {
    auto mapper = board();
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_chr(0x0123U, 0x5AU);
    mapper->write_chr(0x1123U, 0x6BU);
    mapper->write_prg(0x8000U, 0xDFU);
    observe_nametable(*mapper, 2U);
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
    for (std::size_t offset = 0U; offset < kModeStateOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte nametable : {Byte{4U}, Byte{0x40U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kNametableStateOffset] = nametable;
        CAPTURE(nametable);
        reject(invalid);
    }

    auto wrong_prg =
        std::make_unique<Mapper514>(header(), rom(kDefaultPrgSize / 2U, 0U), std::vector<Byte>{});
    const auto before_prg = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == before_prg);

    auto wrong_mirror = board(kDefaultPrgSize, MirrorMode::Vertical);
    const auto before_mirror = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before_mirror);

    auto changed = saved;
    changed[kModeStateOffset] = 0xD3U;
    changed[kNametableStateOffset] = 1U;
    changed[kWramStateOffset + 0x123U] ^= 0xFFU;
    changed[kChrRamStateOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
