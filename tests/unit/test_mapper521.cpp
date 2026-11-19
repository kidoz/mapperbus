#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper521.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kDefaultPrgSize = 0x030000UZ;
constexpr std::size_t kMaxPrgSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kBankStateOffset = 10UZ;
constexpr std::size_t kChrRamStateOffset = 11UZ;
constexpr std::size_t kStateSize = kChrRamStateOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 521U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kDefaultPrgSize;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 12U) * 47U +
                             (offset >> 14U) * 59U + (offset >> 16U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper521> board(std::size_t prg_size = kDefaultPrgSize,
                                 MirrorMode mirror = MirrorMode::Horizontal,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper521::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper521>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, kChrRamStateOffset> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    REQUIRE(bytes.size() == kStateSize);
    std::array<Byte, kChrRamStateOffset> result{};
    std::copy_n(bytes.begin(), kChrRamStateOffset, result.begin());
    return result;
}

void load(Mapper521& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper521& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0123U);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0123U) == chr);
}

void require_prg(Mapper521& mapper, Address addr, Byte bank, std::size_t prg_size, Byte salt) {
    const auto physical = addr >= 0xC000U ? prg_size - kPrgBankSize + (addr & (kPrgBankSize - 1U))
                                          : (static_cast<std::size_t>(bank & 0x0FU) * kPrgBankSize +
                                             (addr & (kPrgBankSize - 1U))) %
                                                prg_size;
    CAPTURE(addr, bank, prg_size, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 521 validates banked PRG geometry and bounded volatile state",
          "[mapper521][geometry][ram][ciram][state]") {
    for (std::size_t size = kPrgBankSize * 2U; size <= kMaxPrgSize; size += kPrgBankSize) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            CAPTURE(size, mirror);
            REQUIRE(Mapper521::validate_geometry(header(mirror), size, 0U));
            auto mapper = board(size, mirror, 0x35U);
            auto different_rom = board(size, mirror, 0x80U);
            REQUIRE(state(*mapper) == state(*different_rom));
            REQUIRE(state(*mapper).size() == kStateSize);
            REQUIRE(state(*mapper).size() == 8203U);
            REQUIRE(register_state(*mapper)[kBankStateOffset] == 0U);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->shared_nvram().empty());
            REQUIRE_FALSE(mapper->has_flash_storage());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE(mapper->read_chr(0U) == 0U);
            REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
            require_prg(*mapper, 0x8000U, 0U, size, 0x35U);
            require_prg(*mapper, 0xFFFFU, 0U, size, 0x35U);
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
        REQUIRE_FALSE(Mapper521::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 517U, 520U, 522U, 4095U}) {
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
                                   0x2000UZ,
                                   0x4000UZ,
                                   0x6000UZ,
                                   0x7FFFUZ,
                                   0x8001UZ,
                                   0x9000UZ,
                                   kMaxPrgSize + kPrgBankSize,
                                   0x100000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ, 0x100000UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = kChrRamSize;
            break;
        case 3U:
            h.prg_nvram_size = kChrRamSize;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x1000U;
            break;
        case 6U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        reject(h, kDefaultPrgSize, 0U);
    }
}

TEST_CASE("Mapper 521 exhaustively decodes the write-only F030 register window",
          "[mapper521][decoding][bus][banking]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        const bool selected = (raw & 0xF030U) == 0x5020U;
        const auto expected_bank = static_cast<Byte>(selected ? value & 0x0FU : 0U);

        mapper->write_expansion(addr, value);
        CAPTURE(raw, value, selected, expected_bank);
        REQUIRE(register_state(*mapper)[kBankStateOffset] == expected_bank);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }

        mapper->write_prg(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg_cpu(addr, value, true);
        REQUIRE(register_state(*mapper)[kBankStateOffset] == expected_bank);
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    for (const Address alias : {0x5020U, 0x502FU, 0x5120U, 0x5F20U, 0x5F2FU}) {
        mapper->write_expansion(alias, 0xFFU);
        CAPTURE(alias);
        REQUIRE(register_state(*mapper)[kBankStateOffset] == 0x0FU);
    }
    for (const Address ignored : {0x4020U, 0x5010U, 0x5030U, 0x503FU, 0x5F30U, 0x6020U}) {
        mapper->write_expansion(ignored, 0U);
        CAPTURE(ignored);
        REQUIRE(register_state(*mapper)[kBankStateOffset] == 0x0FU);
    }
}

