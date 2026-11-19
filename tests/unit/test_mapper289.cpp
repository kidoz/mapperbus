#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper289.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankBytes = 16384U;
constexpr std::size_t kMaxPrgSize = 2097152U;
constexpr std::size_t kChrRamSize = 8192U;

INesHeader header(bool legacy = false, bool horizontal = false) {
    INesHeader result{};
    result.mapper_number = 289U;
    result.is_nes2 = !legacy;
    result.prg_ram_size = legacy ? 8192U : 0U;
    result.chr_ram_size = legacy ? 0U : kChrRamSize;
    result.mirror_mode = horizontal ? MirrorMode::Horizontal : MirrorMode::Vertical;
    return result;
}

Byte prg_byte(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + offset / kPrgBankBytes * 47U + offset / 251U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = prg_byte(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper289> board(std::size_t prg_size = kMaxPrgSize,
                                 bool legacy = false,
                                 bool horizontal = false) {
    REQUIRE(Mapper289::validate_geometry(header(legacy, horizontal), prg_size, 0U));
    return std::make_unique<Mapper289>(
        header(legacy, horizontal), rom(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(std::size_t prg_size, unsigned bank, Address addr) {
    const std::size_t offset = bank * kPrgBankBytes + (addr & 0x3FFFU);
    return prg_byte(offset % prg_size);
}

TEST_CASE("Mapper 289 cold state has only documented hardware and deterministic defaults",
          "[mapper289]") {
    for (bool legacy : {false, true}) {
        for (bool horizontal : {false, true}) {
            auto mapper = board(kMaxPrgSize, legacy, horizontal);
            REQUIRE(mapper->read_prg(0x8000U) == expected_prg(kMaxPrgSize, 0U, 0x8000U));
            REQUIRE(mapper->read_prg(0xC000U) == expected_prg(kMaxPrgSize, 0U, 0xC000U));
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
            REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
            REQUIRE(mapper->maps_prg(0x6000U));
            REQUIRE(mapper->maps_prg(0xFFFFU));
            REQUIRE(mapper->prg_read_mask(0x6000U) == 0x03U);
            REQUIRE(mapper->prg_read_mask(0x7FFFU) == 0x03U);
            REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
            REQUIRE_FALSE(mapper->maps_expansion(0x6000U));
            REQUIRE(state(*mapper).size() == 8201U);
            const auto cold = state(*mapper);
            mapper->reset();
            REQUIRE(state(*mapper) == cold);
        }
    }
}

TEST_CASE("Mapper 289 decodes every documented register alias and ignores unused bits",
          "[mapper289]") {
    auto mapper = board(131072U);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        const auto value = static_cast<Byte>(addr * 37U + addr / 256U);
        mapper->write_prg(static_cast<Address>(addr), value);
        const auto saved = state(*mapper);
        const bool mode_write = (addr & 0xE001U) == 0x6000U;
        const bool outer_write = (addr & 0xE001U) == 0x6001U;
        const bool data_write = addr >= 0x8000U;
        CAPTURE(addr, value, mode_write, outer_write, data_write);
        REQUIRE(saved[6U] == (mode_write ? value & 0x0FU : 0U));
        REQUIRE(saved[7U] == (outer_write ? value & 0x7FU : 0U));
        REQUIRE(saved[8U] == (data_write ? value & 7U : 0U));
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 289 NROM-128 mirrors each seven-bit outer bank in both CPU windows",
          "[mapper289]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0U);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0x6001U, static_cast<Byte>(outer));
        const unsigned bank = outer & 0x7FU;
        for (Address addr : {0x8000U, 0x8123U, 0x9ABCu, 0xBFFFU, 0xC000U, 0xE123U, 0xFFFFU}) {
            CAPTURE(outer, addr);
            REQUIRE(mapper->read_prg(addr) == expected_prg(kMaxPrgSize, bank, addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
    }
}

TEST_CASE("Mapper 289 NROM-256 replaces only bank A14 with CPU A14", "[mapper289]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 1U);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0x6001U, static_cast<Byte>(outer));
        for (Address addr : {0x8000U, 0xA123U, 0xBFFFU, 0xC000U, 0xF123U, 0xFFFFU}) {
            const unsigned bank = (outer & 0x7EU) | ((addr >> 14U) & 1U);
            CAPTURE(outer, addr, bank);
            REQUIRE(mapper->read_prg(addr) == expected_prg(kMaxPrgSize, bank, addr));
        }
    }
}

TEST_CASE("Mapper 289 UNROM combines outer high pins with selectable and fixed low banks",
          "[mapper289]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 2U);
    for (unsigned outer = 0U; outer < 128U; ++outer) {
        mapper->write_prg(0x6001U, static_cast<Byte>(outer));
        for (unsigned data = 0U; data < 8U; ++data) {
            mapper->write_prg(0xFFFFU, static_cast<Byte>(0xF8U | data));
            const unsigned selected = (outer & 0x78U) | data;
            const unsigned fixed = (outer & 0x78U) | 7U;
            for (Address addr : {0x8000U, 0xA234U, 0xBFFFU}) {
                CAPTURE(outer, data, addr, selected);
                REQUIRE(mapper->read_prg(addr) == expected_prg(kMaxPrgSize, selected, addr));
            }
            for (Address addr : {0xC000U, 0xE234U, 0xFFFFU}) {
                CAPTURE(outer, data, addr, fixed);
                REQUIRE(mapper->read_prg(addr) == expected_prg(kMaxPrgSize, fixed, addr));
            }
        }
    }
}

TEST_CASE("Mapper 289 fixed mode selects all low bank pins high in both windows", "[mapper289]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 3U);
    for (unsigned outer = 0U; outer < 256U; ++outer) {
        mapper->write_prg(0x6001U, static_cast<Byte>(outer));
        mapper->write_prg(0x8000U, static_cast<Byte>(outer ^ 0xA5U));
        const unsigned bank = (outer & 0x78U) | 7U;
        for (Address addr : {0x8000U, 0x9FFFU, 0xC000U, 0xFFFFU}) {
            CAPTURE(outer, addr, bank);
            REQUIRE(mapper->read_prg(addr) == expected_prg(kMaxPrgSize, bank, addr));
        }
    }
}

TEST_CASE("Mapper 289 wraps fitted PRG only after decoding bank pins", "[mapper289]") {
    for (std::size_t size : {16384UZ, 49152UZ, 131072UZ, 1048576UZ, 2097152UZ}) {
        auto mapper = board(size);
        for (unsigned mode = 0U; mode < 4U; ++mode) {
            mapper->write_prg(0x6000U, static_cast<Byte>(mode));
            mapper->write_prg(0x6001U, 0x75U);
            mapper->write_prg(0x8000U, 5U);
            for (Address addr : {0x8000U, 0xC000U}) {
                unsigned bank = 0U;
                if (mode == 0U) {
                    bank = 0x75U;
                } else if (mode == 1U) {
                    bank = 0x74U | ((addr >> 14U) & 1U);
                } else if (mode == 2U) {
                    bank = 0x70U | (addr < 0xC000U ? 5U : 7U);
                } else {
                    bank = 0x77U;
                }
                CAPTURE(size, mode, addr, bank);
                REQUIRE(mapper->read_prg(addr) == expected_prg(size, bank, addr));
            }
        }
    }
}

TEST_CASE("Mapper 289 mode bits control CHR protection and H/V mirroring", "[mapper289]") {
    auto mapper = board();
    for (unsigned mode = 0U; mode < 16U; ++mode) {
        mapper->power_on();
        mapper->write_prg(0x6000U, 0U);
        for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
            mapper->write_chr(static_cast<Address>(addr),
                              static_cast<Byte>(addr * 13U + addr / 64U));
        }
        mapper->write_prg(0x6000U, static_cast<Byte>(mode | 0xF0U));
        REQUIRE(mapper->mirror_mode() ==
                ((mode & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
            const auto initial = static_cast<Byte>(addr * 13U + addr / 64U);
            mapper->write_chr(static_cast<Address>(addr + kChrRamSize), 0xA5U);
            const auto expected = (mode & 4U) == 0U ? static_cast<Byte>(0xA5U) : initial;
            CAPTURE(mode, addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == expected);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 2U * kChrRamSize)) == expected);
        }
    }
}

TEST_CASE("Mapper 289 solder pads drive only D0/D1 throughout the low read window",
          "[mapper289][openbus]") {
    auto mapper = board();
    for (unsigned pads = 0U; pads < 4U; ++pads) {
        REQUIRE(mapper->configure_board_jumpers(static_cast<Byte>(pads)));
        for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
            CAPTURE(pads, addr);
            REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == pads);
            REQUIRE(mapper->prg_read_mask(static_cast<Address>(addr)) == 0x03U);
        }
        mapper->reset();
        REQUIRE(mapper->read_prg(0x7000U) == pads);
        mapper->power_on();
        REQUIRE(mapper->read_prg(0x7FFFU) == pads);
    }
    const auto before = state(*mapper);
    for (unsigned invalid : {4U, 5U, 127U, 255U}) {
        REQUIRE_FALSE(mapper->configure_board_jumpers(static_cast<Byte>(invalid)));
        REQUIRE(state(*mapper) == before);
    }
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 289 soft reset clears latches but retains CHR RAM and cold power clears it",
          "[mapper289][reset]") {
    auto mapper = board();
    REQUIRE(mapper->configure_board_jumpers(3U));
    mapper->write_prg(0x6000U, 0x0AU);
    mapper->write_prg(0x6001U, 0x7FU);
    mapper->write_prg(0x8000U, 7U);
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ addr / 256U));
    }
    mapper->reset();
    REQUIRE(state(*mapper)[6U] == 0U);
    REQUIRE(state(*mapper)[7U] == 0U);
    REQUIRE(state(*mapper)[8U] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x6000U) == 3U);
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr ^ addr / 256U));
    }
    mapper->power_on();
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
    }
    REQUIRE(mapper->read_prg(0x6000U) == 3U);
}

