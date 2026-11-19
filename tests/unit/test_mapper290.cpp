#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper290.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMaxPrgSize = 524288U;
constexpr std::size_t kMaxChrSize = 262144U;
constexpr Address kLatchMask = 0x7FC7U;

INesHeader make_header(std::size_t prg_size = kMaxPrgSize,
                       std::size_t chr_size = kMaxChrSize,
                       MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader header{};
    header.prg_rom_banks = static_cast<uint16_t>(prg_size / 16384U);
    header.chr_rom_banks = static_cast<uint16_t>(chr_size / 8192U);
    header.mapper_number = 290U;
    header.mirror_mode = mirror;
    header.is_nes2 = true;
    header.prg_rom_size = prg_size;
    header.chr_rom_size = chr_size;
    return header;
}

Byte prg_byte(std::size_t offset) {
    return static_cast<Byte>(offset * 29U + offset / 251U * 17U + offset / 16384U * 31U);
}

Byte chr_byte(std::size_t offset) {
    return static_cast<Byte>(offset * 23U + offset / 127U * 11U + offset / 8192U * 37U + 5U);
}

std::vector<Byte> make_prg(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = prg_byte(offset);
    }
    return data;
}

std::vector<Byte> make_chr(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = chr_byte(offset);
    }
    return data;
}

std::unique_ptr<Mapper290> make_board(std::size_t prg_size = kMaxPrgSize,
                                      std::size_t chr_size = kMaxChrSize,
                                      MirrorMode mirror = MirrorMode::Vertical) {
    REQUIRE(
        Mapper290::validate_geometry(make_header(prg_size, chr_size, mirror), prg_size, chr_size));
    return std::make_unique<Mapper290>(
        make_header(prg_size, chr_size, mirror), make_prg(prg_size), make_chr(chr_size));
}

std::vector<Byte> save(const Mapper290& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

uint16_t state_latch(const Mapper290& mapper) {
    const auto bytes = save(mapper);
    REQUIRE(bytes.size() == 12U);
    uint16_t latch = 0U;
    std::memcpy(&latch, bytes.data() + 10U, sizeof(latch));
    return latch;
}

Address latch_address(
    unsigned outer, unsigned chr, bool size_16k, bool inner, bool horizontal = false) {
    return static_cast<Address>(0x8000U | (outer << 11U) | (horizontal ? 0x0400U : 0U) |
                                ((chr >> 3U) << 8U) | (size_16k ? 0x0080U : 0U) |
                                (inner ? 0x0040U : 0U) | (chr & 7U));
}

void require_rejected_without_mutation(Mapper290& mapper, std::span<const Byte> bytes) {
    const auto before = save(mapper);
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(save(mapper) == before);
}

TEST_CASE("Mapper 290 accepts only its documented ROM-only NES 2.0 geometry", "[mapper290]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        for (const std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ}) {
            for (const std::size_t chr_size :
                 {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ}) {
                REQUIRE(Mapper290::validate_geometry(
                    make_header(prg_size, chr_size, mirror), prg_size, chr_size));
            }
        }
    }

    auto header = make_header();
    header.is_nes2 = false;
    REQUIRE_FALSE(Mapper290::validate_geometry(header, kMaxPrgSize, kMaxChrSize));
    for (const uint16_t mapper : {0U, 289U, 291U, 511U}) {
        header = make_header();
        header.mapper_number = mapper;
        REQUIRE_FALSE(Mapper290::validate_geometry(header, kMaxPrgSize, kMaxChrSize));
    }
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        header = make_header();
        header.submapper = submapper;
        REQUIRE_FALSE(Mapper290::validate_geometry(header, kMaxPrgSize, kMaxChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 16384UZ, 32767UZ, 32769UZ, 49152UZ, 1048576UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper290::validate_geometry(make_header(), size, kMaxChrSize));
    }
    for (const std::size_t size : {0UZ, 1UZ, 4096UZ, 8191UZ, 8193UZ, 12288UZ, 524288UZ}) {
        CAPTURE(size);
        REQUIRE_FALSE(Mapper290::validate_geometry(make_header(), kMaxPrgSize, size));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        header = make_header();
        switch (field) {
        case 0U:
            header.has_battery = true;
            break;
        case 1U:
            header.prg_ram_size = 8192U;
            break;
        case 2U:
            header.prg_nvram_size = 8192U;
            break;
        case 3U:
            header.chr_ram_size = 8192U;
            break;
        default:
            header.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper290::validate_geometry(header, kMaxPrgSize, kMaxChrSize));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        header = make_header();
        header.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper290::validate_geometry(header, kMaxPrgSize, kMaxChrSize));
    }
}

TEST_CASE("Mapper 290 latches address pins only at every upper CPU address", "[mapper290]") {
    auto mapper = make_board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr * 73U));
        const auto expected = addr >= 0x8000U ? static_cast<Address>(addr & kLatchMask) : 0U;
        CAPTURE(addr);
        REQUIRE(state_latch(*mapper) == expected);
        REQUIRE(mapper->mirror_mode() ==
                ((expected & 0x0400U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(static_cast<Address>(addr)) == 0xFFU);
    }

    mapper->write_prg(0xFED5U, 0x00U);
    const auto first = save(*mapper);
    mapper->write_prg(0xFED5U, 0xFFU);
    REQUIRE(save(*mapper) == first);
}

