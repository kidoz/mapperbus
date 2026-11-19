#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper326.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 128U * 1024U;
constexpr std::size_t kChrSize = 128U * 1024U;
constexpr std::size_t kPrgBankSize = 8192U;
constexpr std::size_t kChrBankSize = 1024U;
constexpr std::size_t kStateSize = 17U;

INesHeader header(bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 326U;
    result.is_nes2 = true;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 41U + offset * 3U + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset * 5U + (offset >> 8U));
}

std::unique_ptr<Mapper326> board(bool vertical = true) {
    const auto h = header(vertical);
    REQUIRE(Mapper326::validate_geometry(h, kPrgSize, kChrSize));
    std::vector<Byte> prg(kPrgSize);
    std::vector<Byte> chr(kChrSize);
    for (std::size_t offset = 0U; offset < prg.size(); ++offset) {
        prg[offset] = prg_cell(offset / kPrgBankSize, offset % kPrgBankSize);
    }
    for (std::size_t offset = 0U; offset < chr.size(); ++offset) {
        chr[offset] = chr_cell(offset / kChrBankSize, offset % kChrBankSize);
    }
    return std::make_unique<Mapper326>(h, std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 326 accepts only the documented native ROM population", "[mapper326]") {
    for (bool vertical : {false, true}) {
        REQUIRE(Mapper326::validate_geometry(header(vertical), kPrgSize, kChrSize));
    }

    for (unsigned id : {0U, 23U, 325U, 327U, 65535U}) {
        auto invalid = header();
        invalid.mapper_number = static_cast<std::uint16_t>(id);
        REQUIRE_FALSE(Mapper326::validate_geometry(invalid, kPrgSize, kChrSize));
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    REQUIRE_FALSE(Mapper326::validate_geometry(legacy, kPrgSize, kChrSize));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto invalid = header();
        invalid.submapper = submapper;
        REQUIRE_FALSE(Mapper326::validate_geometry(invalid, kPrgSize, kChrSize));
    }
    for (std::size_t size : {0UZ, 1UZ, 8192UZ, 65536UZ, 131071UZ, 131073UZ, 262144UZ}) {
        REQUIRE_FALSE(Mapper326::validate_geometry(header(), size, kChrSize));
        REQUIRE_FALSE(Mapper326::validate_geometry(header(), kPrgSize, size));
    }
    for (auto mode : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto invalid = header();
        invalid.mirror_mode = mode;
        REQUIRE_FALSE(Mapper326::validate_geometry(invalid, kPrgSize, kChrSize));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto invalid = header();
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.prg_ram_size = 8192U;
            break;
        case 2U:
            invalid.prg_nvram_size = 8192U;
            break;
        case 3U:
            invalid.chr_ram_size = 8192U;
            break;
        case 4U:
            invalid.chr_nvram_size = 8192U;
            break;
        default:
            break;
        }
        CAPTURE(field);
        REQUIRE_FALSE(Mapper326::validate_geometry(invalid, kPrgSize, kChrSize));
    }
}

TEST_CASE("Mapper 326 cold state maps zero banks and the fixed final PRG bank", "[mapper326]") {
    auto mapper = board();
    REQUIRE(state(*mapper).size() == kStateSize);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        for (unsigned offset : {0U, 1U, 0x123U, 0x1FFFU}) {
            const auto addr = static_cast<Address>(0x8000U + slot * kPrgBankSize + offset);
            REQUIRE(mapper->read_prg(addr) == prg_cell(slot == 3U ? 15U : 0U, offset));
        }
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        REQUIRE(mapper->read_chr(static_cast<Address>(slot * kChrBankSize + 0x155U)) ==
                chr_cell(0U, 0x155U));
    }
    for (unsigned addr = 0x2000U; addr < 0x3F00U; ++addr) {
        REQUIRE(mapper->nametable_address(static_cast<Address>(addr)) ==
                static_cast<Address>(addr & 0x03FFU));
    }
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
}

TEST_CASE("Mapper 326 PRG latches retain seven bits and wrap at the ROM pins", "[mapper326]") {
    auto mapper = board();
    constexpr std::array<Address, 3> registers{0x8000U, 0xA000U, 0xC000U};
    for (unsigned value = 0U; value < 256U; ++value) {
        for (unsigned selected = 0U; selected < 3U; ++selected) {
            mapper->reset();
            mapper->write_prg(registers[selected], static_cast<Byte>(value));
            for (unsigned slot = 0U; slot < 4U; ++slot) {
                const unsigned bank = slot == 3U ? 15U : (slot == selected ? value & 0x0FU : 0U);
                for (unsigned offset : {0U, 0x123U, 0x1FFFU}) {
                    const auto addr = static_cast<Address>(0x8000U + slot * kPrgBankSize + offset);
                    CAPTURE(value, selected, slot, offset);
                    REQUIRE(mapper->read_prg(addr) == prg_cell(bank, offset));
                }
            }
        }
    }
}

