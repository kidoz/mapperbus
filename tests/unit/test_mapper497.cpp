#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper497.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x8000UZ;
constexpr std::size_t kChrRamSize = 0x8000UZ;
constexpr std::size_t kChrBankSize = 0x1000UZ;
constexpr std::size_t kFlagsOffset = 10UZ;
constexpr std::size_t kChrRamOffset = 11UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};
constexpr std::array<Address, 8> kChrAddresses{
    0x0000U, 0x0001U, 0x0123U, 0x0FFFU, 0x1000U, 0x1234U, 0x1FFEU, 0x1FFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 497U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kPrgSize;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 12U) * 47U +
                             (offset >> 14U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size = kPrgSize, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper497> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper497::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper497>(h, rom(kPrgSize, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, kChrRamOffset> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    REQUIRE(bytes.size() == kStateSize);
    std::array<Byte, kChrRamOffset> result{};
    std::copy_n(bytes.begin(), kChrRamOffset, result.begin());
    return result;
}

void load(Mapper497& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

Byte selected_bank(Byte mode, Byte nametable_bits) {
    if (mode < 2U || (mode == 3U && nametable_bits == 3U)) {
        return 1U;
    }
    return nametable_bits + 2U;
}

void latch_nametable(Mapper497& mapper, Byte bits) {
    mapper.observe_ppu_address(0x0000U);
    mapper.observe_ppu_address(static_cast<Address>(0x2012U | ((bits & 3U) << 8U)));
}

void select_upper_bank(Mapper497& mapper, Byte bank) {
    if (bank == 1U) {
        mapper.write_prg(0x8000U, 0U);
        return;
    }
    REQUIRE(bank >= 2U);
    REQUIRE(bank <= 5U);
    mapper.write_prg(0x8000U, 2U);
    latch_nametable(mapper, bank - 2U);
}

void require_prg(Mapper497& mapper, Address addr, Byte salt = 0U) {
    CAPTURE(addr, salt);
    REQUIRE(mapper.read_prg(addr) == cell(addr - 0x8000U, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr_mapping(Mapper497& mapper, Byte mode, Byte bits, Byte salt = 0U) {
    const auto bank = selected_bank(mode, bits);
    CAPTURE(mode, bits, bank, salt);
    for (const auto addr : kChrAddresses) {
        const auto physical = addr < kChrBankSize ? static_cast<std::size_t>(addr)
                                                  : static_cast<std::size_t>(bank) * kChrBankSize +
                                                        (addr & (kChrBankSize - 1U));
        CAPTURE(addr, physical);
        REQUIRE(mapper.read_chr(addr) == cell(physical, salt));
    }
}

TEST_CASE("Mapper 497 validates exact NES 2.0 PRG and volatile CHR RAM geometry",
          "[mapper497][geometry][ram][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        const auto h = header(mirror);
        REQUIRE(Mapper497::validate_geometry(h, kPrgSize, 0U));
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE(register_state(*mapper)[kFlagsOffset] == 0U);
        require_prg(*mapper, 0x8000U, 0x35U);
        require_prg(*mapper, 0xFFFFU, 0x35U);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            REQUIRE(mapper->read_chr(addr) == 0U);
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
        REQUIRE_FALSE(Mapper497::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 495U, 496U, 498U, 4095U}) {
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
    for (const std::size_t size : {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x10000UZ, 0x100000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, kChrRamSize, 0x100000UZ}) {
        reject(header(), kPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kPrgSize, 0U);
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
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x2000U;
            break;
        case 6U:
            h.chr_ram_size = 0x10000U;
            break;
        default:
            h.chr_nvram_size = kChrRamSize;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, 0U);
    }
}

TEST_CASE("Mapper 497 decodes every PRG write without conflicts and floats low CPU reads",
          "[mapper497][decoding][bus]") {
    auto mapper = board();
    const auto cold = register_state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x8000U) {
            expected[kFlagsOffset] = value & 3U;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(register_state(*mapper) == expected);
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

    auto zero_rom =
        std::make_unique<Mapper497>(header(), std::vector<Byte>(kPrgSize, 0U), std::vector<Byte>{});
    zero_rom->write_prg_cpu(0xFFFFU, 0xFFU, true);
    REQUIRE(register_state(*zero_rom)[kFlagsOffset] == 3U);
}

TEST_CASE("Mapper 497 maps one unbanked 32 KiB PRG window", "[mapper497][banking][prg][bus]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    for (const Byte mode : {Byte{0U}, Byte{1U}, Byte{2U}, Byte{3U}, Byte{0xFFU}}) {
        mapper->write_prg(0x8000U, mode);
        for (Address addr = 0x8000U;; ++addr) {
            require_prg(*mapper, addr, 0x35U);
            if (addr == 0xFFFFU) {
                break;
            }
        }
    }
}

TEST_CASE("Mapper 497 fixed and automatic modes select all documented CHR RAM banks",
          "[mapper497][banking][chr][ram]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kChrBankSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x60U));
    }
    for (Byte mode = 0U; mode < 4U; ++mode) {
        for (Byte bits = 0U; bits < 4U; ++bits) {
            mapper->reset();
            mapper->write_prg(0xFFFFU, mode);
            if (mode >= 2U) {
                latch_nametable(*mapper, bits);
            }
            const auto bank = selected_bank(mode, bits);
            for (Address offset = 0U; offset < kChrBankSize; ++offset) {
                const auto physical = static_cast<std::size_t>(bank) * kChrBankSize + offset;
                mapper->write_chr(static_cast<Address>(kChrBankSize + offset),
                                  cell(physical, 0x60U));
            }
            require_chr_mapping(*mapper, mode, bits, 0x60U);
        }
    }

    for (Byte bank = 0U; bank <= 5U; ++bank) {
        if (bank >= 1U) {
            select_upper_bank(*mapper, bank);
        }
        for (Address offset = 0U; offset < kChrBankSize; ++offset) {
            const auto addr = bank == 0U ? offset : static_cast<Address>(kChrBankSize + offset);
            const auto physical = static_cast<std::size_t>(bank) * kChrBankSize + offset;
            CAPTURE(bank, addr, physical);
            REQUIRE(mapper->read_chr(addr) == cell(physical, 0x60U));
        }
    }

    mapper->write_prg(0x8000U, 0U);
    latch_nametable(*mapper, 3U);
    require_chr_mapping(*mapper, 0U, 3U, 0x60U);
    mapper->write_prg(0x8000U, 1U);
    require_chr_mapping(*mapper, 1U, 3U, 0x60U);
    mapper->write_prg(0x8000U, 3U);
    require_chr_mapping(*mapper, 3U, 3U, 0x60U);
    mapper->write_prg(0x8000U, 2U);
    require_chr_mapping(*mapper, 2U, 3U, 0x60U);
}

TEST_CASE("Mapper 497 latches A9 A8 only on rising A13 nametable fetches",
          "[mapper497][decoding][banking][chr][bus]") {
    auto mapper = board();
    REQUIRE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());

    for (unsigned raw = 0U; raw <= 0x3FFFU; ++raw) {
        mapper->reset();
        mapper->write_prg(0x8000U, 2U);
        latch_nametable(*mapper, 1U);
        mapper->observe_ppu_address(0x1000U);
        auto expected = register_state(*mapper);
        const auto addr = static_cast<Address>(raw);
        const bool a13_high = (addr & 0x2000U) != 0U;
        const bool nametable_fetch = (addr & 0x3000U) == 0x2000U && (addr & 0x03C0U) != 0x03C0U;
        if (a13_high && nametable_fetch) {
            expected[kFlagsOffset] = static_cast<Byte>(2U | (((addr >> 8U) & 3U) << 2U) | 0x10U);
        } else if (a13_high) {
            expected[kFlagsOffset] |= 0x10U;
        }

        const auto before = register_state(*mapper);
        if (addr < 0x2000U) {
            (void)mapper->read_chr(addr);
            REQUIRE(register_state(*mapper) == before);
        }
        mapper->observe_ppu_address(addr);
        CAPTURE(raw, nametable_fetch, expected[kFlagsOffset]);
        REQUIRE(register_state(*mapper) == expected);
        mapper->observe_ppu_address(addr);
        REQUIRE(register_state(*mapper) == expected);
    }

    mapper->reset();
    mapper->write_prg(0x8000U, 2U);
    latch_nametable(*mapper, 2U);
    mapper->observe_ppu_address(0x0000U);
    for (Address addr = 0x23C0U; addr <= 0x23FFU; ++addr) {
        mapper->observe_ppu_address(addr);
    }
    REQUIRE(register_state(*mapper)[kFlagsOffset] == 0x1AU);
    mapper->observe_ppu_address(0x0000U);
    mapper->observe_ppu_address(0x2300U);
    REQUIRE(register_state(*mapper)[kFlagsOffset] == 0x1EU);
}

TEST_CASE("Mapper 497 CHR RAM ignores non-pattern accesses and retains ordinary CIRAM",
          "[mapper497][ram][ciram][bus]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        for (Byte mode = 0U; mode < 4U; ++mode) {
            mapper->write_prg(0x8000U, mode);
            latch_nametable(*mapper, mode);
            REQUIRE(mapper->mirror_mode() == mirror);
            for (Address addr = 0x2000U; addr <= 0x3EFFU; ++addr) {
                REQUIRE(mapper->maps_nametable(addr));
                REQUIRE_FALSE(mapper->nametable_address(addr));
                REQUIRE_FALSE(mapper->ciram_address(addr));
                REQUIRE_FALSE(mapper->read_nametable(addr));
                REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
            }
        }
        const auto before = state(*mapper);
        REQUIRE(mapper->read_chr(0x2000U) == 0U);
        REQUIRE(mapper->read_chr(0x3FFFU) == 0U);
        REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
        mapper->write_chr(0x2000U, 0xA5U);
        mapper->write_chr(0x3FFFU, 0x5AU);
        mapper->write_chr(0xFFFFU, 0xFFU);
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->unmirrored_nametables());
        REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
        REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    }
}

