#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper354.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kMaxPrgSizeSubmapper0 = 0x200000UZ;
constexpr std::size_t kMaxPrgSizeSubmapper1 = 0x400000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kAddressOffset = 6UZ;
constexpr std::size_t kDataOffset = 8UZ;
constexpr std::size_t kChrRamOffset = 9UZ;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;
constexpr std::array<Address, 13> kPrgAddresses{0x6000U,
                                                0x6123U,
                                                0x7FFFU,
                                                0x8000U,
                                                0x8123U,
                                                0x9FFFU,
                                                0xA000U,
                                                0xBFFFU,
                                                0xC000U,
                                                0xC123U,
                                                0xDFFFU,
                                                0xE000U,
                                                0xFFFFU};

INesHeader header(Byte submapper = 0U, MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 354U;
    result.submapper = submapper;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 37U +
                             (offset >> 17U) * 43U + (offset >> 19U) * 13U + (offset >> 21U) * 7U +
                             salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper354> board(Byte submapper = 0U,
                                 std::size_t prg_size = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical,
                                 Byte salt = 0U) {
    if (prg_size == 0U) {
        prg_size = submapper == 0U ? kMaxPrgSizeSubmapper0 : kMaxPrgSizeSubmapper1;
    }
    const auto h = header(submapper, mirror);
    REQUIRE(Mapper354::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper354>(h, rom(prg_size, salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::size_t expected_prg_offset(Byte submapper,
                                Address address_latch,
                                Byte data_latch,
                                Address addr) {
    const auto mode = static_cast<unsigned>(address_latch & 7U);
    auto bank = static_cast<std::size_t>(data_latch & 0x3FU);
    std::size_t inner = 0U;
    if (addr < 0x8000U) {
        inner =
            bank * kPrgBankSize + ((data_latch & 0x80U) != 0U ? 0x2000UZ : 0UZ) + (addr & 0x1FFFU);
    } else {
        switch (mode) {
        case 0U:
        case 4U:
            bank = (bank & 0x3EU) | ((addr >> 14U) & 1U);
            inner = bank * kPrgBankSize + (addr & 0x3FFFU);
            break;
        case 1U:
            if (addr >= 0xC000U) {
                bank |= 7U;
            }
            inner = bank * kPrgBankSize + (addr & 0x3FFFU);
            break;
        case 2U:
        case 6U:
            inner = bank * kPrgBankSize + ((data_latch & 0x80U) != 0U ? 0x2000UZ : 0UZ) +
                    (addr & 0x1FFFU);
            break;
        case 3U:
        case 7U:
            inner = bank * kPrgBankSize + (addr & 0x3FFFU);
            break;
        case 5U:
            bank = (bank & 0x38U) | 0x06U | ((addr >> 14U) & 1U);
            inner = bank * kPrgBankSize + (addr & 0x3FFFU);
            break;
        default:
            break;
        }
    }
    const std::size_t outer =
        (submapper == 1U && (address_latch & 0x1000U) != 0U ? 0x200000UZ : 0UZ) +
        ((address_latch & 0x0010U) != 0U ? 0x100000UZ : 0UZ);
    return outer + inner;
}

TEST_CASE("Mapper 354 validates submapper ROM RAM and CIRAM geometry", "[mapper354]") {
    for (const Byte submapper : {0U, 1U}) {
        const auto max_size = submapper == 0U ? kMaxPrgSizeSubmapper0 : kMaxPrgSizeSubmapper1;
        for (std::size_t prg = kPrgBankSize; prg <= max_size; prg *= 2U) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(submapper, prg, mirror);
                REQUIRE(Mapper354::validate_geometry(header(submapper, mirror), prg, 0U));
            }
        }
        for (const auto id : {0U, 15U, 350U, 351U, 353U, 355U, 4095U}) {
            auto h = header(submapper);
            h.mapper_number = static_cast<std::uint16_t>(id);
            REQUIRE_FALSE(Mapper354::validate_geometry(h, max_size, 0U));
        }
        auto h = header(submapper);
        h.is_nes2 = false;
        REQUIRE_FALSE(Mapper354::validate_geometry(h, max_size, 0U));
        for (const std::size_t size :
             {0UZ, 1UZ, 0x2000UZ, 0x3FFFUZ, 0x4001UZ, 0x6000UZ, max_size * 2UZ}) {
            CAPTURE(submapper, size);
            REQUIRE_FALSE(Mapper354::validate_geometry(header(submapper), size, 0U));
        }
        for (const std::size_t size : {1UZ, 0x2000UZ, 0x10000UZ}) {
            REQUIRE_FALSE(Mapper354::validate_geometry(header(submapper), max_size, size));
        }
        for (const auto mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            REQUIRE_FALSE(Mapper354::validate_geometry(header(submapper, mirror), max_size, 0U));
        }
        for (unsigned field = 0U; field < 7U; ++field) {
            h = header(submapper);
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
            default:
                h.chr_nvram_size = 0x2000U;
                break;
            }
            CAPTURE(submapper, field);
            REQUIRE_FALSE(Mapper354::validate_geometry(h, max_size, 0U));
        }
    }
    REQUIRE_FALSE(Mapper354::validate_geometry(header(0U), 0x400000UZ, 0U));
    for (Byte submapper = 2U; submapper < 16U; ++submapper) {
        REQUIRE_FALSE(Mapper354::validate_geometry(header(submapper), 0x400000UZ, 0U));
    }
}

TEST_CASE("Mapper 354 decodes exactly the documented submapper write aliases", "[mapper354]") {
    for (const Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper, kPrgBankSize);
        for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
            mapper->reset();
            auto expected = state(*mapper);
            const auto addr = static_cast<Address>(raw);
            const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U));
            mapper->write_prg(addr, value);
            if (raw >= 0xF000U || (submapper == 1U && raw >= 0xE000U)) {
                expected[kAddressOffset] = static_cast<Byte>(raw);
                expected[kAddressOffset + 1U] = static_cast<Byte>(raw >> 8U);
                expected[kDataOffset] = value;
            }
            CAPTURE(submapper, raw, value);
            REQUIRE(state(*mapper) == expected);
        }
        mapper->reset();
        mapper->write_prg_cpu(submapper == 0U ? 0xF123U : 0xE123U, 0xA6U, false);
        REQUIRE(state(*mapper)[kDataOffset] == 0xA6U);
    }
}

