#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper463.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x200000UZ;
constexpr std::size_t kChrSize = 0x200000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kModeOffset = 10UZ;
constexpr std::size_t kPrgBaseOffset = 11UZ;
constexpr std::size_t kChrBaseOffset = 12UZ;
constexpr std::size_t kStateSize = 13UZ;
constexpr std::array<Address, 12> kPrgAddresses{0x8000U,
                                                0x8001U,
                                                0x8123U,
                                                0x9FFFU,
                                                0xA000U,
                                                0xBFFFU,
                                                0xC000U,
                                                0xC001U,
                                                0xD234U,
                                                0xE000U,
                                                0xFFFEU,
                                                0xFFFFU};
constexpr std::array<Address, 10> kChrAddresses{
    0x0000U, 0x0001U, 0x0123U, 0x03FFU, 0x0400U, 0x07FFU, 0x1000U, 0x1555U, 0x1FFEU, 0x1FFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 463U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_rom_size = kPrgSize;
    result.chr_rom_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 10U) * 13U +
                             (offset >> 12U) * 7U + (offset >> 13U) * 37U + (offset >> 14U) * 47U +
                             (offset >> 15U) * 43U + (offset >> 16U) * 59U + (offset >> 18U) * 71U +
                             (offset >> 20U) * 79U + (offset >> 21U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper463> board(MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U,
                                 std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize) {
    auto h = header(mirror);
    h.prg_rom_size = prg_size;
    h.chr_rom_size = chr_size;
    REQUIRE(Mapper463::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper463>(
        h, rom(prg_size, salt), rom(chr_size, static_cast<Byte>(salt ^ 0x55U)));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper463& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void outer(Mapper463& mapper, unsigned index, Byte value) {
    mapper.write_expansion(static_cast<Address>(0x5000U + index), value);
}

std::size_t expected_prg(Address addr, Byte mode, Byte prg_base) {
    if ((mode & 0x04U) != 0U) {
        return static_cast<std::size_t>(prg_base & ~1U) * kPrgBankSize + (addr & 0x7FFFU);
    }
    return static_cast<std::size_t>(prg_base) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
}

std::size_t expected_chr(Address addr, Byte chr_base) {
    return static_cast<std::size_t>(chr_base) * kChrBankSize + (addr & (kChrBankSize - 1U));
}

void require_prg(Mapper463& mapper,
                 Address addr,
                 Byte mode,
                 Byte prg_base,
                 Byte salt = 0U,
                 std::size_t prg_size = kPrgSize) {
    const auto physical = expected_prg(addr, mode, prg_base) % prg_size;
    CAPTURE(addr, mode, prg_base, physical, salt, prg_size);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(Mapper463& mapper,
                 Address addr,
                 Byte chr_base,
                 Byte salt = 0U,
                 std::size_t chr_size = kChrSize) {
    const auto physical = expected_chr(addr, chr_base) % chr_size;
    CAPTURE(addr, chr_base, physical, salt, chr_size);
    REQUIRE(mapper.read_chr(addr) == cell(physical, static_cast<Byte>(salt ^ 0x55U)));
}

TEST_CASE("Mapper 463 validates NES 2.0 ROM-only geometry and fixed CIRAM",
          "[mapper463][geometry][ciram][ram]") {
    for (std::size_t prg = 0x008000UZ; prg <= kPrgSize; prg *= 2U) {
        for (std::size_t chr = 0x002000UZ; chr <= kChrSize; chr *= 2U) {
            CAPTURE(prg, chr);
            REQUIRE(Mapper463::validate_geometry(header(), prg, chr));
        }
    }
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror, 0x35U);
        auto different_rom = board(mirror, 0x80U);
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8000U, 0U, 0U, 0x35U);
        require_prg(*mapper, 0xC000U, 0U, 0U, 0x35U);
        require_chr(*mapper, 0U, 0U, 0x35U);
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
        REQUIRE_FALSE(Mapper463::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 2U, 176U, 462U, 464U, 4095U}) {
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
         {0UZ, 1UZ, 0x1000UZ, 0x4000UZ, 0x6000UZ, 0x030000UZ, kPrgSize + 1U, kPrgSize * 2U}) {
        reject(header(), size, kChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x3000UZ, 0x018000UZ, kChrSize + 1U, kChrSize * 2U}) {
        reject(header(), kPrgSize, size);
    }
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
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kPrgSize, kChrSize);
    }
}

