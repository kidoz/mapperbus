#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper389.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x080000UZ;
constexpr std::size_t kChrSize = 0x040000UZ;
constexpr std::size_t kNromBankSize = 0x008000UZ;
constexpr std::size_t kUnromBankSize = 0x004000UZ;
constexpr std::size_t kUnromOuterSize = 0x010000UZ;
constexpr std::size_t kChrBankSize = 0x002000UZ;
constexpr std::size_t kOuterPrgOffset = 10UZ;
constexpr std::size_t kOuterChrOffset = 11UZ;
constexpr std::size_t kInnerBankOffset = 12UZ;
constexpr std::size_t kStateSize = 13UZ;
constexpr std::array<Address, 6> kPrgAddresses{
    0x8000U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xDABCU, 0xFFFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 389U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 15U) * 43U + (offset >> 16U) * 59U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper389> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper389::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper389>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper389& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t nrom_offset(Address addr, Byte outer) {
    return static_cast<std::size_t>((outer >> 3U) & 0x0FU) * kNromBankSize + (addr & 0x7FFFU);
}

std::size_t unrom_offset(Address addr, Byte outer, Byte inner) {
    const Byte bank = addr < 0xC000U ? static_cast<Byte>((inner >> 2U) & 0x03U) : 3U;
    return static_cast<std::size_t>((outer >> 4U) & 0x07U) * kUnromOuterSize +
           static_cast<std::size_t>(bank) * kUnromBankSize + (addr & 0x3FFFU);
}

std::size_t chr_offset(Address addr, Byte outer, Byte inner) {
    return static_cast<std::size_t>((outer >> 2U) & 0x07U) * 4UZ * kChrBankSize +
           static_cast<std::size_t>(inner & 0x03U) * kChrBankSize + (addr & 0x1FFFU);
}

TEST_CASE("Mapper 389 validates decoded ROM bounds and ROM-only metadata", "[mapper389]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg = kNromBankSize; prg <= kPrgSize; prg += kNromBankSize) {
            for (std::size_t chr = kChrBankSize; chr <= kChrSize; chr += kChrBankSize) {
                CAPTURE(mirror, prg, chr);
                REQUIRE(Mapper389::validate_geometry(header(mirror), prg, chr));
            }
        }
        auto mapper = board(kPrgSize, kChrSize, mirror);
        REQUIRE(state(*mapper).size() == kStateSize);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
    }

    for (const unsigned id : {0U, 41U, 81U, 388U, 390U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper389::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper389::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper389::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x9000UZ, 0x80001UZ, 0x100000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper389::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x3000UZ, 0x40001UZ, 0x80000UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper389::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper389::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
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
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper389::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 389 latches address pins at every documented CPU write alias",
          "[mapper389][bus][banking][ciram]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw * 41U + (raw >> 8U)));
        const Byte outer_prg = raw < 0x9000U ? static_cast<Byte>(raw & 0x79U) : 0U;
        const Byte outer_chr =
            raw >= 0x9000U && raw < 0xA000U ? static_cast<Byte>(raw & 0x3EU) : 0U;
        const Byte inner = raw >= 0xA000U ? static_cast<Byte>(raw & 0x0FU) : 0U;
        const auto current = state(*mapper);
        CAPTURE(raw, outer_prg, outer_chr, inner);
        REQUIRE(current[kOuterPrgOffset] == outer_prg);
        REQUIRE(current[kOuterChrOffset] == outer_chr);
        REQUIRE(current[kInnerBankOffset] == inner);
        REQUIRE(mapper->mirror_mode() ==
                ((outer_prg & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (const auto addr : kPrgAddresses) {
            const auto physical = (outer_chr & 2U) == 0U ? nrom_offset(addr, outer_prg)
                                                         : unrom_offset(addr, outer_prg, inner);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
        REQUIRE(mapper->read_chr(0x0123U) == cell(chr_offset(0x0123U, outer_chr, inner)));
    }

    mapper->reset();
    mapper->write_prg(0x8079U, 0U);
    mapper->write_prg(0x903EU, 0U);
    mapper->write_prg(0xA00FU, 0U);
    const auto programmed = state(*mapper);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8079U, static_cast<Byte>(value));
        mapper->write_prg(0x903EU, static_cast<Byte>(value));
        mapper->write_prg(0xA00FU, static_cast<Byte>(value));
        CAPTURE(value);
        REQUIRE(state(*mapper) == programmed);
    }
}

TEST_CASE("Mapper 389 NROM-256 ignores inner PRG bits and wraps fitted images",
          "[mapper389][banking]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0x9000U, 0U);
        mapper->write_prg(static_cast<Address>(0x8000U | outer), 0xA5U);
        mapper->write_prg(0xA00CU, 0x5AU);
        for (const auto addr : kPrgAddresses) {
            const auto physical = nrom_offset(addr, static_cast<Byte>(outer & 0x79U));
            CAPTURE(outer, addr, physical);
            REQUIRE(mapper->read_prg(addr) == cell(physical));
        }
    }
    mapper->write_prg(0x8078U, 0U);
    mapper->write_prg(0xA000U, 0U);
    for (Address offset = 0U; offset < kNromBankSize; ++offset) {
        const auto physical = 15UZ * kNromBankSize + offset;
        CAPTURE(offset, physical);
        REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) == cell(physical));
    }

    for (const std::size_t size : {kNromBankSize, 3UZ * kNromBankSize}) {
        auto fitted = board(size, kChrSize);
        for (unsigned outer = 0U; outer < 256U; ++outer) {
            fitted->write_prg(static_cast<Address>(0x8000U | outer), 0U);
            for (const auto addr : kPrgAddresses) {
                const auto physical = nrom_offset(addr, static_cast<Byte>(outer & 0x79U));
                CAPTURE(size, outer, addr, physical);
                REQUIRE(fitted->read_prg(addr) == cell(physical % size));
            }
        }
    }
}

