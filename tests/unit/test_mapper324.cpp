#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper324.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 1024U * 1024U;
constexpr std::size_t kOuterBankSize = 128U * 1024U;
constexpr std::size_t kInnerBankSize = 16U * 1024U;
constexpr std::size_t kChrSize = 8192U;
constexpr std::size_t kStateSize = 8U + kChrSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 324U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.chr_ram_size = kChrSize;
    return result;
}

Byte cell(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 14U) * 31U +
                             (offset >> 17U) * 13U);
}

std::vector<Byte> patterned_rom() {
    std::vector<Byte> bytes(kPrgSize);
    for (std::size_t offset = 0U; offset < bytes.size(); ++offset) {
        bytes[offset] = cell(offset);
    }
    return bytes;
}

std::vector<Byte> flat_rom(Byte value = 0xFFU) {
    return std::vector<Byte>(kPrgSize, value);
}

std::unique_ptr<Mapper324> board_with_rom(std::vector<Byte> prg,
                                          MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper324::validate_geometry(h, prg.size(), 0U));
    return std::make_unique<Mapper324>(h, std::move(prg), std::vector<Byte>{});
}

std::unique_ptr<Mapper324> board(MirrorMode mirror = MirrorMode::Vertical) {
    return board_with_rom(flat_rom(), mirror);
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_latches(Mapper324& mapper, Byte outer, Byte unrom) {
    auto bytes = state(mapper);
    bytes[6U] = outer;
    bytes[7U] = unrom;
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
}

TEST_CASE("Mapper 324 validates the exact native 27C080 and 6264 population", "[mapper324]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper324::validate_geometry(header(mirror), kPrgSize, 0U));
    }

    for (std::size_t size :
         {0UZ, 1UZ, 8192UZ, 16384UZ, 131072UZ, 1048575UZ, 1048577UZ, 2097152UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper324::validate_geometry(header(), size, 0U));
    }
    for (std::size_t size : {1UZ, 4096UZ, 8192UZ, 16384UZ}) {
        REQUIRE_FALSE(Mapper324::validate_geometry(header(), kPrgSize, size));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper324::validate_geometry(h, kPrgSize, 0U));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper324::validate_geometry(h, kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 10U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.mapper_number = 2U;
            break;
        case 1U:
            h.is_nes2 = false;
            break;
        case 2U:
            h.has_battery = true;
            break;
        case 3U:
            h.prg_ram_size = 8192U;
            break;
        case 4U:
            h.prg_nvram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 0U;
            break;
        case 6U:
            h.chr_ram_size = 4096U;
            break;
        case 7U:
            h.chr_ram_size = 16384U;
            break;
        case 8U:
            h.chr_nvram_size = kChrSize;
            break;
        case 9U:
            h.prg_ram_size = 8192U;
            h.prg_nvram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper324::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 324 maps every PRG cell with a fixed last page inside each outer bank",
          "[mapper324]") {
    auto mapper = board_with_rom(patterned_rom());
    for (unsigned outer = 0U; outer < 8U; ++outer) {
        for (unsigned inner = 0U; inner < 8U; ++inner) {
            set_latches(*mapper, static_cast<Byte>(outer << 4U), static_cast<Byte>(inner));
            const std::size_t lower = outer * kOuterBankSize + inner * kInnerBankSize;
            const std::size_t upper = outer * kOuterBankSize + 7U * kInnerBankSize;
            for (unsigned offset = 0U; offset < kInnerBankSize; ++offset) {
                CAPTURE(outer, inner, offset);
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                        cell(lower + offset));
                REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) ==
                        cell(upper + offset));
            }
        }
    }
}

TEST_CASE("Mapper 324 decodes all write aliases and only the two documented latch groups",
          "[mapper324]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto value = static_cast<Byte>((raw * 29U + (raw >> 8U)) & 0x7FU);
        mapper->write_prg(static_cast<Address>(raw), value);
        const auto saved = state(*mapper);
        CAPTURE(raw, value);
        REQUIRE(saved[6U] == 0U);
        REQUIRE(saved[7U] == (value & 0x87U));
    }

    const auto before = state(*mapper);
    for (unsigned raw = 0U; raw < 0x8000U; ++raw) {
        mapper->write_prg(static_cast<Address>(raw), static_cast<Byte>(raw));
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 324 applies AND bus conflicts using the old PRG mapping", "[mapper324]") {
    auto bytes = flat_rom();
    bytes[0U] = 0x25U;
    bytes[5U * kInnerBankSize] = 0x8AU;
    bytes[7U * kInnerBankSize + 0x3FFFU] = 0x73U;
    auto mapper = board_with_rom(std::move(bytes));

    mapper->write_prg(0x8000U, 0xFFU);
    REQUIRE(state(*mapper)[6U] == 0U);
    REQUIRE(state(*mapper)[7U] == 0x05U);
    REQUIRE(mapper->read_prg(0x8000U) == 0x8AU);

    mapper->write_prg(0x8000U, 0xFFU);
    REQUIRE(state(*mapper)[6U] == 0x08U);
    REQUIRE(state(*mapper)[7U] == 0x82U);

    mapper->write_prg(0x8000U, 0U);
    mapper->write_prg(0xFFFFU, 0xF7U);
    REQUIRE(state(*mapper)[6U] == 0x08U);
    REQUIRE(state(*mapper)[7U] == 0x03U);
}

