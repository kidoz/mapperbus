#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper300.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgBankSize = 16384U;
constexpr std::size_t kChrBankSize = 8192U;

INesHeader header(bool vertical = true) {
    INesHeader result{};
    result.mapper_number = 300U;
    result.is_nes2 = true;
    result.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return result;
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + offset / 251U * 17U + offset / kPrgBankSize * 61U);
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>(offset * 43U + offset / 509U * 31U + offset / kChrBankSize * 47U);
}

std::vector<Byte> rom(std::size_t size, bool chr) {
    std::vector<Byte> bytes(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        bytes[offset] = chr ? chr_cell(offset) : prg_cell(offset);
    }
    return bytes;
}

std::unique_ptr<Mapper300> board(INesHeader h = header(),
                                 std::size_t prg_size = 131072U,
                                 std::size_t chr_size = 65536U) {
    REQUIRE(Mapper300::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper300>(h, rom(prg_size, false), rom(chr_size, true));
}

std::vector<Byte> state(const Mapper300& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 300 decodes every CPU address and ignores write data", "[mapper300]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(0x8000U, 0xA7U);
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 37U + addr / 256U));
        const auto expected = static_cast<Byte>(addr >= 0x8000U ? addr & 0x1DU : 0U);
        REQUIRE(state(*mapper)[2U] == expected);
        REQUIRE(mapper->mirror_mode() ==
                ((expected & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
    }

    for (unsigned bank = 0U; bank < 8U; ++bank) {
        for (unsigned mirror = 0U; mirror < 2U; ++mirror) {
            const auto latch = static_cast<Byte>((bank << 2U) | mirror);
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->write_prg(static_cast<Address>(0x8000U | latch), static_cast<Byte>(value));
                REQUIRE(state(*mapper)[2U] == latch);
            }
        }
    }
}

TEST_CASE("Mapper 300 mirrors one 16 KiB PRG bank and shares its CHR bank", "[mapper300]") {
    for (std::size_t prg_size : {16384UZ, 32768UZ, 65536UZ, 131072UZ}) {
        for (std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ}) {
            auto mapper = board(header(), prg_size, chr_size);
            CAPTURE(prg_size, chr_size);
            for (unsigned bank = 0U; bank < 8U; ++bank) {
                CAPTURE(bank);
                mapper->write_prg(static_cast<Address>(0x8000U | (bank << 2U)), 0xFFU);
                for (Address addr : {0x8000U, 0x8001U, 0x9ABCu, 0xBFFFU}) {
                    const std::size_t offset = (bank * kPrgBankSize + (addr & 0x3FFFU)) % prg_size;
                    REQUIRE(mapper->read_prg(addr) == prg_cell(offset));
                    REQUIRE(mapper->read_prg(static_cast<Address>(addr + 0x4000U)) ==
                            prg_cell(offset));
                    REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
                }
                for (Address addr = 0U; addr < 0x2000U; ++addr) {
                    CAPTURE(addr);
                    const std::size_t offset = (bank * kChrBankSize + addr) % chr_size;
                    REQUIRE(mapper->read_chr(addr) == chr_cell(offset));
                }
            }
        }
    }
}

TEST_CASE("Mapper 300 keeps CHR ROM immutable and has no RAM or persistence", "[mapper300]") {
    auto mapper = board();
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        mapper->write_prg(static_cast<Address>(0x8000U | (bank << 2U)), 0U);
        for (Address addr = 0U; addr < 0x2000U; ++addr) {
            const auto before = mapper->read_chr(addr);
            mapper->write_chr(addr, static_cast<Byte>(before ^ 0xFFU));
            REQUIRE(mapper->read_chr(addr) == before);
        }
    }
    REQUIRE(mapper->battery_ram().empty());
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 300 leaves all low CPU windows unmapped and has no IRQ or audio", "[mapper300]") {
    auto mapper = board();
    mapper->write_prg(0x801DU, 0U);
    const auto saved = state(*mapper);
    for (unsigned addr = 0x4020U; addr < 0x8000U; ++addr) {
        const auto address = static_cast<Address>(addr);
        REQUIRE_FALSE(mapper->maps_prg(address));
        REQUIRE_FALSE(mapper->maps_expansion(address));
        mapper->write_prg(address, 0xFFU);
        mapper->write_expansion(address, 0xFFU);
    }
    REQUIRE(state(*mapper) == saved);
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (unsigned cycle = 0U; cycle < 1000U; ++cycle) {
        mapper->clock_cpu();
        mapper->clock_irq_counter();
        mapper->clock_audio();
    }
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(state(*mapper) == saved);
}