TEST_CASE("Mapper 389 UNROM-064 selects four inner pages and fixes upper page three",
          "[mapper389][banking]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0x9002U, 0U);
        mapper->write_prg(static_cast<Address>(0x8000U | outer), 0U);
        for (unsigned inner = 0U; inner < 256U; ++inner) {
            mapper->write_prg(static_cast<Address>(0xA000U | inner), 0U);
            for (const auto addr : kPrgAddresses) {
                const auto physical = unrom_offset(
                    addr, static_cast<Byte>(outer & 0x79U), static_cast<Byte>(inner & 0x0FU));
                CAPTURE(outer, inner, addr, physical);
                REQUIRE(mapper->read_prg(addr) == cell(physical));
            }
        }
    }

    for (const std::size_t size : {kNromBankSize, 3UZ * kNromBankSize}) {
        auto fitted = board(size, kChrSize);
        fitted->write_prg(0x9002U, 0U);
        for (unsigned outer = 0U; outer < 256U; outer += 17U) {
            fitted->write_prg(static_cast<Address>(0x8000U | outer), 0U);
            for (unsigned inner = 0U; inner < 16U; ++inner) {
                fitted->write_prg(static_cast<Address>(0xA000U | inner), 0U);
                for (const auto addr : kPrgAddresses) {
                    const auto physical = unrom_offset(
                        addr, static_cast<Byte>(outer & 0x79U), static_cast<Byte>(inner));
                    CAPTURE(size, outer, inner, addr, physical);
                    REQUIRE(fitted->read_prg(addr) == cell(physical % size));
                }
            }
        }
    }
}

