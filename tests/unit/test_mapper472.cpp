#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper472.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kChrSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kGeometrySize = 10UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kDipOffset = 11UZ;
constexpr std::size_t kStateSize = 27UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8123U, 0x9FFFU, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 472U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kPrgSize;
    result.chr_rom_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + (offset >> 16U) * 71U +
                             (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper472> board(MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper472::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper472>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x55U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper472& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void outer(Mapper472& mapper, Byte value) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(0x6000U, value);
}

void bank(Mapper472& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned expected_prg_bank(Address addr, Byte outer_bank, Byte r6, Byte r7, Byte mode) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned inner = slot == 1U                               ? r7 & 0x3FU
                           : slot == 3U                             ? 31U
                           : (slot == 0U) == ((mode & 0x40U) != 0U) ? 30U
                                                                    : r6 & 0x3FU;
    return (outer_bank & 0xF0U) | (inner & 0x0FU);
}

unsigned expected_chr_bank(Address addr,
                           Byte outer_bank,
                           const std::array<Byte, 8>& banks,
                           Byte mode) {
    const unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    const unsigned inner = slot < 4U ? (banks[slot / 2U] & 0xFEU) | (slot & 1U) : banks[slot - 2U];
    return (outer_bank & 0x20U) != 0U ? (inner & 0x7FU) | ((outer_bank & 0xF0U) << 3U)
                                      : (inner & 0xFFU) | ((outer_bank & 0xE0U) << 3U);
}

void require_prg(Mapper472& mapper,
                 Address addr,
                 Byte outer_bank,
                 Byte r6,
                 Byte r7,
                 Byte mode,
                 Byte salt = 0U,
                 std::size_t prg_size = kPrgSize) {
    const auto selected = expected_prg_bank(addr, outer_bank, r6, r7, mode);
    const auto physical =
        (static_cast<std::size_t>(selected) * kPrgBankSize + (addr & 0x1FFFU)) % prg_size;
    CAPTURE(addr, outer_bank, r6, r7, mode, selected, physical, salt, prg_size);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 472 validates bounded ROM geometry and has no populated RAM",
          "[mapper472][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = 0x008000UZ; prg <= kPrgSize; prg *= 2U) {
            for (std::size_t chr = 0x002000UZ; chr <= kChrSize; chr *= 2U) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper472::validate_geometry(header(mirror), prg, chr));
            }
        }
        auto mapper = board(mirror, 0x35U, 0x008000UZ, 0x002000UZ);
        auto different_rom = board(mirror, 0x80U, 0x008000UZ, 0x002000UZ);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->read_prg(0x6000U) == 0x80U);
        REQUIRE(mapper->read_prg(0x7FFFU) == 0x80U);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
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
        REQUIRE_FALSE(Mapper472::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 52U, 412U, 471U, 473U, 4095U}) {
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
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x018000UZ, 0x400000UZ, 0x400001UZ}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1800UZ, 0x3000UZ, 0x018000UZ, 0x400000UZ}) {
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

TEST_CASE("Mapper 472 decodes outer aliases and WRAM gates while DIP reads stay driven",
          "[mapper472][decoding][bus][ram]") {
    auto mapper = board(MirrorMode::Vertical, 0U, 0x008000UZ, 0x002000UZ);
    for (unsigned raw = 0x4020U; raw < 0x8000U; ++raw) {
        mapper->power_on();
        mapper->write_prg(0xA001U, 0x80U);
        auto expected = state(*mapper);
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x6000U) {
            expected[kOuterOffset] = value;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->read_prg(addr) == (raw >= 0x6000U ? 0x80U : 0U));
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x6000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x6000U ? 0xFFU : 0U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
    }
    REQUIRE(mapper->read_prg(0x0000U) == 0U);
    REQUIRE(mapper->read_prg(0x5FFFU) == 0U);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    for (const Byte control : {Byte{0U}, Byte{0x40U}, Byte{0x80U}, Byte{0xC0U}}) {
        mapper->power_on();
        outer(*mapper, 0xA5U);
        mapper->write_prg(0xA001U, control);
        mapper->write_prg(0x7FFFU, 0x5AU);
        CAPTURE(control);
        REQUIRE(state(*mapper)[kOuterOffset] == (control == 0x80U ? 0x5AU : 0xA5U));
        REQUIRE(state(*mapper)[26U] == control);
        REQUIRE(mapper->read_prg(0x6000U) == 0x80U);
    }
}

