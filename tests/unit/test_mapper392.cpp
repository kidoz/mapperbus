#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper392.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kGamePrgSize = 0x100000UZ;
constexpr std::size_t kMenuPrgSize = 0x008000UZ;
constexpr std::size_t kPrgSize = kGamePrgSize + kMenuPrgSize;
constexpr std::size_t kChrSize = 0x100000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kOuterOffset = 12UZ;
constexpr std::size_t kChrRamOffset = 27UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 392U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 14U) * 37U + (offset >> 15U) * 43U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 19U) * 79U + (offset >> 20U) * 83U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper392> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper392::validate_geometry(h, kPrgSize, kChrSize));
    return std::make_unique<Mapper392>(h, rom(kPrgSize, salt), rom(kChrSize, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper392& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper392& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void enter_game(Mapper392& mapper, Byte outer = 0U) {
    mapper.write_prg(0x6000U, static_cast<Byte>(0x10U | (outer & 7U)));
}

unsigned expected_inner_prg(Address addr, Byte mode, Byte r6, Byte r7) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    if (slot == 1U) {
        return r7 & 0x3FU;
    }
    if (((mode & 0x40U) == 0U && slot == 0U) || ((mode & 0x40U) != 0U && slot == 2U)) {
        return r6 & 0x3FU;
    }
    return slot == 3U ? 15U : 14U;
}

std::size_t expected_prg(Address addr, Byte outer, Byte mode, Byte r6, Byte r7) {
    const unsigned bank = ((outer & 7U) << 4U) | (expected_inner_prg(addr, mode, r6, r7) & 15U);
    return bank * kPrgBankSize + (addr & 0x1FFFU);
}

unsigned expected_inner_chr(Address addr, Byte mode, const std::array<Byte, 6>& values) {
    const unsigned slot = ((addr >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return (values[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return values[slot - 2U];
}

std::size_t expected_chr(Address addr, Byte outer, Byte mode, const std::array<Byte, 6>& values) {
    const unsigned bank = ((outer & 7U) << 7U) | (expected_inner_chr(addr, mode, values) & 127U);
    return bank * kChrBankSize + (addr & 0x3FFU);
}

TEST_CASE("Mapper 392 validates the exact PCB memory and CIRAM geometry", "[mapper392]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        CAPTURE(mirror);
        REQUIRE(Mapper392::validate_geometry(header(mirror), kPrgSize, kChrSize));
        auto mapper = board(mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8219U);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const unsigned id : {0U, 4U, 52U, 391U, 393U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper392::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper392::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper392::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x8000UZ, 0x100000UZ, 0x107FFFUZ, 0x108001UZ, 0x110000UZ, 0x200000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper392::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x80000UZ, 0x0FFFFFUZ, 0x100001UZ, 0x200000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper392::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper392::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x2000U;
            break;
        case 1U:
            h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 2U:
            h.chr_ram_size = 0U;
            break;
        case 3U:
            h.chr_ram_size = 0x1000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper392::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto battery = header();
    battery.has_battery = true;
    REQUIRE_FALSE(Mapper392::validate_geometry(battery, kPrgSize, kChrSize));
}

TEST_CASE("Mapper 392 menu maps U1 and writable unbanked CHR-RAM", "[mapper392][banking][ram]") {
    auto mapper = board();
    for (const Byte outer : {Byte{0U}, Byte{1U}, Byte{7U}}) {
        mapper->reset();
        mapper->write_prg(0x6000U, outer);
        for (Address addr = 0x8000U; addr != 0U; ++addr) {
            CAPTURE(outer, addr);
            REQUIRE(mapper->read_prg(addr) == cell(kGamePrgSize + (addr & 0x7FFFU)));
        }
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            const auto value = static_cast<Byte>(cell(addr, 0xA5U));
            mapper->write_chr(addr, value);
            REQUIRE(mapper->read_chr(addr) == value);
        }
    }
    REQUIRE(kMenuPrgSize == 0x8000U);
}

TEST_CASE("Mapper 392 decodes every outer alias and locks only in game mode",
          "[mapper392][bus][banking][reset]") {
    auto mapper = board();
    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), 0x07U);
        CAPTURE(raw);
        REQUIRE(state(*mapper)[kOuterOffset] == 0x07U);
        REQUIRE(mapper->read_prg(0x8123U) == cell(kGamePrgSize + 0x123U));
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(raw)));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(raw)) == 0U);
        REQUIRE(mapper->read_prg(static_cast<Address>(raw)) == 0U);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0xA001U, 0U);
        mapper->write_prg(0x7123U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper)[kOuterOffset] == (value & 0x17U));
    }

    enter_game(*mapper, 5U);
    const auto locked = state(*mapper);
    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
        REQUIRE(state(*mapper) == locked);
    }
    mapper->write_prg(0xA001U, 0xC0U);
    for (const Byte value : {Byte{0U}, Byte{0x17U}, Byte{0xFFU}}) {
        mapper->write_prg(0x6000U, value);
        REQUIRE(state(*mapper) == locked);
    }
    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    mapper->write_prg(0x6000U, 0x06U);
    REQUIRE(state(*mapper)[kOuterOffset] == 0x06U);
}