TEST_CASE("Mapper 326 decodes every CPU register alias without bus conflicts", "[mapper326]") {
    auto mapper = board();
    for (unsigned raw_addr = 0x8000U; raw_addr <= 0xFFFFU; ++raw_addr) {
        mapper->reset();
        const auto addr = static_cast<Address>(raw_addr);
        mapper->write_prg(addr, 0xA5U);

        std::array<Byte, 3> prg_banks{};
        std::array<Byte, 8> chr_banks{};
        std::array<Byte, 4> nametable_banks{};
        const Address low_register = addr & 0x801FU;
        if (low_register >= 0x8010U && low_register <= 0x8017U) {
            chr_banks[low_register & 7U] = 0x25U;
        } else if (low_register >= 0x8018U && low_register <= 0x801BU) {
            nametable_banks[low_register & 3U] = 1U;
        } else {
            switch (addr & 0xE010U) {
            case 0x8000U:
                prg_banks[0] = 0x25U;
                break;
            case 0xA000U:
                prg_banks[1] = 0x25U;
                break;
            case 0xC000U:
                prg_banks[2] = 0x25U;
                break;
            default:
                break;
            }
        }

        CAPTURE(raw_addr);
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            const unsigned bank = slot == 3U ? 15U : prg_banks[slot] & 0x0FU;
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * kPrgBankSize +
                                                          0x123U)) == prg_cell(bank, 0x123U));
        }
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * kChrBankSize + 0x155U)) ==
                    chr_cell(chr_banks[slot], 0x155U));
        }
        for (unsigned table = 0U; table < 4U; ++table) {
            REQUIRE(
                mapper->nametable_address(static_cast<Address>(0x2000U + table * 0x400U + 23U)) ==
                static_cast<Address>(nametable_banks[table] * 0x400U + 23U));
        }
    }

    const auto initial = state(*mapper);
    for (unsigned addr = 0U; addr < 0x8000U; ++addr) {
        REQUIRE_FALSE(mapper->maps_prg(static_cast<Address>(addr)));
        REQUIRE(mapper->read_prg(static_cast<Address>(addr)) == 0U);
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
    }
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 326 CHR latches select every populated 1 KiB bank", "[mapper326]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(0x8010U + slot), static_cast<Byte>(value));
            for (unsigned current = 0U; current < 8U; ++current) {
                CAPTURE(slot, value, current);
                REQUIRE(mapper->read_chr(static_cast<Address>(current * kChrBankSize + 0x155U)) ==
                        chr_cell(current == slot ? value & 0x7FU : 0U, 0x155U));
            }
        }
    }
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8010U + slot), static_cast<Byte>(slot * 19U + 7U));
        const unsigned bank = (slot * 19U + 7U) & 0x7FU;
        for (unsigned offset = 0U; offset < kChrBankSize; ++offset) {
            REQUIRE(mapper->read_chr(static_cast<Address>(slot * kChrBankSize + offset)) ==
                    chr_cell(bank, offset));
        }
    }

    const auto before = state(*mapper);
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        const auto original = mapper->read_chr(static_cast<Address>(addr));
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr ^ 0xA5U));
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == original);
    }
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 326 CIRAM controls are independent and cover all nametable mirrors",
          "[mapper326]") {
    auto mapper = board();
    for (unsigned table = 0U; table < 4U; ++table) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(0x8018U + table), static_cast<Byte>(value));
            for (unsigned current = 0U; current < 4U; ++current) {
                const Address expected =
                    static_cast<Address>((current == table ? value & 1U : 0U) * 0x400U + 0x2AAU);
                REQUIRE(mapper->nametable_address(
                            static_cast<Address>(0x2000U + current * 0x400U + 0x2AAU)) == expected);
            }
        }
    }

    for (unsigned selection = 0U; selection < 16U; ++selection) {
        for (unsigned table = 0U; table < 4U; ++table) {
            mapper->write_prg(static_cast<Address>(0xF018U + table),
                              static_cast<Byte>((selection >> table) & 1U));
        }
        for (unsigned addr = 0x2000U; addr < 0x3F00U; ++addr) {
            const unsigned table = ((addr - 0x2000U) >> 10U) & 3U;
            const Address expected =
                static_cast<Address>(((selection >> table) & 1U) * 0x400U + (addr & 0x03FFU));
            CAPTURE(selection, addr);
            REQUIRE(mapper->nametable_address(static_cast<Address>(addr)) == expected);
        }
    }
    for (Address addr : {0x0000U, 0x1FFFU, 0x3F00U, 0x3FFFU, 0xFFFFU}) {
        REQUIRE_FALSE(mapper->nametable_address(addr));
    }
}

