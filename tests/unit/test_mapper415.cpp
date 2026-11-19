#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper415.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x002000UZ;
constexpr std::size_t kFixedPrgSize = 0x008000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kDataLatchOffset = 14UZ;
constexpr std::size_t kChrRamOffset = 15UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 12> kPrgAddresses{0x6000U,
                                                0x6123U,
                                                0x7000U,
                                                0x7FFFU,
                                                0x8000U,
                                                0x9234U,
                                                0xA000U,
                                                0xB234U,
                                                0xC000U,
                                                0xD234U,
                                                0xE000U,
                                                0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 415U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper415> board(std::size_t prg_size = kMaxPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper415::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper415>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper415& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t switchable_address(Address addr, unsigned bank, std::size_t prg_size) {
    return (static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U))) %
           prg_size;
}

std::size_t fixed_address(Address addr, std::size_t prg_size) {
    return prg_size - kFixedPrgSize + (addr & (kFixedPrgSize - 1U));
}

TEST_CASE("Mapper 415 validates decoder geometry and deterministic power-on",
          "[mapper415][geometry][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize, mirror);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 8207U);
        REQUIRE(mapper->read_prg(0x6000U) == cell(0U));
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(kFixedPrgSize - 1U));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        auto other_rom = board(kMinPrgSize, mirror, 0x80U);
        REQUIRE(state(*other_rom) == state(*mapper));
    }
    for (const std::size_t size : {kMinPrgSize, 0xA000UZ, 0x10000UZ, 0x18000UZ, kMaxPrgSize}) {
        CAPTURE(size);
        REQUIRE(Mapper415::validate_geometry(header(), size, 0U));
    }

    for (const unsigned id : {0U, 2U, 256U, 414U, 416U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper415::validate_geometry(h, kMinPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper415::validate_geometry(legacy, kMinPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        CAPTURE(submapper);
        REQUIRE_FALSE(Mapper415::validate_geometry(h, kMinPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x6000UZ, 0x7FFFUZ, 0x8001UZ, 0x8800UZ, kMaxPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper415::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, kMinPrgSize, kMaxPrgSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper415::validate_geometry(header(), kMinPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper415::validate_geometry(header(mirror), kMinPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 8U; ++field) {
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
        case 4U:
            h.chr_ram_size = 0x1000U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        case 6U:
            h.chr_nvram_size = kChrRamSize;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper415::validate_geometry(h, kMinPrgSize, 0U));
    }
}

TEST_CASE("Mapper 415 decodes every A15 write without address or bus-conflict effects",
          "[mapper415][decoding][banking][bus]") {
    auto mapper = board();
    mapper->write_chr(0x0123U, 0x5AU);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        const bool decoded = raw >= 0x8000U;
        const unsigned bank = decoded ? value & 0x0FU : 0U;
        mapper->write_prg(addr, value);
        CAPTURE(raw, value, decoded, bank);
        REQUIRE(mapper->read_prg(0x6123U) == cell(switchable_address(0x6123U, bank, kMaxPrgSize)));
        REQUIRE(mapper->read_prg(0xC123U) == cell(fixed_address(0xC123U, kMaxPrgSize)));
        REQUIRE(mapper->mirror_mode() ==
                (decoded && (value & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->read_chr(0x0123U) == 0x5AU);
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xA5A5U, static_cast<Byte>(value));
        const auto bytes = state(*mapper);
        CAPTURE(value);
        REQUIRE(bytes[kDataLatchOffset] == (value & 0x1FU));
        REQUIRE(mapper->read_prg(0x7123U) ==
                cell(switchable_address(0x7123U, value & 0x0FU, kMaxPrgSize)));
    }

    mapper->reset();
    const auto before = state(*mapper);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU}) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    for (const auto addr : kPrgAddresses) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA7U));
}

TEST_CASE("Mapper 415 maps all sixteen low banks and the physical final 32 KiB",
          "[mapper415][banking][bus]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(static_cast<Address>(0x8000U | (bank * 0x0101U)),
                          static_cast<Byte>(bank));
        for (const auto addr : kPrgAddresses) {
            const auto physical = addr < 0x8000U ? switchable_address(addr, bank, kMaxPrgSize)
                                                 : fixed_address(addr, kMaxPrgSize);
            CAPTURE(bank, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0xA000UZ, 0x10000UZ, 0x18000UZ, kMaxPrgSize}) {
        auto fitted = board(size);
        fitted->write_prg(0xFFFFU, 0x0FU);
        for (const auto addr : kPrgAddresses) {
            const auto physical =
                addr < 0x8000U ? switchable_address(addr, 15U, size) : fixed_address(addr, size);
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 415 provides unbanked writable 8 KiB CHR RAM", "[mapper415][ram][chr]") {
    auto mapper = board();
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        const auto value = cell(addr, 0x35U);
        mapper->write_chr(addr, value);
        CAPTURE(addr, value);
        REQUIRE(mapper->read_chr(addr) == value);
        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) == value);
    }
    mapper->write_prg(0xFFFFU, 0x1FU);
    for (const Address addr : {0x0000U, 0x0123U, 0x07FFU, 0x0800U, 0x1123U, 0x1FFFU}) {
        CAPTURE(addr);
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0x35U));
    }
}