TEST_CASE("Mapper 354 decodes every PRG mode bank and outer address pin", "[mapper354]") {
    for (const Byte submapper : {0U, 1U}) {
        const auto prg_size = submapper == 0U ? kMaxPrgSizeSubmapper0 : kMaxPrgSizeSubmapper1;
        auto mapper = board(submapper, prg_size);
        for (const Address outer : {0xE000U, 0xE010U, 0xF000U, 0xF010U}) {
            if (submapper == 0U && outer < 0xF000U) {
                continue;
            }
            for (Address mode = 0U; mode < 8U; ++mode) {
                const auto latch_address = static_cast<Address>(outer | mode);
                for (unsigned data = 0U; data < 256U; ++data) {
                    const auto latch_data = static_cast<Byte>(data);
                    mapper->write_prg(latch_address, latch_data);
                    for (const auto addr : kPrgAddresses) {
                        CAPTURE(submapper, latch_address, latch_data, addr);
                        if (addr < 0x8000U && mode != 5U) {
                            REQUIRE(mapper->read_prg(addr) == 0U);
                        } else {
                            const auto offset =
                                expected_prg_offset(submapper, latch_address, latch_data, addr) %
                                prg_size;
                            REQUIRE(mapper->read_prg(addr) == cell(offset));
                        }
                    }
                    REQUIRE(mapper->mirror_mode() == ((latch_data & 0x40U) != 0U
                                                          ? MirrorMode::Horizontal
                                                          : MirrorMode::Vertical));
                }
            }
        }
    }
}