TEST_CASE("Mapper 289 validates bank extents and exact native memory metadata", "[mapper289]") {
    for (bool legacy : {false, true}) {
        for (std::size_t size : {16384UZ, 32768UZ, 49152UZ, 131072UZ, 1048576UZ, 2097152UZ}) {
            REQUIRE(Mapper289::validate_geometry(header(legacy), size, 0U));
        }
        for (std::size_t size : {0UZ,
                                 1UZ,
                                 8192UZ,
                                 16383UZ,
                                 16385UZ,
                                 32767UZ,
                                 32769UZ,
                                 2097151UZ,
                                 2097168UZ,
                                 4194304UZ}) {
            CAPTURE(legacy, size);
            REQUIRE_FALSE(Mapper289::validate_geometry(header(legacy), size, 0U));
        }
        for (std::size_t chr_size : {1UZ, 4096UZ, 8192UZ, 16384UZ}) {
            REQUIRE_FALSE(Mapper289::validate_geometry(header(legacy), kMaxPrgSize, chr_size));
        }
        for (auto mode :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
            auto invalid = header(legacy);
            invalid.mirror_mode = mode;
            REQUIRE_FALSE(Mapper289::validate_geometry(invalid, kMaxPrgSize, 0U));
        }
        for (unsigned field = 0U; field < 3U; ++field) {
            auto invalid = header(legacy);
            if (field == 0U) {
                invalid.has_battery = true;
            } else if (field == 1U) {
                invalid.prg_nvram_size = 8192U;
            } else {
                invalid.chr_nvram_size = 8192U;
            }
            REQUIRE_FALSE(Mapper289::validate_geometry(invalid, kMaxPrgSize, 0U));
        }
    }
    for (unsigned field = 0U; field < 2U; ++field) {
        auto invalid = header();
        if (field == 0U) {
            invalid.prg_ram_size = 8192U;
        } else {
            invalid.chr_ram_size = 0U;
        }
        REQUIRE_FALSE(Mapper289::validate_geometry(invalid, kMaxPrgSize, 0U));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper289::validate_geometry(invalid, kMaxPrgSize, 0U));
    }
    auto wrong_id = header();
    wrong_id.mapper_number = 288U;
    REQUIRE_FALSE(Mapper289::validate_geometry(wrong_id, kMaxPrgSize, 0U));
}

