#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper393.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kChrSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kChrBankSize = 0x000400UZ;
constexpr std::size_t kWramSize = 0x002000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kOuterOffset = 10UZ;
constexpr std::size_t kBankSelectOffset = 11UZ;
constexpr std::size_t kBankModeOffset = 12UZ;
constexpr std::size_t kBanksOffset = 13UZ;
constexpr std::size_t kUnromOffset = 21UZ;
constexpr std::size_t kIrqCounterOffset = 23UZ;
constexpr std::size_t kIrqReloadOffset = 24UZ;
constexpr std::size_t kIrqFlagsOffset = 25UZ;
constexpr std::size_t kRamFlagsOffset = 26UZ;
constexpr std::size_t kChrRamOffset = 27UZ;
constexpr std::size_t kStateWithoutWram = kChrRamOffset + kChrRamSize;
constexpr std::size_t kStateWithWram = kStateWithoutWram + kWramSize;

enum class RamKind { Absent, Volatile, Battery };

INesHeader header(MirrorMode mirror = MirrorMode::Vertical, RamKind ram = RamKind::Volatile) {
    INesHeader result{};
    result.mapper_number = 393U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.has_battery = ram == RamKind::Battery;
    result.prg_ram_size = ram == RamKind::Volatile ? kWramSize : 0U;
    result.prg_nvram_size = ram == RamKind::Battery ? kWramSize : 0U;
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

std::unique_ptr<Mapper393> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 RamKind ram = RamKind::Volatile,
                                 Byte salt = 0U) {
    const auto h = header(mirror, ram);
    REQUIRE(Mapper393::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper393>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper393& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void bank(Mapper393& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void outer(Mapper393& mapper, Byte value, Address addr = 0x6000U) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(addr, value);
}

std::size_t mmc3_offset(Address addr, Byte control, Byte bank_mode, Byte r6, Byte r7) {
    const unsigned slot = (addr - 0x8000U) >> 13U;
    const bool swapped = (bank_mode & 0x40U) != 0U;
    const unsigned inner = slot == 1U                ? r7 & 0x0FU
                           : slot == 3U              ? 15U
                           : (slot == 0U) == swapped ? 14U
                                                     : r6 & 0x0FU;
    return (static_cast<std::size_t>(control & 7U) << 17U) |
           (static_cast<std::size_t>(inner) << 13U) | (addr & 0x1FFFU);
}

std::size_t bnrom_offset(Address addr, Byte control, Byte r6) {
    return (static_cast<std::size_t>(control & 7U) << 17U) |
           (static_cast<std::size_t>((r6 >> 2U) & 3U) << 15U) | (addr & 0x7FFFU);
}

std::size_t unrom_offset(Address addr, Byte control, Byte latch) {
    const unsigned inner = addr < 0xC000U ? latch & 7U : 7U;
    return (static_cast<std::size_t>(control & 7U) << 17U) |
           (static_cast<std::size_t>(inner) << 14U) | (addr & 0x3FFFU);
}

Byte mmc3_chr_bank(Address addr, Byte bank_mode, const std::array<Byte, 6>& values) {
    unsigned slot =
        ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ ((bank_mode & 0x80U) != 0U ? 4U : 0U);
    if (slot < 4U) {
        return static_cast<Byte>((values[slot / 2U] & 0xFEU) | (slot & 1U));
    }
    return values[slot - 2U];
}

std::size_t chr_offset(Address addr, Byte bank_mode, const std::array<Byte, 6>& values) {
    return static_cast<std::size_t>(mmc3_chr_bank(addr, bank_mode, values)) * kChrBankSize +
           (addr & 0x03FFU);
}

TEST_CASE("Mapper 393 validates decoded ROM bounds memory and CIRAM metadata", "[mapper393]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const auto ram : {RamKind::Absent, RamKind::Volatile, RamKind::Battery}) {
            CAPTURE(mirror, ram);
            REQUIRE(Mapper393::validate_geometry(header(mirror, ram), kPrgSize, kChrSize));
            auto mapper = board(kPrgSize, kChrSize, mirror, ram);
            REQUIRE(state(*mapper).size() ==
                    (ram == RamKind::Absent ? kStateWithoutWram : kStateWithWram));
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().size() == (ram == RamKind::Battery ? kWramSize : 0U));
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
        }
    }
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper393::validate_geometry(header(), size, kChrSize));
    }
    for (std::size_t size = 0x2000UZ; size <= kChrSize; size += kChrBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper393::validate_geometry(header(), kPrgSize, size));
    }

    for (const unsigned id : {0U, 4U, 2U, 392U, 394U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper393::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper393::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper393::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x100001UZ, 0x200000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper393::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x2200UZ, 0x40001UZ, 0x80000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper393::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper393::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0x1000U;
            break;
        case 1U:
            h.prg_nvram_size = kWramSize;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = 0U;
            h.prg_nvram_size = kWramSize;
            h.has_battery = false;
            break;
        case 4U:
            h.prg_nvram_size = kWramSize;
            h.has_battery = true;
            break;
        case 5U:
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 0x1000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper393::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 393 outer register uses every low alias and the exact WRAM write gate",
          "[mapper393][bus][banking][ram]") {
    auto mapper = board();
    for (unsigned raw = 0x6000U; raw < 0x8000U; ++raw) {
        mapper->reset();
        outer(*mapper, 0x2DU, static_cast<Address>(raw));
        CAPTURE(raw);
        REQUIRE(state(*mapper)[kOuterOffset] == 0x2DU);
        REQUIRE(mapper->read_prg(static_cast<Address>(raw)) == 0x2DU);
        REQUIRE(mapper->read_prg(0x8123U) == cell(bnrom_offset(0x8123U, 0x2DU, 0U)));
    }

    for (unsigned control = 0U; control < 256U; ++control) {
        mapper->power_on();
        mapper->write_prg(0xA001U, static_cast<Byte>(control));
        mapper->write_prg(0x6123U, 0xA5U);
        const auto current = state(*mapper);
        CAPTURE(control);
        REQUIRE(current[kOuterOffset] == ((control & 0xC0U) == 0x80U ? 0x25U : 0U));
        REQUIRE(current[kRamFlagsOffset] == (control & 0xC0U));
        REQUIRE(mapper->maps_prg(0x6123U) == ((control & 0x80U) != 0U));
        REQUIRE(mapper->prg_read_mask(0x6123U) == ((control & 0x80U) != 0U ? 0xFFU : 0U));
        REQUIRE(mapper->read_prg(0x6123U) == ((control & 0xC0U) == 0x80U ? 0xA5U : 0U));
    }

    auto without_ram = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Absent);
    outer(*without_ram, 0x3FU);
    REQUIRE(state(*without_ram)[kOuterOffset] == 0x3FU);
    REQUIRE_FALSE(without_ram->maps_prg(0x6000U));
    REQUIRE(without_ram->read_prg(0x6000U) == 0U);
}

