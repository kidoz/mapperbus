#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper338.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 16384UZ;
constexpr std::size_t kChrBankSize = 8192UZ;
constexpr std::size_t kPrgSize = 8UZ * kPrgBankSize;
constexpr std::size_t kChrSize = 8UZ * kChrBankSize;
constexpr std::size_t kPrgSizeOffset = 2UZ;
constexpr std::size_t kLatchOffset = 10UZ;
constexpr std::size_t kStateSize = 11UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 338U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 7U + (offset >> 8U) * 13U + (offset >> 14U) * 31U +
                             (offset >> 16U) * 43U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = cell(offset, salt);
    }
    return bytes;
}

std::unique_ptr<Mapper338> board(std::size_t prg_size = kPrgSize,
                                 std::size_t chr_size = kChrSize,
                                 Byte salt = 0U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper338::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper338>(h, rom(prg_size, salt), rom(chr_size, salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(Byte latch, Address addr, std::size_t prg_size = kPrgSize, Byte salt = 0U) {
    const std::size_t offset = (latch & 7U) * kPrgBankSize + (addr & 0x3FFFU);
    return cell(offset % prg_size, salt);
}

Byte expected_chr(Byte latch, Address addr, std::size_t chr_size = kChrSize, Byte salt = 0U) {
    const std::size_t offset = (latch & 7U) * kChrBankSize + (addr & 0x1FFFU);
    return cell(offset % chr_size, salt);
}

TEST_CASE("Mapper 338 validates mapper submapper and ROM-only memory population", "[mapper338]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (std::size_t prg_size = kPrgBankSize; prg_size <= kPrgSize; prg_size *= 2U) {
            for (std::size_t chr_size = kChrBankSize; chr_size <= kChrSize; chr_size *= 2U) {
                REQUIRE(Mapper338::validate_geometry(header(mirror), prg_size, chr_size));
            }
        }
    }

    for (const auto mapper_number : {0U, 2U, 200U, 337U, 339U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(mapper_number);
        REQUIRE_FALSE(Mapper338::validate_geometry(h, kPrgSize, kChrSize));
    }
    auto h = header();
    h.is_nes2 = false;
    REQUIRE_FALSE(Mapper338::validate_geometry(h, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(Mapper338::validate_geometry(h, kPrgSize, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 8192UZ, 16383UZ, 16385UZ, 24576UZ, 131071UZ, 131073UZ, 262144UZ}) {
        REQUIRE_FALSE(Mapper338::validate_geometry(header(), size, kChrSize));
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 4096UZ, 8191UZ, 8193UZ, 12288UZ, 65535UZ, 65537UZ, 131072UZ}) {
        REQUIRE_FALSE(Mapper338::validate_geometry(header(), kPrgSize, size));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        REQUIRE_FALSE(Mapper338::validate_geometry(header(mirror), kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper338::validate_geometry(h, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 338 latches A3 through A0 throughout the upper CPU window", "[mapper338]") {
    auto mapper = board();
    for (unsigned raw_addr = 0U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        const auto value = static_cast<Byte>(raw_addr * 29U + (raw_addr >> 8U));
        mapper->write_prg(static_cast<Address>(raw_addr), value);
        CAPTURE(raw_addr, value);
        REQUIRE(state(*mapper)[kLatchOffset] == (raw_addr >= 0x8000U ? raw_addr & 0x0FU : 0U));
    }

    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0xA345U, static_cast<Byte>(value));
        REQUIRE(state(*mapper)[kLatchOffset] == 5U);
    }
    const auto initial = state(*mapper);
    for (const Address addr : {0U, 0x401FU, 0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == initial);
    for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
    }
}

TEST_CASE("Mapper 338 mirrors the selected 16 KiB PRG bank in both CPU windows", "[mapper338]") {
    for (std::size_t prg_size = kPrgBankSize; prg_size <= kPrgSize; prg_size *= 2U) {
        auto mapper = board(prg_size);
        for (unsigned latch = 0U; latch < 16U; ++latch) {
            mapper->write_prg(0x8000U | latch, 0xFFU);
            for (const Address addr :
                 {0x8000U, 0x8123U, 0x9FFFU, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU}) {
                CAPTURE(prg_size, latch, addr);
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(static_cast<Byte>(latch), addr, prg_size));
            }
            for (unsigned offset = 0U; offset < kPrgBankSize; ++offset) {
                const auto lower = static_cast<Address>(0x8000U + offset);
                const auto upper = static_cast<Address>(0xC000U + offset);
                REQUIRE(mapper->read_prg(lower) == mapper->read_prg(upper));
            }
        }
    }
}

TEST_CASE("Mapper 338 selects CHR with the PRG bank and protects ROM", "[mapper338]") {
    for (std::size_t chr_size = kChrBankSize; chr_size <= kChrSize; chr_size *= 2U) {
        auto mapper = board(kPrgSize, chr_size);
        for (unsigned latch = 0U; latch < 16U; ++latch) {
            mapper->write_prg(0xFFF0U | latch, 0U);
            for (const Address addr : {0U, 1U, 0x0123U, 0x1FFEU, 0x1FFFU, 0x2123U, 0x3FFFU}) {
                CAPTURE(chr_size, latch, addr);
                REQUIRE(mapper->read_chr(addr) ==
                        expected_chr(static_cast<Byte>(latch), addr, chr_size));
            }
        }
        const auto before = state(*mapper);
        for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
            const auto address = static_cast<Address>(addr);
            const auto original = mapper->read_chr(address);
            mapper->write_chr(address, static_cast<Byte>(original ^ 0xFFU));
            REQUIRE(mapper->read_chr(address) == original);
        }
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 338 A3 controls CIRAM and no auxiliary hardware is exposed", "[mapper338]") {
    auto mapper = board(kPrgSize, kChrSize, 0U, MirrorMode::Horizontal);
    for (unsigned latch = 0U; latch < 16U; ++latch) {
        mapper->write_prg(0x8000U | latch, 0U);
        CAPTURE(latch);
        REQUIRE(mapper->mirror_mode() ==
                ((latch & 8U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal));
    }
    REQUIRE_FALSE(mapper->nametable_address(0x2000U));
    REQUIRE_FALSE(mapper->ciram_address(0x2000U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    for (unsigned cycle = 0U; cycle < 256U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 338 reset and cold power select bank zero and horizontal CIRAM",
          "[mapper338][reset]") {
    auto mapper = board();
    const auto initial = state(*mapper);
    mapper->write_prg(0xFFFFU, 0xA5U);
    REQUIRE(state(*mapper) != initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0x8000U) == cell(0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(0U));
    REQUIRE(mapper->read_chr(0x0123U) == cell(0x0123U));

    mapper->write_prg(0x800FU, 0U);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 338 snapshots replay the latch without ROM serialization", "[mapper338][state]") {
    auto mapper = board();
    mapper->write_prg(0xBFFDU, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved.size() < kChrSize);
    REQUIRE(saved[kLatchOffset] == 0x0DU);

    auto copy = board(kPrgSize, kChrSize, 0x80U, MirrorMode::Horizontal);
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == expected_prg(0x0DU, addr, kPrgSize, 0x80U));
    }
    for (const Address addr : {0U, 0x0123U, 0x1FFFU}) {
        REQUIRE(copy->read_chr(addr) == expected_chr(0x0DU, addr, kChrSize, 0x80U));
    }

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 338 rejects bounded malformed and incompatible states atomically",
          "[mapper338][state]") {
    auto mapper = board();
    mapper->write_prg(0xFFFEU, 0U);
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
    for (std::size_t offset = kPrgSizeOffset; offset < kLatchOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        reject(invalid);
    }
    for (const Byte latch : {0x10U, 0x11U, 0x80U, 0xFFU}) {
        auto invalid = saved;
        invalid[kLatchOffset] = latch;
        reject(invalid);
    }

    auto other_prg = board(kPrgSize / 2U);
    const auto before_prg = state(*other_prg);
    StateReader prg_reader(saved);
    other_prg->load_state(prg_reader);
    REQUIRE_FALSE(prg_reader.ok());
    REQUIRE(state(*other_prg) == before_prg);

    auto other_chr = board(kPrgSize, kChrSize / 2U);
    const auto before_chr = state(*other_chr);
    StateReader chr_reader(saved);
    other_chr->load_state(chr_reader);
    REQUIRE_FALSE(chr_reader.ok());
    REQUIRE(state(*other_chr) == before_chr);
}

TEST_CASE("Mapper 338 state validation accepts every reachable latch", "[mapper338][state]") {
    auto mapper = board();
    const auto saved = state(*mapper);
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        auto candidate = saved;
        candidate[kLatchOffset] = static_cast<Byte>(latch);
        StateReader reader(candidate);
        mapper->load_state(reader);
        if (latch < 16U) {
            REQUIRE(reader.ok());
            REQUIRE(reader.remaining() == 0U);
            REQUIRE(state(*mapper) == candidate);
            REQUIRE(mapper->read_prg(0x8123U) == expected_prg(static_cast<Byte>(latch), 0x8123U));
        } else {
            REQUIRE_FALSE(reader.ok());
        }
    }
    mapper->reset();
    REQUIRE(state(*mapper) == saved);
}

} // namespace
