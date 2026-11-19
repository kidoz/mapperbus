#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper347.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 0x20000UZ;
constexpr std::size_t kPrgRamSize = 0x2000UZ;
constexpr std::size_t kChrRamSize = 0x2000UZ;
constexpr std::size_t kBankSize = 0x1000UZ;
constexpr std::size_t kBank1Offset = 6UZ;
constexpr std::size_t kBank2Offset = 7UZ;
constexpr std::size_t kMirrorOffset = 8UZ;
constexpr std::size_t kPrgRamOffset = 9UZ;
constexpr std::size_t kChrRamOffset = kPrgRamOffset + kPrgRamSize;
constexpr std::size_t kStateSize = kChrRamOffset + kChrRamSize;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 347U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.prg_ram_size = kPrgRamSize;
    result.chr_ram_size = kChrRamSize;
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

std::unique_ptr<Mapper347> board(MirrorMode mirror = MirrorMode::Vertical, Byte salt = 0U) {
    const auto h = header(mirror);
    REQUIRE(Mapper347::validate_geometry(h, kPrgSize, 0U));
    return std::make_unique<Mapper347>(h, rom(salt), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Address ram_address(std::size_t offset) {
    if (offset < 0x0C00UZ) {
        return static_cast<Address>(0x6000UZ + offset);
    }
    if (offset < 0x1400UZ) {
        return static_cast<Address>(0xB800UZ + offset - 0x0C00UZ);
    }
    return static_cast<Address>(0xCC00UZ + offset - 0x1400UZ);
}

TEST_CASE("Mapper 347 validates ID submapper ROM and exact memory population", "[mapper347]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        REQUIRE(Mapper347::validate_geometry(header(mirror), kPrgSize, 0U));
    }

    for (const unsigned id : {0U, 4U, 103U, 302U, 346U, 348U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper347::validate_geometry(h, kPrgSize, 0U));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper347::validate_geometry(legacy, kPrgSize, 0U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper347::validate_geometry(h, kPrgSize, 0U));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x10000UZ, 0x1FFFFUZ, 0x20001UZ, 0x40000UZ}) {
        REQUIRE_FALSE(Mapper347::validate_geometry(header(), size, 0U));
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x10000UZ}) {
        REQUIRE_FALSE(Mapper347::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper347::validate_geometry(header(mirror), kPrgSize, 0U));
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
            h.prg_nvram_size = kPrgRamSize;
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
            h.chr_nvram_size = kChrRamSize;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper347::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 347 maps every fixed and switchable PRG ROM cell", "[mapper347]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(0x8000U, static_cast<Byte>(bank));
        for (unsigned offset = 0U; offset < kBankSize; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_prg(static_cast<Address>(0x7000U + offset)) ==
                    cell(0x10000UZ + bank * kBankSize + offset));
        }
    }
    for (unsigned bank = 0U; bank < 16U; ++bank) {
        mapper->write_prg(0x9000U, static_cast<Byte>(bank));
        for (unsigned offset = 0U; offset < 0x0C00U; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_prg(static_cast<Address>(0xC000U + offset)) ==
                    cell(bank * kBankSize + offset));
        }
        for (unsigned offset = 0U; offset < 0x0400U; ++offset) {
            CAPTURE(bank, offset);
            REQUIRE(mapper->read_prg(static_cast<Address>(0x6C00U + offset)) ==
                    cell(bank * kBankSize + 0x0C00UZ + offset));
        }
    }
    for (unsigned addr = 0x8000U; addr <= 0xB7FFU; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == cell(0x10000UZ + addr));
    }
    for (unsigned addr = 0xD800U; addr <= 0xFFFFU; ++addr) {
        CAPTURE(addr);
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == cell(0x10000UZ + addr));
    }
}

