#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper177.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(Byte submapper = 0U,
                  std::size_t wram_size = 8192U,
                  MirrorMode mirror = MirrorMode::Vertical,
                  bool legacy = false) {
    INesHeader h{};
    h.mapper_number = 177U;
    h.submapper = submapper;
    h.mirror_mode = mirror;
    h.region = Region::NTSC;
    h.has_battery = true;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.prg_nvram_size = legacy ? 0U : wram_size;
    h.chr_ram_size = submapper == 1U ? 16384U : 8192U;
    return h;
}

std::vector<Byte> prg_rom(std::size_t size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / 32768U);
    return data;
}

std::unique_ptr<Henggedianzi177> board(std::size_t prg_size = 131072U,
                                       Byte submapper = 0U,
                                       std::size_t wram_size = 8192U,
                                       MirrorMode mirror = MirrorMode::Vertical,
                                       bool legacy = false) {
    return std::make_unique<Henggedianzi177>(
        header(submapper, wram_size, mirror, legacy), prg_rom(prg_size), std::vector<Byte>{});
}

std::vector<Byte> state(const Henggedianzi177& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 177 selects all PRG banks without conflicts and controls submapper 0 CIRAM",
          "[mapper177]") {
    for (bool legacy : {false, true}) {
        for (std::size_t size = 32768U; size <= 1048576U; size *= 2U) {
            auto mapper = board(size, 0U, 8192U, MirrorMode::Horizontal, legacy);
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->write_prg(0xFFFFU, static_cast<Byte>(value));
                const auto expected = static_cast<Byte>((value & 31U) & (size / 32768U - 1U));
                for (Address addr : {0x8000U, 0xBFFFU, 0xC000U, 0xFFFFU})
                    REQUIRE(mapper->read_prg(addr) == expected);
                REQUIRE(mapper->mirror_mode() ==
                        ((value & 0x20U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            }
        }
    }

    auto mapper = board(1048576U);
    mapper->write_prg(0x8000U, 0U);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    mapper->write_prg(0x8000U, 0xFFU);
    REQUIRE(mapper->read_prg(0x8000U) == 31U);
    REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
}

TEST_CASE("Mapper 177 decodes the complete CPU register range and no other CPU window",
          "[mapper177]") {
    auto mapper = board(131072U, 0U, 32768U);
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->write_prg(0x8000U, 0U);
        mapper->write_prg(static_cast<Address>(addr), 0xADU);
        REQUIRE(mapper->read_prg(0x8000U) == (addr >= 0x8000U ? 1U : 0U));
        REQUIRE(mapper->mirror_mode() ==
                (addr >= 0x8000U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
        REQUIRE_FALSE(mapper->maps_expansion(static_cast<Address>(addr)));
    }
    REQUIRE(mapper->read_prg(0U) == 0U);
    REQUIRE(mapper->read_prg(0x4020U) == 0U);
    REQUIRE(mapper->read_prg(0x5FFFU) == 0U);
}

TEST_CASE("Mapper 177 banks every WRAM cell and imports only exact battery images", "[mapper177]") {
    for (std::size_t size : {8192UZ, 16384UZ, 32768UZ}) {
        auto mapper = board(131072U, 0U, size);
        REQUIRE(mapper->battery_ram().size() == size);
        const auto banks = size / 8192U;
        for (unsigned bank = 0U; bank < banks; ++bank) {
            mapper->write_prg(0x8000U, static_cast<Byte>(bank << 6U));
            for (unsigned offset = 0U; offset < 8192U; ++offset) {
                const auto addr = static_cast<Address>(0x6000U + offset);
                mapper->write_prg(addr, static_cast<Byte>(bank * 0x40U + offset));
            }
        }
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->write_prg(0xFFFFU, static_cast<Byte>((bank << 6U) | 0x3FU));
            const auto physical = bank & (banks - 1U);
            for (unsigned offset = 0U; offset < 8192U; ++offset) {
                const auto addr = static_cast<Address>(0x6000U + offset);
                REQUIRE(mapper->read_prg(addr) == static_cast<Byte>(physical * 0x40U + offset));
            }
        }

        std::vector<Byte> imported(size);
        for (std::size_t i = 0U; i < size; ++i)
            imported[i] = static_cast<Byte>(i * 13U);
        mapper->set_battery_ram(imported);
        REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
        const auto before = state(*mapper);
        std::vector<Byte> wrong(size + 1U, 0xA7U);
        mapper->set_battery_ram(wrong);
        REQUIRE(state(*mapper) == before);
        wrong.pop_back();
        wrong.pop_back();
        mapper->set_battery_ram(wrong);
        REQUIRE(state(*mapper) == before);
        mapper->reset();
        REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
        mapper->power_on();
        REQUIRE(std::ranges::equal(mapper->battery_ram(), imported));
    }
}

TEST_CASE("Mapper 177 submapper 0 uses fixed writable 8 KiB CHR RAM", "[mapper177]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        mapper->write_chr(static_cast<Address>(addr), static_cast<Byte>(addr * 7U));
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        for (Address addr : {0U, 0x0FFFU, 0x1000U, 0x1FFFU})
            REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(addr * 7U));
        REQUIRE_FALSE(mapper->observes_ppu_address());
    }
    const auto saved = state(*mapper);
    mapper->reset();
    REQUIRE(state(*mapper) != saved);
    REQUIRE(mapper->read_chr(0x1234U) == static_cast<Byte>(0x1234U * 7U));
    mapper->power_on();
    for (unsigned addr = 0U; addr < 8192U; ++addr)
        REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
}

