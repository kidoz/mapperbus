#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/mappers/mapper414.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x004000UZ;
constexpr std::size_t kMaxPrgSize = 0x040000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x010000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kGeometrySize = 14UZ;
constexpr std::size_t kSolderOffset = 14UZ;
constexpr std::size_t kControlOffset = 16UZ;
constexpr std::size_t kChrBankOffset = 18UZ;
constexpr std::size_t kStateSize = 19UZ;
constexpr Address kControlMask = 0x20FFU;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x9234U, 0xA000U, 0xB234U, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 414U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 47U +
                             (offset >> 16U) * 59U + (offset >> 18U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper414> board(std::size_t prg_size = 0x20000UZ,
                                 std::size_t chr_size = 0x10000UZ,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper414::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper414>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x35U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper414& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void configure(Mapper414& mapper, std::uint16_t pads) {
    REQUIRE(mapper.configure_board_profile("solder-pads-" + std::to_string(pads)));
}

std::size_t expected_prg(Address addr, Address control) {
    unsigned bank = ((control & 6U) >> 1U) << 1U;
    if ((control & 0x2000U) != 0U) {
        bank |= (addr >> 14U) & 1U;
    }
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

void write_u16(std::span<Byte> bytes, std::size_t offset, std::uint16_t value) {
    REQUIRE(offset + sizeof(value) <= bytes.size());
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

TEST_CASE("Mapper 414 validates decoder geometry and deterministic reset",
          "[mapper414][geometry][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize, kMinChrSize, mirror);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
        REQUIRE(mapper->read_chr(0U) == cell(0U, 0x35U));
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        auto other_rom = board(kMinPrgSize, kMinChrSize, mirror, 0x80U);
        REQUIRE(state(*other_rom) == state(*mapper));
    }
    for (const std::size_t prg : {kMinPrgSize, 0xC000UZ, 0x18000UZ, kMaxPrgSize}) {
        for (const std::size_t chr : {kMinChrSize, 0x6000UZ, 0xA000UZ, kMaxChrSize}) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper414::validate_geometry(header(), prg, chr));
        }
    }

    for (const unsigned id : {0U, 4U, 256U, 412U, 413U, 415U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper414::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper414::validate_geometry(legacy, kMinPrgSize, kMinChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        CAPTURE(submapper);
        REQUIRE_FALSE(Mapper414::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x3FFFUZ, 0x4001UZ, kMaxPrgSize + kPrgBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper414::validate_geometry(header(), size, kMinChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, kMaxChrSize + kChrBankSize}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper414::validate_geometry(header(), kMinPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper414::validate_geometry(header(mirror), kMinPrgSize, kMinChrSize));
    }
    for (unsigned field = 0U; field < 6U; ++field) {
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
        REQUIRE_FALSE(Mapper414::validate_geometry(h, kMinPrgSize, kMinChrSize));
    }
}

TEST_CASE("Mapper 414 decodes only the documented address pins", "[mapper414][decoding][bus]") {
    auto actual = board();
    const auto initial = state(*actual);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        actual->reset();
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
        actual->write_prg(static_cast<Address>(raw), value);
        auto wanted = initial;
        if (raw >= 0x8000U) {
            const auto addr = static_cast<Address>(raw);
            write_u16(wanted, kControlOffset, addr & kControlMask);
            wanted[kChrBankOffset] = value & cell(expected_prg(addr, 0U)) & 3U;
        }
        CAPTURE(raw);
        REQUIRE(state(*actual) == wanted);
    }

    actual->reset();
    actual->write_prg(0xA123U, 0xA5U);
    const auto before = state(*actual);
    for (const Address addr : {0x0000U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        CAPTURE(addr);
        REQUIRE_FALSE(actual->maps_prg(addr));
        REQUIRE(actual->prg_read_mask(addr) == 0U);
        REQUIRE(actual->read_prg(addr) == 0U);
        actual->write_prg(addr, 0xFFU);
        REQUIRE_FALSE(actual->maps_expansion(addr));
        REQUIRE(actual->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(actual->read_expansion(addr) == 0U);
        actual->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*actual) == before);
    REQUIRE(actual->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(actual->write_ppu_register(0x2000U, 0xA5U));
    REQUIRE_FALSE(actual->read_ppu_register(0x2000U, 0xA5U));
}

TEST_CASE("Mapper 414 applies old-bank PRG bus conflicts to CHR data",
          "[mapper414][decoding][banking][bus]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize);
    constexpr Address kOldControl = 0x2086U;
    mapper->write_prg(static_cast<Address>(0x8000U | kOldControl), 0U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(static_cast<Address>(0x8000U | kOldControl), 0U);
        constexpr Address kAddress = 0xD234U;
        const auto old_rom = cell(expected_prg(kAddress, kOldControl));
        mapper->write_prg(kAddress, static_cast<Byte>(value));
        const auto bytes = state(*mapper);
        CAPTURE(value, old_rom);
        REQUIRE(bytes[kChrBankOffset] == (value & old_rom & 3U));
    }

    mapper->reset();
    mapper->write_prg(0x8123U, 0xFFU);
    REQUIRE(state(*mapper)[kChrBankOffset] == (cell(0x123U) & 3U));
    mapper->write_prg(0x8123U, 0U);
    REQUIRE(state(*mapper)[kChrBankOffset] == 0U);
}

TEST_CASE("Mapper 414 implements NROM-128 and NROM-256 before fitted-ROM wrapping",
          "[mapper414][banking][bus]") {
    auto mapper = board(kMaxPrgSize, kMinChrSize);
    for (const Address nrom_256 : {Address{0U}, Address{0x2000U}}) {
        for (Address pp = 0U; pp < 4U; ++pp) {
            const auto control = static_cast<Address>(nrom_256 | (pp << 1U));
            mapper->write_prg(static_cast<Address>(0x8000U | control), 0U);
            for (const auto addr : kPrgAddresses) {
                const auto physical = expected_prg(addr, control);
                CAPTURE(nrom_256, pp, addr, physical);
                REQUIRE(mapper->maps_prg(addr));
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }

    for (const std::size_t size : {kMinPrgSize, 0xC000UZ, 0x18000UZ}) {
        auto fitted = board(size, kMinChrSize);
        constexpr Address kControl = 0x2006U;
        fitted->write_prg(static_cast<Address>(0x8000U | kControl), 0U);
        for (const auto addr : kPrgAddresses) {
            const auto physical = expected_prg(addr, kControl) % size;
            CAPTURE(size, addr, physical);
            REQUIRE(fitted->read_prg(addr) == cell(physical));
        }
    }
}

TEST_CASE("Mapper 414 solder-pad test opens only the upper PRG window",
          "[mapper414][decoding][banking][bus]") {
    auto mapper = board();
    for (unsigned selection = 0U; selection < 16U; ++selection) {
        configure(*mapper, static_cast<std::uint16_t>(1U << selection));
        const auto test_control = static_cast<Address>(selection << 3U);
        mapper->write_prg(static_cast<Address>(0x8000U | test_control), 0U);
        CAPTURE(selection);
        REQUIRE(mapper->maps_prg(0xC123U));
        REQUIRE(mapper->prg_read_mask(0xC123U) == 0U);
        REQUIRE(mapper->read_prg(0xC123U) == 0U);
        REQUIRE(mapper->prg_read_mask(0xFFFFU) == 0U);
        REQUIRE(mapper->prg_read_mask(0x8123U) == 0xFFU);
        REQUIRE(mapper->read_prg(0x8123U) == cell(0x123U));

        mapper->write_prg(0xC123U, 0xFFU);
        REQUIRE(state(*mapper)[kChrBankOffset] == 3U);
        mapper->write_prg(static_cast<Address>(0x8000U | test_control | 0x80U), 0U);
        REQUIRE(mapper->prg_read_mask(0xC123U) == 0xFFU);
        REQUIRE(mapper->read_prg(0xC123U) == cell(0x123U));
    }

    configure(*mapper, 0U);
    mapper->write_prg(0x8078U, 0U);
    REQUIRE(mapper->prg_read_mask(0xC123U) == 0xFFU);
    configure(*mapper, 0x8000U);
    REQUIRE(mapper->prg_read_mask(0xC123U) == 0U);
    configure(*mapper, 0xFFFFU);
    REQUIRE(mapper->prg_read_mask(0xC123U) == 0U);

    const auto before = state(*mapper);
    for (const std::string_view profile : {"",
                                           "default",
                                           "solder-pads-",
                                           "solder-pads--1",
                                           "solder-pads-65536",
                                           "solder-pads-0x10",
                                           "solder-pads-1x",
                                           "solder-pads-1-1"}) {
        CAPTURE(profile);
        REQUIRE_FALSE(mapper->configure_board_profile(profile));
        REQUIRE(state(*mapper) == before);
    }
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 414 CHR uses two conflict-filtered bank bits and stays read-only",
          "[mapper414][banking][chr]") {
    auto mapper = board(kMinPrgSize, kMaxChrSize);
    const auto conflict = cell(0x123U);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        mapper->write_prg(0x8123U, static_cast<Byte>(value));
        const auto bank = static_cast<Byte>(value & conflict & 3U);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
            const auto physical = static_cast<std::size_t>(bank) * kChrBankSize + 0x123U;
            CAPTURE(value, bank, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
            mapper->write_chr(addr, static_cast<Byte>(physical ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
        }
    }

    for (const std::size_t size : {kMinChrSize, 0x6000UZ}) {
        auto fitted = board(kMinPrgSize, size);
        for (unsigned value = 0U; value < 256U; ++value) {
            fitted->write_prg(0x8123U, static_cast<Byte>(value | 3U));
            const auto physical = (3UZ * kChrBankSize + 0x123U) % size;
            CAPTURE(size, physical);
            REQUIRE(fitted->read_chr(0x123U) == cell(physical, 0x35U));
        }
    }
}

TEST_CASE("Mapper 414 uses M for horizontal vertical console CIRAM only", "[mapper414][ciram]") {
    auto mapper = board(kMinPrgSize, kMinChrSize, MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(static_cast<Address>(0x8000U | (value & 1U)), static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
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

TEST_CASE("Mapper 414 has no RAM IRQ audio or pin-observer side effects",
          "[mapper414][ram][irq][audio]") {
    auto mapper = board();
    mapper->write_prg(0xA5A5U, 0xA5U);
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
}

TEST_CASE("Mapper 414 reset clears both latches and retains solder wiring",
          "[mapper414][reset][ciram]") {
    for (const bool cold : {false, true}) {
        auto mapper = board(0x20000UZ, 0x10000UZ, MirrorMode::Horizontal);
        configure(*mapper, 0xA55AU);
        mapper->write_prg(0xA5A5U, 0xFFU);
        const auto configured = state(*mapper);
        if (cold) {
            mapper->power_on();
        } else {
            mapper->reset();
        }
        CAPTURE(cold);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(state(*mapper)[kControlOffset] == 0U);
        REQUIRE(state(*mapper)[kControlOffset + 1U] == 0U);
        REQUIRE(state(*mapper)[kChrBankOffset] == 0U);
        REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
        REQUIRE(configured[kSolderOffset] == state(*mapper)[kSolderOffset]);
        REQUIRE(configured[kSolderOffset + 1U] == state(*mapper)[kSolderOffset + 1U]);
    }
}

TEST_CASE("Mapper 414 bounded snapshots replay all mutable state without ROM",
          "[mapper414][state][bus]") {
    auto mapper = board();
    configure(*mapper, 0xA55AU);
    mapper->write_prg(0xA5A5U, 0xFFU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kMinPrgSize + kMinChrSize);

    auto copy = board(0x20000UZ, 0x10000UZ, MirrorMode::Vertical, 0x80U);
    configure(*copy, 0xA55AU);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    const auto control = static_cast<Address>(0xA5A5U & kControlMask);
    for (const auto addr : kPrgAddresses) {
        const auto physical = expected_prg(addr, control);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
        REQUIRE(mapper->read_prg(addr) == cell(physical));
    }
    const auto chr_bank = saved[kChrBankOffset];
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        const auto addr = static_cast<Address>(slot * kChrBankSize + 0x123U);
        const auto physical = static_cast<std::size_t>(chr_bank) * kChrBankSize + 0x123U;
        CAPTURE(addr, physical);
        REQUIRE(copy->read_chr(addr) == cell(physical, 0xB5U));
        REQUIRE(mapper->read_chr(addr) == cell(physical, 0x35U));
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

TEST_CASE("Mapper 414 rejects malformed truncated and mismatched states atomically",
          "[mapper414][state]") {
    auto mapper = board();
    configure(*mapper, 0xA55AU);
    mapper->write_prg(0xA5A5U, 0xFFU);
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
    for (const std::uint16_t control : {0x0100U, 0x1000U, 0x4000U, 0x8000U, 0xFFFFU}) {
        auto invalid = saved;
        write_u16(invalid, kControlOffset, control);
        CAPTURE(control);
        reject(invalid);
    }
    for (const Byte bank : {4U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kChrBankOffset] = bank;
        CAPTURE(bank);
        reject(invalid);
    }

    auto wrong_prg = board(0x10000UZ);
    configure(*wrong_prg, 0xA55AU);
    const auto wrong_prg_before = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == wrong_prg_before);

    auto wrong_chr = board(0x20000UZ, 0x8000UZ);
    configure(*wrong_chr, 0xA55AU);
    const auto wrong_chr_before = state(*wrong_chr);
    StateReader wrong_chr_reader(saved);
    wrong_chr->load_state(wrong_chr_reader);
    REQUIRE_FALSE(wrong_chr_reader.ok());
    REQUIRE(state(*wrong_chr) == wrong_chr_before);

    auto wrong_pads = board();
    configure(*wrong_pads, 0x5AA5U);
    const auto wrong_pads_before = state(*wrong_pads);
    StateReader wrong_pads_reader(saved);
    wrong_pads->load_state(wrong_pads_reader);
    REQUIRE_FALSE(wrong_pads_reader.ok());
    REQUIRE(state(*wrong_pads) == wrong_pads_before);

    auto changed = saved;
    write_u16(changed, kControlOffset, 0x2087U);
    changed[kChrBankOffset] = 2U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
