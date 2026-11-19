#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper468.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kChrSize = 0x100000UZ;
constexpr std::size_t kWramStart = 187UZ;
constexpr std::size_t kChrRamStart = kWramStart + 0x2000UZ;
constexpr std::size_t kAudioLength = kChrRamStart + 0x8000UZ;

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 468U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kPrgSize;
    result.chr_rom_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 14U) * 47U + (offset >> 16U) * 59U +
                             (offset >> 18U) * 71U + (offset >> 20U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper468> board(Byte submapper = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 std::size_t prg_ram = 0U,
                                 std::size_t prg_nvram = 0U,
                                 std::size_t chr_ram = 0U) {
    auto h = header(submapper, mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    h.prg_ram_size = prg_ram;
    h.prg_nvram_size = prg_nvram;
    h.has_battery = prg_nvram != 0U;
    h.chr_ram_size = chr_size == 0U ? (chr_ram == 0U ? 0x8000U : chr_ram) : 0U;
    REQUIRE(Mapper468::validate_geometry(h, prg_size, chr_size));
    auto mapper = std::make_unique<Mapper468>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x55U)));
    mapper->write_expansion(0x5701U, 0x00U);
    mapper->write_expansion(0x5601U, 0x00U);
    return mapper;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper468& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void select(Mapper468& mapper, Byte value) {
    mapper.write_expansion(0x5700U, value);
}