TEST_CASE("Mapper 472 combines outer high nibbles with every MMC3 PRG window",
          "[mapper472][banking][prg][decoding]") {
    auto mapper = board();
    for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
        bank(*mapper, 6U, 0x35U, mode);
        bank(*mapper, 7U, 0x2AU, mode);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto outer_bank = static_cast<Byte>(raw);
            outer(*mapper, outer_bank);
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, outer_bank, 0x35U, 0x2AU, mode);
            }
        }
    }

    auto small = board(MirrorMode::Horizontal, 0x35U, 0x008000UZ, 0x002000UZ);
    outer(*small, 0xFFU);
    bank(*small, 6U, 0xFFU);
    bank(*small, 7U, 0xFFU, 0x40U);
    for (const auto addr : kPrgAddresses) {
        require_prg(*small, addr, 0xFFU, 0x3FU, 0x3FU, 0x40U, 0x35U, 0x008000UZ);
    }
}

TEST_CASE("Mapper 472 applies both outer CHR widths pairing and inversion before wrapping",
          "[mapper472][banking][chr][decoding]") {
    auto mapper = board();
    constexpr std::array<Byte, 6> values = {0xA3U, 0x5CU, 0xD2U, 0x37U, 0xF1U, 0x89U};
    std::array<Byte, 8> banks{};
    for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte index = 0U; index < 6U; ++index) {
            banks[index] = values[index];
            bank(*mapper, index, values[index], mode);
        }
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto outer_bank = static_cast<Byte>(raw);
            outer(*mapper, outer_bank);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                const auto selected = expected_chr_bank(addr, outer_bank, banks, mode);
                const auto physical = static_cast<std::size_t>(selected) * kChrBankSize + 0x155U;
                CAPTURE(mode, outer_bank, slot, selected, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
                mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
            }
        }
    }

    auto small = board(MirrorMode::Vertical, 0U, 0x008000UZ, 0x002000UZ);
    outer(*small, 0xFFU);
    bank(*small, 2U, 0xFFU);
    const auto selected = expected_chr_bank(0x1123U, 0xFFU, {0U, 0U, 0xFFU}, 0U);
    const auto physical = (static_cast<std::size_t>(selected) * kChrBankSize + 0x123U) % 0x002000UZ;
    REQUIRE(small->read_chr(0x1123U) == cell(physical, 0x55U));
}

