#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper449.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kChrBankOffset = 12UZ;
constexpr std::size_t kChrRamOffset = 13UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 10> kPrgAddresses{
    0x8000U, 0x8001U, 0x800FU, 0x8010U, 0x8123U, 0x9A05U, 0xBFFFU, 0xC000U, 0xC123U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 449U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + (offset >> 16U) * 59U + (offset >> 18U) * 71U +
                             (offset >> 19U) * 41U + salt);
}

std::vector<Byte> rom(Byte salt = 0U) {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper449> board(MirrorMode mirror = MirrorMode::Vertical,
                                 Byte pads = 0U,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper449::validate_geometry(h, kPrgSize, 0U));
    auto mapper = std::make_unique<Mapper449>(h, rom(salt), std::vector<Byte>{});
    REQUIRE(mapper->configure_board_jumpers(pads));
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper449& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t expected_prg(Address addr, Address latch, Byte pads) {
    const bool upper = (addr & 0x4000U) != 0U;
    const unsigned outer = ((latch >> 5U) & 3U) | ((latch >> 6U) & 4U);
    unsigned bank = (outer << 3U) | ((latch >> 2U) & 7U);
    if ((latch & 0x0080U) == 0U) {
        if (upper) {
            bank = (outer << 3U) | 7U;
        }
    } else if ((latch & 1U) != 0U) {
        bank = (bank & ~1U) | (upper ? 1U : 0U);
    }

    auto offset = static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
    if ((latch & 0x0200U) != 0U) {
        offset = (offset & ~0x0FUZ) | pads;
    }
    return offset;
}

void require_prg(Mapper449& mapper, Address addr, Address latch, Byte pads, Byte salt = 0U) {
    const auto physical = expected_prg(addr, latch, pads);
    CAPTURE(addr, latch, pads, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void fill_chr(Mapper449& mapper, Byte salt = 0U) {
    for (Byte bank = 0U; bank < 4U; ++bank) {
        mapper.write_prg(0x8000U, bank);
        for (Address addr = 0U; addr < kChrBankSize; ++addr) {
            mapper.write_chr(addr,
                             cell(static_cast<std::size_t>(bank) * kChrBankSize + addr, salt));
        }
    }
}

TEST_CASE("Mapper 449 validates the exact Super Games King geometry", "[mapper449][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0U, 0x35U);
        auto different_rom = board(mirror, 0U, 0x80U);
        CAPTURE(mirror);
        REQUIRE(Mapper449::validate_geometry(header(mirror), kPrgSize, 0U));
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(7UZ * kPrgBankSize + 0x3FFFU, 0x35U));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
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
        REQUIRE_FALSE(Mapper449::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 380U, 448U, 450U, 4095U}) {
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
         {0UZ, 1UZ, 0x004000UZ, 0x080000UZ, 0x0FFFFFUZ, 0x100001UZ, 0x200000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, 0U);
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
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 5U:
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 0x2000U;
            break;
        case 7U:
            h.chr_ram_size = 0x4000U;
            break;
        case 8U:
            h.chr_ram_size = 0x10000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, 0U);
    }
}

TEST_CASE("Mapper 449 decodes address A9-A0 and data D1-D0 over the full write range",
          "[mapper449][decoding][bus]") {
    auto actual = board();
    auto expected = board();
    fill_chr(*actual, 0x40U);
    fill_chr(*expected, 0x40U);
    const auto prepare = [](Mapper449& mapper) {
        mapper.reset();
        mapper.write_prg(0x82A5U, 0x5AU);
    };

    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        prepare(*actual);
        prepare(*expected);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected->write_prg(static_cast<Address>(0x8000U | (raw & 0x03FFU)), value);
        }
        CAPTURE(raw, value);
        REQUIRE(actual->mirror_mode() == expected->mirror_mode());
        REQUIRE(actual->read_prg(0x9A05U) == expected->read_prg(0x9A05U));
        REQUIRE(actual->read_prg(0xC123U) == expected->read_prg(0xC123U));
        REQUIRE(actual->read_chr(0x0123U) == expected->read_chr(0x0123U));
        REQUIRE(actual->maps_prg(static_cast<Address>(raw)) == (raw >= 0x8000U));
        REQUIRE(actual->prg_read_mask(static_cast<Address>(raw)) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }

    actual->reset();
    expected->reset();
    for (unsigned value = 0U; value < 256U; ++value) {
        actual->write_prg(0x8000U, static_cast<Byte>(value));
        expected->write_prg(0x8000U, static_cast<Byte>(value & 3U));
        CAPTURE(value);
        REQUIRE(state(*actual) == state(*expected));
    }
    actual->write_prg_cpu(0xFFFFU, 0xFFU, true);
    expected->write_prg(0xFFFFU, 0x03U);
    REQUIRE(state(*actual) == state(*expected));
}