TEST_CASE("Mapper 326 reset restores zero latches and owns no persistent or peripheral hardware",
          "[mapper326]") {
    auto mapper = board(false);
    const auto initial = state(*mapper);
    mapper->write_prg(0x8000U, 0x71U);
    mapper->write_prg(0xA000U, 0x62U);
    mapper->write_prg(0xC000U, 0x53U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8010U + slot), static_cast<Byte>(slot * 13U + 1U));
    }
    for (unsigned table = 0U; table < 4U; ++table) {
        mapper->write_prg(static_cast<Address>(0x8018U + table), static_cast<Byte>(table & 1U));
    }
    REQUIRE(state(*mapper) != initial);

    mapper->reset();
    REQUIRE(state(*mapper) == initial);
    mapper->write_prg(0xC000U, 0xFFU);
    REQUIRE(state(*mapper) != initial);
    mapper->power_on();
    REQUIRE(state(*mapper) == initial);

    const std::array<Byte, 4> imported{1U, 2U, 3U, 4U};
    mapper->set_battery_ram(imported);
    REQUIRE(mapper->battery_ram().empty());
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        mapper->write_expansion(addr, 0xA5U);
    }
    for (unsigned cycle = 0U; cycle < 4096U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
        REQUIRE_FALSE(mapper->irq_pending());
    }
    mapper->acknowledge_irq();
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 326 snapshots replay only bounded latch state", "[mapper326][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x71U);
    mapper->write_prg(0xA000U, 0x32U);
    mapper->write_prg(0xC000U, 0x03U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8010U + slot),
                          static_cast<Byte>(slot * 17U + 0x23U));
    }
    for (unsigned table = 0U; table < 4U; ++table) {
        mapper->write_prg(static_cast<Address>(0x8018U + table),
                          static_cast<Byte>(table == 0U || table >= 2U));
    }
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (const Address addr :
         {0x8000U, 0x9123U, 0xA456U, 0xBFFFU, 0xC789U, 0xDFFFU, 0xE000U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    for (unsigned addr = 0U; addr < 8192U; ++addr) {
        REQUIRE(copy->read_chr(static_cast<Address>(addr)) ==
                mapper->read_chr(static_cast<Address>(addr)));
    }
    for (unsigned addr = 0x2000U; addr < 0x3000U; ++addr) {
        REQUIRE(copy->nametable_address(static_cast<Address>(addr)) ==
                mapper->nametable_address(static_cast<Address>(addr)));
    }
}

TEST_CASE("Mapper 326 malformed states reject atomically", "[mapper326][state]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x75U);
    mapper->write_prg(0xA000U, 0x36U);
    mapper->write_prg(0xC000U, 0x07U);
    for (unsigned slot = 0U; slot < 8U; ++slot) {
        mapper->write_prg(static_cast<Address>(0x8010U + slot), static_cast<Byte>(slot * 11U + 3U));
    }
    mapper->write_prg(0x8018U, 1U);
    mapper->write_prg(0x801AU, 1U);
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
    for (std::size_t offset = 2U; offset < 13U; ++offset) {
        for (Byte value : {0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[offset] = value;
            CAPTURE(offset, value);
            reject(invalid);
        }
    }
    for (std::size_t offset = 13U; offset < saved.size(); ++offset) {
        for (Byte value : {2U, 0x80U, 0xFFU}) {
            auto invalid = saved;
            invalid[offset] = value;
            CAPTURE(offset, value);
            reject(invalid);
        }
    }

    for (std::size_t offset = 2U; offset < 13U; ++offset) {
        for (unsigned value = 0U; value < 128U; ++value) {
            auto valid = saved;
            valid[offset] = static_cast<Byte>(value);
            StateReader reader(valid);
            mapper->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == valid);
        }
    }
    for (std::size_t offset = 13U; offset < saved.size(); ++offset) {
        for (Byte value : {0U, 1U}) {
            auto valid = saved;
            valid[offset] = value;
            StateReader reader(valid);
            mapper->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == valid);
        }
    }
}

} // namespace