TEST_CASE("Mapper 324 clocks the outer latch only on an unlocked zero-to-one C transition",
          "[mapper324]") {
    auto mapper = board();
    for (unsigned outer = 0U; outer < 8U; ++outer) {
        const auto inner = static_cast<Byte>(7U - outer);
        const auto command = static_cast<Byte>(0x80U | (outer << 4U) | inner);
        mapper->write_prg(0x8000U, command);
        REQUIRE(state(*mapper)[6U] == static_cast<Byte>(outer << 4U));
        REQUIRE(state(*mapper)[7U] == static_cast<Byte>(0x80U | inner));

        mapper->write_prg(0xFFFFU, 0x55U);
        REQUIRE(state(*mapper)[7U] == 0x05U);
        mapper->write_prg(0xFFFFU, 0x55U);
        REQUIRE(state(*mapper)[6U] == static_cast<Byte>(outer << 4U));
        mapper->write_prg(0x8000U, inner);
        REQUIRE(state(*mapper)[7U] == inner);
    }

    mapper->write_prg(0x8000U, 0x9BU);
    REQUIRE(state(*mapper)[6U] == 0x18U);
    REQUIRE(state(*mapper)[7U] == 0x83U);
    mapper->write_prg(0x8000U, 0U);
    mapper->write_prg(0x8000U, 0xE6U);
    REQUIRE(state(*mapper)[6U] == 0x18U);
    REQUIRE(state(*mapper)[7U] == 0x86U);

    mapper->reset();
    REQUIRE(state(*mapper)[6U] == 0U);
    REQUIRE(state(*mapper)[7U] == 0x86U);
    mapper->write_prg(0x8000U, 0U);
    mapper->write_prg(0x8000U, 0xD5U);
    REQUIRE(state(*mapper)[6U] == 0x50U);
    REQUIRE(state(*mapper)[7U] == 0x85U);
}

TEST_CASE("Mapper 324 reset clears outer bank and lock but retains C and the UNROM page",
          "[mapper324]") {
    auto mapper = board();
    REQUIRE(state(*mapper)[6U] == 0U);
    REQUIRE(state(*mapper)[7U] == 0U);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 19U + addr / 7U));
    }

    mapper->write_prg(0x8000U, 0xDEU);
    REQUIRE(state(*mapper)[6U] == 0x58U);
    REQUIRE(state(*mapper)[7U] == 0x86U);
    mapper->reset();
    REQUIRE(state(*mapper)[6U] == 0U);
    REQUIRE(state(*mapper)[7U] == 0x86U);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr * 19U + addr / 7U));
    }

    mapper->write_prg(0x8000U, 0x03U);
    mapper->write_prg(0x8000U, 0xA9U);
    mapper->power_on();
    REQUIRE(state(*mapper)[6U] == 0U);
    REQUIRE(state(*mapper)[7U] == 0x81U);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
    }
}

TEST_CASE("Mapper 324 has independent volatile CHR RAM and hard-wired H/V mirroring",
          "[mapper324]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        for (unsigned addr = 0U; addr < kChrSize; ++addr) {
            const auto value = static_cast<Byte>(addr ^ (addr >> 8U) ^ 0xA5U);
            mapper->write_chr(static_cast<Address>(addr), value);
        }
        for (Byte outer = 0U; outer < 0x80U; outer += 0x10U) {
            for (Byte unrom : {0U, 1U, 7U, 0x80U, 0x87U}) {
                set_latches(*mapper, outer, unrom);
                for (unsigned addr = 0U; addr < kChrSize; ++addr) {
                    REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                            static_cast<Byte>(addr ^ (addr >> 8U) ^ 0xA5U));
                }
                REQUIRE(mapper->mirror_mode() == mirror);
            }
        }
    }
}

TEST_CASE("Mapper 324 leaves unrelated CPU windows floating and owns no peripheral hardware",
          "[mapper324]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
        mapper->write_expansion(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 324 snapshots replay all mutable state without immutable ROM",
          "[mapper324][state]") {
    auto mapper = board(MirrorMode::Horizontal);
    mapper->write_prg(0x8000U, 0xBDU);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 7U + addr / 31U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[6U] == 0x38U);
    REQUIRE(saved[7U] == 0x85U);

    auto copy = board(MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr * 7U + addr / 31U));
    }

    for (Byte outer = 0U; outer < 0x80U; outer += 0x10U) {
        for (Byte unrom = 0U; unrom < 0x88U; ++unrom) {
            if ((unrom & ~0x87U) != 0U) {
                continue;
            }
            auto valid = saved;
            valid[6U] = outer;
            valid[7U] = unrom;
            StateReader valid_reader(valid);
            copy->load_state(valid_reader);
            REQUIRE(valid_reader.ok());
            REQUIRE(state(*copy) == valid);
        }
    }
}

TEST_CASE("Mapper 324 bounded malformed states reject atomically", "[mapper324][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0xBDU);
    for (unsigned addr = 0U; addr < kChrSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0x5AU));
    }
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        reject(std::span(saved).first(size));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        reject(invalid);
    }
    for (std::size_t offset = 2U; offset < 6U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        if ((value & ~0x78U) != 0U) {
            auto invalid = saved;
            invalid[6U] = static_cast<Byte>(value);
            reject(invalid);
        }
        if ((value & ~0x87U) != 0U) {
            auto invalid = saved;
            invalid[7U] = static_cast<Byte>(value);
            reject(invalid);
        }
    }

    auto horizontal = board(MirrorMode::Horizontal);
    const auto horizontal_before = state(*horizontal);
    StateReader wrong_mirror(saved);
    horizontal->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*horizontal) == horizontal_before);

    auto mismatched = std::make_unique<Mapper324>(
        header(), std::vector<Byte>(kPrgSize / 2U, 0xFFU), std::vector<Byte>{});
    const auto mismatched_before = state(*mismatched);
    StateReader wrong_size(saved);
    mismatched->load_state(wrong_size);
    REQUIRE_FALSE(wrong_size.ok());
    REQUIRE(state(*mismatched) == mismatched_before);
}

} // namespace