TEST_CASE("Mapper 463 decodes only three write-only outer registers across every CPU address",
          "[mapper463][decoding][bus]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->power_on();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x5000U && raw < 0x6000U) {
            switch (raw & 3U) {
            case 0U:
                expected[kModeOffset] = value & 0x07U;
                break;
            case 1U:
                expected[kPrgBaseOffset] = value & 0x7FU;
                break;
            case 2U:
                expected[kChrBaseOffset] = value;
                break;
            default:
                break;
            }
        }
        mapper->write_expansion(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);

        mapper->power_on();
        mapper->write_prg(addr, value);
        mapper->write_prg_cpu(addr, value, true);
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }

    for (const unsigned index : {0U, 1U, 2U, 3U}) {
        for (const Address addr :
             {Address{0x5000U}, Address{0x5004U}, Address{0x5010U}, Address{0x5FFCU}}) {
            mapper->power_on();
            mapper->write_expansion(static_cast<Address>(addr + index), 0xFFU);
            CAPTURE(index, addr);
            REQUIRE(state(*mapper)[kModeOffset] == (index == 0U ? 0x07U : 0U));
            REQUIRE(state(*mapper)[kPrgBaseOffset] == (index == 1U ? 0x7FU : 0U));
            REQUIRE(state(*mapper)[kChrBaseOffset] == (index == 2U ? 0xFFU : 0U));
        }
    }
}

TEST_CASE("Mapper 463 NROM-128 mirrors each selected 16 KiB bank in both CPU windows",
          "[mapper463][banking][prg]") {
    auto mapper = board();
    outer(*mapper, 0U, 3U);
    for (Byte base = 0U; base < 0x80U; ++base) {
        outer(*mapper, 1U, base);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, 3U, base);
        }
        REQUIRE(expected_prg(0x8123U, 3U, base) == expected_prg(0xC123U, 3U, base));
    }

    outer(*mapper, 1U, 0x7FU);
    for (Address addr = 0x8000U; addr != 0U; ++addr) {
        require_prg(*mapper, addr, 3U, 0x7FU);
    }
}

TEST_CASE("Mapper 463 NROM-256 aligns outer banks and maps consecutive 16 KiB pages",
          "[mapper463][banking][prg]") {
    auto mapper = board();
    outer(*mapper, 0U, 4U);
    for (Byte base = 0U; base < 0x80U; ++base) {
        outer(*mapper, 1U, base);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, 4U, base);
        }
    }

    outer(*mapper, 1U, 0x7FU);
    for (Address addr = 0x8000U; addr != 0U; ++addr) {
        require_prg(*mapper, addr, 4U, 0x7FU);
    }

    for (Byte mode = 0U; mode < 8U; ++mode) {
        outer(*mapper, 0U, mode);
        outer(*mapper, 1U, 0x35U);
        for (const auto addr : kPrgAddresses) {
            require_prg(*mapper, addr, mode, 0x35U);
        }
    }
}

TEST_CASE("Mapper 463 selects every 8 KiB CHR outer bank and keeps ROM immutable",
          "[mapper463][banking][chr][ram]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 0x100U; ++raw) {
        const auto base = static_cast<Byte>(raw);
        outer(*mapper, 2U, base);
        for (const auto addr : kChrAddresses) {
            require_chr(*mapper, addr, base);
            const auto before = mapper->read_chr(addr);
            mapper->write_chr(addr, static_cast<Byte>(~before));
            REQUIRE(mapper->read_chr(addr) == before);
        }
    }

    outer(*mapper, 2U, 0xFFU);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        require_chr(*mapper, addr, 0xFFU);
    }
}

