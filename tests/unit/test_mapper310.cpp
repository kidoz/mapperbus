#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper310.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 4U * 1024U * 1024U;
constexpr std::size_t kChrBankSize = 8192U;
constexpr std::size_t kChrSize = 4U * kChrBankSize;
constexpr std::size_t kStateSize = 9U + kChrSize;

INesHeader header() {
    INesHeader result{};
    result.mapper_number = 310U;
    result.is_nes2 = true;
    result.chr_ram_size = kChrSize;
    result.mirror_mode = MirrorMode::Vertical;
    return result;
}

Byte pattern(std::size_t offset) {
    return static_cast<Byte>(offset * 5U + (offset >> 8U) * 11U + (offset >> 14U) * 37U +
                             (offset >> 21U) * 19U);
}

std::vector<Byte> rom(std::size_t size) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = pattern(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper310> board(std::size_t prg_size = 2U * 1024U * 1024U,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    auto h = header();
    h.mirror_mode = mirror;
    REQUIRE(Mapper310::validate_geometry(h, prg_size, 0U));
    return std::make_unique<Mapper310>(h, rom(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

Byte expected_prg(std::size_t prg_size, Byte data_latch, Byte address_latch, Address addr) {
    const unsigned mode = address_latch & 3U;
    std::size_t bank =
        static_cast<std::size_t>((address_latch >> 2U) & 3U) * 64U + (data_latch & 0x3FU);
    std::size_t offset = 0U;
    switch (mode) {
    case 0U:
        bank = (bank & 0xFEU) | ((addr >= 0xC000U) ? 1U : 0U);
        offset = bank * 16384U + (addr & 0x3FFFU);
        break;
    case 1U:
        if (addr >= 0xC000U) {
            bank = (bank & 0xF8U) | 7U;
        }
        offset = bank * 16384U + (addr & 0x3FFFU);
        break;
    case 2U:
        offset = bank * 16384U + ((data_latch & 0x80U) != 0U ? 8192U : 0U) + (addr & 0x1FFFU);
        break;
    case 3U:
        offset = bank * 16384U + (addr & 0x3FFFU);
        break;
    }
    return pattern(offset % prg_size);
}

TEST_CASE("Mapper 310 accepts only the documented native board geometry", "[mapper310]") {
    for (std::size_t size = 16384U; size <= kPrgSize; size *= 2U) {
        for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            auto h = header();
            h.mirror_mode = mirror;
            REQUIRE(Mapper310::validate_geometry(h, size, 0U));
        }
    }

    auto legacy = header();
    legacy.is_nes2 = false;
    legacy.prg_ram_size = 8192U;
    REQUIRE_FALSE(Mapper310::validate_geometry(legacy, kPrgSize, 0U));

    for (std::size_t size :
         {0UZ, 1UZ, 8192UZ, 16383UZ, 16385UZ, 24576UZ, 49152UZ, 3145728UZ, 4210688UZ, 8388608UZ}) {
        REQUIRE_FALSE(Mapper310::validate_geometry(header(), size, 0U));
    }
    for (std::size_t chr_size : {1UZ, 8192UZ, 32768UZ}) {
        REQUIRE_FALSE(Mapper310::validate_geometry(header(), kPrgSize, chr_size));
    }
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = static_cast<Byte>(submapper);
        REQUIRE_FALSE(Mapper310::validate_geometry(h, kPrgSize, 0U));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper310::validate_geometry(h, kPrgSize, 0U));
    }
    for (unsigned field = 0U; field < 7U; ++field) {
        auto h = header();
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
            h.chr_ram_size = 0U;
            break;
        case 4U:
            h.chr_ram_size = 8192U;
            break;
        case 5U:
            h.chr_ram_size = 65536U;
            break;
        case 6U:
            h.chr_nvram_size = kChrSize;
            break;
        }
        REQUIRE_FALSE(Mapper310::validate_geometry(h, kPrgSize, 0U));
    }
}