TEST_CASE("Mapper 393 MMC3 modes use four inner PRG pins and local fixed banks",
          "[mapper393][banking]") {
    auto mapper = board();
    constexpr Byte kR6 = 0x35U;
    constexpr Byte kR7 = 0x47U;
    for (const Byte prg_mode : {Byte{0U}, Byte{1U}}) {
        for (const Byte bank_mode : {Byte{0U}, Byte{0x40U}}) {
            bank(*mapper, 6U, kR6, bank_mode);
            bank(*mapper, 7U, kR7, bank_mode);
            for (Byte outer_bank = 0U; outer_bank < 8U; ++outer_bank) {
                const auto control = static_cast<Byte>((prg_mode << 4U) | outer_bank);
                outer(*mapper, control);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const auto addr = static_cast<Address>(0x8123U + slot * kPrgBankSize);
                    const auto physical = mmc3_offset(addr, control, bank_mode, kR6, kR7);
                    CAPTURE(prg_mode, bank_mode, outer_bank, addr, physical);
                    REQUIRE(mapper->read_prg(addr) == cell(physical));
                }
            }
        }
    }

    for (const std::size_t size : {0x8000UZ, 0x18000UZ, 0x48000UZ}) {
        auto fitted = board(size);
        bank(*fitted, 6U, 0x3FU, 0x40U);
        bank(*fitted, 7U, 0x3FU, 0x40U);
        outer(*fitted, 0x17U);
        for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
            const auto physical = mmc3_offset(addr, 0x17U, 0x40U, 0x3FU, 0x3FU) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 393 BNROM takes A15 A16 from R6 and low pins from the CPU",
          "[mapper393][banking]") {
    auto mapper = board();
    for (Byte r6 = 0U; r6 < 64U; ++r6) {
        bank(*mapper, 6U, r6, 0x40U);
        for (Byte outer_bank = 0U; outer_bank < 8U; ++outer_bank) {
            const auto control = static_cast<Byte>(0x20U | outer_bank);
            outer(*mapper, control);
            for (const Address addr : {0x8000U, 0xA123U, 0xBFFFU, 0xC000U, 0xE456U, 0xFFFFU}) {
                const auto physical = bnrom_offset(addr, control, r6);
                CAPTURE(r6, outer_bank, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }
}

TEST_CASE("Mapper 393 UNROM uses the independent high-write latch and fixed bank seven",
          "[mapper393][bus][banking]") {
    auto mapper = board();
    outer(*mapper, 0x37U);
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        mapper->write_prg(static_cast<Address>(raw), value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper)[kUnromOffset] == (value & 7U));
        REQUIRE(mapper->read_prg(0x9123U) == cell(unrom_offset(0x9123U, 0x37U, value)));
        REQUIRE(mapper->read_prg(0xF123U) == cell(unrom_offset(0xF123U, 0x37U, value)));
    }
    for (Byte outer_bank = 0U; outer_bank < 8U; ++outer_bank) {
        const auto control = static_cast<Byte>(0x30U | outer_bank);
        outer(*mapper, control);
        mapper->write_prg(0xFFFFU, 0x06U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(unrom_offset(0x8000U, control, 6U)));
        REQUIRE(mapper->read_prg(0xC000U) == cell(unrom_offset(0xC000U, control, 6U)));
    }
}

TEST_CASE("Mapper 393 MMC3 register aliases retain their ordinary effects in every PRG mode",
          "[mapper393][bus][banking][irq]") {
    auto mapper = board();
    for (const Byte prg_mode : {Byte{0U}, Byte{0x20U}, Byte{0x30U}}) {
        outer(*mapper, static_cast<Byte>(prg_mode | 5U));
        for (unsigned addr = 0x8000U; addr < 0xA000U; addr += 2U) {
            mapper->write_prg(static_cast<Address>(addr), 0xC5U);
            const auto current = state(*mapper);
            CAPTURE(prg_mode, addr);
            REQUIRE(current[kBankSelectOffset] == 5U);
            REQUIRE(current[kBankModeOffset] == 0xC0U);
        }
        for (unsigned addr = 0x8001U; addr < 0xA000U; addr += 2U) {
            mapper->write_prg(0x8000U, 2U);
            mapper->write_prg(static_cast<Address>(addr), 0xA6U);
            CAPTURE(prg_mode, addr);
            REQUIRE(state(*mapper)[kBanksOffset + 2U] == 0xA6U);
        }
        for (unsigned addr = 0xA000U; addr < 0xC000U; addr += 2U) {
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr >> 1U));
            CAPTURE(prg_mode, addr);
            REQUIRE(mapper->mirror_mode() ==
                    ((addr & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
        for (unsigned addr = 0xC000U; addr < 0xE000U; addr += 2U) {
            mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
            CAPTURE(prg_mode, addr);
            REQUIRE(state(*mapper)[kIrqReloadOffset] == static_cast<Byte>(addr));
        }
        for (unsigned addr = 0xC001U; addr < 0xE000U; addr += 2U) {
            mapper->write_prg(static_cast<Address>(addr), 0U);
            CAPTURE(prg_mode, addr);
            REQUIRE((state(*mapper)[kIrqFlagsOffset] & 1U) != 0U);
            REQUIRE(state(*mapper)[kIrqCounterOffset] == 0U);
        }
        mapper->write_prg(0xC000U, 0U);
        for (unsigned addr = 0xE000U; addr < 0x10000U; addr += 2U) {
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending());
            mapper->write_prg(static_cast<Address>(addr), 0U);
            CAPTURE(prg_mode, addr);
            REQUIRE_FALSE(mapper->irq_pending());
        }
        for (unsigned addr = 0xE001U; addr < 0x10000U; addr += 2U) {
            mapper->write_prg(0xE000U, 0U);
            mapper->write_prg(static_cast<Address>(addr), 0U);
            mapper->clock_irq_counter();
            CAPTURE(prg_mode, addr);
            REQUIRE(mapper->irq_pending());
        }
    }
}

TEST_CASE("Mapper 393 switches between banked CHR ROM and flat writable CHR RAM",
          "[mapper393][banking][ram]") {
    auto mapper = board();
    constexpr std::array<Byte, 6> values = {0xA2U, 0x8BU, 0x93U, 0xACU, 0xB1U, 0x87U};
    for (const Byte bank_mode : {Byte{0U}, Byte{0x80U}}) {
        for (Byte reg = 0U; reg < 6U; ++reg) {
            bank(*mapper, reg, values[reg], bank_mode);
        }
        for (Byte outer_bank = 0U; outer_bank < 8U; ++outer_bank) {
            outer(*mapper, outer_bank);
            for (unsigned slot = 0U; slot < 8U; ++slot) {
                const auto addr = static_cast<Address>(slot * kChrBankSize + 0x155U);
                const auto physical = chr_offset(addr, bank_mode, values);
                CAPTURE(bank_mode, outer_bank, addr, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical));
                mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
                REQUIRE(mapper->read_chr(addr) == cell(physical));
            }
        }
    }

    outer(*mapper, 0x0BU);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto value = static_cast<Byte>(cell(addr, 0xA5U));
        mapper->write_chr(addr, value);
        REQUIRE(mapper->read_chr(addr) == value);
    }
    for (Byte reg = 0U; reg < 8U; ++reg) {
        bank(*mapper, reg, static_cast<Byte>(reg * 37U), 0xC0U);
    }
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(cell(addr, 0xA5U)));
    }
    outer(*mapper, 0x03U);
    constexpr std::array<Byte, 6> changed = {0U, 37U, 74U, 111U, 148U, 185U};
    REQUIRE(mapper->read_chr(0x0155U) == cell(chr_offset(0x0155U, 0xC0U, changed)));

    auto fitted = board(kPrgSize, 0x3000UZ);
    for (Byte reg = 0U; reg < 6U; ++reg) {
        bank(*fitted, reg, values[reg]);
    }
    for (const Address addr : {0x0000U, 0x03FFU, 0x0800U, 0x1000U, 0x1555U, 0x1FFFU}) {
        const auto physical = chr_offset(addr, 0U, values) % 0x3000UZ;
        CAPTURE(addr, physical);
        REQUIRE(fitted->read_chr(addr) == cell(physical));
    }
}