TEST_CASE("Mapper 497 soft reset clears pin latches and retains RAM while cold power clears it",
          "[mapper497][reset][ram][ciram]") {
    auto mapper = board(MirrorMode::Horizontal);
    const auto cold = state(*mapper);
    mapper->write_chr(0x0123U, 0xA5U);
    select_upper_bank(*mapper, 5U);
    mapper->write_chr(0x1123U, 0x5AU);
    latch_nametable(*mapper, 3U);
    mapper->write_prg(0xFFFFU, 3U);
    REQUIRE(state(*mapper) != cold);

    mapper->reset();
    REQUIRE(register_state(*mapper)[kFlagsOffset] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_chr(0x0123U) == 0xA5U);
    select_upper_bank(*mapper, 5U);
    REQUIRE(mapper->read_chr(0x1123U) == 0x5AU);

    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(std::ranges::all_of(std::span(cold).subspan(kChrRamOffset),
                                [](Byte value) { return value == 0U; }));
}

TEST_CASE("Mapper 497 has no CPU RAM IRQ audio or unrelated observer effects",
          "[mapper497][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 3U);
    mapper->write_chr(0x1456U, 0xA7U);
    mapper->observe_ppu_address(0x1000U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x1000U);
    mapper->observe_ppu_read(0x2012U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 497 bounded snapshots replay latches and CHR RAM without PRG ROM",
          "[mapper497][state][banking][ram]") {
    auto mapper = board(MirrorMode::Vertical, 0x35U);
    mapper->write_chr(0x0321U, 0xA6U);
    mapper->write_prg(0x8000U, 3U);
    latch_nametable(*mapper, 2U);
    mapper->write_chr(0x1432U, 0x6BU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kFlagsOffset] == 0x1BU);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x80U);
    }
    REQUIRE(copy->read_chr(0x0321U) == 0xA6U);
    REQUIRE(copy->read_chr(0x1432U) == 0x6BU);
    copy->observe_ppu_address(0x1000U);
    copy->observe_ppu_address(0x2300U);
    REQUIRE(register_state(*copy)[kFlagsOffset] == 0x1FU);

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

TEST_CASE("Mapper 497 rejects truncated damaged and mismatched states atomically",
          "[mapper497][state][ram][ciram]") {
    auto mapper = board();
    mapper->write_chr(0x0123U, 0xA5U);
    mapper->write_prg(0x8000U, 3U);
    latch_nametable(*mapper, 2U);
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
    for (std::size_t offset = 0U; offset < kFlagsOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte flags : {Byte{0x20U}, Byte{0x40U}, Byte{0x80U}, Byte{0xFFU}}) {
        auto invalid = saved;
        invalid[kFlagsOffset] = flags;
        CAPTURE(flags);
        reject(invalid);
    }

    auto wrong_prg =
        std::make_unique<Mapper497>(header(), rom(kPrgSize / 2U, 0U), std::vector<Byte>{});
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
    changed[kFlagsOffset] = 0x0AU;
    changed[kChrRamOffset + 0x123U] ^= 0xFFU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(register_state(*mapper)[kFlagsOffset] == 0x0AU);
}

} // namespace