void require_prg(Mapper468& mapper,
                 Address addr,
                 std::size_t physical,
                 Byte salt = 0U,
                 std::size_t prg_size = kPrgSize) {
    const auto actual = mapper.read_prg(addr);
    CAPTURE(addr, physical, salt, prg_size, static_cast<unsigned>(actual));
    REQUIRE(actual == cell(physical % prg_size, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_prg8(Mapper468& mapper,
                  Address addr,
                  std::size_t bank,
                  Byte salt = 0U,
                  std::size_t prg_size = kPrgSize) {
    require_prg(mapper, addr, bank * 0x2000U + (addr & 0x1FFFU), salt, prg_size);
}

void require_prg16(Mapper468& mapper,
                   Address addr,
                   std::size_t bank,
                   Byte salt = 0U,
                   std::size_t prg_size = kPrgSize) {
    require_prg(mapper, addr, bank * 0x4000U + (addr & 0x3FFFU), salt, prg_size);
}

void require_prg32(Mapper468& mapper,
                   Address addr,
                   std::size_t bank,
                   Byte salt = 0U,
                   std::size_t prg_size = kPrgSize) {
    require_prg(mapper, addr, bank * 0x8000U + (addr & 0x7FFFU), salt, prg_size);
}

void require_chr1k(Mapper468& mapper,
                   Address addr,
                   std::size_t bank,
                   Byte salt = 0U,
                   std::size_t chr_size = kChrSize) {
    const std::size_t physical = bank * 0x400U + (addr & 0x3FFU);
    const auto actual = mapper.read_chr(addr);
    CAPTURE(addr, bank, physical, salt, chr_size, static_cast<unsigned>(actual));
    REQUIRE(actual == cell(physical % chr_size, static_cast<Byte>(salt ^ 0x55U)));
}

void write_serial(Mapper468& mapper, Address addr, Byte value) {
    for (unsigned bit = 0U; bit < 5U; ++bit) {
        mapper.write_prg(addr, static_cast<Byte>((value >> bit) & 1U));
    }
}

void write_vrc7_register(Mapper468& mapper, Byte reg, Byte value) {
    mapper.write_prg(0x9010U, reg);
    mapper.write_prg(0x9030U, value);
}

TEST_CASE("Mapper 468 validates all submappers ROM shapes and bounded memory metadata",
          "[mapper468][geometry][ram]") {
    for (Byte submapper = 0U; submapper <= 5U; ++submapper) {
        for (std::size_t prg = 0x004000UZ; prg <= kPrgSize; prg *= 2U) {
            for (std::size_t chr = 0x002000UZ; chr <= kChrSize; chr *= 2U) {
                CAPTURE(submapper, prg, chr);
                REQUIRE(Mapper468::validate_geometry(header(submapper), prg, chr));
            }
        }
        for (const std::size_t chr_ram : {0x2000UZ, 0x8000UZ}) {
            auto h = header(submapper);
            h.chr_ram_size = chr_ram;
            REQUIRE(Mapper468::validate_geometry(h, kPrgSize, 0U));
        }
        for (const bool persistent : {false, true}) {
            auto mapper = board(submapper,
                                MirrorMode::Vertical,
                                0U,
                                kPrgSize,
                                kChrSize,
                                persistent ? 0U : 0x2000U,
                                persistent ? 0x2000U : 0U);
            CAPTURE(submapper, persistent);
            REQUIRE(mapper->battery_ram().size() == (persistent ? 0x2000U : 0U));
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE(state(*mapper).size() < 0x10000U);
            auto different = board(submapper,
                                   MirrorMode::Vertical,
                                   0xA5U,
                                   kPrgSize,
                                   kChrSize,
                                   persistent ? 0U : 0x2000U,
                                   persistent ? 0x2000U : 0U);
            REQUIRE(state(*mapper) == state(*different));
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
        REQUIRE_FALSE(Mapper468::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 1U, 467U, 469U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, kChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, kChrSize);
    for (Byte submapper = 6U; submapper < 16U; ++submapper) {
        reject(header(submapper), kPrgSize, kChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x6000UZ, 0x018000UZ, kPrgSize + 1U, kPrgSize * 2U}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size :
         {1UZ, 0x1000UZ, 0x3000UZ, 0x018000UZ, kChrSize + 1U, kChrSize * 2U}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(0U, mirror), kPrgSize, kChrSize);
    }
    for (unsigned field = 0U; field < 10U; ++field) {
        auto h = header();
        std::size_t chr = kChrSize;
        switch (field) {
        case 0U:
            h.has_trainer = true;
            break;
        case 1U:
            h.prg_ram_size = 0x1000U;
            break;
        case 2U:
            h.prg_nvram_size = 0x4000U;
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 4U:
            h.has_battery = true;
            break;
        case 5U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 6U:
            h.chr_ram_size = 0x2000U;
            break;
        case 7U:
            h.chr_nvram_size = 0x2000U;
            break;
        case 8U:
            h.chr_ram_size = 0x4000U;
            chr = 0U;
            break;
        default:
            h.chr_ram_size = 0U;
            chr = 0U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, chr);
    }
}

TEST_CASE("Mapper 468 supervisor uses exact addresses lockable outer pins and open reads",
          "[mapper468][decoding][bus][reset]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    mapper->write_expansion(0x5703U, 0xAAU);
    mapper->write_expansion(0x5600U, 0xAAU);
    mapper->write_expansion(0x5700U, 0xEEU);
    REQUIRE(state(*mapper) != cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    mapper->write_prg(0x8000U, 0x55U);
    const auto latched = state(*mapper);
    select(*mapper, 0xEEU);
    REQUIRE(state(*mapper) == latched);

    select(*mapper, 0xA7U);
    mapper->write_prg(0x8000U, 0x55U);
    REQUIRE(state(*mapper) != latched);
    select(*mapper, 0xCFU);
    mapper->write_expansion(0x5601U, 0x10U);
    mapper->write_expansion(0x5601U, 0x20U);
    require_prg32(*mapper, 0x8123U, 6U);
    mapper->write_expansion(0x5601U, 0x80U);
    mapper->write_expansion(0x5601U, 0x00U);
    require_prg32(*mapper, 0x8123U, 6U);
    mapper->write_expansion(0x5701U, 0x01U);
    require_prg32(*mapper, 0x8123U, 0x0EU);

    for (Address addr = 0x5000U; addr < 0x6000U; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_expansion(addr) == (addr == 0x5301U || addr == 0x5601U));
        REQUIRE(mapper->expansion_read_mask(addr) ==
                (addr == 0x5301U || addr == 0x5601U ? 0x80U : 0U));
        if (!mapper->maps_expansion(addr)) {
            REQUIRE(mapper->read_expansion(addr) == 0U);
        }
    }
    for (const Byte submapper : {Byte{0U}, Byte{1U}, Byte{2U}, Byte{3U}, Byte{4U}, Byte{5U}}) {
        auto other = board(submapper);
        select(*other, 0xFFU);
        REQUIRE_FALSE(other->maps_prg(0x8000U));
        REQUIRE(other->prg_read_mask(0x8000U) == 0U);
        REQUIRE(other->read_prg(0x8000U) == 0U);
        REQUIRE_FALSE(other->irq_pending());
        REQUIRE_FALSE(other->has_expansion_audio());
        REQUIRE(other->audio_output() == 0.0F);
    }
}

TEST_CASE("Mapper 468 SxROM and SUROM implement serial banking mirroring and outer PRG pins",
          "[mapper468][banking][mmc1][ciram][bus]") {
    auto mapper = board();
    select(*mapper, 0x00U);
    mapper->write_expansion(0x5701U, 0x20U);
    write_serial(*mapper, 0xE000U, 0x03U);
    require_prg16(*mapper, 0x8000U, 0x203U);
    require_prg16(*mapper, 0xC000U, 0x20FU);
    write_serial(*mapper, 0x8000U, 0x02U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    write_serial(*mapper, 0x8000U, 0x03U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    write_serial(*mapper, 0x8000U, 0x00U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    write_serial(*mapper, 0x8000U, 0x01U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);

    write_serial(*mapper, 0x8000U, 0x1CU);
    write_serial(*mapper, 0xA000U, 0x05U);
    write_serial(*mapper, 0xC000U, 0x09U);
    require_chr1k(*mapper, 0x0000U, 20U);
    require_chr1k(*mapper, 0x1000U, 36U);
    write_serial(*mapper, 0x8000U, 0x0CU);
    require_chr1k(*mapper, 0x0000U, 16U);
    require_chr1k(*mapper, 0x1000U, 20U);

    mapper->write_prg(0x6000U, 0xA5U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA5U);
    write_serial(*mapper, 0xE000U, 0x13U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6000U) == 0U);

    mapper->write_prg(0xE000U, 0x80U);
    const auto before_filter = state(*mapper);
    mapper->write_prg_cpu(0xE000U, 0x01U, true);
    REQUIRE(state(*mapper) == before_filter);
    mapper->write_prg(0xE000U, 0x01U);
    REQUIRE(state(*mapper) != before_filter);
    mapper->write_prg_cpu(0xE000U, 0x80U, true);
    REQUIRE(state(*mapper) == before_filter);

    select(*mapper, 0x10U);
    mapper->write_expansion(0x5701U, 0x00U);
    write_serial(*mapper, 0x8000U, 0x0CU);
    write_serial(*mapper, 0xA000U, 0x10U);
    write_serial(*mapper, 0xE000U, 0x02U);
    require_prg16(*mapper, 0x8000U, 0x12U);
    require_prg16(*mapper, 0xC000U, 0x1FU);
    require_chr1k(*mapper, 0x0000U, 0U);
}

TEST_CASE("Mapper 468 discrete latch variants apply independent PRG CHR and CIRAM wiring",
          "[mapper468][banking][discrete][ciram][ram]") {
    auto mapper = board();
    select(*mapper, 0x40U);
    mapper->write_prg(0x8000U, 0x15U);
    require_prg32(*mapper, 0x8000U, 5U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    require_chr1k(*mapper, 0x1234U, 4U);
    select(*mapper, 0x4FU);
    mapper->write_prg(0xFFFFU, 0x03U);
    require_prg32(*mapper, 0x8000U, 7U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    select(*mapper, 0xB0U);
    mapper->write_prg(0x8000U, 0xE5U);
    require_prg16(*mapper, 0x8000U, 5U);
    require_prg16(*mapper, 0xC000U, 0x1FU);
    require_chr1k(*mapper, 0x0123U, 0x38U);

    select(*mapper, 0xC0U);
    mapper->write_prg(0x8000U, 0xA2U);
    require_prg32(*mapper, 0x8000U, 2U);
    require_chr1k(*mapper, 0x0123U, 0x50U);
    mapper->write_expansion(0x5601U, 0x10U);
    require_prg32(*mapper, 0x8000U, 0x802U);
    require_chr1k(*mapper, 0x0123U, 0x10U);

    select(*mapper, 0x50U);
    mapper->write_expansion(0x5701U, 0x00U);
    mapper->write_expansion(0x5601U, 0x00U);
    mapper->write_prg(0xE000U, 0x01U);
    mapper->write_prg(0x8000U, 0x02U);
    require_prg32(*mapper, 0x8000U, 1U);
    require_chr1k(*mapper, 0x0123U, 0x10U);
    mapper->write_prg(0x9000U, 0x10U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleUpper);
    select(*mapper, 0x53U);
    mapper->write_prg(0xE000U, 0x01U);
    mapper->write_prg(0x8000U, 0x03U);
    require_prg16(*mapper, 0x8000U, 3U);
    require_prg16(*mapper, 0xC000U, 3U);
    require_chr1k(*mapper, 0x0123U, 0x18U);

    auto uxrom = board(1U);
    uxrom->write_expansion(0x5702U, 0x00U);
    select(*uxrom, 0x7EU);
    uxrom->write_prg(0x8000U, 0x02U);
    require_prg16(*uxrom, 0x8000U, 2U);
    require_prg16(*uxrom, 0xC000U, 0x07U);
    REQUIRE(uxrom->mirror_mode() == MirrorMode::Horizontal);
    select(*uxrom, 0x76U);
    uxrom->write_prg(0x8000U, 0x07U);
    uxrom->write_prg(0xC000U, 0x02U);
    require_prg16(*uxrom, 0x8000U, 2U);
    require_prg16(*uxrom, 0xC000U, 7U);
    require_chr1k(*uxrom, 0x0123U, 0x18U);
    REQUIRE(uxrom->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 468 Nanjing provides WRAM split CHR RAM and supervisor mirroring",
          "[mapper468][nanjing][banking][ram][ciram]") {
    auto mapper = board(0U, MirrorMode::Vertical, 0U, kPrgSize, 0U);
    select(*mapper, 0xE4U);
    mapper->write_expansion(0x5000U, 0x85U);
    mapper->write_expansion(0x5200U, 0x01U);
    mapper->write_expansion(0x5300U, 0x04U);
    require_prg32(*mapper, 0x8000U, 0x15U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x6123U, 0xA6U);
    REQUIRE(mapper->read_prg(0x6123U) == 0xA6U);
    mapper->write_chr(0x0010U, 0x11U);
    REQUIRE(mapper->read_chr(0x0010U) == 0x11U);
    REQUIRE(mapper->read_chr(0x1010U) == 0x11U);
    for (unsigned line = 0U; line < 128U; ++line) {
        mapper->on_ppu_hblank();
    }
    mapper->write_chr(0x0010U, 0x22U);
    REQUIRE(mapper->read_chr(0x0010U) == 0x22U);
    REQUIRE(mapper->read_chr(0x1010U) == 0x22U);
    mapper->on_ppu_frame_start();
    REQUIRE(mapper->read_chr(0x0010U) == 0x11U);

    auto rom_backed = board();
    select(*rom_backed, 0xE0U);
    rom_backed->write_expansion(0x5000U, 0x80U);
    REQUIRE(rom_backed->read_chr(0x0010U) == 0U);
}

TEST_CASE("Mapper 468 PNROM and FxROM preserve latch-triggered CHR and outer banking",
          "[mapper468][banking][mmc2][mmc4][ciram][ram]") {
    auto mapper = board();
    select(*mapper, 0xA0U);
    mapper->write_prg(0xA000U, 0x03U);
    require_prg8(*mapper, 0x8000U, 3U);
    require_prg8(*mapper, 0xA000U, 0x0DU);
    require_prg8(*mapper, 0xC000U, 0x0EU);
    require_prg8(*mapper, 0xE000U, 0x0FU);
    mapper->write_prg(0xB000U, 0x02U);
    mapper->write_prg(0xD000U, 0x05U);
    REQUIRE(state(*mapper)[34U] == 0x02U);
    REQUIRE(state(*mapper)[36U] == 0x05U);
    require_chr1k(*mapper, 0x0000U, 8U);
    static_cast<void>(mapper->read_chr(0x0FD8U));
    require_chr1k(*mapper, 0x0000U, 8U);
    static_cast<void>(mapper->read_chr(0x0FE8U));
    REQUIRE(state(*mapper)[38U] == 0x01U);
    require_chr1k(*mapper, 0x0000U, 20U);
    mapper->write_prg(0x6000U, 0x66U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x66U);

    select(*mapper, 0x80U);
    mapper->write_prg(0xA000U, 0x04U);
    require_prg16(*mapper, 0x8000U, 4U);
    require_prg16(*mapper, 0xC000U, 0x0FU);
    mapper->write_prg(0xB000U, 0x01U);
    mapper->write_prg(0xD000U, 0x03U);
    static_cast<void>(mapper->read_chr(0x0FDBU));
    require_chr1k(*mapper, 0x0000U, 4U);
    mapper->write_prg(0xF000U, 0x01U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 468 TxROM implements MMC3 banking WRAM gates and repeat-zero IRQs",
          "[mapper468][mmc3][banking][irq][ram][ciram]") {
    auto mapper = board(1U);
    select(*mapper, 0x10U);
    mapper->write_prg(0x8000U, 0x06U);
    mapper->write_prg(0x8001U, 0x03U);
    mapper->write_prg(0x8000U, 0x07U);
    mapper->write_prg(0x8001U, 0x05U);
    require_prg8(*mapper, 0x8000U, 3U);
    require_prg8(*mapper, 0xA000U, 5U);
    require_prg8(*mapper, 0xC000U, 0x7EU);
    require_prg8(*mapper, 0xE000U, 0x7FU);
    mapper->write_prg(0x8000U, 0xC6U);
    require_prg8(*mapper, 0x8000U, 0x7EU);
    require_prg8(*mapper, 0xC000U, 3U);
    mapper->write_prg(0x8000U, 0x00U);
    mapper->write_prg(0x8001U, 0x82U);
    require_chr1k(*mapper, 0x0000U, 0x82U);
    require_chr1k(*mapper, 0x0400U, 0x83U);
    mapper->write_prg(0x8000U, 0x80U);
    require_chr1k(*mapper, 0x1000U, 0x82U);

    REQUIRE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x6000U, 0x5AU);
    REQUIRE(mapper->read_prg(0x6000U) == 0x5AU);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6000U, 0xA5U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x5AU);
    mapper->write_prg(0xA001U, 0x00U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6000U) == 0U);

    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    select(*mapper, 0x20U);
    mapper->write_prg(0xA000U, 0x02U);
    mapper->write_prg(0x8000U, 0x00U);
    mapper->write_prg(0x8001U, 0x80U);
    for (const Address addr : {0x2000U, 0x2400U, 0x2800U, 0x2C00U}) {
        const auto mapped = mapper->nametable_address(addr);
        REQUIRE(mapped);
        REQUIRE(*mapped == ((addr < 0x2800U ? 0x0400U : 0U) | (addr & 0x03FFU)));
    }
    mapper->write_prg(0x8001U, 0x00U);
    REQUIRE(mapper->nametable_address(0x2000U) == 0x0000U);
    mapper->write_prg(0xA000U, 0x01U);
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 468 VRC2 and VRC4 decode address wiring CHR bits swap and CPU IRQs",
          "[mapper468][vrc][banking][irq][ciram]") {
    auto vrc2 = board(2U);
    select(*vrc2, 0x00U);
    vrc2->write_prg(0x8000U, 0x03U);
    vrc2->write_prg(0xA000U, 0x05U);
    require_prg8(*vrc2, 0x8000U, 3U);
    require_prg8(*vrc2, 0xA000U, 5U);
    require_prg8(*vrc2, 0xC000U, 0x1EU);
    vrc2->write_prg(0xB000U, 0x02U);
    vrc2->write_prg(0xB001U, 0x01U);
    require_chr1k(*vrc2, 0x0000U, 9U);
    vrc2->write_prg(0x9000U, 0x01U);
    REQUIRE(vrc2->mirror_mode() == MirrorMode::Horizontal);
    vrc2->write_prg(0xF002U, 0x07U);
    vrc2->clock_cpu();
    REQUIRE_FALSE(vrc2->irq_pending());

    auto vrc4 = board(2U);
    select(*vrc4, 0x30U);
    vrc4->write_prg(0x8000U, 0x03U);
    vrc4->write_prg(0x9001U, 0x02U);
    require_prg8(*vrc4, 0x8000U, 0x1EU);
    require_prg8(*vrc4, 0xC000U, 3U);
    vrc4->write_prg(0xB000U, 0x02U);
    vrc4->write_prg(0xB002U, 0x01U);
    require_chr1k(*vrc4, 0x0000U, 0x12U);
    vrc4->write_prg(0xF000U, 0x0EU);
    vrc4->write_prg(0xF002U, 0x0FU);
    vrc4->write_prg(0xF001U, 0x07U);
    vrc4->clock_cpu();
    REQUIRE_FALSE(vrc4->irq_pending());
    vrc4->clock_cpu();
    REQUIRE(vrc4->irq_pending());
    vrc4->write_prg(0xF003U, 0U);
    REQUIRE(vrc4->irq_pending() == false);
    REQUIRE(vrc4->read_prg(0x6000U) == 0U);
    vrc4->write_prg(0x6000U, 0x77U);
    REQUIRE(vrc4->read_prg(0x6000U) == 0x77U);
}

TEST_CASE("Mapper 468 VRC1 VRC3 VRC6 VRC7 and FME-7 retain their distinct register maps",
          "[mapper468][vrc][fme7][banking][ciram]") {
    auto vrc1 = board(4U);
    select(*vrc1, 0x00U);
    vrc1->write_prg(0x8000U, 0x03U);
    vrc1->write_prg(0xA000U, 0x04U);
    vrc1->write_prg(0xC000U, 0x05U);
    require_prg8(*vrc1, 0x8000U, 3U);
    require_prg8(*vrc1, 0xA000U, 4U);
    require_prg8(*vrc1, 0xC000U, 5U);
    require_prg8(*vrc1, 0xE000U, 0x7FU);
    vrc1->write_prg(0xE000U, 0x02U);
    vrc1->write_prg(0x9000U, 0x03U);
    require_chr1k(*vrc1, 0x0000U, 0x48U);
    REQUIRE(vrc1->mirror_mode() == MirrorMode::Horizontal);

    auto vrc3 = board(4U);
    select(*vrc3, 0x40U);
    vrc3->write_prg(0xF000U, 0x03U);
    require_prg16(*vrc3, 0x8000U, 3U);
    require_prg16(*vrc3, 0xC000U, 7U);
    vrc3->write_prg(0x8000U, 0x0EU);
    vrc3->write_prg(0x9000U, 0x0FU);
    vrc3->write_prg(0xA000U, 0x0FU);
    vrc3->write_prg(0xB000U, 0x0FU);
    vrc3->write_prg(0xC000U, 0x06U);
    vrc3->clock_cpu();
    REQUIRE_FALSE(vrc3->irq_pending());
    vrc3->clock_cpu();
    REQUIRE(vrc3->irq_pending());
    vrc3->write_prg(0xD000U, 0U);
    REQUIRE_FALSE(vrc3->irq_pending());

    auto vrc6 = board(3U);
    select(*vrc6, 0x00U);
    vrc6->write_prg(0x8000U, 0x03U);
    vrc6->write_prg(0xC000U, 0x05U);
    require_prg16(*vrc6, 0x8000U, 3U);
    require_prg8(*vrc6, 0xC000U, 5U);
    require_prg8(*vrc6, 0xE000U, 0x1FU);
    vrc6->write_prg(0xD000U, 0x81U);
    require_chr1k(*vrc6, 0x0000U, 0x81U);
    vrc6->write_prg(0xB003U, 0x04U);
    REQUIRE(vrc6->mirror_mode() == MirrorMode::Horizontal);

    auto vrc7 = board(4U);
    select(*vrc7, 0x10U);
    vrc7->write_prg(0x8000U, 0x03U);
    vrc7->write_prg(0x8010U, 0x04U);
    vrc7->write_prg(0x9000U, 0x05U);
    require_prg8(*vrc7, 0x8000U, 3U);
    require_prg8(*vrc7, 0xA000U, 4U);
    require_prg8(*vrc7, 0xC000U, 5U);
    require_prg8(*vrc7, 0xE000U, 0x7FU);
    vrc7->write_prg(0xA000U, 0x81U);
    require_chr1k(*vrc7, 0x0000U, 0x81U);
    vrc7->write_prg(0xE000U, 0x01U);
    REQUIRE(vrc7->mirror_mode() == MirrorMode::Horizontal);

    auto fme7 = board(5U);
    select(*fme7, 0x00U);
    fme7->write_prg(0x8000U, 0x09U);
    fme7->write_prg(0xA000U, 0x03U);
    require_prg8(*fme7, 0xA000U, 3U);
    require_prg8(*fme7, 0xE000U, 0x1FU);
    fme7->write_prg(0x8000U, 0x00U);
    fme7->write_prg(0xA000U, 0x81U);
    require_chr1k(*fme7, 0x0000U, 0x81U);
    fme7->write_prg(0x8000U, 0x0CU);
    fme7->write_prg(0xA000U, 0x01U);
    REQUIRE(fme7->mirror_mode() == MirrorMode::Horizontal);
    fme7->write_prg(0x8000U, 0x08U);
    fme7->write_prg(0xA000U, 0xC3U);
    fme7->write_prg(0x6000U, 0x39U);
    REQUIRE(fme7->read_prg(0x6000U) == 0x39U);
}

TEST_CASE("Mapper 468 SMB2J and FME-7 clock independent CPU IRQ counters",
          "[mapper468][irq][lf36][fme7][bus]") {
    auto lf36 = board();
    select(*lf36, 0x70U);
    lf36->write_prg(0xE000U, 0x03U);
    require_prg8(*lf36, 0xC000U, 3U);
    require_prg8(*lf36, 0x8000U, 4U);
    require_prg8(*lf36, 0xA000U, 5U);
    require_prg8(*lf36, 0xE000U, 7U);
    require_prg8(*lf36, 0x6000U, 6U);
    REQUIRE(lf36->maps_prg(0x6000U));
    lf36->write_prg(0x6000U, 0xFFU);
    require_prg8(*lf36, 0x6000U, 6U);
    lf36->write_prg(0xA000U, 0U);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        lf36->clock_cpu();
    }
    REQUIRE(lf36->irq_pending());
    lf36->write_prg(0x8000U, 0U);
    REQUIRE_FALSE(lf36->irq_pending());

    auto fme7 = board(5U);
    select(*fme7, 0x00U);
    fme7->write_prg(0x8000U, 0x0EU);
    fme7->write_prg(0xA000U, 0x01U);
    fme7->write_prg(0x8000U, 0x0FU);
    fme7->write_prg(0xA000U, 0x00U);
    fme7->write_prg(0x8000U, 0x0DU);
    fme7->write_prg(0xA000U, 0x81U);
    fme7->clock_audio();
    REQUIRE_FALSE(fme7->irq_pending());
    fme7->clock_audio();
    REQUIRE(fme7->irq_pending());
    fme7->acknowledge_irq();
    REQUIRE_FALSE(fme7->irq_pending());
}

TEST_CASE("Mapper 468 routes VRC6 VRC7 and FME-7 expansion audio every CPU cycle",
          "[mapper468][audio][vrc6][vrc7][fme7][irq]") {
    auto vrc6 = board(3U);
    select(*vrc6, 0x00U);
    REQUIRE(vrc6->has_expansion_audio());
    REQUIRE(vrc6->audio_output() == 0.0F);
    vrc6->write_prg(0x9000U, 0x8FU);
    vrc6->write_prg(0x9002U, 0x80U);
    REQUIRE(vrc6->audio_output() > 0.08F);
    vrc6->write_prg(0xF000U, 0xFEU);
    vrc6->write_prg(0xF001U, 0x07U);
    vrc6->clock_audio();
    REQUIRE_FALSE(vrc6->irq_pending());
    vrc6->clock_audio();
    REQUIRE(vrc6->irq_pending());
    select(*vrc6, 0x00U);
    REQUIRE(vrc6->audio_output() == 0.0F);
    REQUIRE_FALSE(vrc6->irq_pending());

    auto vrc7 = board(4U);
    select(*vrc7, 0x10U);
    REQUIRE(vrc7->has_expansion_audio());
    write_vrc7_register(*vrc7, 0x04U, 0xF0U);
    write_vrc7_register(*vrc7, 0x05U, 0xF0U);
    write_vrc7_register(*vrc7, 0x10U, 0xFFU);
    write_vrc7_register(*vrc7, 0x20U, 0x10U);
    write_vrc7_register(*vrc7, 0x30U, 0x00U);
    for (unsigned cycle = 0U; cycle < 36U; ++cycle) {
        vrc7->clock_audio();
    }
    REQUIRE(vrc7->audio_output() != 0.0F);
    vrc7->write_prg(0xE010U, 0xFEU);
    vrc7->write_prg(0xF000U, 0x03U);
    for (unsigned cycle = 0U; cycle < 228U; ++cycle) {
        vrc7->clock_audio();
    }
    REQUIRE(vrc7->irq_pending());

    auto fme7 = board(5U);
    select(*fme7, 0x00U);
    REQUIRE(fme7->has_expansion_audio());
    fme7->write_prg(0xC000U, 0x07U);
    fme7->write_prg(0xE000U, 0x3FU);
    fme7->write_prg(0xC000U, 0x08U);
    fme7->write_prg(0xE000U, 0x0FU);
    REQUIRE(fme7->audio_output() > 0.07F);
    const float initial = fme7->audio_output();
    fme7->write_prg(0xC000U, 0x00U);
    fme7->write_prg(0xE000U, 0x01U);
    fme7->write_prg(0xC000U, 0x07U);
    fme7->write_prg(0xE000U, 0x3EU);
    for (unsigned cycle = 0U; cycle < 32U; ++cycle) {
        fme7->clock_audio();
    }
    REQUIRE(fme7->audio_output() != initial);
}

TEST_CASE("Mapper 468 reset reselects defaults retains RAM and cold power follows persistence",
          "[mapper468][reset][ram]") {
    auto mapper = board(0U, MirrorMode::Vertical, 0U, kPrgSize, 0U, 0x2000U);
    select(*mapper, 0xB0U);
    mapper->write_prg(0x8000U, 0xE5U);
    select(*mapper, 0xE0U);
    mapper->write_prg(0x6000U, 0x5AU);
    mapper->write_chr(0x0123U, 0xA6U);
    mapper->write_expansion(0x5701U, 0x20U);
    const auto cold = state(*mapper);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6000U) == 0x5AU);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA6U);
    mapper->write_expansion(0x5701U, 0x00U);
    select(*mapper, 0x00U);
    require_prg16(*mapper, 0x8000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(state(*mapper) != cold);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->read_chr(0x0123U) == 0U);

    auto persistent = board(0U, MirrorMode::Vertical, 0U, kPrgSize, kChrSize, 0U, 0x2000U);
    persistent->write_prg(0x6000U, 0xA5U);
    persistent->power_on();
    REQUIRE(persistent->read_prg(0x6000U) == 0xA5U);
    persistent->set_battery_ram(std::array<Byte, 0x2000>{});
    persistent->write_prg(0x6000U, 0xA5U);
    persistent->reset();
    REQUIRE(persistent->read_prg(0x6000U) == 0xA5U);
    persistent->power_on();
    REQUIRE(persistent->read_prg(0x6000U) == 0xA5U);
}

TEST_CASE("Mapper 468 serial EEPROM bit protocol reads back written nybbles",
          "[mapper468][bus][eeprom][state]") {
    auto mapper = board();
    const auto write_bit = [&](Byte bit) {
        mapper->write_expansion(0x5301U, bit);
        mapper->write_expansion(0x5301U, static_cast<Byte>(bit | 2U));
    };
    const auto write_byte = [&](Byte value) {
        Byte output = 0U;
        for (unsigned bit = 0U; bit < 8U; ++bit) {
            write_bit((value >> (7U - bit)) & 1U);
            output = static_cast<Byte>((output << 1U) |
                                       (mapper->read_expansion(0x5301U) != 0U ? 1U : 0U));
        }
        return output;
    };
    static_cast<void>(write_byte(0xA3U));
    REQUIRE(write_byte(0xA5U) == 0x07U);
    static_cast<void>(write_byte(0x53U));
    Byte result = 0U;
    for (unsigned bit = 0U; bit < 8U; ++bit) {
        mapper->write_expansion(0x5301U, 0U);
        mapper->write_expansion(0x5301U, 2U);
        result =
            static_cast<Byte>((result << 1U) | (mapper->read_expansion(0x5301U) != 0U ? 1U : 0U));
    }
    REQUIRE(result == 0xA5U);
}

TEST_CASE("Mapper 468 snapshots replay active IRQ audio EEPROM and RAM without ROM bytes",
          "[mapper468][state][audio][irq][ram]") {
    auto mapper = board(5U);
    select(*mapper, 0x00U);
    mapper->write_expansion(0x5701U, 0x20U);
    mapper->write_prg(0x8000U, 0x09U);
    mapper->write_prg(0xA000U, 0x03U);
    mapper->write_prg(0x8000U, 0x08U);
    mapper->write_prg(0xA000U, 0xC1U);
    mapper->write_prg(0x6000U, 0xA5U);
    mapper->write_prg(0xC000U, 0x07U);
    mapper->write_prg(0xE000U, 0x3FU);
    mapper->write_prg(0xC000U, 0x08U);
    mapper->write_prg(0xE000U, 0x0FU);
    mapper->write_prg(0x8000U, 0x0EU);
    mapper->write_prg(0xA000U, 0x02U);
    mapper->write_prg(0x8000U, 0x0FU);
    mapper->write_prg(0xA000U, 0x00U);
    mapper->write_prg(0x8000U, 0x0DU);
    mapper->write_prg(0xA000U, 0x81U);
    mapper->clock_audio();
    const auto saved = state(*mapper);
    auto copy = board(5U, MirrorMode::Vertical, 0xA5U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6000U) == 0xA5U);
    require_prg8(*copy, 0xA000U, 0x403U, 0xA5U);
    REQUIRE(copy->audio_output() == mapper->audio_output());
    copy->clock_audio();
    REQUIRE_FALSE(copy->irq_pending());
    copy->clock_audio();
    REQUIRE(copy->irq_pending());
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE_FALSE(copy->irq_pending());

    auto fm = board(4U);
    select(*fm, 0x10U);
    write_vrc7_register(*fm, 0x04U, 0xF0U);
    write_vrc7_register(*fm, 0x05U, 0xF0U);
    write_vrc7_register(*fm, 0x10U, 0xFFU);
    write_vrc7_register(*fm, 0x20U, 0x10U);
    for (unsigned cycle = 0U; cycle < 72U; ++cycle) {
        fm->clock_audio();
    }
    const auto fm_saved = state(*fm);
    auto fm_copy = board(4U, MirrorMode::Vertical, 0x35U);
    load(*fm_copy, fm_saved);
    REQUIRE(state(*fm_copy) == fm_saved);
    REQUIRE(fm_copy->audio_output() == fm->audio_output());
}

TEST_CASE("Mapper 468 rejects truncated damaged and mismatched states atomically",
          "[mapper468][state][geometry][audio]") {
    auto mapper = board(4U);
    select(*mapper, 0x10U);
    mapper->write_prg(0x8000U, 0x03U);
    mapper->write_prg(0x6000U, 0xA5U);
    write_vrc7_register(*mapper, 0x04U, 0xF0U);
    write_vrc7_register(*mapper, 0x05U, 0xF0U);
    write_vrc7_register(*mapper, 0x10U, 0xFFU);
    write_vrc7_register(*mapper, 0x20U, 0x10U);
    for (unsigned cycle = 0U; cycle < 36U; ++cycle) {
        mapper->clock_audio();
    }
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    for (std::size_t length = 0U; length < saved.size(); length += 997U) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    reject(std::span(saved).first(saved.size() - 1U));

    constexpr std::array<std::pair<std::size_t, Byte>, 20> invalid_fields = {
        {{0U, 1U},     {2U, 1U},     {3U, 1U},     {11U, 4U},    {13U, 1U},
         {16U, 2U},    {26U, 4U},    {27U, 0x20U}, {28U, 5U},    {29U, 0x20U},
         {33U, 0x10U}, {34U, 0x20U}, {38U, 2U},    {40U, 0x08U}, {49U, 4U},
         {70U, 0x10U}, {79U, 8U},    {80U, 2U},    {92U, 3U},    {180U, 2U}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }
    auto excessive_audio = saved;
    excessive_audio[kAudioLength] = 0xFFU;
    excessive_audio[kAudioLength + 1U] = 0xFFU;
    excessive_audio[kAudioLength + 2U] = 0xFFU;
    excessive_audio[kAudioLength + 3U] = 0xFFU;
    reject(excessive_audio);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    mapper->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*mapper) == saved);

    auto wrong_geometry = board(4U, MirrorMode::Vertical, 0U, kPrgSize / 2U, kChrSize);
    const auto before = state(*wrong_geometry);
    StateReader wrong_reader(saved);
    wrong_geometry->load_state(wrong_reader);
    REQUIRE_FALSE(wrong_reader.ok());
    REQUIRE(state(*wrong_geometry) == before);
}

} // namespace