TEST_CASE("Mapper 300 reset and cold power select bank zero and vertical CIRAM", "[mapper300]") {
    for (bool vertical : {false, true}) {
        auto mapper = board(header(vertical));
        const auto initial = state(*mapper);
        REQUIRE(initial.size() == 3U);
        REQUIRE(mapper->read_prg(0x8000U) == prg_cell(0U));
        REQUIRE(mapper->read_prg(0xFFFFU) == prg_cell(0x3FFFU));
        REQUIRE(mapper->read_chr(0U) == chr_cell(0U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);

        mapper->write_prg(0xFFFDU, 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->reset();
        REQUIRE(state(*mapper) == initial);
        mapper->write_prg(0xFFFCU, 0U);
        mapper->power_on();
        REQUIRE(state(*mapper) == initial);
        REQUIRE(mapper->read_prg(0xC000U) == prg_cell(0U));
        REQUIRE(mapper->read_chr(0x1FFFU) == chr_cell(0x1FFFU));
    }
}

TEST_CASE("Mapper 300 snapshots are bounded, complete and atomic", "[mapper300][state]") {
    for (unsigned bank = 0U; bank < 8U; ++bank) {
        for (unsigned mirror = 0U; mirror < 2U; ++mirror) {
            const auto latch = static_cast<Byte>((bank << 2U) | mirror);
            auto mapper = board();
            mapper->write_prg(static_cast<Address>(0x8000U | latch), 0U);
            const auto saved = state(*mapper);
            REQUIRE(saved.size() == 3U);

            auto copy = board(header(false), 16384U, 8192U);
            StateReader reader(saved);
            copy->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(reader.remaining() == 0U);
            REQUIRE(state(*copy) == saved);
            REQUIRE(copy->read_prg(0x8000U) == prg_cell((bank * kPrgBankSize) % 16384U));
            REQUIRE(copy->read_prg(0xFFFFU) == prg_cell((bank * kPrgBankSize + 0x3FFFU) % 16384U));
            REQUIRE(copy->read_chr(0x1234U) == chr_cell((bank * kChrBankSize + 0x1234U) % 8192U));
            REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
        }
    }

    auto mapper = board();
    const auto saved = state(*mapper);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        mapper->write_prg(0x801DU, 0U);
        const auto before = state(*mapper);
        StateReader truncated(std::span{saved}.first(size));
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == before);
    }
    for (std::size_t offset : {0UZ, 1UZ}) {
        auto invalid = saved;
        invalid[offset] ^= 0xFFU;
        mapper->write_prg(0x801CU, 0U);
        const auto before = state(*mapper);
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == before);
    }
    for (unsigned latch = 0U; latch < 256U; ++latch) {
        if ((latch & ~0x1DU) == 0U) {
            continue;
        }
        auto invalid = saved;
        invalid[2U] = static_cast<Byte>(latch);
        mapper->write_prg(0x801DU, 0U);
        const auto before = state(*mapper);
        StateReader reader(invalid);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == before);
    }
}

TEST_CASE("Mapper 300 validates its complete ROM and memory envelope", "[mapper300]") {
    for (bool vertical : {false, true}) {
        for (std::size_t prg_size : {16384UZ, 32768UZ, 65536UZ, 131072UZ}) {
            for (std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ}) {
                REQUIRE(Mapper300::validate_geometry(header(vertical), prg_size, chr_size));
            }
        }
    }
    for (unsigned kind = 0U; kind < 24U; ++kind) {
        auto h = header();
        std::size_t prg_size = 131072U;
        std::size_t chr_size = 65536U;
        switch (kind) {
        case 0U:
            h.is_nes2 = false;
            break;
        case 1U:
            h.mapper_number = 299U;
            break;
        case 2U:
            h.mapper_number = 301U;
            break;
        case 3U:
            h.submapper = 1U;
            break;
        case 4U:
            h.submapper = 15U;
            break;
        case 5U:
            h.has_battery = true;
            break;
        case 6U:
            h.prg_ram_size = 8192U;
            break;
        case 7U:
            h.prg_nvram_size = 8192U;
            break;
        case 8U:
            h.chr_ram_size = 8192U;
            break;
        case 9U:
            h.chr_nvram_size = 8192U;
            break;
        case 10U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 11U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        case 12U:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        case 13U:
            prg_size = 0U;
            break;
        case 14U:
            prg_size = 8192U;
            break;
        case 15U:
            prg_size = 16385U;
            break;
        case 16U:
            prg_size = 49152U;
            break;
        case 17U:
            prg_size = 262144U;
            break;
        case 18U:
            chr_size = 0U;
            break;
        case 19U:
            chr_size = 4096U;
            break;
        case 20U:
            chr_size = 8193U;
            break;
        case 21U:
            chr_size = 24576U;
            break;
        case 22U:
            chr_size = 131072U;
            break;
        default:
            prg_size = 131071U;
            break;
        }
        INFO("kind=" << kind);
        REQUIRE_FALSE(Mapper300::validate_geometry(h, prg_size, chr_size));
    }
}

} // namespace
