#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper403.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x008000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kPrgBankOffset = 10UZ;
constexpr std::size_t kChrBankOffset = 11UZ;
constexpr std::size_t kModeOffset = 12UZ;
constexpr std::size_t kChrRamOffset = 13UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr Address kLowRegisterMask = 0xE103U;
constexpr Byte kHorizontalMirroring = 0x10U;
constexpr Byte kHighChrLatchEnable = 0x04U;
constexpr Byte kNrom128Mode = 0x01U;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 403U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 19U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper403> board(std::size_t prg_size = kPrgSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper403::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper403>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::vector<Byte> register_state(const Mapper& mapper) {
    auto bytes = state(mapper);
    bytes.resize(kChrRamOffset);
    return bytes;
}

void load(Mapper403& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

bool decoded_low_register(Address addr) {
    const auto decoded = static_cast<unsigned>(addr & kLowRegisterMask);
    return decoded >= 0x4100U && decoded <= 0x4102U;
}

std::size_t expected_prg(Address addr, Byte bank_value, Byte mode) {
    unsigned bank = (bank_value & 0x7EU) >> 1U;
    if ((mode & kNrom128Mode) == 0U) {
        bank = (bank & 0x3EU) | ((addr & 0x4000U) != 0U ? 1U : 0U);
    }
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x3FFFU);
}

TEST_CASE("Mapper 403 validates bounded ROM and volatile CHR RAM geometry",
          "[mapper403][geometry]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kPrgSize, mirror);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(register_state(*mapper)[kPrgBankOffset] == 0U);
        REQUIRE(register_state(*mapper)[kChrBankOffset] == 0U);
        REQUIRE(register_state(*mapper)[kModeOffset] == 0U);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(state(*mapper).size() == 32781U);
    }
    for (std::size_t size = 0x8000UZ; size <= kPrgSize; size += kPrgBankSize) {
        CAPTURE(size);
        REQUIRE(Mapper403::validate_geometry(header(), size, 0U));
    }

    for (const unsigned id : {0U, 4U, 400U, 402U, 404U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper403::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper403::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper403::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, kPrgSize + 1U, kPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper403::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, kChrRamSize, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper403::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper403::validate_geometry(header(mirror), kPrgSize, 0U));
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
            h.chr_ram_size = 0x2000U;
            break;
        case 5U:
            h.chr_nvram_size = kChrRamSize;
            h.has_battery = true;
            break;
        default:
            h.has_trainer = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper403::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 403 decodes every low register alias and floats read data",
          "[mapper403][decoding][bus]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        const auto addr = static_cast<Address>(raw);
        const bool decoded = decoded_low_register(addr);
        actual->write_expansion(addr, value);
        if (decoded) {
            expected->write_expansion(addr & kLowRegisterMask, value);
        }
        CAPTURE(raw, value, decoded);
        REQUIRE(register_state(*actual) == register_state(*expected));
        REQUIRE(actual->maps_expansion(addr) == decoded);
        REQUIRE(actual->expansion_read_mask(addr) == (decoded ? 0U : 0xFFU));
        REQUIRE(actual->read_expansion(addr) == 0U);
    }
}

TEST_CASE("Mapper 403 high latch decodes all addresses only while C is set",
          "[mapper403][decoding][bus]") {
    auto actual = board();
    auto expected = board();
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        expected->reset();
        actual->write_expansion(0x4102U, 0x15U);
        expected->write_expansion(0x4102U, 0x15U);
        const auto value = static_cast<Byte>(raw * 37U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected->write_prg(0x8000U, value);
        }
        CAPTURE(raw, value);
        REQUIRE(register_state(*actual) == register_state(*expected));
    }

    actual->write_expansion(0x4102U, 0U);
    actual->write_expansion(0x4101U, 2U);
    const auto before = register_state(*actual);
    for (const Address addr : {0x8000U, 0xA5A5U, 0xC000U, 0xFFFFU}) {
        actual->write_prg(addr, 3U);
        REQUIRE(register_state(*actual) == before);
    }
}

TEST_CASE("Mapper 403 implements PRG A19 through A14 and both NROM modes",
          "[mapper403][banking][bus]") {
    auto mapper = board();
    constexpr std::array<Address, 4> addresses{0x8123U, 0xA123U, 0xC123U, 0xE123U};
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x4100U, static_cast<Byte>(value));
        for (const Byte mode : {Byte{0U}, Byte{kNrom128Mode}}) {
            mapper->write_expansion(0x4102U, mode);
            for (const auto addr : addresses) {
                const auto physical = expected_prg(addr, static_cast<Byte>(value), mode);
                CAPTURE(value, mode, addr, physical);
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(kPrgBankSize));
    mapper->write_expansion(0x4100U, 0x7EU);
    REQUIRE(mapper->read_prg(0x8000U) == cell(62UZ * kPrgBankSize));
    REQUIRE(mapper->read_prg(0xC000U) == cell(63UZ * kPrgBankSize));
    mapper->write_expansion(0x4102U, kNrom128Mode);
    REQUIRE(mapper->read_prg(0x8000U) == cell(63UZ * kPrgBankSize));
    REQUIRE(mapper->read_prg(0xC000U) == cell(63UZ * kPrgBankSize));

    auto fitted = board(0x18000UZ);
    fitted->write_expansion(0x4100U, 0x7EU);
    fitted->write_expansion(0x4102U, kNrom128Mode);
    for (const auto addr : addresses) {
        const auto physical = expected_prg(addr, 0x7EU, kNrom128Mode) % 0x18000UZ;
        CAPTURE(addr, physical);
        REQUIRE(fitted->read_prg(addr) == cell(physical));
    }
}