TEST_CASE("Mapper 290 decodes every PRG bank and wraps fitted ROMs after decoding", "[mapper290]") {
    for (const std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ, 262144UZ, 524288UZ}) {
        auto mapper = make_board(prg_size);
        for (unsigned outer = 0U; outer < 16U; ++outer) {
            for (bool size_16k : {false, true}) {
                for (unsigned inner = 0U; inner < (size_16k ? 2U : 1U); ++inner) {
                    mapper->write_prg(latch_address(outer, 0U, size_16k, inner != 0U), 0xA5U);
                    const std::size_t bank = size_16k ? outer * 2U + inner : outer;
                    const std::size_t unit = size_16k ? 16384UZ : 32768UZ;
                    const Address address_mask = size_16k ? 0x3FFFU : 0x7FFFU;
                    for (const Address addr :
                         {0x8000U, 0x8001U, 0x9ABCU, 0xBFFFU, 0xC000U, 0xC001U, 0xDABCU, 0xFFFFU}) {
                        const std::size_t offset =
                            (bank * unit + (addr & address_mask)) & (prg_size - 1U);
                        CAPTURE(prg_size, outer, size_16k, inner, addr);
                        REQUIRE(mapper->read_prg(addr) == prg_byte(offset));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 290 decodes the split five-bit CHR bank and protects ROM", "[mapper290]") {
    for (const std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ, 262144UZ}) {
        auto mapper = make_board(kMaxPrgSize, chr_size);
        for (unsigned bank = 0U; bank < 32U; ++bank) {
            mapper->write_prg(latch_address(0U, bank, false, false), 0U);
            for (const Address addr : {0x0000U, 0x0001U, 0x0123U, 0x1FFFU, 0x2123U, 0x3FFFU}) {
                const std::size_t offset = (bank * 8192UZ + (addr & 0x1FFFU)) & (chr_size - 1U);
                CAPTURE(chr_size, bank, addr);
                REQUIRE(mapper->read_chr(addr) == chr_byte(offset));
                mapper->write_chr(addr, static_cast<Byte>(addr ^ 0xA5U));
                REQUIRE(mapper->read_chr(addr) == chr_byte(offset));
            }
        }
    }
}

TEST_CASE("Mapper 290 exposes no RAM persistence IRQ audio or expansion device", "[mapper290]") {
    auto mapper = make_board();
    REQUIRE(save(*mapper).size() == 12U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->audio_output() == 0.0F);
    for (const Address addr : {0x4020U, 0x4100U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
        mapper->write_expansion(addr, 0xFFU);
    }
    mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA5U));
    mapper->clock_cpu();
    mapper->clock_irq_counter();
    mapper->clock_audio();
    mapper->acknowledge_irq();
    REQUIRE(save(*mapper) == save(*make_board()));
    REQUIRE(mapper->battery_ram().empty());
}

TEST_CASE("Mapper 290 reset and power-on select the deterministic zero latch", "[mapper290]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = make_board(kMaxPrgSize, kMaxChrSize, initial);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->write_prg(0xFFFFU, 0xFFU);
        REQUIRE(state_latch(*mapper) == kLatchMask);
        mapper->reset();
        REQUIRE(state_latch(*mapper) == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_prg(0x8000U) == prg_byte(0U));
        REQUIRE(mapper->read_chr(0U) == chr_byte(0U));

        mapper->write_prg(0xFFFFU, 0U);
        mapper->power_on();
        REQUIRE(state_latch(*mapper) == 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        REQUIRE(mapper->read_prg(0xFFFFU) == prg_byte(32767U));
        REQUIRE(mapper->read_chr(0x1FFFU) == chr_byte(8191U));
    }
}

TEST_CASE("Mapper 290 snapshots are bounded replayable and atomically validated",
          "[mapper290][state]") {
    auto mapper = make_board();
    auto copy = make_board();
    mapper->write_prg(latch_address(13U, 29U, true, true, true), 0x5AU);
    const auto saved = save(*mapper);
    REQUIRE(saved.size() == 12U);

    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(save(*copy) == saved);
    for (const Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    REQUIRE(copy->read_chr(0x0123U) == mapper->read_chr(0x0123U));
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        require_rejected_without_mutation(*mapper, std::span(saved).first(size));
    }
    for (const std::size_t offset : {0UZ, 1UZ, 2UZ, 5UZ, 6UZ, 9UZ}) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        require_rejected_without_mutation(*mapper, invalid);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        if ((kLatchMask & (1U << bit)) != 0U) {
            continue;
        }
        auto invalid = saved;
        uint16_t latch = 0U;
        std::memcpy(&latch, invalid.data() + 10U, sizeof(latch));
        latch ^= static_cast<uint16_t>(1U << bit);
        std::memcpy(invalid.data() + 10U, &latch, sizeof(latch));
        require_rejected_without_mutation(*mapper, invalid);
    }
    for (const std::size_t prg_size : {32768UZ, 65536UZ, 131072UZ, 262144UZ}) {
        auto other = make_board(prg_size);
        require_rejected_without_mutation(*other, saved);
    }
    for (const std::size_t chr_size : {8192UZ, 16384UZ, 32768UZ, 65536UZ, 131072UZ}) {
        auto other = make_board(kMaxPrgSize, chr_size);
        require_rejected_without_mutation(*other, saved);
    }

    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    REQUIRE(save(*mapper) == saved);
}

} // namespace
