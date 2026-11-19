#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper450.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x080000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x080000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kPrgBanksOffset = 11UZ;
constexpr std::size_t kChrBanksOffset = 13UZ;
constexpr std::size_t kMirrorOffset = 21UZ;
constexpr std::size_t kStateSize = 22UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8123U, 0x9FFFU, 0xA000U, 0xBABCU, 0xC123U, 0xDFFFU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 450U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 15U) * 43U +
                             (offset >> 16U) * 59U + (offset >> 17U) * 61U + (offset >> 18U) * 71U +
                             (offset >> 19U) * 37U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper450> board(std::size_t prg_size = kMaxPrgSize,
                                 std::size_t chr_size = kMaxChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    CAPTURE(prg_size, chr_size, mirror, salt);
    const auto h = header(mirror);
    REQUIRE(Mapper450::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper450>(h,
                                       rom(prg_size, static_cast<Byte>(0x35U + salt)),
                                       rom(chr_size, static_cast<Byte>(0xA5U + salt)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper450& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void select_chr(Mapper450& mapper, unsigned slot, Byte low, Byte high) {
    const auto reg = static_cast<Address>(0xB000U + (slot / 2U) * 0x1000U + (slot & 1U) * 2U);
    mapper.write_prg(reg, low);
    mapper.write_prg(static_cast<Address>(reg + 1U), high);
}

std::size_t expected_prg(Address addr, Byte outer, Byte bank0, Byte bank1) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned inner = slot == 0U   ? bank0 & 0x1FU
                           : slot == 1U ? bank1 & 0x1FU
                                        : 30U + slot - 2U;
    return ((inner | (outer << 5U)) * kPrgBankSize + (addr & 0x1FFFU));
}

void require_prg(Mapper450& mapper,
                 Address addr,
                 Byte outer,
                 Byte bank0,
                 Byte bank1,
                 std::size_t prg_size = kMaxPrgSize,
                 Byte salt = 0U) {
    const auto physical = expected_prg(addr, outer, bank0, bank1) % prg_size;
    CAPTURE(addr, outer, bank0, bank1, physical, prg_size, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, static_cast<Byte>(0x35U + salt)));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 450 validates decoded geometry memory and CIRAM metadata",
          "[mapper450][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const auto& sizes : {std::pair{kMinPrgSize, kMinChrSize},
                                  std::pair{0x00A000UZ, 0x002C00UZ},
                                  std::pair{kMaxPrgSize, kMaxChrSize}}) {
            auto mapper = board(sizes.first, sizes.second, mirror);
            auto different = board(sizes.first, sizes.second, mirror, 0x80U);
            CAPTURE(mirror, sizes.first, sizes.second);
            REQUIRE(state(*mapper) == state(*different));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (std::size_t size = kMinPrgSize; size <= kMaxPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper450::validate_geometry(header(), size, kMinChrSize));
    }
    for (std::size_t size = kMinChrSize; size <= kMaxChrSize; size += kChrBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper450::validate_geometry(header(), kMinPrgSize, size));
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
        REQUIRE_FALSE(Mapper450::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 22U, 47U, 449U, 451U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, kMinChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, kMinChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x80001UZ, 0x100000UZ}) {
        reject(header(), size, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x400UZ, 0x1FFFUZ, 0x2001UZ, 0x2401UZ, 0x80001UZ, 0x100000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, kMinChrSize);
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x2000U;
            break;
        case 1U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        case 4U:
            h.chr_nvram_size = 0x2000U;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, kMinChrSize);
    }
}

TEST_CASE("Mapper 450 outer writes cover only 6000-6FFF and low reads float",
          "[mapper450][decoding][bus][ram]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x2000U; ++raw) {
        const auto addr = static_cast<Address>(0x6000U + raw);
        const auto value = static_cast<Byte>(raw * 41U + 0xA5U);
        auto expected = state(*mapper);
        if (raw < 0x1000U) {
            expected[kOuterOffset] = value & 3U;
        }
        CAPTURE(raw, addr, value);
        mapper->write_prg(addr, value);
        REQUIRE(state(*mapper) == expected);
        mapper->write_prg(addr, static_cast<Byte>(~value));
        if (raw < 0x1000U) {
            expected[kOuterOffset] = static_cast<Byte>(~value) & 3U;
        }
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }
    for (const Address addr : {0x0000U, 0x4017U, 0x4020U, 0x5FFFU}) {
        const auto before = state(*mapper);
        mapper->write_prg(addr, 0xFFU);
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 450 decodes direct A0 A1 VRC2 ports and ignores F-page writes",
          "[mapper450][decoding][irq]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        actual->power_on();
        expected->power_on();
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U));
        const unsigned group = raw >> 12U;
        auto canonical = static_cast<Address>(raw & 0xF000U);
        if (group >= 11U && group <= 14U) {
            canonical = static_cast<Address>(canonical | (raw & 3U));
        }
        actual->write_prg(static_cast<Address>(raw), value);
        if (group != 15U) {
            expected->write_prg(canonical, value);
        }
        CAPTURE(raw, canonical, value);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE_FALSE(actual->irq_pending());
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(raw)));
    }
}