TEST_CASE("Mapper 354 applies decoded PRG pins before wrapping smaller ROMs", "[mapper354]") {
    for (const std::size_t prg_size : {0x4000UZ, 0x8000UZ, 0x20000UZ, 0x80000UZ}) {
        auto mapper = board(0U, prg_size);
        for (const Address outer : {0xF000U, 0xF010U}) {
            for (Address mode = 0U; mode < 8U; ++mode) {
                const auto latch_address = static_cast<Address>(outer | mode);
                for (const Byte data : {0x00U, 0x01U, 0x3FU, 0x7FU, 0x80U, 0xFFU}) {
                    mapper->write_prg(latch_address, data);
                    for (const auto addr : kPrgAddresses) {
                        if (addr < 0x8000U && mode != 5U) {
                            continue;
                        }
                        const auto offset =
                            expected_prg_offset(0U, latch_address, data, addr) % prg_size;
                        CAPTURE(prg_size, latch_address, data, addr, offset);
                        REQUIRE(mapper->read_prg(addr) == cell(offset));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 354 CHR RAM is flat and protected only by address bit three", "[mapper354]") {
    for (const Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper, kPrgBankSize);
        mapper->write_prg(0xF000U, 0x40U);
        for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
            mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
        }
        for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
        }
        REQUIRE(mapper->read_chr(0x0123U) == mapper->read_chr(0x2123U));

        for (const Address protect : {0xF008U, 0xF00FU, 0xF018U}) {
            mapper->write_prg(protect, 0xFFU);
            for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
                mapper->write_chr(static_cast<Address>(addr), 0x5AU);
                CAPTURE(submapper, protect, addr);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
            }
        }
        mapper->write_prg(submapper == 0U ? 0xF010U : 0xE000U, 0U);
        mapper->write_chr(0x0321U, 0xA7U);
        REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);
    }
}