TEST_CASE("Mapper 449 maps QQQ outer and PPp inner banks in all three PRG modes",
          "[mapper449][banking][prg]") {
    auto mapper = board(MirrorMode::Vertical, 0x0BU);
    for (Address latch = 0U; latch <= 0x03FFU; ++latch) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), static_cast<Byte>(latch));
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, latch, 0x0BU);
        }
        CAPTURE(latch);
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    for (const Address latch : {0x0000U,
                                0x0001U,
                                0x007CU,
                                0x0080U,
                                0x0081U,
                                0x0083U,
                                0x01FFU,
                                0x0200U,
                                0x02A5U,
                                0x0380U,
                                0x0381U,
                                0x03FFU}) {
        mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            require_prg(*mapper, static_cast<Address>(raw), latch, 0x0BU);
        }
    }
}

TEST_CASE("Mapper 449 selects four independent writable 8 KiB CHR RAM banks",
          "[mapper449][banking][chr][ram]") {
    auto mapper = board();
    fill_chr(*mapper, 0x55U);
    for (Byte bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xFFFFU, static_cast<Byte>(bank | 0xFCU));
        for (Address addr = 0U; addr < kChrBankSize; ++addr) {
            const auto physical = static_cast<std::size_t>(bank) * kChrBankSize + addr;
            CAPTURE(bank, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                    cell(physical, 0x55U));
        }
    }

    for (Address latch = 0U; latch <= 0x03FFU; ++latch) {
        const auto data = static_cast<Byte>(latch * 37U);
        mapper->write_prg(static_cast<Address>(0x8000U | latch), data);
        const auto physical = static_cast<std::size_t>(data & 3U) * kChrBankSize + 0x1234U;
        CAPTURE(latch, data, physical);
        REQUIRE(mapper->read_chr(0x1234U) == cell(physical, 0x55U));
        mapper->write_chr(0x3234U, static_cast<Byte>(latch));
        REQUIRE(mapper->read_chr(0x1234U) == static_cast<Byte>(latch));
        mapper->write_chr(0x1234U, cell(physical, 0x55U));
    }
}

TEST_CASE("Mapper 449 uses ordinary latch-controlled H/V CIRAM only", "[mapper449][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x0101U) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
}

TEST_CASE("Mapper 449 leaves CPU RAM expansion IRQ and audio absent",
          "[mapper449][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0xFFFFU, 0xFFU);
    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        const auto addr = static_cast<Address>(raw);
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
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

TEST_CASE("Mapper 449 solder pads replace PRG A3-A0 only in menu mode",
          "[mapper449][banking][bus][reset]") {
    auto mapper = board();
    for (Byte pads = 0U; pads < 16U; ++pads) {
        REQUIRE(mapper->configure_board_jumpers(pads));
        mapper->write_prg(0x8000U, 0U);
        const auto ordinary = mapper->read_prg(0x8000U);
        mapper->write_prg(0x8200U, 0U);
        CAPTURE(pads);
        REQUIRE(ordinary == cell(0U));
        REQUIRE(mapper->read_prg(0x8000U) == cell(pads));
        REQUIRE(state(*mapper)[0] == static_cast<Byte>(0x90U | pads));
        mapper->reset();
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    }
    const auto saved = state(*mapper);
    for (const Byte pads : {16U, 31U, 255U}) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(pads));
        REQUIRE(state(*mapper) == saved);
    }
    REQUIRE_FALSE(mapper->configure_board_profile("menu-1"));
    REQUIRE(state(*mapper) == saved);
}

