#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper341.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 0x4000UZ;
constexpr std::size_t kChrBankSize = 0x2000UZ;
constexpr std::size_t kPrgSize = 8UZ * kPrgBankSize;
constexpr std::size_t kChrSize = 8UZ * kChrBankSize;
constexpr std::size_t kControlOffset = 10UZ;
constexpr std::size_t kStateSize = 11UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 341U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 13U + (offset >> 8U) * 19U + (offset >> 13U) * 31U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper341> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 Byte salt = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper341::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper341>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte bank, Address addr, std::size_t prg_size = kPrgSize, Byte salt = 0U) {
    const std::size_t offset =
        static_cast<std::size_t>(bank & 7U) * kPrgBankSize + (addr & (kPrgBankSize - 1U));
    return cell(offset % prg_size, salt);
}

Byte expected_chr(Byte bank, Address addr, std::size_t chr_size = kChrSize, Byte salt = 0U) {
    const std::size_t offset =
        static_cast<std::size_t>(bank & 7U) * kChrBankSize + (addr & (kChrBankSize - 1U));
    return cell(offset % chr_size, salt);
}

TEST_CASE("Mapper 341 validates mapper submapper ROM and exact memory population", "[mapper341]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg_size :
             {kPrgBankSize, 2UZ * kPrgBankSize, 4UZ * kPrgBankSize, kPrgSize}) {
            for (const std::size_t chr_size :
                 {kChrBankSize, 2UZ * kChrBankSize, 4UZ * kChrBankSize, kChrSize}) {
                REQUIRE(Mapper341::validate_geometry(header(mirror), prg_size, chr_size));
            }
        }
    }

    for (const unsigned id : {0U, 2U, 210U, 340U, 342U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper341::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper341::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper341::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x3FFFUZ, 0x4001UZ, 0x6000UZ, 0x1FFFFUZ, 0x20001UZ, 0x40000UZ}) {
        REQUIRE_FALSE(Mapper341::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x3000UZ, 0x10001UZ, 0x20000UZ}) {
        REQUIRE_FALSE(Mapper341::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper341::validate_geometry(header(mirror), kPrgSize, kChrSize));
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
            h.has_battery = true;
            break;
        case 3U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            h.has_battery = true;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper341::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 341 decodes every upper CPU alias and ignores write data", "[mapper341]") {
    auto mapper = board();
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        const auto value = static_cast<Byte>(raw_addr * 37U + (raw_addr >> 8U));
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        const auto saved = state(*mapper);
        const auto control =
            static_cast<Byte>(((raw_addr >> 8U) & 7U) | ((raw_addr & 2U) != 0U ? 8U : 0U));
        CAPTURE(raw_addr, value, control);
        REQUIRE(saved[kControlOffset] == control);
        REQUIRE(mapper->mirror_mode() ==
                ((control & 8U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->read_prg(0x8123U) == expected_prg(control, 0x8123U));
        REQUIRE(mapper->read_prg(0xF456U) == expected_prg(control, 0xF456U));
        REQUIRE(mapper->read_chr(0x0789U) == expected_chr(control, 0x0789U));
    }

    mapper->write_prg(0xBD02U, 0xA5U);
    const auto before_low_writes = state(*mapper);
    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before_low_writes);
    for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
}

TEST_CASE("Mapper 341 mirrors one PRG bank and selects the matching CHR bank", "[mapper341]") {
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU};
    for (const std::size_t prg_size :
         {kPrgBankSize, 2UZ * kPrgBankSize, 4UZ * kPrgBankSize, kPrgSize}) {
        auto mapper = board(prg_size, kChrSize);
        for (Byte bank = 0U; bank < 8U; ++bank) {
            mapper->write_prg(static_cast<Address>(0x8000U | (bank << 8U)), 0xFFU);
            for (const auto addr : addresses) {
                CAPTURE(prg_size, bank, addr);
                REQUIRE(mapper->read_prg(addr) == expected_prg(bank, addr, prg_size));
                REQUIRE(mapper->read_prg(addr ^ 0x4000U) == expected_prg(bank, addr, prg_size));
            }
        }
    }

    auto mapper = board();
    for (Byte bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(static_cast<Address>(0x8000U | (bank << 8U)), bank);
        for (unsigned offset = 0U; offset < 0x8000U; ++offset) {
            const auto addr = static_cast<Address>(0x8000U + offset);
            CAPTURE(bank, addr);
            REQUIRE(mapper->read_prg(addr) == expected_prg(bank, addr));
        }
        for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
            CAPTURE(bank, addr);
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                    expected_chr(bank, static_cast<Address>(addr)));
        }
    }
}