TEST_CASE("Mapper 289 snapshots replay bounded mutable state without ROM and reject atomically",
          "[mapper289][state]") {
    auto mapper = board(49152U);
    auto copy = board(49152U);
    REQUIRE(mapper->configure_board_jumpers(2U));
    REQUIRE(copy->configure_board_jumpers(2U));
    mapper->write_prg(0x6000U, 0x09U);
    mapper->write_prg(0x6001U, 0xABU);
    mapper->write_prg(0x8000U, 0xFEU);
    for (unsigned addr = 0U; addr < kChrRamSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 7U + addr / 31U));
    }
    mapper->write_prg(0x6000U, 0x0DU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 8201U);
    REQUIRE(saved[6U] == 0x0DU);
    REQUIRE(saved[7U] == 0x2BU);
    REQUIRE(saved[8U] == 6U);
    REQUIRE(state(*board(16384U)).size() == saved.size());
    REQUIRE(state(*board(kMaxPrgSize)).size() == saved.size());

    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));
    REQUIRE(copy->read_chr(0x0123U) == mapper->read_chr(0x0123U));

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated{std::span(saved).first(size)};
        copy->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        invalid[100U] ^= 0xFFU;
        CAPTURE(bit);
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (unsigned offset = 2U; offset <= 5U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        invalid[100U] ^= 0xFFU;
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (const auto [offset, value] : std::array<std::pair<unsigned, Byte>, 5>{
             {{6U, 0x10U}, {6U, 0xFFU}, {7U, 0x80U}, {8U, 8U}, {8U, 0xFFU}}}) {
        auto invalid = saved;
        invalid[offset] = value;
        invalid[100U] ^= 0xFFU;
        CAPTURE(offset, value);
        StateReader reader(invalid);
        copy->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*copy) == saved);
    }
    for (unsigned pads : {0U, 1U, 3U}) {
        auto other = board(49152U);
        REQUIRE(other->configure_board_jumpers(static_cast<Byte>(pads)));
        const auto before = state(*other);
        StateReader wrong_pads(saved);
        other->load_state(wrong_pads);
        REQUIRE_FALSE(wrong_pads.ok());
        REQUIRE(state(*other) == before);
    }
    for (std::size_t size : {16384UZ, 131072UZ, kMaxPrgSize}) {
        auto other = board(size);
        REQUIRE(other->configure_board_jumpers(2U));
        const auto before = state(*other);
        StateReader wrong_size(saved);
        other->load_state(wrong_size);
        REQUIRE_FALSE(wrong_size.ok());
        REQUIRE(state(*other) == before);
    }
    StateReader restore(saved);
    copy->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(state(*copy) == saved);
}

} // namespace