TEST_CASE("Mapper 393 uses MMC3 horizontal vertical console CIRAM only", "[mapper393][ciram]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, kChrSize, initial);
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

TEST_CASE("Mapper 393 exposes optional WRAM but no expansion or audio device",
          "[mapper393][bus][ram][audio]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x6000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

    mapper->reset();
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

TEST_CASE("Mapper 393 MMC3 IRQ reload acknowledge disable and zero repeats survive mode changes",
          "[mapper393][irq]") {
    auto mapper = board();
    for (const Byte prg_mode : {Byte{0U}, Byte{0x10U}, Byte{0x20U}, Byte{0x30U}}) {
        mapper->reset();
        mapper->write_prg(0xC000U, 2U);
        mapper->write_prg(0xDFFFU, 0U);
        mapper->write_prg(0xFFFFU, 0U);
        mapper->clock_irq_counter();
        mapper->clock_irq_counter();
        REQUIRE_FALSE(mapper->irq_pending());
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        outer(*mapper, static_cast<Byte>(prg_mode | 6U));
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
    }
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 393 reset restores registers and cold power clears only volatile RAM",
          "[mapper393][reset][ram]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto initial = state(*mapper);
    outer(*mapper, 0x3EU);
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->write_prg(0x6123U, 0x5CU);
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
    REQUIRE(mapper->read_prg(0x6123U) == 0x5CU);
    outer(*mapper, 0x08U);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA7U);
    mapper->write_chr(0x0123U, 0x5AU);
    mapper->write_prg(0x6123U, 0x39U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    outer(*mapper, 0x08U);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);

    auto persistent = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Battery);
    std::array<Byte, kWramSize> bytes{};
    for (std::size_t index = 0U; index < bytes.size(); ++index) {
        bytes[index] = cell(index, 0xA5U);
    }
    persistent->set_battery_ram(bytes);
    REQUIRE(persistent->battery_ram().size() == kWramSize);
    outer(*persistent, 0x3FU);
    persistent->power_on();
    REQUIRE(persistent->read_prg(0x6123U) == bytes[0x123U]);
    REQUIRE(state(*persistent)[kOuterOffset] == 0U);
}