TEST_CASE("Mapper 450 replaces PRG A18 A17 after inner bank decoding",
          "[mapper450][banking][prg]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 4U; ++outer) {
        mapper->write_prg(0x6FFFU, outer);
        for (unsigned value = 0U; value < 256U; ++value) {
            const auto bank0 = static_cast<Byte>(value);
            const auto bank1 = static_cast<Byte>(value ^ 0xA5U);
            mapper->write_prg(0x8003U, bank0);
            mapper->write_prg(0xA002U, bank1);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, outer, bank0, bank1);
            }
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0x00A000UZ, 0x018000UZ}) {
        auto fitted = board(size, kMinChrSize);
        fitted->write_prg(0x6000U, 3U);
        fitted->write_prg(0x8000U, 0xFFU);
        fitted->write_prg(0xA000U, 0xE7U);
        for (const auto addr : kPrgAddresses) {
            require_prg(*fitted, addr, 3U, 0x1FU, 7U, size);
        }
    }
}

TEST_CASE("Mapper 450 uses unshifted VRC2b CHR banks and outer A18 A17",
          "[mapper450][banking][chr][bus]") {
    auto mapper = board();
    for (Byte outer = 0U; outer < 4U; ++outer) {
        mapper->write_prg(0x6000U, outer);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            for (unsigned value = 0U; value < 256U; ++value) {
                const auto high = static_cast<Byte>(value ^ 0x5AU);
                select_chr(*mapper, slot, static_cast<Byte>(value), high);
                const unsigned inner = (value & 0x0FU) | ((high & 0x0FU) << 4U);
                const unsigned bank = (inner & 0x7FU) | (outer << 7U);
                for (const Address offset : {0U, 1U, 0x155U, 0x3FFU}) {
                    const auto addr = static_cast<Address>(slot * kChrBankSize + offset);
                    const auto physical = bank * kChrBankSize + offset;
                    CAPTURE(outer, slot, value, high, addr, physical);
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0xA5U));
                    mapper->write_chr(addr, static_cast<Byte>(~value));
                    REQUIRE(mapper->read_chr(addr) == cell(physical, 0xA5U));
                }
            }
        }
    }

    for (unsigned bank = 0U; bank < 256U; ++bank) {
        const unsigned slot = bank & 7U;
        select_chr(*mapper, slot, static_cast<Byte>(bank), static_cast<Byte>(bank >> 4U));
        mapper->write_prg(0x6000U, static_cast<Byte>(bank & 3U));
        const auto physical = (((bank & 0x7FU) | ((bank & 3U) << 7U)) * kChrBankSize + 0x234U);
        CAPTURE(bank, slot, physical);
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * kChrBankSize + 0x234U)) ==
                cell(physical, 0xA5U));
    }

    auto fitted = board(kMinPrgSize, kMinChrSize);
    fitted->write_prg(0x6000U, 3U);
    select_chr(*fitted, 7U, 0xFFU, 0xFFU);
    for (Address offset = 0U; offset < kChrBankSize; ++offset) {
        const auto physical = ((0x1FFUZ * kChrBankSize + offset) % kMinChrSize);
        CAPTURE(offset, physical);
        REQUIRE(fitted->read_chr(static_cast<Address>(7U * kChrBankSize + offset)) ==
                cell(physical, 0xA5U));
    }
}

