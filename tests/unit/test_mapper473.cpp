#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper473.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x00100000UZ;
constexpr std::size_t kChrSize = 0x00040000UZ;
constexpr std::size_t kPrgBankSize = 0x00002000UZ;
constexpr std::size_t kChrBankSize = 0x00000400UZ;
constexpr std::size_t kPrgRamSize = 0x00002000UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kOuterOffset = 14UZ;
constexpr std::size_t kSelectOffset = 18UZ;
constexpr std::size_t kModeOffset = 19UZ;
constexpr std::size_t kBanksOffset = 20UZ;
constexpr std::size_t kMirrorOffset = 28UZ;
constexpr std::size_t kIrqOffset = 31UZ;
constexpr std::size_t kRamControlOffset = 32UZ;
constexpr std::size_t kRamOffset = 33UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8123U, 0x9FFFU, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xFFFFU};

enum class ChrKind { Rom, Ram };

INesHeader header(MirrorMode mirror = MirrorMode::Vertical,
                  bool wram = false,
                  ChrKind chr_kind = ChrKind::Rom,
                  std::size_t prg_size = kPrgSize,
                  std::size_t chr_size = kChrSize) {
    INesHeader result{};
    result.mapper_number = 473U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = prg_size;
    result.chr_rom_size = chr_kind == ChrKind::Rom ? chr_size : 0U;
    result.chr_ram_size = chr_kind == ChrKind::Ram ? chr_size : 0U;
    result.prg_ram_size = wram ? kPrgRamSize : 0U;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + (offset >> 16U) * 71U +
                             (offset >> 19U) * 83U + (offset >> 24U) * 101U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper473> board(MirrorMode mirror = MirrorMode::Vertical,
                                 bool wram = false,
                                 ChrKind chr_kind = ChrKind::Rom,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    const auto h = header(mirror, wram, chr_kind, prg_size, chr_size);
    REQUIRE(Mapper473::validate_geometry(h, prg_size, h.chr_rom_size));
    return std::make_unique<Mapper473>(h,
                                       rom(prg_size, salt),
                                       chr_kind == ChrKind::Rom
                                           ? rom(chr_size, static_cast<Byte>(salt ^ 0x55U))
                                           : std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper473& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void outer(Mapper473& mapper, unsigned index, Byte value) {
    mapper.write_expansion(static_cast<Address>(0x4800U + index), value);
}

void bank(Mapper473& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

unsigned expected_prg_bank(
    Address addr, const std::array<Byte, 4>& outer, Byte r6, Byte r7, Byte mode) {
    const unsigned slot = (addr - 0x8000U) / kPrgBankSize;
    const unsigned inner = slot == 1U                               ? r7 & 0x3FU
                           : slot == 3U                             ? 63U
                           : (slot == 0U) == ((mode & 0x40U) != 0U) ? 62U
                                                                    : r6 & 0x3FU;
    const unsigned mask = (outer[0U] & 0x20U) != 0U ? 0xFFU >> (7U - (outer[0U] & 7U)) : 0U;
    const unsigned base = (outer[0U] & 0x20U) != 0U ? static_cast<unsigned>(outer[1U]) |
                                                          (static_cast<unsigned>(outer[2U]) << 8U)
                                                    : 63U;
    return (base & ~mask) | (inner & mask);
}

unsigned expected_chr_bank(Address addr, const std::array<Byte, 8>& banks, Byte mode) {
    const unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((mode & 0x80U) != 0U ? 4U : 0U);
    return slot < 4U ? (banks[slot / 2U] & 0xFEU) | (slot & 1U) : banks[slot - 2U];
}

std::size_t expected_state_size(bool wram, ChrKind chr_kind, std::size_t chr_size) {
    return kRamOffset + (wram ? kPrgRamSize : 0U) + (chr_kind == ChrKind::Ram ? 8U + chr_size : 0U);
}

TEST_CASE("Mapper 473 validates bounded ROM RAM and CIRAM geometry", "[mapper473][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const auto chr_kind : {ChrKind::Rom, ChrKind::Ram}) {
            for (const bool wram : {false, true}) {
                for (std::size_t prg = 0x8000UZ; prg <= 0x2000000UZ; prg *= 2U) {
                    for (std::size_t chr = 0x2000UZ; chr <= kChrSize; chr *= 2U) {
                        CAPTURE(mirror, chr_kind, wram, prg, chr);
                        REQUIRE(
                            Mapper473::validate_geometry(header(mirror, wram, chr_kind, prg, chr),
                                                         prg,
                                                         chr_kind == ChrKind::Rom ? chr : 0U));
                    }
                }
            }
        }
    }

    for (const auto chr_kind : {ChrKind::Rom, ChrKind::Ram}) {
        auto mapper = board(MirrorMode::Vertical, true, chr_kind, 0x35U, 0x8000UZ, 0x2000UZ);
        auto different = board(MirrorMode::Vertical, true, chr_kind, 0x80U, 0x8000UZ, 0x2000UZ);
        CAPTURE(chr_kind);
        REQUIRE(state(*mapper) == state(*different));
        REQUIRE(state(*mapper).size() == expected_state_size(true, chr_kind, 0x2000UZ));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->irq_pending());
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
        REQUIRE_FALSE(Mapper473::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 268U, 472U, 474U, 4095U}) {
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
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x18000UZ, 0x4000000UZ, 0x4000001UZ}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x1000UZ, 0x1800UZ, 0x3000UZ, 0x80000UZ}) {
        reject(header(MirrorMode::Vertical, false, ChrKind::Rom, kPrgSize, size), kPrgSize, size);
        reject(header(MirrorMode::Vertical, false, ChrKind::Ram, kPrgSize, size), kPrgSize, 0U);
    }
    reject(header(MirrorMode::Vertical, false, ChrKind::Ram), kPrgSize, kChrSize);
    auto no_chr = header();
    no_chr.chr_rom_size = 0U;
    reject(no_chr, kPrgSize, 0U);
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
            h.prg_ram_size = 0x1000U;
            break;
        case 3U:
            h.prg_ram_size = 0x4000U;
            break;
        case 4U:
            h.prg_nvram_size = kPrgRamSize;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, kChrSize);
    }
}