TEST_CASE("Mapper 521 selects four-bit lower banks and fixes upper PRG to the last bank",
          "[mapper521][banking][prg][bus]") {
    for (std::size_t size = kPrgBankSize * 2U; size <= kMaxPrgSize; size += kPrgBankSize) {
        auto mapper = board(size, MirrorMode::Vertical, 0x35U);
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            const auto value = static_cast<Byte>(raw);
            mapper->write_expansion(0x5020U, value);
            REQUIRE(register_state(*mapper)[kBankStateOffset] == (value & 0x0FU));
            for (const auto addr : kPrgAddresses) {
                require_prg(*mapper, addr, value, size, 0x35U);
            }
        }
    }

    auto mapper = board(0xC000UZ, MirrorMode::Horizontal, 0xA0U);
    mapper->write_expansion(0x5F2FU, 0xFFU);
    for (Address addr = 0x8000U;; ++addr) {
        require_prg(*mapper, addr, 0xFFU, 0xC000UZ, 0xA0U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
}

TEST_CASE("Mapper 521 provides unbanked CHR RAM and ordinary hard-wired CIRAM",
          "[mapper521][ram][ciram][bus][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, mirror);
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            mapper->write_chr(addr, cell(addr, 0x60U));
        }
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            CAPTURE(addr);
            REQUIRE(mapper->read_chr(addr) == cell(addr, 0x60U));
        }
        REQUIRE(mapper->mirror_mode() == mirror);
        for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
            REQUIRE(mapper->maps_nametable(addr));
            REQUIRE_FALSE(mapper->nametable_address(addr));
            REQUIRE_FALSE(mapper->ciram_address(addr));
            REQUIRE_FALSE(mapper->read_nametable(addr));
            REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
        }
        const auto before = state(*mapper);
        REQUIRE(mapper->read_chr(0x2000U) == 0U);
        REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
        mapper->write_chr(0x2000U, 0xA5U);
        mapper->write_chr(0xFFFFU, 0x5AU);
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
        REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));

        mapper->write_expansion(0x5020U, 0xFFU);
        mapper->reset();
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U, 0x60U));
        mapper->power_on();
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            REQUIRE(mapper->read_chr(addr) == 0U);
        }
    }
}

TEST_CASE("Mapper 521 exposes no work RAM IRQ audio or unrelated side effects",
          "[mapper521][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_expansion(0x5020U, 0xFFU);
    mapper->write_chr(0x0123U, 0x5AU);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x2012U);
    mapper->observe_ppu_read(0x2012U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE(mapper->led_outputs() == std::nullopt);
}

TEST_CASE("Mapper 521 reset selects bank zero and cold power clears only CHR RAM",
          "[mapper521][reset][banking][ram][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kDefaultPrgSize, mirror);
        const auto cold = state(*mapper);
        const auto cold_registers = register_state(*mapper);
        mapper->write_chr(0x0123U, 0xA5U);
        mapper->write_expansion(0x5020U, 0xDFU);
        REQUIRE(state(*mapper) != cold);

        mapper->reset();
        REQUIRE(register_state(*mapper) == cold_registers);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
        require_prg(*mapper, 0x8000U, 0U, kDefaultPrgSize, 0U);
        require_prg(*mapper, 0xFFFFU, 0U, kDefaultPrgSize, 0U);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE(mapper->audio_output() == 0.0F);

        mapper->write_expansion(0x5020U, 0xFFU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
    }
}

TEST_CASE("Mapper 521 bounded snapshots replay bank and CHR without ROM",
          "[mapper521][state][banking][ram]") {
    auto mapper = board(kDefaultPrgSize, MirrorMode::Vertical, 0x35U);
    mapper->write_chr(0x0123U, 0xA6U);
    mapper->write_expansion(0x5020U, 0xDFU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < 0x2100UZ);
    REQUIRE(saved[kBankStateOffset] == 0x0FU);
    REQUIRE(saved[kChrRamStateOffset + 0x123U] == 0xA6U);

    auto copy = board(kDefaultPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0xDFU, kDefaultPrgSize, 0x80U);
    }
    REQUIRE(copy->read_chr(0x0123U) == 0xA6U);
    REQUIRE(copy->audio_output() == 0.0F);
    REQUIRE_FALSE(copy->irq_pending());

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

TEST_CASE("Mapper 521 rejects truncated damaged and mismatched states atomically",
          "[mapper521][state][atomic][ram][banking]") {
    auto mapper = board();
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_expansion(0x5020U, 0xDFU);
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kBankStateOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        load_rejected(*mapper, invalid);
    }
    for (const Byte invalid_bank : {Byte{0x10U}, Byte{0x80U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kBankStateOffset] = invalid_bank;
        CAPTURE(invalid_bank);
        load_rejected(*mapper, invalid);
    }

    auto wrong_prg = board(kDefaultPrgSize / 2U);
    load_rejected(*wrong_prg, saved);
    auto wrong_mirror = board(kDefaultPrgSize, MirrorMode::Vertical);
    load_rejected(*wrong_mirror, saved);

    auto changed = saved;
    changed[kBankStateOffset] = 0x03U;
    changed[kChrRamStateOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(register_state(*mapper)[kBankStateOffset] == 0x03U);
}

} // namespace
