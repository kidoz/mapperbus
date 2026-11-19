#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper435.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = Mapper435::kPrgSize;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = Mapper435::kChrRamSize;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kChrRamOffset = 12UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr Address kLatchMask = 0x0F7FU;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 435U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 14U) * 47U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 83U + (offset >> 20U) * 97U +
                             salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper435> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper435::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper435>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Address saved_latch(const Mapper& mapper) {
    const auto bytes = state(mapper);
    return static_cast<Address>(bytes[kLatchOffset] |
                                (static_cast<Address>(bytes[kLatchOffset + 1U]) << 8U));
}

void load(Mapper435& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_offset(Address addr, Address latch) {
    const bool upper = (addr & 0x4000U) != 0U;
    const unsigned outer =
        ((latch & 0x0400U) != 0U ? 0x08U : 0U) | ((latch & 0x0100U) != 0U ? 0x04U : 0U) |
        ((latch & 0x0040U) != 0U ? 0x02U : 0U) | ((latch & 0x0020U) != 0U ? 0x01U : 0U);
    unsigned inner = (latch >> 2U) & 0x07U;
    if ((latch & 0x0200U) == 0U) {
        if (upper) {
            inner = 7U;
        }
    } else if ((latch & 0x0001U) == 0U) {
        inner = (inner & 0x06U) | static_cast<unsigned>(upper);
    }
    return (((outer << 3U) | inner) * kPrgBankSize) + (addr & 0x3FFFU);
}

TEST_CASE("Mapper 435 validates only the documented NES 2.0 population",
          "[mapper435][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        CAPTURE(mirror);
        REQUIRE(Mapper435::validate_geometry(header(mirror), kPrgSize, 0U));
        auto mapper = board(mirror);
        auto identical_state = board(mirror, 0x80U);
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
        REQUIRE_FALSE(Mapper435::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 380U, 434U, 436U, 4095U}) {
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
    for (const std::size_t size :
         {0UZ, 1UZ, 0x004000UZ, 0x008000UZ, 0x100000UZ, 0x1FFFFFUZ, 0x200001UZ, 0x400000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x100000UZ}) {
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

TEST_CASE("Mapper 435 decodes address pins only across the complete CPU range",
          "[mapper435][decoding][bus]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(saved_latch(*mapper) == (raw >= 0x8000U ? raw & kLatchMask : 0U));
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    for (Address latch = 0U; latch <= 0x0FFFU; ++latch) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        CAPTURE(latch);
        REQUIRE(saved_latch(*mapper) == (latch & kLatchMask));
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 0x0002U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    mapper->write_prg_cpu(0xFFFFU, 0U, false);
    REQUIRE(saved_latch(*mapper) == 0x0F7FU);
    mapper->write_prg_cpu(0x8000U, 0xFFU, true);
    REQUIRE(saved_latch(*mapper) == 0U);
}

TEST_CASE("Mapper 435 composes all outer and inner PRG pins in every mode",
          "[mapper435][decoding][banking][prg]") {
    auto mapper = board();
    for (Address latch = 0U; latch <= 0x0FFFU; ++latch) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0xA5U);
        for (const auto addr : kPrgAddresses) {
            const auto physical = expected_offset(addr, latch & kLatchMask);
            CAPTURE(latch, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
    }

    for (const Address latch : {0x0000U,
                                0x0001U,
                                0x001CU,
                                0x0020U,
                                0x0040U,
                                0x0100U,
                                0x0200U,
                                0x0201U,
                                0x0400U,
                                0x06A0U,
                                0x0F7FU}) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const auto physical = expected_offset(addr, latch);
            CAPTURE(latch, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 435 UNROM and NROM forms implement the effective banking table",
          "[mapper435][banking][decoding]") {
    auto mapper = board();
    for (const Address outer_latch : {0x0000U, 0x0020U, 0x0040U, 0x0100U, 0x0400U, 0x0560U}) {
        for (Address inner = 0U; inner < 8U; ++inner) {
            const Address base = static_cast<Address>(outer_latch | (inner << 2U));
            mapper->write_prg(static_cast<Address>(0x8000U | base), 0U);
            CAPTURE(outer_latch, inner);
            REQUIRE(mapper->read_prg(0x8000U) == cell(expected_offset(0x8000U, base)));
            REQUIRE(mapper->read_prg(0xC000U) ==
                    cell(expected_offset(0xC000U, static_cast<Address>(base | 0x001CU))));

            mapper->write_prg(static_cast<Address>(0x8000U | base | 0x0200U), 0U);
            const auto even_bank = expected_offset(0x8000U, static_cast<Address>(base | 0x0200U));
            const auto odd_bank = expected_offset(0xC000U, static_cast<Address>(base | 0x0200U));
            REQUIRE(mapper->read_prg(0x8000U) == cell(even_bank));
            REQUIRE(mapper->read_prg(0xC000U) == cell(odd_bank));
            REQUIRE((odd_bank - even_bank) == kPrgBankSize);

            mapper->write_prg(static_cast<Address>(0x8000U | base | 0x0201U), 0U);
            REQUIRE(mapper->read_prg(0x8000U) == mapper->read_prg(0xC000U));
        }
    }
}

TEST_CASE("Mapper 435 owns flat CHR RAM and honors only the C write-protect pin",
          "[mapper435][ram][banking]") {
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

    for (Address latch = 0U; latch <= 0x0FFFU; ++latch) {
        mapper->write_prg(0x8000U, 0U);
        mapper->write_chr(0x1234U, 0x5AU);
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        mapper->write_chr(0x1234U, 0xA7U);
        CAPTURE(latch);
        REQUIRE(mapper->read_chr(0x1234U) == ((latch & 0x0800U) == 0U ? 0xA7U : 0x5AU));
    }
}

TEST_CASE("Mapper 435 uses ordinary CIRAM and no native PPU register or pin hooks",
          "[mapper435][ciram][bus]") {
    auto mapper = board(MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Address latch = 0U; latch <= 0x0FFFU; ++latch) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        CAPTURE(latch);
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
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
}

TEST_CASE("Mapper 435 exposes no low CPU RAM expansion device IRQ or audio",
          "[mapper435][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0xFFFFU, 0U);
    mapper->write_chr(0x0123U, 0xA5U);
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
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
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
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 435 reset clears the latch and cold power also clears CHR RAM",
          "[mapper435][reset][banking][ciram][ram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial, 0x35U);
        const auto cold = state(*mapper);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            mapper->write_chr(addr, cell(addr, 0xA5U));
        }
        mapper->write_prg(0xFFFFU, 0U);
        const auto programmed = state(*mapper);
        const auto programmed_chr = std::span(programmed).subspan(kChrRamOffset, kChrRamSize);

        mapper->reset();
        const auto reset = state(*mapper);
        CAPTURE(initial);
        REQUIRE(saved_latch(*mapper) == 0U);
        REQUIRE(std::ranges::equal(std::span(reset).subspan(kChrRamOffset, kChrRamSize),
                                   programmed_chr));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(7UZ * kPrgBankSize, 0x35U));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(8UZ * kPrgBankSize - 1U, 0x35U));
        mapper->write_chr(0x0123U, 0x67U);
        REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

        mapper->write_prg(0xFFFFU, 0U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 435 bounded snapshots replay latch and RAM without serializing ROM",
          "[mapper435][state][banking][ram]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0x8765U, 0xFFU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved_latch(*mapper) == 0x0765U);

    auto copy = board(MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_offset(addr, 0x0765U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
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

TEST_CASE("Mapper 435 rejects truncated damaged and mismatched states atomically",
          "[mapper435][state][ram]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrRamSize; addr += 0x0101U) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_prg(0x8765U, 0U);
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
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Address reserved : {0x0080U, 0x1000U, 0x2000U, 0x4000U, 0x8000U, 0xF080U}) {
        auto invalid = saved;
        invalid[kLatchOffset] |= static_cast<Byte>(reserved);
        invalid[kLatchOffset + 1U] |= static_cast<Byte>(reserved >> 8U);
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(reserved);
        reject(invalid);
    }

    auto wrong_prg = std::make_unique<Mapper435>(header(), rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto changed = saved;
    changed[kLatchOffset] = 0x21U;
    changed[kLatchOffset + 1U] = 0x06U;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(expected_offset(0x8000U, 0x0621U)));
}

} // namespace