TEST_CASE("Mapper 310 decodes every PRG pin combination in all four modes", "[mapper310]") {
    auto mapper = board(kPrgSize);
    constexpr std::array<Address, 8> addresses{
        0x8000U, 0x9FFFU, 0xA000U, 0xBFFFU, 0xC000U, 0xDFFFU, 0xE000U, 0xFFFFU};
    for (unsigned control = 0U; control < 16U; ++control) {
        mapper->write_prg(static_cast<Address>(0xC000U | control), 0U);
        for (unsigned data = 0U; data < 256U; ++data) {
            mapper->write_prg(0x8000U, static_cast<Byte>(data));
            for (const auto addr : addresses) {
                CAPTURE(control, data, addr);
                REQUIRE(mapper->read_prg(addr) ==
                        expected_prg(
                            kPrgSize, static_cast<Byte>(data), static_cast<Byte>(control), addr));
            }
            REQUIRE(mapper->mirror_mode() ==
                    ((data & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
}

TEST_CASE("Mapper 310 register masks cover every upper CPU address independently", "[mapper310]") {
    auto mapper = board();
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        const auto addr = static_cast<Address>(raw_addr);
        const auto value = static_cast<Byte>(raw_addr * 29U + (raw_addr >> 8U));
        mapper->reset();
        Byte expected_data = 0U;
        Byte expected_control = 0U;
        if (addr < 0xC000U) {
            expected_data = value;
            mapper->write_prg(addr, value);
        } else {
            mapper->write_prg(0x8000U, 0xA5U);
            expected_data = 0xA5U;
            expected_control = addr & 0x0FU;
            mapper->write_prg(addr, value);
        }
        CAPTURE(raw_addr, value);
        REQUIRE(mapper->read_prg(0x8123U) ==
                expected_prg(2U * 1024U * 1024U, expected_data, expected_control, 0x8123U));
        REQUIRE(mapper->read_prg(0xE234U) ==
                expected_prg(2U * 1024U * 1024U, expected_data, expected_control, 0xE234U));
        REQUIRE(mapper->mirror_mode() ==
                ((expected_data & 0x40U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr < 0x8000U; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 310 banks and protects every CHR RAM cell according to the PRG mode",
          "[mapper310]") {
    auto mapper = board();
    for (unsigned mode = 0U; mode < 4U; ++mode) {
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0xC001U, static_cast<Byte>(bank));
            for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
                mapper->write_chr(static_cast<Address>(addr),
                                  static_cast<Byte>(addr * 3U + bank * 53U));
            }

            mapper->write_prg(static_cast<Address>(0xC000U | mode), static_cast<Byte>(bank));
            for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
                const auto original = static_cast<Byte>(addr * 3U + bank * 53U);
                const auto attempted = static_cast<Byte>(original ^ 0xA5U);
                mapper->write_chr(static_cast<Address>(addr), attempted);
                CAPTURE(mode, bank, addr);
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                        (mode == 1U || mode == 2U ? attempted : original));
            }
        }
    }

    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xC001U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
            mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr + bank * 71U));
        }
    }
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xC003U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
            REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x2000U)) ==
                    static_cast<Byte>(addr + bank * 71U));
        }
    }
}

TEST_CASE("Mapper 310 reset clears both latches and power-on clears volatile CHR RAM",
          "[mapper310]") {
    auto mapper = board();
    mapper->write_prg(0xC001U, 2U);
    for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 7U));
    }
    mapper->write_prg(0x9FFFU, 0xFFU);
    mapper->write_prg(0xFFFFU, 3U);
    mapper->reset();

    auto saved = state(*mapper);
    REQUIRE(saved[6U] == 0U);
    REQUIRE(saved[7U] == 0U);
    REQUIRE(saved[8U] == 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(mapper->read_prg(0x8000U) == expected_prg(2U * 1024U * 1024U, 0U, 0U, 0x8000U));
    REQUIRE(mapper->read_prg(0xC000U) == expected_prg(2U * 1024U * 1024U, 0U, 0U, 0xC000U));
    mapper->write_prg(0xC000U, 2U);
    for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == static_cast<Byte>(addr * 7U));
    }

    mapper->power_on();
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xC000U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
        }
    }
}

TEST_CASE("Mapper 310 leaves unrelated CPU windows floating and owns no persistent hardware",
          "[mapper310]") {
    auto mapper = board();
    const auto before = state(*mapper);
    for (const Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->maps_prg(0x8000U));
    REQUIRE(mapper->maps_prg(0xFFFFU));
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::vector<Byte>(kChrSize, 0xA5U));
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 310 snapshots replay all mutable state without immutable ROM",
          "[mapper310][state]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper->write_prg(0xC001U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
            mapper->write_chr(static_cast<Address>(addr),
                              static_cast<Byte>(addr * 13U + bank * 29U));
        }
    }
    mapper->write_prg(0xBFFFU, 0xEDU);
    mapper->write_prg(0xC00EU, 2U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(saved[6U] == 0xEDU);
    REQUIRE(saved[7U] == 0x0EU);
    REQUIRE(saved[8U] == 2U);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
    for (const Address addr : {0x8000U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    for (unsigned addr = 0U; addr < kChrBankSize; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) ==
                mapper->read_chr(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 310 rejects truncated and invalid states without mutation",
          "[mapper310][state]") {
    auto mapper = board();
    mapper->write_prg(0xC001U, 1U);
    mapper->write_chr(0x123U, 0xA7U);
    mapper->write_prg(0x8000U, 0xFDU);
    mapper->write_prg(0xC00FU, 3U);
    const auto saved = state(*mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (std::size_t offset = 0U; offset < 6U; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte value : {0x10U, 0x80U, 0xF0U, 0xFFU}) {
        auto invalid = saved;
        invalid[7U] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (Byte value : {4U, 0x40U, 0xFFU}) {
        auto invalid = saved;
        invalid[8U] = value;
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto other = board(1024U * 1024U);
    const auto before = state(*other);
    StateReader mismatch(saved);
    other->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other) == before);

    for (unsigned data = 0U; data < 256U; ++data) {
        auto valid = saved;
        valid[6U] = static_cast<Byte>(data);
        StateReader reader(valid);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(state(*mapper) == valid);
    }
    for (unsigned control = 0U; control < 16U; ++control) {
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            auto valid = saved;
            valid[7U] = static_cast<Byte>(control);
            valid[8U] = static_cast<Byte>(bank);
            StateReader reader(valid);
            mapper->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == valid);
        }
    }
}

} // namespace
