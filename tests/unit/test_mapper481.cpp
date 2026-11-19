#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper481.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x040000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrRamSize = 0x002000UZ;
constexpr std::size_t kBankOffset = 10UZ;
constexpr std::size_t kChrOffset = 11UZ;
constexpr std::size_t kStateSize = kChrOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 481U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kPrgSize;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 47U + (offset >> 14U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper481> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper481::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper481>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper481& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t prg_offset(Address addr, Byte bank) {
    const auto inner = addr < 0xC000U ? static_cast<unsigned>(bank & 7U) : 7U;
    const auto outer = static_cast<unsigned>((bank & 0x80U) >> 4U);
    return static_cast<std::size_t>(outer | inner) * kPrgBankSize + (addr & 0x3FFFU);
}

void require_prg(Mapper481& mapper, Address addr, Byte bank, Byte salt = 0U) {
    const auto physical = prg_offset(addr, bank);
    CAPTURE(addr, bank, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

TEST_CASE("Mapper 481 validates exact NES 2.0 geometry and volatile CHR RAM",
          "[mapper481][geometry][ram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        const auto h = header(mirror);
        CAPTURE(mirror);
        REQUIRE(Mapper481::validate_geometry(h, kPrgSize, 0U));
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8000U, 0U, 0x35U);
        require_prg(*mapper, 0xFFFFU, 0U, 0x35U);
        REQUIRE(mapper->read_chr(0U) == 0U);
        REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
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
        REQUIRE_FALSE(Mapper481::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 241U, 480U, 482U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x20000UZ, 0x30000UZ, 0x40001UZ, 0x80000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x2000UZ, 0x4000UZ, kPrgSize}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 7U; ++field) {
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
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, 0U);
    }
}

TEST_CASE("Mapper 481 decodes every high write and leaves all other CPU routes undriven",
          "[mapper481][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x8000U) {
            expected[kBankOffset] = value & 0x87U;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    mapper->write_prg_cpu(0xFFFFU, 0xFFU, true);
    REQUIRE(state(*mapper)[kBankOffset] == 0x87U);
}

TEST_CASE("Mapper 481 combines D7 outer banking with inner UNROM windows",
          "[mapper481][banking][prg][bus]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        const auto value = static_cast<Byte>(raw);
        mapper->write_prg(0xBFFFU, value);
        const auto bank = static_cast<Byte>(value & 0x87U);
        REQUIRE(state(*mapper)[kBankOffset] == bank);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, bank, 0x35U);
        }
    }

    for (const Byte bank : {Byte{0U}, Byte{7U}, Byte{0x80U}, Byte{0x87U}}) {
        mapper->write_prg(0x8000U, bank);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; raw += 0x101U) {
            require_prg(*mapper, static_cast<Address>(raw), bank, 0x35U);
        }
    }
}

TEST_CASE("Mapper 481 provides fixed unbanked CHR RAM and hard-wired CIRAM",
          "[mapper481][chr][ciram][ram][banking]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        for (Address addr = 0U; addr < kChrRamSize; ++addr) {
            const auto value = cell(addr, 0xA5U);
            mapper->write_chr(addr, value);
            CAPTURE(mirror, addr, value);
            REQUIRE(mapper->read_chr(addr) == value);
        }
        const auto populated = state(*mapper);
        for (const Byte bank : {Byte{1U}, Byte{0x87U}, Byte{0xFFU}}) {
            mapper->write_prg(0xFFFFU, bank);
            for (const Address addr :
                 {Address{0U}, Address{0x123U}, Address{0x1000U}, Address{0x1FFFU}}) {
                CAPTURE(mirror, bank, addr);
                REQUIRE(mapper->read_chr(addr) == populated[kChrOffset + addr]);
            }
            REQUIRE(mapper->mirror_mode() == mirror);
        }
        mapper->write_chr(0x2000U, 0xFFU);
        mapper->write_chr(0xFFFFU, 0xFFU);
        REQUIRE(mapper->read_chr(0x2000U) == 0U);
        REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
        for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x101U) {
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
}

TEST_CASE("Mapper 481 reset clears banking while soft reset retains and cold power clears CHR",
          "[mapper481][reset][ram][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        const auto cold = state(*mapper);
        mapper->write_prg(0xFFFFU, 0xFFU);
        mapper->write_chr(0x0123U, 0xA5U);
        mapper->write_chr(0x1FFFU, 0x5AU);
        auto expected = cold;
        expected[kChrOffset + 0x123U] = 0xA5U;
        expected[kChrOffset + 0x1FFFU] = 0x5AU;

        mapper->reset();
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == expected);
        REQUIRE(mapper->mirror_mode() == mirror);
        require_prg(*mapper, 0x8000U, 0U);
        require_prg(*mapper, 0xC000U, 0U);
        REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
        REQUIRE(mapper->read_chr(0x1FFFU) == 0x5AU);

        mapper->write_prg(0x8000U, 0x87U);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->read_chr(0x0123U) == 0U);
        REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
    }
}

TEST_CASE("Mapper 481 has no CPU RAM IRQ expansion audio or observer side effects",
          "[mapper481][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x87U);
    mapper->write_chr(0x123U, 0xA5U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1234U);
    mapper->observe_ppu_read(0x1234U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();
    mapper->write_expansion(0x6000U, 0x5AU);

    REQUIRE(state(*mapper) == before);
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
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 481 bounded snapshots replay latch and CHR RAM without ROM",
          "[mapper481][state][banking][ram]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    mapper->write_prg(0xFFFFU, 0xFFU);
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_chr(0x1FFFU, 0x5AU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kBankOffset] == 0x87U);
    REQUIRE(saved[kChrOffset + 0x123U] == 0xA5U);
    REQUIRE(saved[kChrOffset + 0x1FFFU] == 0x5AU);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_chr(0x0123U) == 0xA5U);
    REQUIRE(copy->read_chr(0x1FFFU) == 0x5AU);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x87U, 0x80U);
    }

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

TEST_CASE("Mapper 481 rejects truncated damaged and mismatched states atomically",
          "[mapper481][state][ram][ciram]") {
    auto mapper = board();
    mapper->write_prg(0xFFFFU, 0x87U);
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_chr(0x1FFFU, 0x5AU);
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
    for (std::size_t offset = 0U; offset < kBankOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte bank : {Byte{0x08U}, Byte{0x40U}, Byte{0x78U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kBankOffset] = bank;
        CAPTURE(bank);
        reject(invalid);
    }

    auto wrong_prg =
        std::make_unique<Mapper481>(header(), rom(kPrgSize / 2U, 0U), std::vector<Byte>{});
    const auto before_prg = state(*wrong_prg);
    StateReader wrong_prg_reader(saved);
    wrong_prg->load_state(wrong_prg_reader);
    REQUIRE_FALSE(wrong_prg_reader.ok());
    REQUIRE(state(*wrong_prg) == before_prg);

    auto wrong_mirror = board(MirrorMode::Horizontal);
    const auto before_mirror = state(*wrong_mirror);
    StateReader wrong_mirror_reader(saved);
    wrong_mirror->load_state(wrong_mirror_reader);
    REQUIRE_FALSE(wrong_mirror_reader.ok());
    REQUIRE(state(*wrong_mirror) == before_mirror);

    auto changed = saved;
    changed[kBankOffset] = 0x80U;
    changed[kChrOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(mapper->read_chr(0x0123U) == static_cast<Byte>(0xA5U ^ 0xFFU));
    for (const auto addr : kPrgAddresses) {
        require_prg(*mapper, addr, 0x80U);
    }
}

} // namespace