TEST_CASE("Mapper 392 applies outer and inner PRG banks in both MMC3 modes",
          "[mapper392][banking]") {
    auto mapper = board();
    constexpr Byte kR6 = 0x35U;
    constexpr Byte kR7 = 0x47U;
    for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
        for (Byte outer = 0U; outer < 8U; ++outer) {
            mapper->reset();
            bank(*mapper, 6U, kR6, mode);
            bank(*mapper, 7U, kR7, mode);
            enter_game(*mapper, outer);
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const auto addr = static_cast<Address>(0x8123U + slot * kPrgBankSize);
                const auto physical = expected_prg(addr, outer, mode, kR6, kR7);
                CAPTURE(mode, outer, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        bank(*mapper, 6U, static_cast<Byte>(value));
        bank(*mapper, 7U, static_cast<Byte>(value));
        enter_game(*mapper, 7U);
        CAPTURE(value);
        REQUIRE(mapper->read_prg(0x8000U) ==
                cell(expected_prg(0x8000U, 7U, 0U, static_cast<Byte>(value & 0x3FU), 0U)));
        REQUIRE(mapper->read_prg(0xA000U) ==
                cell(expected_prg(0xA000U, 7U, 0U, 0U, static_cast<Byte>(value & 0x3FU))));
    }
}

TEST_CASE("Mapper 392 combines outer CHR windows with paired and inverted MMC3 banks",
          "[mapper392][banking][ram]") {
    auto mapper = board();
    constexpr std::array<Byte, 6> values = {0xA2U, 0x8BU, 0x93U, 0xACU, 0xB1U, 0x87U};
    for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte outer = 0U; outer < 8U; ++outer) {
            mapper->reset();
            for (Byte reg = 0U; reg < 6U; ++reg) {
                bank(*mapper, reg, values[reg], mode);
            }
            enter_game(*mapper, outer);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                const auto physical = expected_chr(addr, outer, mode, values);
                CAPTURE(mode, outer, addr, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical));
                mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                REQUIRE(mapper->read_chr(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 392 decodes all MMC3 register aliases", "[mapper392][bus][banking][irq]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr < 0xA000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0xC5U);
        const auto current = state(*mapper);
        CAPTURE(addr);
        REQUIRE(current[13U] == 5U);
        REQUIRE(current[14U] == 0xC0U);
    }
    for (unsigned addr = 0x8001U; addr < 0xA000U; addr += 2U) {
        mapper->write_prg(0x8000U, 2U);
        mapper->write_prg(static_cast<Address>(addr), 0xA6U);
        CAPTURE(addr);
        REQUIRE(state(*mapper)[17U] == 0xA6U);
    }
    for (unsigned addr = 0xA000U; addr < 0xC000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr >> 1U));
        CAPTURE(addr);
        REQUIRE(mapper->mirror_mode() ==
                ((addr & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (unsigned addr = 0xC000U; addr < 0xE000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
        CAPTURE(addr);
        REQUIRE(state(*mapper)[25U] == static_cast<Byte>(addr));
    }
    for (unsigned addr = 0xC001U; addr < 0xE000U; addr += 2U) {
        mapper->write_prg(static_cast<Address>(addr), 0U);
        CAPTURE(addr);
        REQUIRE((state(*mapper)[26U] & 1U) != 0U);
        REQUIRE(state(*mapper)[24U] == 0U);
    }
    mapper->write_prg(0xC000U, 0U);
    for (unsigned addr = 0xE000U; addr < 0x10000U; addr += 2U) {
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(static_cast<Address>(addr), 0U);
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->irq_pending());
    }
    for (unsigned addr = 0xE001U; addr < 0x10000U; addr += 2U) {
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(static_cast<Address>(addr), 0U);
        mapper->clock_irq_counter();
        CAPTURE(addr);
        REQUIRE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 392 uses MMC3 horizontal vertical console CIRAM only", "[mapper392][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg((value & 1U) != 0U ? 0xBFFEU : 0xA000U, static_cast<Byte>(value));
            CAPTURE(initial, value);
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == initial);
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        REQUIRE_FALSE(mapper->unmirrored_nametables());
    }
}

TEST_CASE("Mapper 392 has no CPU RAM expansion device or audio", "[mapper392][bus][ram][audio]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    enter_game(*mapper);
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

TEST_CASE("Mapper 392 MMC3 IRQ latch reload acknowledge disable and zero repeats",
          "[mapper392][irq]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xDFFFU, 0U);
    mapper->write_prg(0xFFFFU, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    enter_game(*mapper, 6U);
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    for (unsigned clock = 0U; clock < 3U; ++clock) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->acknowledge_irq();
    }
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 392 soft reset returns to menu and cold power clears CHR-RAM",
          "[mapper392][reset][ram]") {
    auto mapper = board(MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    mapper->write_chr(0x0123U, 0xA7U);
    enter_game(*mapper, 7U);
    bank(*mapper, 6U, 19U, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->reset();
    REQUIRE(state(*mapper)[kOuterOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(kGamePrgSize));
    mapper->write_chr(0x0123U, 0x5AU);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);
}

TEST_CASE("Mapper 392 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper392][state]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        mapper->write_chr(addr, static_cast<Byte>(cell(addr) & 0x7FU));
    }
    bank(*mapper, 6U, 19U, 0xC0U);
    bank(*mapper, 7U, 7U, 0xC0U);
    bank(*mapper, 0U, 0xA2U, 0xC0U);
    enter_game(*mapper, 6U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterOffset] == 0x16U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
        const auto physical = expected_prg(addr, 6U, 0xC0U, 19U, 7U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    constexpr std::array<Byte, 6> chr_values = {0xA2U, 0U, 0U, 0U, 0U, 0U};
    for (const Address addr : {0x0000U, 0x0155U, 0x0800U, 0x1000U, 0x1555U, 0x1FFFU}) {
        const auto physical = expected_chr(addr, 6U, 0xC0U, chr_values);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0x80U));
    }
    copy->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE_FALSE(copy->irq_pending());

    mapper->reset();
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(cell(addr) & 0x7FU));
    }
    load(*mapper, saved);
    REQUIRE(state(*mapper) == saved);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);

    mapper->write_prg(0xA000U, 1U);
    const auto active = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == active);
    };
    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    constexpr std::array<std::pair<std::size_t, Byte>, 13> invalid_fields = {{{0U, 0U},
                                                                              {2U, 1U},
                                                                              {6U, 1U},
                                                                              {10U, 0U},
                                                                              {12U, 0x80U},
                                                                              {13U, 8U},
                                                                              {14U, 1U},
                                                                              {21U, 0x40U},
                                                                              {22U, 0x40U},
                                                                              {23U, 2U},
                                                                              {26U, 8U},
                                                                              {26U, 4U},
                                                                              {10U, 0xFFU}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto h = header();
    Mapper392 wrong_prg(h, rom(kPrgSize / 2U), rom(kChrSize));
    const auto wrong_prg_before = state(wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg.load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(wrong_prg) == wrong_prg_before);

    Mapper392 wrong_chr(h, rom(kPrgSize), rom(kChrSize / 2U));
    const auto wrong_chr_before = state(wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr.load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(wrong_chr) == wrong_chr_before);
}

} // namespace