TEST_CASE("Mapper 473 decodes only 4800-4FFF outer writes and leaves reads undriven",
          "[mapper473][decoding][bus]") {
    auto mapper = board(MirrorMode::Vertical, false, ChrKind::Rom, 0U, 0x8000UZ, 0x2000UZ);
    const auto cold = state(*mapper);
    for (unsigned raw = 0x4020U; raw < 0x8000U; ++raw) {
        mapper->power_on();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x4800U && raw < 0x5000U) {
            expected[kOuterOffset + (raw & 3U)] = value;
        }
        mapper->write_prg(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_expansion(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_expansion(addr) == (raw >= 0x4800U && raw < 0x5000U));
        REQUIRE(mapper->expansion_read_mask(addr) ==
                (raw >= 0x4800U && raw < 0x5000U ? 0U : 0xFFU));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
    REQUIRE(mapper->read_prg(0U) == 0U);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 473 combines every outer PRG width with MMC3 windows",
          "[mapper473][banking][prg][decoding]") {
    auto mapper = board();
    bank(*mapper, 6U, 0x35U);
    bank(*mapper, 7U, 0x2AU);
    for (const Byte mode : {Byte{0U}, Byte{0x40U}}) {
        mapper->write_prg(0x8000U, mode);
        for (unsigned width = 0U; width < 8U; ++width) {
            for (unsigned high : {0U, 1U, 15U, 128U, 255U}) {
                const std::array<Byte, 4> control = {
                    static_cast<Byte>(0x20U | width), 0xA5U, static_cast<Byte>(high), 0U};
                for (unsigned index = 0U; index < control.size(); ++index) {
                    outer(*mapper, index, control[index]);
                }
                for (const auto addr : kPrgAddresses) {
                    const auto selected = expected_prg_bank(addr, control, 0x35U, 0x2AU, mode);
                    const auto physical =
                        (static_cast<std::size_t>(selected) * kPrgBankSize + (addr & 0x1FFFU)) %
                        kPrgSize;
                    CAPTURE(mode, width, high, addr, selected, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                    REQUIRE(mapper->maps_prg(addr));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
            }
        }
    }

    for (const Byte control : {Byte{0U}, Byte{1U}, Byte{0x1FU}, Byte{0x40U}, Byte{0x80U}}) {
        outer(*mapper, 0U, control);
        for (const auto addr : kPrgAddresses) {
            const auto physical = (63UZ * kPrgBankSize + (addr & 0x1FFFU)) % kPrgSize;
            CAPTURE(control, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 473 selects fixed 8 KiB or banked MMC3 CHR before wrapping",
          "[mapper473][banking][chr]") {
    auto mapper = board();
    outer(*mapper, 0U, 0U);
    for (unsigned bank_number = 0U; bank_number < 256U; ++bank_number) {
        outer(*mapper, 3U, static_cast<Byte>(bank_number));
        for (const Address addr : {Address{0x0123U}, Address{0x1000U}, Address{0x1FFFU}}) {
            const auto physical = (bank_number * 0x2000UZ + (addr & 0x1FFFU)) % kChrSize;
            CAPTURE(bank_number, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
            mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
        }
    }

    constexpr std::array<Byte, 6> values = {0xA3U, 0x5CU, 0xD2U, 0x37U, 0xF1U, 0x89U};
    std::array<Byte, 8> banks{};
    outer(*mapper, 0U, 0x80U);
    for (const Byte mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte index = 0U; index < 6U; ++index) {
            banks[index] = values[index];
            bank(*mapper, index, values[index], mode);
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
            const auto selected = expected_chr_bank(addr, banks, mode);
            const auto physical =
                (static_cast<std::size_t>(selected) * kChrBankSize + 0x155U) % kChrSize;
            CAPTURE(mode, slot, selected, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x55U));
        }
    }
}

TEST_CASE("Mapper 473 decodes MMC3 aliases and NROM mode suppresses every high write",
          "[mapper473][decoding][irq][ciram][reset]") {
    auto mapper = board(MirrorMode::Vertical, true, ChrKind::Rom, 0U, 0x8000UZ, 0x2000UZ);
    mapper->power_on();
    const auto cold = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        switch (raw & 0xE001U) {
        case 0x8000U:
            mapper->write_prg(addr, 0xC6U);
            expected[kSelectOffset] = 6U;
            expected[kModeOffset] = 0xC0U;
            break;
        case 0x8001U:
            mapper->write_prg(addr, 0xA5U);
            expected[kBanksOffset] = 0xA5U;
            break;
        case 0xA000U:
            mapper->write_prg(addr, 1U);
            expected[kMirrorOffset] = static_cast<Byte>(MirrorMode::Horizontal);
            break;
        case 0xA001U:
            mapper->write_prg(addr, 0xC0U);
            expected[kRamControlOffset] = 0xC0U;
            break;
        case 0xC000U:
            mapper->write_prg(addr, 0xA5U);
            expected[30U] = 0xA5U;
            break;
        case 0xC001U:
            mapper->write_prg(addr, 0U);
            expected[kIrqOffset] = 1U;
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
            expected[kIrqOffset] = 2U;
            break;
        }
        CAPTURE(raw);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }

    bank(*mapper, 6U, 0x39U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    outer(*mapper, 0U, 0x40U);
    const auto locked = state(*mapper);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw * 37U));
        CAPTURE(raw);
        REQUIRE(state(*mapper) == locked);
    }
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
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

TEST_CASE("Mapper 473 optional volatile PRG and CHR RAM obey mapping and reset",
          "[mapper473][ram][reset][banking]") {
    auto absent = board(MirrorMode::Vertical, false, ChrKind::Rom, 0U, 0x8000UZ, 0x2000UZ);
    absent->write_prg(0x6123U, 0xA5U);
    REQUIRE(absent->read_prg(0x6123U) == 0U);
    REQUIRE_FALSE(absent->maps_prg(0x6123U));
    REQUIRE(absent->prg_read_mask(0x6123U) == 0U);

    auto mapper = board(MirrorMode::Vertical, true, ChrKind::Rom, 0U, 0x8000UZ, 0x2000UZ);
    mapper->write_prg(0x6123U, 0xA5U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->write_prg(0xA001U, 0U);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6123U));
    mapper->write_prg(0x6123U, 0x5AU);
    mapper->write_prg(0xA001U, 0x80U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    outer(*mapper, 0U, 0x40U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    REQUIRE(state(*mapper)[kRamControlOffset] == 0x80U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == 0U);

    auto chr = board(MirrorMode::Vertical, false, ChrKind::Ram, 0U, 0x8000UZ, 0x40000UZ);
    for (const Byte control : {Byte{0U}, Byte{0x80U}}) {
        outer(*chr, 0U, control);
        outer(*chr, 3U, 0x1FU);
        bank(*chr, 2U, 0xF1U);
        for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
            const auto value = static_cast<Byte>(addr * 13U + control);
            chr->write_chr(addr, value);
            REQUIRE(chr->read_chr(addr) == value);
        }
        const auto before_reset = state(*chr);
        chr->reset();
        REQUIRE(state(*chr) != before_reset);
        chr->power_on();
        for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
            REQUIRE(chr->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 473 retains MMC3B IRQs and reset restores deterministic defaults",
          "[mapper473][irq][audio][reset]") {
    auto mapper = board(MirrorMode::Vertical, false, ChrKind::Rom, 0U, 0x8000UZ, 0x2000UZ);
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

    mapper->power_on();
    const auto cold = state(*mapper);
    bank(*mapper, 6U, 0x3FU, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    for (unsigned index = 0U; index < 4U; ++index) {
        outer(*mapper, index, 0xFFU);
    }
    mapper->reset();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE_FALSE(mapper->irq_pending());

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
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 473 bounded snapshots replay registers and RAM without ROM",
          "[mapper473][state][banking][ram][irq]") {
    auto mapper = board(MirrorMode::Vertical, true, ChrKind::Ram, 0U, 0x8000UZ, 0x2000UZ);
    bank(*mapper, 0U, 0x83U, 0xC0U);
    bank(*mapper, 6U, 0x39U, 0xC0U);
    bank(*mapper, 7U, 0x2AU, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xA001U, 0x80U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    outer(*mapper, 0U, 0xE7U);
    outer(*mapper, 1U, 0xA5U);
    outer(*mapper, 2U, 0x5AU);
    outer(*mapper, 3U, 0x1FU);
    mapper->write_chr(0x1123U, 0x6CU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == expected_state_size(true, ChrKind::Ram, 0x2000UZ));
    REQUIRE(saved[kOuterOffset] == 0xE7U);
    REQUIRE(saved[kRamOffset + 0x123U] == 0xA5U);

    auto copy = board(MirrorMode::Vertical, true, ChrKind::Ram, 0x80U, 0x8000UZ, 0x2000UZ);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->read_prg(0x6123U) == 0xA5U);
    REQUIRE(copy->read_chr(0x1123U) == 0x6CU);
    const std::array<Byte, 4> control = {0xE7U, 0xA5U, 0x5AU, 0x1FU};
    for (const auto addr : kPrgAddresses) {
        const auto selected = expected_prg_bank(addr, control, 0x39U, 0x2AU, 0xC0U);
        const auto physical =
            (static_cast<std::size_t>(selected) * kPrgBankSize + (addr & 0x1FFFU)) % 0x8000UZ;
        CAPTURE(addr, selected, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    copy->clock_irq_counter();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 473 rejects truncated damaged and mismatched states atomically",
          "[mapper473][state][ram][irq]") {
    auto mapper = board(MirrorMode::Vertical, true, ChrKind::Ram, 0U, 0x8000UZ, 0x2000UZ);
    bank(*mapper, 6U, 0x39U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    outer(*mapper, 0U, 0xA5U);
    mapper->write_chr(0x123U, 0x6CU);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 12> invalid_fields{{
        {kSelectOffset, 8U},
        {kSelectOffset, 0xFFU},
        {kModeOffset, 1U},
        {kModeOffset, 0x3FU},
        {kBanksOffset + 6U, 0x40U},
        {kBanksOffset + 7U, 0x40U},
        {kMirrorOffset, 2U},
        {kMirrorOffset, 0xFFU},
        {kIrqOffset, 8U},
        {kIrqOffset, 4U},
        {kRamControlOffset, 1U},
        {kRamControlOffset, 0x3FU},
    }};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    constexpr std::array<std::uint64_t, 5> invalid_lengths = {
        0U, 1U, 0x1FFFU, 0x2001U, 0x100000000U};
    for (const std::uint64_t size : invalid_lengths) {
        auto invalid = saved;
        const auto offset = kRamOffset + kPrgRamSize;
        for (unsigned byte = 0U; byte < 8U; ++byte) {
            invalid[offset + byte] = static_cast<Byte>(size >> (byte * 8U));
        }
        CAPTURE(size);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper473>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg = board(MirrorMode::Vertical, true, ChrKind::Ram, 0U, 0x10000UZ, 0x2000UZ);
    mismatch(wrong_prg);
    auto wrong_chr = board(MirrorMode::Vertical, true, ChrKind::Ram, 0U, 0x8000UZ, 0x4000UZ);
    mismatch(wrong_chr);
    auto wrong_kind = board(MirrorMode::Vertical, true, ChrKind::Rom, 0U, 0x8000UZ, 0x2000UZ);
    mismatch(wrong_kind);
    auto no_wram = board(MirrorMode::Vertical, false, ChrKind::Ram, 0U, 0x8000UZ, 0x2000UZ);
    mismatch(no_wram);
    auto wrong_mirror = board(MirrorMode::Horizontal, true, ChrKind::Ram, 0U, 0x8000UZ, 0x2000UZ);
    mismatch(wrong_mirror);

    auto changed = saved;
    changed[kOuterOffset] = 0xE7U;
    changed[kRamOffset + 0x123U] ^= 0xFFU;
    changed[kRamOffset + kPrgRamSize + 8U + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_prg(0x6123U) == static_cast<Byte>(0xA5U ^ 0xFFU));
}

} // namespace