TEST_CASE("Mapper 341 wraps disconnected ROM pins independently", "[mapper341]") {
    for (const std::size_t chr_size : {kChrBankSize, 2UZ * kChrBankSize, 4UZ * kChrBankSize}) {
        auto mapper = board(kPrgSize, chr_size);
        for (Byte bank = 0U; bank < 8U; ++bank) {
            mapper->write_prg(static_cast<Address>(0x8000U | (bank << 8U)), 0U);
            for (const Address addr : {0U, 0x0001U, 0x0123U, 0x1FFFU}) {
                CAPTURE(chr_size, bank, addr);
                REQUIRE(mapper->read_chr(addr) == expected_chr(bank, addr, chr_size));
            }
        }
    }
}

TEST_CASE("Mapper 341 A1 alone controls CIRAM and ROM stays read-only", "[mapper341]") {
    auto mapper = board(kPrgSize, kChrSize, 0U, MirrorMode::Horizontal);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (unsigned low_byte = 0U; low_byte < 256U; ++low_byte) {
        mapper->write_prg(static_cast<Address>(0xFF00U | low_byte), 0U);
        CAPTURE(low_byte);
        REQUIRE(mapper->mirror_mode() ==
                ((low_byte & 2U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));

    for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
        const auto address = static_cast<Address>(addr);
        const auto original = mapper->read_chr(address);
        mapper->write_chr(address, static_cast<Byte>(original ^ 0xFFU));
        REQUIRE(mapper->read_chr(address) == original);
    }
}

TEST_CASE("Mapper 341 reset and power-on select bank zero and vertical mirroring",
          "[mapper341][reset]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    mapper->write_prg(0xBD02U, 0xFFU);
    REQUIRE(state(*mapper) != initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
    REQUIRE(mapper->read_chr(0U) == cell(0U));

    mapper->write_prg(0xC702U, 0U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 341 exposes no undocumented RAM persistence IRQ or audio", "[mapper341]") {
    auto mapper = board();
    const auto before = state(*mapper);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    mapper->write_expansion(0x5000U, 0xFFU);
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 341 snapshots replay the latch without ROM serialization", "[mapper341][state]") {
    auto mapper = board();
    mapper->write_prg(0xBD02U, 0xA5U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kChrBankSize);
    REQUIRE(saved[kControlOffset] == 0x0DU);

    auto copy = board(kPrgSize, kChrSize, 0x80U, MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const Address addr : {0x8000U, 0x8123U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == expected_prg(5U, addr, kPrgSize, 0x80U));
    }
    for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) ==
                expected_chr(5U, static_cast<Address>(addr), kChrSize, 0x80U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 341 rejects bounded malformed and incompatible states atomically",
          "[mapper341][state]") {
    auto mapper = board();
    mapper->write_prg(0xBD02U, 0U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto invalid = saved;
        invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        CAPTURE(bit);
        reject(invalid);
    }
    for (std::size_t offset = 2U; offset < kControlOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (const Byte control : {0x10U, 0x11U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kControlOffset] = control;
        reject(invalid);
    }

    const auto incompatible = [&](std::unique_ptr<Mapper341> other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    incompatible(board(kPrgSize / 2U, kChrSize));
    incompatible(board(kPrgSize, kChrSize / 2U));
}

TEST_CASE("Mapper 341 state validation accepts every reachable latch", "[mapper341][state]") {
    auto mapper = board();
    const auto saved = state(*mapper);
    for (Byte control = 0U; control < 16U; ++control) {
        auto candidate = saved;
        candidate[kControlOffset] = control;
        StateReader reader(candidate);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*mapper) == candidate);
        mapper->reset();
    }
}

} // namespace