TEST_CASE("Mapper 177 submapper 1 latches nametable A8 A9 only in 16 KiB CHR mode", "[mapper177]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(131072U, 1U, 8192U, mirror);
        mapper->write_prg(0x8000U, 0x20U);
        REQUIRE(mapper->observes_ppu_address());
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->observe_ppu_address(static_cast<Address>(0x2000U | (bank << 8U) | 0x5AU));
            for (unsigned addr = 0U; addr < 4096U; ++addr)
                mapper->write_chr(static_cast<Address>(addr),
                                  static_cast<Byte>(bank * 0x40U + addr));
        }
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            for (Address nametable :
                 {static_cast<Address>(0x2000U | (bank << 8U)),
                  static_cast<Address>((0x2B00U & ~(3U << 8U)) | (bank << 8U)),
                  static_cast<Address>(0x3400U | (bank << 8U)),
                  static_cast<Address>((0x3EFFU & ~(3U << 8U)) | (bank << 8U))}) {
                mapper->observe_ppu_address(nametable);
                for (Address addr : {0U, 0x0123U, 0x0FFFU})
                    REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(bank * 0x40U + addr));
                for (Address addr : {0x1000U, 0x1123U, 0x1FFFU}) {
                    const auto offset = addr & 0x0FFFU;
                    REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(3U * 0x40U + offset));
                }
            }
            mapper->observe_ppu_address(0x1FFFU);
            mapper->observe_ppu_address(0x3F00U);
            mapper->observe_ppu_address(0x3FFFU);
            REQUIRE(mapper->read_chr(0U) == static_cast<Byte>(bank * 0x40U));
        }

        mapper->write_prg(0xFFFFU, 0U);
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE(mapper->mirror_mode() == mirror);
        for (Address addr : {0U, 0x0FFFU, 0x1000U, 0x1FFFU}) {
            const auto offset = addr & 0x0FFFU;
            const auto expected = static_cast<Byte>(offset + (addr >= 0x1000U ? 0x40U : 0U));
            REQUIRE(mapper->read_chr(addr) == expected);
        }
        const auto normal = state(*mapper);
        mapper->observe_ppu_address(0x2300U);
        REQUIRE(state(*mapper) == normal);
        mapper->write_chr(0U, 0xA7U);
        mapper->write_chr(0x1000U, 0xB8U);
        mapper->write_prg(0x8000U, 0x20U);
        mapper->observe_ppu_address(0x2000U);
        REQUIRE(mapper->read_chr(0U) == 0xA7U);
        mapper->observe_ppu_address(0x2100U);
        REQUIRE(mapper->read_chr(0U) == 0xB8U);
        REQUIRE(mapper->read_chr(0x1000U) == 0xC0U);
        mapper->write_prg(0x8000U, 0xE0U);
        REQUIRE(mapper->mirror_mode() == mirror);
        mapper->reset();
        REQUIRE_FALSE(mapper->observes_ppu_address());
        REQUIRE(mapper->read_chr(0U) == 0xA7U);
        mapper->power_on();
        mapper->write_prg(0x8000U, 0x20U);
        for (unsigned bank = 0U; bank < 4U; ++bank) {
            mapper->observe_ppu_address(static_cast<Address>(0x2000U | (bank << 8U)));
            for (unsigned addr = 0U; addr < 4096U; ++addr) {
                REQUIRE(mapper->read_chr(static_cast<Address>(addr)) == 0U);
                REQUIRE(mapper->read_chr(static_cast<Address>(0x1000U + addr)) == 0U);
            }
        }
    }
}

