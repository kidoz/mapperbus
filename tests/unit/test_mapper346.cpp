#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper346.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x10000UZ;
constexpr std::size_t kPrgBankSize = 0x8000UZ;
constexpr std::size_t kWorkRamSize = 0x2000UZ;
constexpr std::size_t kBankOffset = 6UZ;
constexpr std::size_t kPrgRamOffset = 7UZ;
constexpr std::size_t kChrRamOffset = kPrgRamOffset + kWorkRamSize;
constexpr std::size_t kStateSize = kChrRamOffset + kWorkRamSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 346U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kWorkRamSize;
    result.chr_ram_size = kWorkRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 23U + (offset >> 8U) * 31U + (offset >> 15U) * 47U + salt);
}

std::vector<Byte> rom(Byte salt = 0U) {
    std::vector<Byte> data(kPrgSize);
    for (std::size_t offset = 0U; offset < data.size(); ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper346> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper346::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper346>(h, rom(salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 346 validates ID submapper ROM and exact memory population", "[mapper346]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper346::validate_geometry(header(mirror), kPrgSize, 0U));
    }

    for (const unsigned id : {0U, 4U, 108U, 302U, 345U, 347U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper346::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper346::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper346::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size : {0UZ, 1UZ, 0x2000UZ, 0x8000UZ, 0xFFFFUZ, 0x10001UZ, 0x20000UZ}) {
        REQUIRE_FALSE(Mapper346::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        REQUIRE_FALSE(Mapper346::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper346::validate_geometry(header(mirror), kPrgSize, 0U));
    }

    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 0U;
            break;
        case 2U:
            h.prg_ram_size = 0x1000U;
            break;
        case 3U:
            h.prg_nvram_size = kWorkRamSize;
            break;
        case 4U:
            h.chr_ram_size = 0U;
            break;
        case 5U:
            h.chr_ram_size = 0x1000U;
            break;
        case 6U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kWorkRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper346::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 346 powers on in bank 1 with RAM and fixed CIRAM", "[mapper346][reset]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(mirror);
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->maps_expansion(0x6000U));
        REQUIRE_FALSE(mapper->nametable_address(0x2000U));
        REQUIRE_FALSE(mapper->ciram_address(0x2000U));

        for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
            const auto addr = static_cast<Address>(0x8000U + offset);
            REQUIRE(mapper->read_prg(addr) == cell(kPrgBankSize + offset));
        }
        for (unsigned offset = 0U; offset < kWorkRamSize; ++offset) {
            REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) == 0U);
            REQUIRE(mapper->read_chr(static_cast<Address>(offset)) == 0U);
        }
        for (const Address addr : {0x0000U, 0x401FU, 0x5000U, 0x5FFFU}) {
            REQUIRE_FALSE(mapper->maps_prg(addr));
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
        for (const Address addr : {0x6000U, 0x7FFFU, 0x8000U, 0xE0A0U, 0xEE36U, 0xFFFFU}) {
            REQUIRE(mapper->maps_prg(addr));
            REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
        }
    }
}

TEST_CASE("Mapper 346 decodes only the two documented registers without conflicts", "[mapper346]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(0xE0A0U, 0U);
        mapper->write_prg(static_cast<Address>(addr), 0xA5U);
        CAPTURE(addr);
        REQUIRE(mapper->read_prg(0x8000U) == cell((addr == 0xEE36U ? kPrgBankSize : 0UZ)));

        mapper->write_prg(0xEE36U, 0U);
        mapper->write_prg(static_cast<Address>(addr), 0x5AU);
        REQUIRE(mapper->read_prg(0x8000U) == cell((addr == 0xE0A0U ? 0UZ : kPrgBankSize)));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xE0A0U, static_cast<Byte>(value));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(kPrgBankSize - 1U));
        mapper->write_prg(0xEE36U, static_cast<Byte>(value));
        REQUIRE(mapper->read_prg(0xFFFFU) == cell(kPrgSize - 1U));
    }
}

TEST_CASE("Mapper 346 maps all volatile PRG and CHR RAM cells", "[mapper346]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < kWorkRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), cell(offset, 0x53U));
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0xA7U));
    }
    for (unsigned offset = 0U; offset < kWorkRamSize; ++offset) {
        CAPTURE(offset);
        REQUIRE(mapper->read_prg(static_cast<Address>(0x6000U + offset)) == cell(offset, 0x53U));
        REQUIRE(mapper->read_chr(static_cast<Address>(offset)) == cell(offset, 0xA7U));
        REQUIRE(mapper->read_chr(static_cast<Address>(0x2000U + offset)) == cell(offset, 0xA7U));
    }
    mapper->write_prg(0xE0A0U, 0U);
    mapper->write_prg(0xEE36U, 0U);
    REQUIRE(mapper->read_prg(0x6000U) == cell(0U, 0x53U));
    REQUIRE(mapper->read_chr(0U) == cell(0U, 0xA7U));
}

TEST_CASE("Mapper 346 soft reset retains RAM and cold power clears it", "[mapper346][reset]") {
    auto mapper = board(MirrorMode::Horizontal);
    mapper->write_prg(0x6123U, 0xA6U);
    mapper->write_chr(0x0456U, 0x69U);
    mapper->write_prg(0xE0A0U, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == cell(kPrgBankSize));
    REQUIRE(mapper->read_prg(0x6123U) == 0xA6U);
    REQUIRE(mapper->read_chr(0x0456U) == 0x69U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    mapper->write_prg(0xE0A0U, 0U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == cell(kPrgBankSize));
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    REQUIRE(mapper->read_chr(0x0456U) == 0U);
}

TEST_CASE("Mapper 346 snapshots replay bank and RAM without ROM serialization",
          "[mapper346][state]") {
    auto mapper = board();
    mapper->write_prg(0xE0A0U, 0U);
    for (unsigned offset = 0U; offset < kWorkRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), cell(offset, 0x37U));
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0x91U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 16391UZ);
    REQUIRE(saved[kBankOffset] == 0U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == cell(0U, 0x80U));
    REQUIRE(copy->read_prg(0xFFFFU) == cell(kPrgBankSize - 1U, 0x80U));
    for (unsigned offset = 0U; offset < kWorkRamSize; ++offset) {
        REQUIRE(copy->read_prg(static_cast<Address>(0x6000U + offset)) == cell(offset, 0x37U));
        REQUIRE(copy->read_chr(static_cast<Address>(offset)) == cell(offset, 0x91U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 346 rejects bounded malformed and incompatible states atomically",
          "[mapper346][state]") {
    auto mapper = board();
    mapper->write_prg(0xE0A0U, 0U);
    for (unsigned offset = 0U; offset < kWorkRamSize; ++offset) {
        mapper->write_prg(static_cast<Address>(0x6000U + offset), cell(offset, 0x42U));
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0x84U));
    }
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
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (std::size_t offset = 2U; offset < kBankOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 0x80U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte bank : {2U, 3U, 0x7FU, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kBankOffset] = bank;
        reject(invalid);
    }

    auto other = board(MirrorMode::Horizontal);
    const auto before = state(*other);
    StateReader reader(saved);
    other->load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