TEST_CASE("Mapper 354 exposes only FDS-mode low ROM and no other CPU RAM", "[mapper354][bus]") {
    auto mapper = board(0U, kMaxPrgSizeSubmapper0);
    for (Address mode = 0U; mode < 8U; ++mode) {
        mapper->write_prg(static_cast<Address>(0xF000U | mode), 0x35U);
        const auto before = state(*mapper);
        for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            const bool mapped = mode == 5U && addr >= 0x6000U;
            CAPTURE(mode, addr, mapped);
            REQUIRE(mapper->maps_prg(addr) == mapped);
            REQUIRE(mapper->prg_read_mask(addr) == (mapped ? 0xFFU : 0U));
            if (!mapped) {
                REQUIRE(mapper->read_prg(addr) == 0U);
            }
            mapper->write_prg(addr, 0xFFU);
            mapper->write_expansion(addr, 0xFFU);
            REQUIRE_FALSE(mapper->maps_expansion(addr));
            REQUIRE(mapper->read_expansion(addr) == 0U);
        }
        REQUIRE(state(*mapper) == before);
        for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(mapper->maps_nametable(0x2000U));
    REQUIRE(mapper->maps_nametable(0x3EFFU));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 354 has no IRQ or expansion audio state", "[mapper354][irq][audio]") {
    auto mapper = board(1U, kPrgBankSize);
    mapper->write_prg(0xE005U, 0xA6U);
    mapper->write_chr(0x0123U, 0xA7U);
    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->acknowledge_irq();
    mapper->clock_irq_counter();
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->on_ppu_hblank();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 354 reset clears both latches and cold power clears CHR RAM",
          "[mapper354][reset]") {
    for (const Byte submapper : {0U, 1U}) {
        auto mapper = board(submapper, kPrgBankSize, MirrorMode::Horizontal);
        auto fresh = board(submapper, kPrgBankSize, MirrorMode::Horizontal);
        mapper->write_prg(0xF000U, 0x40U);
        mapper->write_chr(0x0321U, 0xA7U);
        mapper->write_prg(submapper == 0U ? 0xF01DU : 0xE01DU, 0xFDU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        REQUIRE(state(*mapper) != state(*fresh));

        mapper->reset();
        REQUIRE(state(*mapper)[kAddressOffset] == 0U);
        REQUIRE(state(*mapper)[kAddressOffset + 1U] == 0U);
        REQUIRE(state(*mapper)[kDataOffset] == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_chr(0x0321U) == 0xA7U);
        for (const auto addr : kPrgAddresses) {
            const auto expected = addr < 0x8000U ? 0U : cell((addr & 0x7FFFU) % kPrgBankSize);
            CAPTURE(submapper, addr);
            REQUIRE(mapper->read_prg(addr) == expected);
        }
        mapper->write_chr(0x0321U, 0x55U);
        REQUIRE(mapper->read_chr(0x0321U) == 0x55U);

        mapper->power_on();
        REQUIRE(state(*mapper) == state(*fresh));
        REQUIRE(mapper->read_chr(0x0321U) == 0U);
    }
}

TEST_CASE("Mapper 354 snapshots replay latches and RAM without ROM", "[mapper354][state]") {
    auto mapper = board(1U);
    mapper->write_prg(0xF000U, 0U);
    for (unsigned addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        mapper->write_chr(static_cast<Address>(addr), cell(addr, 0xA5U));
    }
    mapper->write_prg(0xF015U, 0xFDU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == state(*board(0U)).size());
    REQUIRE(saved.size() < kPrgBankSize);

    auto copy = board(1U, 0U, MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (unsigned addr = 0U; addr < kChrRamSize; addr += 0x101U) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) == cell(addr, 0xA5U));
    }
    for (const auto addr : kPrgAddresses) {
        const auto offset = expected_prg_offset(1U, 0xF015U, 0xFDU, addr) % kMaxPrgSizeSubmapper1;
        CAPTURE(addr, offset);
        REQUIRE(copy->read_prg(addr) == cell(offset, 0x80U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    copy->power_on();
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);

    auto fresh = board(0U, kPrgBankSize);
    const auto reset_state = state(*fresh);
    StateReader reset_reader(reset_state);
    fresh->load_state(reset_reader);
    REQUIRE(reset_reader.ok());
    REQUIRE(state(*fresh) == reset_state);
}

TEST_CASE("Mapper 354 rejects malformed and incompatible states atomically", "[mapper354][state]") {
    auto mapper = board(1U);
    mapper->write_prg(0xF000U, 0U);
    mapper->write_chr(0x0234U, 0xA7U);
    mapper->write_prg(0xF015U, 0xFDU);
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
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
        CAPTURE(bit);
        reject(invalid);
    }
    const auto replace = [&](std::size_t offset, Byte value) {
        auto invalid = saved;
        invalid[offset] = value;
        CAPTURE(offset, value);
        reject(invalid);
    };
    replace(kAddressOffset + 1U, 0U);
    replace(kAddressOffset + 1U, 0xD0U);
    replace(4U, 0U);

    auto smaller = board(1U, kMaxPrgSizeSubmapper0);
    const auto smaller_before = state(*smaller);
    StateReader wrong_geometry(saved);
    smaller->load_state(wrong_geometry);
    REQUIRE_FALSE(wrong_geometry.ok());
    REQUIRE(state(*smaller) == smaller_before);

    auto other_submapper = board(0U);
    const auto other_before = state(*other_submapper);
    StateReader wrong_submapper(saved);
    other_submapper->load_state(wrong_submapper);
    REQUIRE_FALSE(wrong_submapper.ok());
    REQUIRE(state(*other_submapper) == other_before);
}

TEST_CASE("Mapper 354 state validation accepts every data latch value", "[mapper354][state]") {
    auto mapper = board(0U, kPrgBankSize);
    mapper->write_prg(0xF000U, 0U);
    const auto saved = state(*mapper);
    for (unsigned data = 0U; data < 256U; ++data) {
        auto valid = saved;
        valid[kDataOffset] = static_cast<Byte>(data);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == valid);
    }
}

} // namespace
