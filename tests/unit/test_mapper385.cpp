#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper385.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x020000UZ;
constexpr std::size_t kFiveGamePrgSize = 0x014000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kChrRamOffset = 11UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 385U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper385> board(std::size_t prg_size = kPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper385::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper385>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper385& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

TEST_CASE("Mapper 385 validates NTDEC 2779 geometry and CHR-RAM metadata", "[mapper385]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = kPrgBankSize; prg <= kPrgSize; prg += kPrgBankSize) {
            CAPTURE(mirror, prg);
            REQUIRE(Mapper385::validate_geometry(header(mirror), prg, 0U));
        }
        auto mapper = board(kFiveGamePrgSize, mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8203U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const unsigned id : {0U, 2U, 202U, 384U, 386U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper385::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper385::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper385::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x3FFFUZ, 0x4001UZ, 0x6000UZ, 0x20001UZ, 0x24000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper385::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper385::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper385::validate_geometry(header(mirror), kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
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
            break;
        case 3U:
            h.chr_ram_size = 0U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper385::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 385 latches address bits at every CPU write alias and ignores data",
          "[mapper385][bus][banking][ciram]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw * 41U + (raw >> 8U)));
        const auto bank = static_cast<std::size_t>((raw >> 1U) & 7U);
        CAPTURE(raw, bank);
        REQUIRE(mapper->read_prg(0x8123U) == cell(bank * kPrgBankSize + 0x123U));
        REQUIRE(mapper->read_prg(0xC123U) == cell(bank * kPrgBankSize + 0x123U));
        REQUIRE(mapper->mirror_mode() ==
                ((raw & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    mapper->reset();
    const auto initial = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
        CAPTURE(raw);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xFFFFU, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kLatchOffset] == 0x0FU);
        REQUIRE(mapper->read_prg(0x8000U) == cell(7UZ * kPrgBankSize));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    }
    mapper->reset();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 385 mirrors one decoded 16 KiB PRG page into both CPU windows",
          "[mapper385][banking]") {
    auto mapper = board();
    for (Byte bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(static_cast<Address>(0x8000U | (bank << 1U)), 0xA5U);
        for (Address offset = 0U; offset < kPrgBankSize; ++offset) {
            const auto physical = static_cast<std::size_t>(bank) * kPrgBankSize + offset;
            CAPTURE(bank, offset, physical);
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) == cell(physical));
            REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) == cell(physical));
        }
    }

    for (const std::size_t size : {kPrgBankSize, kFiveGamePrgSize}) {
        auto fitted = board(size);
        for (Byte bank = 0U; bank < 8U; ++bank) {
            fitted->write_prg(static_cast<Address>(0x8000U | (bank << 1U)), 0U);
            for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
                const auto physical =
                    static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
                CAPTURE(size, bank, addr, physical);
                REQUIRE(fitted->read_prg(addr) == cell(physical % size));
            }
        }
    }
}

TEST_CASE("Mapper 385 provides flat writable 8 KiB CHR RAM independent of the latch",
          "[mapper385][ram][banking]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == 0U);
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    for (Byte latch = 0U; latch < 16U; ++latch) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            CAPTURE(latch, addr);
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == cell(addr, 0xA5U));
        }
    }
}

TEST_CASE("Mapper 385 selects only vertical or horizontal console CIRAM", "[mapper385][ciram]") {
    auto mapper = board(kPrgSize, MirrorMode::Horizontal);
    for (Byte latch = 0U; latch < 16U; ++latch) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        CAPTURE(latch);
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 385 leaves CPU RAM expansion IRQ and audio hardware absent",
          "[mapper385][bus][ram][irq][audio]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    const auto before = state(*mapper);
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
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 385 soft reset clears the latch and cold power also clears CHR RAM",
          "[mapper385][reset][ram]") {
    auto mapper = board(kPrgSize, MirrorMode::Horizontal, 0x35U);
    const auto initial = state(*mapper);
    mapper->write_prg(0x800FU, 0U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    REQUIRE(mapper->read_prg(0x8000U) == cell(7UZ * kPrgBankSize, 0x35U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(0x3FFFU, 0x35U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x5AU));
    }

    mapper->write_prg(0x800FU, 0U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == 0U);
    }
}

TEST_CASE("Mapper 385 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper385][state]") {
    auto mapper = board(kFiveGamePrgSize, MirrorMode::Vertical, 0x20U);
    mapper->write_prg(0x800DU, 0U);
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA7U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x3000UZ);
    REQUIRE(saved[kLatchOffset] == 0x0DU);

    auto copy = board(kFiveGamePrgSize, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
        const auto physical = (6UZ * kPrgBankSize + (addr & 0x3FFFU)) % kFiveGamePrgSize;
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(copy->read_chr(addr) == cell(addr, 0xA7U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);

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
    for (const Byte latch : {0x10U, 0x20U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kLatchOffset] = latch;
        CAPTURE(latch);
        reject(invalid);
    }

    auto wrong_geometry = board(kPrgSize);
    const auto before = state(*wrong_geometry);
    StateReader wrong_reader(saved);
    wrong_geometry->load_state(wrong_reader);
    REQUIRE_FALSE(wrong_reader.ok());
    REQUIRE(state(*wrong_geometry) == before);
}

} // namespace