TEST_CASE("Mapper 415 uses only latch M for horizontal vertical console CIRAM",
          "[mapper415][ciram][decoding]") {
    auto mapper = board(kMaxPrgSize, MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 0x10U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (const Address addr : {0x0000U, 0x0123U, 0x1FFFU, 0x2000U, 0x2ABCU, 0x3000U, 0x3EFFU}) {
            REQUIRE_FALSE(mapper->ciram_address(addr));
            if (addr >= 0x2000U) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, 0xA5U));
            }
        }
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 415 has no CPU RAM IRQ audio or observer side effects",
          "[mapper415][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0xA5A5U, 0x1DU);
    mapper->write_chr(0x1123U, 0x5CU);
    const auto before = state(*mapper);
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1FFFU);
    mapper->observe_ppu_read(0x1FFFU);
    mapper->clock_irq_counter();
    mapper->acknowledge_irq();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("default"));
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 415 reset clears the latch and cold power clears CHR RAM",
          "[mapper415][reset][ram][ciram]") {
    for (const bool cold : {false, true}) {
        auto mapper = board(kMaxPrgSize, MirrorMode::Horizontal);
        mapper->write_prg(0xA5A5U, 0x1FU);
        mapper->write_chr(0x0123U, 0xA7U);
        mapper->write_chr(0x1123U, 0x5CU);
        if (cold) {
            mapper->power_on();
        } else {
            mapper->reset();
        }
        CAPTURE(cold);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(state(*mapper)[kDataLatchOffset] == 0U);
        REQUIRE(mapper->read_prg(0x6123U) == cell(0x0123U));
        REQUIRE(mapper->read_prg(0xC123U) == cell(fixed_address(0xC123U, kMaxPrgSize)));
        REQUIRE(mapper->read_chr(0x0123U) == (cold ? 0U : 0xA7U));
        REQUIRE(mapper->read_chr(0x1123U) == (cold ? 0U : 0x5CU));
    }
}

TEST_CASE("Mapper 415 bounded snapshots replay latch and CHR RAM without ROM",
          "[mapper415][state][ram]") {
    auto mapper = board(kMaxPrgSize, MirrorMode::Horizontal);
    mapper->write_prg(0xA5A5U, 0x1DU);
    mapper->write_chr(0x0123U, 0xA7U);
    mapper->write_chr(0x1123U, 0x5CU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kMinPrgSize);
    REQUIRE(saved[kDataLatchOffset] == 0x1DU);
    REQUIRE(saved[kChrRamOffset + 0x0123U] == 0xA7U);
    REQUIRE(saved[kChrRamOffset + 0x1123U] == 0x5CU);

    auto copy = board(kMaxPrgSize, MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        const auto physical = addr < 0x8000U ? switchable_address(addr, 13U, kMaxPrgSize)
                                             : fixed_address(addr, kMaxPrgSize);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    REQUIRE(copy->read_chr(0x0123U) == 0xA7U);
    REQUIRE(copy->read_chr(0x1123U) == 0x5CU);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 415 rejects damaged truncated and mismatched states atomically",
          "[mapper415][state]") {
    auto mapper = board();
    mapper->write_prg(0xA5A5U, 0x1DU);
    mapper->write_chr(0x1123U, 0x5AU);
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
    for (const Byte latch : {0x20U, 0x40U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kDataLatchOffset] = latch;
        CAPTURE(latch);
        reject(invalid);
    }

    auto wrong_prg = board(0x10000UZ);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto changed = saved;
    changed[kDataLatchOffset] = 0x12U;
    changed[kChrRamOffset + 0x0123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