TEST_CASE("Mapper 463 has fixed CIRAM and no RAM IRQ audio or observer side effects",
          "[mapper463][ciram][ram][irq][audio][bus]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        const auto before = state(*mapper);
        mapper->write_prg(0xA000U, 1U);
        mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
        mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE(mapper->mirror_mode() == mirror);
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
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        mapper->observe_cpu_read(0x8000U, 0xA5U);
        mapper->observe_cpu_write(0x8000U, 0xA5U);
        mapper->observe_ppu_a12(true);
        mapper->observe_ppu_address(0x1FFFU);
        mapper->observe_ppu_read(0x1FFFU);
        mapper->clock_cpu();
        mapper->clock_cpu_falling_edge();
        mapper->on_ppu_hblank();
        mapper->on_ppu_frame_start();
        mapper->clock_irq_counter();
        mapper->acknowledge_irq();
        mapper->clock_audio();
        mapper->set_region(Region::PAL);
        REQUIRE(state(*mapper) == before);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->observes_ppu_a12());
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE_FALSE(mapper->observes_ppu_reads());
        REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
        REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    }
}

TEST_CASE("Mapper 463 reset clears outer latches and preserves fixed header CIRAM",
          "[mapper463][reset][ciram]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        const auto cold = state(*mapper);
        outer(*mapper, 0U, 4U);
        outer(*mapper, 1U, 0x7FU);
        outer(*mapper, 2U, 0xFFU);
        require_prg(*mapper, 0xFFFFU, 4U, 0x7FU);
        require_chr(*mapper, 0x1FFFU, 0xFFU);

        mapper->reset();
        CAPTURE(mirror);
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
        require_prg(*mapper, 0xFFFFU, 0U, 0U);
        require_chr(*mapper, 0x1FFFU, 0U);

        outer(*mapper, 0U, 4U);
        outer(*mapper, 1U, 0x55U);
        outer(*mapper, 2U, 0xAAU);
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
    }
}

TEST_CASE("Mapper 463 bounded snapshots replay all latches without ROM bytes",
          "[mapper463][state][banking]") {
    auto mapper = board();
    outer(*mapper, 0U, 4U);
    outer(*mapper, 1U, 0x7FU);
    outer(*mapper, 2U, 0xFFU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kModeOffset] == 4U);
    REQUIRE(saved[kPrgBaseOffset] == 0x7FU);
    REQUIRE(saved[kChrBaseOffset] == 0xFFU);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 4U, 0x7FU, 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, 0xFFU, 0x80U);
    }

    outer(*copy, 0U, 3U);
    outer(*copy, 1U, 0x12U);
    outer(*copy, 2U, 0x34U);
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

TEST_CASE("Mapper 463 rejects truncated damaged and mismatched states atomically",
          "[mapper463][state]") {
    auto mapper = board();
    outer(*mapper, 0U, 4U);
    outer(*mapper, 1U, 0x7FU);
    outer(*mapper, 2U, 0xFFU);
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
    constexpr std::array<std::pair<std::size_t, Byte>, 7> invalid_fields = {
        {{0U, 1U},
         {1U, 1U},
         {2U, 1U},
         {6U, 1U},
         {kModeOffset, 8U},
         {kPrgBaseOffset, 0x80U},
         {kPrgBaseOffset, 0xFFU}}};
    for (const auto& [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

    const auto mismatch = [&](std::unique_ptr<Mapper463>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_mirror = board(MirrorMode::Horizontal);
    mismatch(wrong_mirror);
    auto wrong_prg = board(MirrorMode::Vertical, 0U, kPrgSize / 2U, kChrSize);
    mismatch(wrong_prg);
    auto wrong_chr = board(MirrorMode::Vertical, 0U, kPrgSize, kChrSize / 2U);
    mismatch(wrong_chr);

    auto changed = saved;
    changed[kModeOffset] = 3U;
    changed[kPrgBaseOffset] = 0x12U;
    changed[kChrBaseOffset] = 0x34U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    require_prg(*mapper, 0xFFFFU, 3U, 0x12U);
    require_chr(*mapper, 0x1FFFU, 0x34U);
}

} // namespace