TEST_CASE("Mapper 393 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper393][state]") {
    auto mapper = board();
    outer(*mapper, 0x08U);
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        mapper->write_chr(addr, static_cast<Byte>(cell(addr) & 0x7FU));
    }
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        mapper->write_prg(addr, static_cast<Byte>(cell(addr) & 0x7FU));
    }
    bank(*mapper, 6U, 19U, 0xC0U);
    bank(*mapper, 7U, 7U, 0xC0U);
    bank(*mapper, 0U, 0xA2U, 0xC0U);
    mapper->write_prg(0xFFFFU, 6U);
    outer(*mapper, 0x3EU, 0x7000U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    bank(*mapper, 7U, 6U, 0xC0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateWithWram);
    REQUIRE(saved.size() == 16411U);
    REQUIRE(saved[kOuterOffset] == 0x3EU);
    REQUIRE(saved[kUnromOffset] == 6U);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Volatile, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU}) {
        const auto physical = unrom_offset(addr, 0x3EU, 6U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0U; addr < 0x2000U; addr += 0x101U) {
        REQUIRE(copy->read_chr(addr) == static_cast<Byte>(cell(addr) & 0x7FU));
    }
    for (Address addr = 0x6000U; addr < 0x8000U; addr += 0x101U) {
        REQUIRE(copy->read_prg(addr) == static_cast<Byte>(cell(addr) & 0x7FU));
    }
    copy->clock_irq_counter();
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE_FALSE(copy->irq_pending());

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);

    outer(*mapper, 0U);
    mapper->write_prg(0x6123U, 0x55U);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 14> invalid_fields = {{{0U, 0U},
                                                                              {2U, 1U},
                                                                              {6U, 1U},
                                                                              {10U, 0x40U},
                                                                              {11U, 8U},
                                                                              {12U, 1U},
                                                                              {19U, 0x40U},
                                                                              {20U, 0x40U},
                                                                              {21U, 8U},
                                                                              {22U, 2U},
                                                                              {25U, 8U},
                                                                              {25U, 4U},
                                                                              {26U, 1U},
                                                                              {0U, 0xFFU}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_mirror = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    const auto wrong_mirror_before = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == wrong_mirror_before);

    auto wrong_prg = board(kPrgSize / 2U, kChrSize);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(kPrgSize, kChrSize / 2U);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto no_ram = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Absent);
    const auto no_ram_before = state(*no_ram);
    StateReader no_ram_reader(saved);
    no_ram->load_state(no_ram_reader);
    REQUIRE_FALSE(no_ram_reader.ok());
    REQUIRE(state(*no_ram) == no_ram_before);
    REQUIRE(no_ram_before.size() == kStateWithoutWram);

    auto battery = board(kPrgSize, kChrSize, MirrorMode::Vertical, RamKind::Battery);
    const auto battery_before = state(*battery);
    StateReader battery_reader(saved);
    battery->load_state(battery_reader);
    REQUIRE_FALSE(battery_reader.ok());
    REQUIRE(state(*battery) == battery_before);
}

} // namespace