TEST_CASE("Mapper 449 reset restores zero latches and cold power clears CHR RAM",
          "[mapper449][reset][ram]") {
    auto mapper = board(MirrorMode::Horizontal, 13U, 0x35U);
    const auto initial = state(*mapper);
    fill_chr(*mapper, 0xA5U);
    mapper->write_prg(0xFFFFU, 0xFFU);
    const auto programmed = state(*mapper);
    const auto programmed_chr = std::span(programmed).subspan(kChrRamOffset, kChrRamSize);

    mapper->reset();
    const auto reset = state(*mapper);
    REQUIRE(reset[kLatchOffset] == 0U);
    REQUIRE(reset[kLatchOffset + 1U] == 0U);
    REQUIRE(reset[kChrBankOffset] == 0U);
    REQUIRE(
        std::ranges::equal(std::span(reset).subspan(kChrRamOffset, kChrRamSize), programmed_chr));
    REQUIRE(reset[0] == initial[0]);
    REQUIRE(reset[1] == initial[1]);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U, 0x35U));
    REQUIRE(mapper->read_prg(0xFFFFU) == cell(7UZ * kPrgBankSize + 0x3FFFU, 0x35U));
    mapper->write_chr(0x0123U, 0x67U);
    REQUIRE(mapper->read_chr(0x0123U) == 0x67U);

    mapper->write_prg(0xFFFFU, 0xFFU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    for (Byte bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0x8000U, bank);
        for (Address addr = 0U; addr < kChrBankSize; addr += 0x0101U) {
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 449 bounded snapshots replay latches and CHR RAM without ROM",
          "[mapper449][state][banking][ram]") {
    auto mapper = board(MirrorMode::Vertical, 9U);
    fill_chr(*mapper, 0xA5U);
    mapper->write_prg(0x83A7U, 0xFEU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 32781U);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kLatchOffset] == 0xA7U);
    REQUIRE(saved[kLatchOffset + 1U] == 0x03U);
    REQUIRE(saved[kChrBankOffset] == 2U);

    auto copy = board(MirrorMode::Horizontal, 9U, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x03A7U, 9U, 0x80U);
    }
    for (Byte bank = 0U; bank < 4U; ++bank) {
        copy->write_prg(0x8000U, bank);
        for (Address addr = 0U; addr < kChrBankSize; addr += 0x0101U) {
            const auto physical = static_cast<std::size_t>(bank) * kChrBankSize + addr;
            REQUIRE(copy->read_chr(addr) == cell(physical, 0xA5U));
        }
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

TEST_CASE("Mapper 449 rejects damaged truncated and incompatible states atomically",
          "[mapper449][state][ram]") {
    auto mapper = board(MirrorMode::Vertical, 9U);
    fill_chr(*mapper, 0xA5U);
    mapper->write_prg(0x83A7U, 0xFEU);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };

    for (std::size_t length = 0U; length < active.size(); ++length) {
        CAPTURE(length);
        reject(std::span(active).first(length));
    }
    for (unsigned bit = 0U; bit < kLatchOffset * 8U; ++bit) {
        auto invalid = active;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(bit);
        reject(invalid);
    }
    for (const Byte reserved : {0x04U, 0x08U, 0x10U, 0x20U, 0x40U, 0x80U, 0xFCU}) {
        auto invalid = active;
        invalid[kLatchOffset + 1U] |= reserved;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(reserved);
        reject(invalid);
    }
    for (const Byte bank : {4U, 8U, 0x80U, 0xFFU}) {
        auto invalid = active;
        invalid[kChrBankOffset] = bank;
        invalid[kChrRamOffset + 0x123U] ^= 0xFFU;
        CAPTURE(bank);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper449>& other) {
        const auto before = state(*other);
        StateReader reader(active);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_pads = board(MirrorMode::Vertical, 8U);
    mismatch(wrong_pads);
    const auto h = header();
    auto wrong_prg =
        std::make_unique<Mapper449>(h, std::vector<Byte>(kPrgSize / 2U), std::vector<Byte>{});
    mismatch(wrong_prg);

    auto changed = active;
    changed[kLatchOffset] = 0x5AU;
    changed[kLatchOffset + 1U] = 0x01U;
    changed[kChrBankOffset] = 3U;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

} // namespace