TEST_CASE("Mapper 472 decodes every MMC3 alias without side effects on other registers",
          "[mapper472][decoding][irq][ciram]") {
    auto mapper = board(MirrorMode::Vertical, 0U, 0x008000UZ, 0x002000UZ);
    mapper->power_on();
    const auto cold = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        switch (raw & 0xE001U) {
        case 0x8000U:
            mapper->write_prg(addr, 0xC6U);
            expected[12U] = 6U;
            expected[13U] = 0xC0U;
            break;
        case 0x8001U:
            mapper->write_prg(addr, 0xA5U);
            expected[14U] = 0xA5U;
            break;
        case 0xA000U:
            mapper->write_prg(addr, 1U);
            expected[22U] = static_cast<Byte>(MirrorMode::Horizontal);
            break;
        case 0xA001U:
            mapper->write_prg(addr, 0xC0U);
            expected[26U] = 0xC0U;
            break;
        case 0xC000U:
            mapper->write_prg(addr, 0xA5U);
            expected[24U] = 0xA5U;
            break;
        case 0xC001U:
            mapper->write_prg(addr, 0U);
            expected[25U] = 1U;
            break;
        case 0xE000U:
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(0xC001U, 0U);
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
            mapper->write_prg(addr, 0U);
            break;
        default:
            mapper->write_prg(addr, 0U);
            expected[25U] = 2U;
            break;
        }
        CAPTURE(raw);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }

    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_prg(0xBFFEU, static_cast<Byte>(raw));
        CAPTURE(raw);
        REQUIRE(mapper->mirror_mode() ==
                ((raw & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
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

TEST_CASE("Mapper 472 retains MMC3B scanline IRQs and has no audio or observer state",
          "[mapper472][irq][audio][bus]") {
    auto mapper = board(MirrorMode::Vertical, 0U, 0x008000UZ, 0x002000UZ);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        mapper->power_on();
        mapper->write_prg(0xC000U, static_cast<Byte>(latch));
        mapper->write_prg(0xDFFFU, 0U);
        mapper->write_prg(0xFFFFU, 0U);
        mapper->clock_irq_counter();
        CAPTURE(latch);
        REQUIRE(mapper->irq_pending() == (latch == 0U));
        for (unsigned count = 0U; count < latch; ++count) {
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending() == (count + 1U == latch));
        }
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
    }

    for (unsigned repeat = 0U; repeat < 3U; ++repeat) {
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
        REQUIRE_FALSE(mapper->irq_pending());
    }

    const auto before = state(*mapper);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1234U);
    mapper->observe_ppu_read(0x1234U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 472 soft reset toggles DIP and cold power restores all deterministic defaults",
          "[mapper472][reset][irq][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0U, 0x008000UZ, 0x002000UZ);
        const auto cold = state(*mapper);
        outer(*mapper, 0xFFU);
        bank(*mapper, 6U, 0x3FU, 0xC0U);
        mapper->write_prg(0xA000U, mirror == MirrorMode::Vertical ? Byte{1U} : Byte{0U});
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xC001U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());

        mapper->reset();
        auto toggled = cold;
        toggled[kDipOffset] = 0U;
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == toggled);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->read_prg(0x6000U) == 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->write_prg(0x6000U, 0xA5U);
        REQUIRE(state(*mapper)[kOuterOffset] == 0U);

        mapper->reset();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->read_prg(0x7FFFU) == 0x80U);
        outer(*mapper, 0x5AU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
    }
}

TEST_CASE("Mapper 472 bounded snapshots replay every register without ROM bytes",
          "[mapper472][state][banking][irq]") {
    auto mapper = board();
    mapper->reset();
    outer(*mapper, 0xA5U);
    std::array<Byte, 8> banks{};
    bank(*mapper, 0U, 0x83U, 0xC0U);
    banks[0U] = 0x83U;
    bank(*mapper, 6U, 0x39U, 0xC0U);
    banks[6U] = 0x39U;
    bank(*mapper, 7U, 0x2AU, 0xC0U);
    banks[7U] = 0x2AU;
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == 0xA5U);
    REQUIRE(saved[kDipOffset] == 0U);
    REQUIRE(saved[25U] == 6U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->irq_pending());
    REQUIRE(copy->read_prg(0x6000U) == 0U);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0xA5U, 0x39U, 0x2AU, 0xC0U, 0x80U);
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
        const auto selected = expected_chr_bank(addr, 0xA5U, banks, 0xC0U);
        const auto physical = static_cast<std::size_t>(selected) * kChrBankSize + 0x123U;
        CAPTURE(addr, selected, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0xD5U));
    }

    copy->power_on();
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 472 rejects truncated damaged and mismatched states atomically",
          "[mapper472][state][irq]") {
    auto mapper = board();
    mapper->reset();
    outer(*mapper, 0xA5U);
    bank(*mapper, 6U, 0x39U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
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
    for (std::size_t offset = 0U; offset < kGeometrySize; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 16> invalid_fields{{
        {11U, 1U},
        {11U, 0x40U},
        {11U, 0x81U},
        {12U, 8U},
        {12U, 0xFFU},
        {13U, 1U},
        {13U, 0x3FU},
        {20U, 0x40U},
        {21U, 0x40U},
        {22U, 2U},
        {22U, 0xFFU},
        {25U, 8U},
        {25U, 4U},
        {25U, 0xFFU},
        {26U, 1U},
        {26U, 0x3FU},
    }};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = board(MirrorMode::Vertical, 0U, kPrgSize / 2U, kChrSize);
    const auto before_prg = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == before_prg);

    auto wrong_chr = board(MirrorMode::Vertical, 0U, kPrgSize, kChrSize / 2U);
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
    changed[kOuterOffset] = 0x5AU;
    changed[kDipOffset] = 0x80U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_prg(0x6000U) == 0x80U);
}

} // namespace