TEST_CASE("Mapper 177 validates PRG RAM CHR RAM battery submapper and CIRAM geometry",
          "[mapper177]") {
    for (bool legacy : {false, true}) {
        for (std::size_t prg_size = 32768U; prg_size <= 1048576U; prg_size *= 2U) {
            for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                REQUIRE(Henggedianzi177::validate_geometry(
                    header(0U, 8192U, mirror, legacy), prg_size, 0U));
                if (!legacy) {
                    for (Byte submapper : {0U, 1U})
                        for (std::size_t wram_size : {8192UZ, 16384UZ, 32768UZ})
                            REQUIRE(Henggedianzi177::validate_geometry(
                                header(submapper, wram_size, mirror), prg_size, 0U));
                }
            }
        }
    }
    for (auto size : {0UZ, 16384UZ, 32767UZ, 32769UZ, 49152UZ, 1048577UZ, 2097152UZ})
        REQUIRE_FALSE(Henggedianzi177::validate_geometry(header(), size, 0U));
    for (auto size : {1UZ, 8192UZ, 16384UZ})
        REQUIRE_FALSE(Henggedianzi177::validate_geometry(header(), 32768U, size));
    for (Byte submapper = 2U; submapper < 16U; ++submapper)
        REQUIRE_FALSE(Henggedianzi177::validate_geometry(header(submapper, 8192U), 32768U, 0U));
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper})
        REQUIRE_FALSE(Henggedianzi177::validate_geometry(header(0U, 8192U, mirror), 32768U, 0U));
    for (bool legacy : {false, true}) {
        auto h = header(0U, 8192U, MirrorMode::Vertical, legacy);
        h.has_battery = false;
        REQUIRE_FALSE(Henggedianzi177::validate_geometry(h, 32768U, 0U));
    }
    for (unsigned field = 0U; field < 4U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 8192U;
            break;
        case 1U:
            h.prg_nvram_size = 4096U;
            break;
        case 2U:
            h.chr_ram_size = 4096U;
            break;
        case 3U:
            h.chr_nvram_size = 8192U;
            break;
        default:
            break;
        }
        REQUIRE_FALSE(Henggedianzi177::validate_geometry(h, 32768U, 0U));
    }
    auto sub1 = header(1U);
    sub1.chr_ram_size = 8192U;
    REQUIRE_FALSE(Henggedianzi177::validate_geometry(sub1, 32768U, 0U));
}