TEST_CASE("Mapper 403 CHR RAM has four writable banks and reset retention",
          "[mapper403][ram][reset]") {
    auto mapper = board(kPrgSize, MirrorMode::Horizontal);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_expansion(0x4101U, static_cast<Byte>(bank));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            mapper->write_chr(offset, cell(bank * kChrBankSize + offset, 0x35U));
        }
    }
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_expansion(0x4101U, static_cast<Byte>(bank));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            const auto expected = cell(bank * kChrBankSize + offset, 0x35U);
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_chr(offset) == expected);
            REQUIRE(mapper->read_chr(static_cast<Address>(offset + 0x2000U)) == expected);
            REQUIRE(mapper->read_chr(static_cast<Address>(offset + 0x4000U)) == expected);
            REQUIRE(mapper->read_chr(static_cast<Address>(offset + 0x6000U)) == expected);
        }
    }

    mapper->write_expansion(0x4102U, kHighChrLatchEnable);
    mapper->write_prg(0xFFFFU, 3U);
    REQUIRE(mapper->read_chr(0x123U) == cell(3UZ * kChrBankSize + 0x123U, 0x35U));
    mapper->write_expansion(0x4100U, 0xFCU);
    mapper->write_expansion(0x4102U, 0x15U);
    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(register_state(*mapper)[kPrgBankOffset] == 0U);
    REQUIRE(register_state(*mapper)[kChrBankOffset] == 0U);
    REQUIRE(register_state(*mapper)[kModeOffset] == 0U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_expansion(0x4101U, static_cast<Byte>(bank));
        REQUIRE(mapper->read_chr(0x123U) == cell(bank * kChrBankSize + 0x123U, 0x35U));
    }

    mapper->power_on();
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_expansion(0x4101U, static_cast<Byte>(bank));
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_chr(offset) == 0U);
        }
    }
}

TEST_CASE("Mapper 403 uses H for horizontal vertical console CIRAM only", "[mapper403][ciram]") {
    auto mapper = board(kPrgSize, MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_expansion(0x4102U, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(
            mapper->mirror_mode() ==
            ((value & kHorizontalMirroring) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        CAPTURE(addr);
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 403 exposes no CPU RAM IRQ or expansion audio",
          "[mapper403][bus][ram][irq][audio]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (const Address addr : {0x4020U, 0x4FFFU, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_expansion(addr, 0xA5U);
        mapper->write_prg(addr, 0xA5U);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());

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
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2000U, 0xA5U));
}

TEST_CASE("Mapper 403 bounded snapshots replay registers and CHR RAM without ROM",
          "[mapper403][state]") {
    auto mapper = board();
    mapper->write_expansion(0x4100U, 0xFCU);
    mapper->write_expansion(0x4102U, 0x15U);
    mapper->write_prg(0xFFFFU, 3U);
    for (Address addr = 0U; addr < kChrBankSize; addr += 0x101U) {
        mapper->write_chr(addr, cell(addr, 0x5AU));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kPrgSize);
    REQUIRE(saved[kPrgBankOffset] == 0x7CU);
    REQUIRE(saved[kChrBankOffset] == 3U);
    REQUIRE(saved[kModeOffset] == 0x15U);

    auto copy = board(kPrgSize, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const Address addr : {0x8000U, 0x9234U, 0xC000U, 0xD234U, 0xFFFFU}) {
        const auto physical = expected_prg(addr, 0xFCU, 0x15U);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    for (Address addr = 0U; addr < kChrBankSize; addr += 0x101U) {
        CAPTURE(addr);
        REQUIRE(copy->read_chr(addr) == cell(addr, 0x5AU));
    }
    mapper->write_expansion(0x4101U, 1U);
    copy->write_expansion(0x4101U, 1U);
    mapper->write_chr(0x321U, 0xA7U);
    copy->write_chr(0x321U, 0xA7U);
    REQUIRE(state(*copy) == state(*mapper));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 403 rejects malformed and incompatible states atomically", "[mapper403][state]") {
    auto mapper = board();
    mapper->write_expansion(0x4100U, 0xFCU);
    mapper->write_expansion(0x4102U, 0x15U);
    mapper->write_prg(0x8000U, 3U);
    mapper->write_chr(0x0123U, 0xA5U);
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
    for (std::size_t offset = 0U; offset < kPrgBankOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const auto& [offset, value] :
         std::array<std::pair<std::size_t, Byte>, 9>{std::pair{kPrgBankOffset, Byte{0x80U}},
                                                     std::pair{kPrgBankOffset, Byte{0x01U}},
                                                     std::pair{kPrgBankOffset, Byte{0xFFU}},
                                                     std::pair{kChrBankOffset, Byte{4U}},
                                                     std::pair{kChrBankOffset, Byte{0xFFU}},
                                                     std::pair{kModeOffset, Byte{0x02U}},
                                                     std::pair{kModeOffset, Byte{0x08U}},
                                                     std::pair{kModeOffset, Byte{0x20U}},
                                                     std::pair{kModeOffset, Byte{0xFFU}}}) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    auto wrong_prg = std::make_unique<Mapper403>(header(), rom(kPrgSize / 2U), std::vector<Byte>{});
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);
}

} // namespace