TEST_CASE("Mapper 450 provides only VRC2 H V CIRAM control", "[mapper450][ciram][decoding]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned port = 0U; port < 4U; ++port) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(static_cast<Address>(0x9000U + port), static_cast<Byte>(value));
            CAPTURE(port, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
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

TEST_CASE("Mapper 450 has no RAM IRQ or expansion audio state",
          "[mapper450][bus][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 3U);
    mapper->write_prg(0x8000U, 0xA5U);
    mapper->write_prg(0xB000U, 0x5AU);
    const auto before = state(*mapper);
    for (unsigned addr = 0xF000U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 19U));
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->on_ppu_hblank();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);
    }
    mapper->acknowledge_irq();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_frame_start();
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    const std::array<Byte, 4> imported{1U, 2U, 3U, 4U};
    mapper->set_battery_ram(imported);
    mapper->set_shared_nvram(imported);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->maps_expansion(0x5000U));
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 450 reset restores deterministic VRC2 defaults", "[mapper450][reset][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxPrgSize, kMaxChrSize, mirror, 0x35U);
        const auto initial = state(*mapper);
        mapper->write_prg(0x6FFFU, 0xFFU);
        mapper->write_prg(0x8000U, 0xA5U);
        mapper->write_prg(0xA000U, 0x5AU);
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            select_chr(*mapper, slot, static_cast<Byte>(slot * 17U), static_cast<Byte>(slot * 29U));
        }
        mapper->write_prg(0x9003U, mirror == MirrorMode::Vertical ? 0U : 1U);
        mapper->write_prg(0xF002U, 7U);
        REQUIRE(state(*mapper) != initial);
        mapper->reset();
        REQUIRE(state(*mapper) == initial);
        REQUIRE(mapper->mirror_mode() == mirror);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, 0U, 0U, 1U, kMaxPrgSize, 0x35U);
        }
        mapper->write_prg(0x6000U, 2U);
        mapper->write_prg(0x8000U, 9U);
        mapper->power_on();
        REQUIRE(state(*mapper) == initial);
    }
}

TEST_CASE("Mapper 450 bounded snapshots replay all latches without ROM",
          "[mapper450][state][banking]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, 0x10U);
    mapper->write_prg(0x6000U, 3U);
    mapper->write_prg(0x8000U, 0xC5U);
    mapper->write_prg(0xA000U, 0x9AU);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        select_chr(
            *mapper, slot, static_cast<Byte>(slot * 23U + 5U), static_cast<Byte>(slot * 31U + 7U));
    }
    mapper->write_prg(0x9000U, 1U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == 3U);
    REQUIRE(saved[kPrgBanksOffset] == 5U);
    REQUIRE(saved[kPrgBanksOffset + 1U] == 0x1AU);
    REQUIRE(saved[kMirrorOffset] == static_cast<Byte>(MirrorMode::Horizontal));

    auto copy = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 3U, 5U, 0x1AU, kMaxPrgSize, 0x80U);
    }
    for (Address addr = 0U; addr < 0x2000U; addr += 0x0101U) {
        const unsigned slot = addr >> 10U;
        const unsigned inner = saved[kChrBanksOffset + slot];
        const auto physical =
            (((inner & 0x7FU) | (saved[kOuterOffset] << 7U)) * kChrBankSize + (addr & 0x3FFU));
        CAPTURE(addr, slot, inner, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0x25U));
    }
    copy->write_prg(0x7000U, 0U);
    REQUIRE(state(*copy) == saved);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 450 rejects damaged truncated and incompatible states atomically",
          "[mapper450][state][ram]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 3U);
    mapper->write_prg(0x8000U, 0xA5U);
    mapper->write_prg(0xA000U, 0x5AU);
    select_chr(*mapper, 6U, 0xC5U, 0x3AU);
    mapper->write_prg(0x9000U, 1U);
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
    const std::array<std::pair<std::size_t, Byte>, 12> invalid{{
        {0U, 0U},
        {1U, 0U},
        {2U, 1U},
        {5U, 1U},
        {6U, 1U},
        {9U, 1U},
        {kOuterOffset, 4U},
        {kPrgBanksOffset, 0x20U},
        {kPrgBanksOffset + 1U, 0xFFU},
        {kMirrorOffset, static_cast<Byte>(MirrorMode::FourScreen)},
        {kMirrorOffset, static_cast<Byte>(MirrorMode::SingleLower)},
        {kMirrorOffset, 0xFFU},
    }};
    for (const auto& [offset, value] : invalid) {
        auto bad = active;
        bad[offset] = value;
        bad[kChrBanksOffset] ^= 0xFFU;
        CAPTURE(offset, value);
        reject(bad);
    }

    auto wrong_mirror = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Horizontal);
    const auto before = state(*wrong_mirror);
    StateReader mirror_reader(active);
    wrong_mirror->load_state(mirror_reader);
    REQUIRE_FALSE(mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before);

    const auto h = header();
    auto wrong_prg =
        std::make_unique<Mapper450>(h, rom(kMinPrgSize, 0x35U), rom(kMaxChrSize, 0xA5U));
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader prg_reader(active);
    wrong_prg->load_state(prg_reader);
    REQUIRE_FALSE(prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr =
        std::make_unique<Mapper450>(h, rom(kMaxPrgSize, 0x35U), rom(kMinChrSize, 0xA5U));
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader chr_reader(active);
    wrong_chr->load_state(chr_reader);
    REQUIRE_FALSE(chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto changed = active;
    changed[kOuterOffset] = 1U;
    changed[kPrgBanksOffset] = 0x1FU;
    changed[kChrBanksOffset] ^= 0xFFU;
    changed[kMirrorOffset] = static_cast<Byte>(MirrorMode::Vertical);
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