TEST_CASE("Mapper 177 snapshots replay all RAM and reject mismatches without mutation",
          "[mapper177]") {
    for (Byte submapper : {0U, 1U}) {
        for (std::size_t wram_size : {8192UZ, 16384UZ, 32768UZ}) {
            auto mapper = board(1048576U, submapper, wram_size);
            mapper->write_prg(0x8000U, static_cast<Byte>(0xC0U | (submapper << 5U) | 0x1FU));
            mapper->observe_ppu_address(0x2200U);
            mapper->write_prg(0x6123U, 0xA7U);
            mapper->write_chr(0x0123U, 0x5AU);
            const auto saved = state(*mapper);
            REQUIRE(saved.size() == 20U + wram_size + (submapper == 1U ? 16384U : 8192U));

            auto copy = board(1048576U, submapper, wram_size);
            StateReader reader(saved);
            copy->load_state(reader);
            REQUIRE(reader.ok());
            REQUIRE(reader.remaining() == 0U);
            REQUIRE(state(*copy) == saved);
            REQUIRE(copy->read_prg(0x8000U) == 31U);
            REQUIRE(copy->read_prg(0x6123U) == 0xA7U);
            REQUIRE(copy->read_chr(0x0123U) == 0x5AU);

            const auto before = state(*mapper);
            for (std::size_t length : {0UZ,
                                       1UZ,
                                       2UZ,
                                       3UZ,
                                       4UZ,
                                       11UZ,
                                       12UZ,
                                       12UZ + wram_size,
                                       20UZ + wram_size,
                                       saved.size() - 1U}) {
                StateReader truncated(std::span<const Byte>(saved).first(length));
                mapper->load_state(truncated);
                REQUIRE_FALSE(truncated.ok());
                REQUIRE(state(*mapper) == before);
            }
            for (unsigned bit = 0U; bit < 16U; ++bit) {
                auto invalid = saved;
                invalid[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
                StateReader bad(invalid);
                mapper->load_state(bad);
                REQUIRE_FALSE(bad.ok());
                REQUIRE(state(*mapper) == before);
            }
            auto invalid = saved;
            invalid[3] = 4U;
            StateReader bad_bank(invalid);
            mapper->load_state(bad_bank);
            REQUIRE_FALSE(bad_bank.ok());
            REQUIRE(state(*mapper) == before);

            for (unsigned latch = 0U; latch < 256U; ++latch) {
                invalid = saved;
                invalid[2] = static_cast<Byte>(latch);
                StateReader replay(invalid);
                mapper->load_state(replay);
                REQUIRE(replay.ok());
                REQUIRE(state(*mapper) == invalid);
            }
            StateReader restore(saved);
            mapper->load_state(restore);
            REQUIRE(restore.ok());

            auto mismatch = board(131072U, submapper, wram_size);
            const auto mismatch_before = state(*mismatch);
            StateReader wrong_geometry(saved);
            mismatch->load_state(wrong_geometry);
            REQUIRE_FALSE(wrong_geometry.ok());
            REQUIRE(state(*mismatch) == mismatch_before);

            for (unsigned profile = 0U; profile < 3U; ++profile) {
                auto other =
                    board(1048576U,
                          profile == 0U ? submapper ^ 1U : submapper,
                          profile == 1U ? (wram_size == 8192U ? 16384U : 8192U) : wram_size,
                          profile == 2U ? MirrorMode::Horizontal : MirrorMode::Vertical);
                const auto other_before = state(*other);
                StateReader wrong_profile(saved);
                other->load_state(wrong_profile);
                REQUIRE_FALSE(wrong_profile.ok());
                REQUIRE(state(*other) == other_before);
            }
            for (std::size_t offset : {4UZ, 12UZ + wram_size}) {
                auto bad_length = saved;
                bad_length[offset] = 0xFFU;
                StateReader bad(bad_length);
                mapper->load_state(bad);
                REQUIRE_FALSE(bad.ok());
                REQUIRE(state(*mapper) == before);
            }
        }
    }
}

TEST_CASE("Mapper 177 optional LPC profile uses the documented nibble interface and state tag",
          "[mapper177][lpc]") {
    auto mapper = board();
    REQUIRE(mapper->configure_board_profile("standard"));
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->configure_board_profile("lpc"));
    REQUIRE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE(mapper->has_expansion_audio());
    for (unsigned addr = 0x4020U; addr < 0x6000U; ++addr) {
        const bool mapped = addr >= 0x5000U;
        REQUIRE(mapper->maps_expansion(static_cast<Address>(addr)) == mapped);
        REQUIRE(mapper->expansion_read_mask(static_cast<Address>(addr)) == (mapped ? 0x40U : 0U));
    }
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    for (unsigned cycle = 0U; cycle < 37287U; ++cycle)
        mapper->clock_audio();
    REQUIRE(mapper->read_expansion(0x5FFFU) == 0x40U);

    const auto speech_offset = 20U + 8192U + 8192U;
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->reset();
        for (unsigned cycle = 0U; cycle < 40000U; ++cycle)
            mapper->clock_audio();
        const auto before = state(*mapper);
        mapper->write_expansion(0x5000U, static_cast<Byte>(value));
        const auto after = state(*mapper);
        if ((value & 0x50U) == 0x50U)
            REQUIRE(after[speech_offset + 19U] == 4U);
        else if ((value & 0x50U) == 0U)
            REQUIRE(mapper->read_expansion(0x5000U) == 0U);
        else
            REQUIRE(after == before);
        REQUIRE(mapper->read_prg(0x8000U) == 0U);
    }

    mapper->set_region(Region::PAL);
    mapper->write_expansion(0x5000U, 0x5AU);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_chr(0U, 0x5AU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == speech_offset + 129U);
    auto copy = board();
    REQUIRE(copy->configure_board_profile("lpc"));
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(state(*copy) == saved);

    auto plain = board();
    const auto plain_before = state(*plain);
    StateReader mismatch(saved);
    plain->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*plain) == plain_before);
    auto malformed = saved;
    malformed.back() = 0x7FU;
    malformed[malformed.size() - 2U] = 0xFFU;
    const auto before = state(*mapper);
    StateReader bad(malformed);
    mapper->load_state(bad);
    REQUIRE_FALSE(bad.ok());
    REQUIRE(state(*mapper) == before);
}

} // namespace