TEST_CASE("Mapper 389 combines outer and inner CHR pins and protects ROM",
          "[mapper389][banking][ram]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(static_cast<Address>(0x9000U | outer), 0U);
        for (unsigned inner = 0U; inner < 256U; ++inner) {
            mapper->write_prg(static_cast<Address>(0xA000U | inner), 0U);
            const auto physical = chr_offset(
                0x0123U, static_cast<Byte>(outer & 0x3EU), static_cast<Byte>(inner & 0x0FU));
            CAPTURE(outer, inner, physical);
            REQUIRE(mapper->read_chr(0x0123U) == cell(physical));
        }
    }

    mapper->write_prg(0x903CU, 0U);
    mapper->write_prg(0xA003U, 0U);
    const auto before = state(*mapper);
    for (Address offset = 0U; offset < kChrBankSize; ++offset) {
        const auto physical = 31UZ * kChrBankSize + offset;
        CAPTURE(offset, physical);
        REQUIRE(mapper->read_chr(offset) == cell(physical));
        mapper->write_chr(offset, static_cast<Byte>(offset ^ 0xFFU));
        REQUIRE(mapper->read_chr(offset) == cell(physical));
    }
    REQUIRE(state(*mapper) == before);

    for (const std::size_t size : {kChrBankSize, 3UZ * kChrBankSize}) {
        auto fitted = board(kPrgSize, size);
        for (unsigned outer = 0U; outer < 64U; ++outer) {
            fitted->write_prg(static_cast<Address>(0x9000U | outer), 0U);
            for (unsigned inner = 0U; inner < 4U; ++inner) {
                fitted->write_prg(static_cast<Address>(0xA000U | inner), 0U);
                const auto physical =
                    chr_offset(0x1FFFU, static_cast<Byte>(outer & 0x3EU), static_cast<Byte>(inner));
                CAPTURE(size, outer, inner, physical);
                REQUIRE(fitted->read_chr(0x1FFFU) == cell(physical % size));
            }
        }
    }
}

TEST_CASE("Mapper 389 uses only horizontal or vertical console CIRAM", "[mapper389][ciram]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal);
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(static_cast<Address>(0x8000U | value), 0U);
        CAPTURE(value);
        REQUIRE(mapper->mirror_mode() ==
                ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    for (Address addr = 0x2000U; addr <= 0x3EFFU; addr += 0x100U) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 389 leaves RAM expansion IRQ and audio hardware absent",
          "[mapper389][bus][ram][irq][audio]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        CAPTURE(addr);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_expansion(addr) == 0U);
        mapper->write_prg(addr, 0xA5U);
        mapper->write_expansion(addr, 0xA5U);
    }
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
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
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE(state(*mapper) == initial);
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

TEST_CASE("Mapper 389 reset restores all zero power-up latches", "[mapper389][reset]") {
    auto mapper = board(kPrgSize, kChrSize, MirrorMode::Horizontal, 0x35U);
    const auto initial = state(*mapper);
    mapper->write_prg(0x8079U, 0U);
    mapper->write_prg(0x903EU, 0U);
    mapper->write_prg(0xA00FU, 0U);
    REQUIRE(state(*mapper) != initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (const auto addr : kPrgAddresses) {
        REQUIRE(mapper->read_prg(addr) == cell(nrom_offset(addr, 0U), 0x35U));
    }
    REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U, 0x35U));

    mapper->write_prg(0x8079U, 0U);
    mapper->write_prg(0x903EU, 0U);
    mapper->write_prg(0xA00FU, 0U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 389 bounded snapshots replay without ROM and reject damage atomically",
          "[mapper389][state]") {
    auto mapper = board();
    mapper->write_prg(0x8069U, 0U);
    mapper->write_prg(0x903AU, 0U);
    mapper->write_prg(0xA00DU, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[kOuterPrgOffset] == 0x69U);
    REQUIRE(saved[kOuterChrOffset] == 0x3AU);
    REQUIRE(saved[kInnerBankOffset] == 0x0DU);

    auto copy = board(kPrgSize, kChrSize, MirrorMode::Horizontal, 0x80U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        const auto physical = unrom_offset(addr, 0x69U, 0x0DU);
        CAPTURE(addr, physical);
        REQUIRE(copy->read_prg(addr) == cell(physical, 0x80U));
    }
    REQUIRE(copy->read_chr(0x1FFFU) == cell(chr_offset(0x1FFFU, 0x3AU, 0x0DU), 0x80U));

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);

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
    for (unsigned bit = 0U; bit < kOuterPrgOffset * 8U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    const std::array<std::pair<std::size_t, Byte>, 9> invalid_fields{{{kOuterPrgOffset, 0x80U},
                                                                      {kOuterPrgOffset, 0x06U},
                                                                      {kOuterPrgOffset, 0xFFU},
                                                                      {kOuterChrOffset, 0x01U},
                                                                      {kOuterChrOffset, 0x40U},
                                                                      {kOuterChrOffset, 0xFFU},
                                                                      {kInnerBankOffset, 0x10U},
                                                                      {kInnerBankOffset, 0x80U},
                                                                      {kInnerBankOffset, 0xFFU}}};
    for (const auto [offset, value] : invalid_fields) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    }

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
}

} // namespace