TEST_CASE("Mapper 347 decodes only the two register ranges without conflicts", "[mapper347]") {
    auto mapper = board();
    Byte bank_1 = 0U;
    Byte bank_2 = 0U;
    auto mirror = MirrorMode::Vertical;

    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        const auto value = static_cast<Byte>(addr * 37U + (addr >> 5U) * 19U);
        if ((addr & 0xF000U) == 0x8000U) {
            bank_1 = value & 7U;
            mirror = (value & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
        } else if ((addr & 0xF000U) == 0x9000U) {
            bank_2 = value & 15U;
        }
        mapper->write_prg(static_cast<Address>(addr), value);
        CAPTURE(addr, value);
        REQUIRE(mapper->read_prg(0x7000U) == cell(0x10000UZ + bank_1 * kBankSize));
        REQUIRE(mapper->read_prg(0xC000U) == cell(bank_2 * kBankSize));
        REQUIRE(mapper->read_prg(0x6C00U) == cell(bank_2 * kBankSize + 0x0C00UZ));
        REQUIRE(mapper->mirror_mode() == mirror);
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8FFFU, static_cast<Byte>(value));
        REQUIRE(mapper->read_prg(0x7FFFU) ==
                cell(0x10000UZ + (value & 7U) * kBankSize + kBankSize - 1U));
        REQUIRE(mapper->mirror_mode() ==
                ((value & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        mapper->write_prg(0x9FFFU, static_cast<Byte>(value));
        REQUIRE(mapper->read_prg(0xCBFFU) == cell((value & 15U) * kBankSize + 0x0BFFUZ));
        REQUIRE(mapper->read_prg(0x6FFFU) == cell((value & 15U) * kBankSize + 0x0FFFUZ));
    }
}

TEST_CASE("Mapper 347 maps all split PRG RAM and CHR RAM cells", "[mapper347]") {
    auto mapper = board();
    for (unsigned offset = 0U; offset < kPrgRamSize; ++offset) {
        const auto addr = ram_address(offset);
        CAPTURE(offset, addr);
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, cell(offset, 0x53U));
        REQUIRE(mapper->read_prg(addr) == cell(offset, 0x53U));
    }
    for (unsigned offset = 0U; offset < kChrRamSize; ++offset) {
        CAPTURE(offset);
        REQUIRE(mapper->read_chr(static_cast<Address>(offset)) == 0U);
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0xA7U));
        REQUIRE(mapper->read_chr(static_cast<Address>(offset)) == cell(offset, 0xA7U));
        REQUIRE(mapper->read_chr(static_cast<Address>(0x2000U + offset)) == cell(offset, 0xA7U));
    }

    for (const Address addr : {0x0000U, 0x401FU, 0x5000U, 0x5FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
    }
    for (const Address addr :
         {0x6000U, 0x6BFFU, 0x6C00U, 0x7FFFU, 0x8000U, 0xB800U, 0xCC00U, 0xD800U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
}

TEST_CASE("Mapper 347 soft reset restores banks and cold power clears volatile RAM",
          "[mapper347][reset]") {
    for (const auto initial_mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(initial_mirror);
        mapper->write_prg(0x6123U, 0xA6U);
        mapper->write_prg(0xB987U, 0x69U);
        mapper->write_prg(0xD123U, 0x3CU);
        mapper->write_chr(0x0456U, 0xC7U);
        mapper->write_prg(0x8000U, 0x0FU);
        mapper->write_prg(0x9000U, 0x0EU);
        REQUIRE(mapper->read_prg(0x7000U) == cell(0x17000UZ));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0x0E000UZ));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

        mapper->reset();
        REQUIRE(mapper->read_prg(0x7000U) == cell(0x10000UZ));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
        REQUIRE(mapper->mirror_mode() == initial_mirror);
        REQUIRE(mapper->read_prg(0x6123U) == 0xA6U);
        REQUIRE(mapper->read_prg(0xB987U) == 0x69U);
        REQUIRE(mapper->read_prg(0xD123U) == 0x3CU);
        REQUIRE(mapper->read_chr(0x0456U) == 0xC7U);

        mapper->write_prg(0x8000U, 0x07U);
        mapper->write_prg(0x9000U, 0x0FU);
        mapper->power_on();
        REQUIRE(mapper->read_prg(0x7000U) == cell(0x10000UZ));
        REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
        REQUIRE(mapper->mirror_mode() == initial_mirror);
        REQUIRE(mapper->read_prg(0x6123U) == 0U);
        REQUIRE(mapper->read_prg(0xB987U) == 0U);
        REQUIRE(mapper->read_prg(0xD123U) == 0U);
        REQUIRE(mapper->read_chr(0x0456U) == 0U);
    }
}

TEST_CASE("Mapper 347 snapshots replay registers and RAM without ROM serialization",
          "[mapper347][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x06U);
    mapper->write_prg(0x9000U, 0x0DU);
    for (unsigned offset = 0U; offset < kPrgRamSize; ++offset) {
        mapper->write_prg(ram_address(offset), cell(offset, 0x37U));
        mapper->write_chr(static_cast<Address>(offset), cell(offset, 0x91U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() == 16393UZ);
    REQUIRE(saved[kBank1Offset] == 6U);
    REQUIRE(saved[kBank2Offset] == 13U);
    REQUIRE(saved[kMirrorOffset] == 1U);

    auto copy = board(MirrorMode::Vertical, 0x80U);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x7000U) == cell(0x16000UZ, 0x80U));
    REQUIRE(copy->read_prg(0xC000U) == cell(0x0D000UZ, 0x80U));
    REQUIRE(copy->read_prg(0x8000U) == cell(0x18000UZ, 0x80U));
    for (unsigned offset = 0U; offset < kPrgRamSize; ++offset) {
        REQUIRE(copy->read_prg(ram_address(offset)) == cell(offset, 0x37U));
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

TEST_CASE("Mapper 347 rejects bounded malformed and incompatible states atomically",
          "[mapper347][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x05U);
    mapper->write_prg(0x9000U, 0x0BU);
    for (unsigned offset = 0U; offset < kPrgRamSize; ++offset) {
        mapper->write_prg(ram_address(offset), cell(offset, 0x42U));
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
    for (std::size_t offset = 2U; offset < kBank1Offset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 0x80U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (const Byte bank : {8U, 9U, 0x7FU, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kBank1Offset] = bank;
        reject(invalid);
    }
    for (const Byte bank : {16U, 17U, 0x7FU, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kBank2Offset] = bank;
        reject(invalid);
    }
    for (const Byte mirror : {2U, 3U, 0x7FU, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kMirrorOffset] = mirror;
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
