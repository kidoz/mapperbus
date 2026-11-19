#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/ffe_super_magic_card.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kStateSize = 823364U;

INesHeader header(Byte submapper = 0U, bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 17U;
    h.submapper = submapper;
    h.is_nes2 = !legacy;
    h.prg_ram_size = legacy ? 0U : 32768U;
    h.chr_ram_size = legacy ? 0U : 262144U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
}

Byte chr_8k_cell(unsigned bank, unsigned offset) {
    return chr_cell(bank * 8U + offset / 1024U, offset % 1024U);
}

std::vector<Byte> prg_image(std::size_t size = 524288U) {
    std::vector<Byte> data(size);
    for (unsigned i = 0U; i < size; ++i)
        data[i] = prg_cell(i / 8192U, i % 8192U);
    return data;
}

std::unique_ptr<FfeSuperMagicCard> board(Byte submapper = 0U,
                                         bool legacy = false,
                                         bool vertical = true,
                                         std::size_t size = 524288U) {
    const auto h = header(submapper, legacy, vertical);
    REQUIRE(FfeSuperMagicCard::validate_geometry(h, size, 0U));
    return std::make_unique<FfeSuperMagicCard>(h, prg_image(size), std::vector<Byte>{});
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void expect_prg_8k(Mapper& mapper, Address addr, unsigned bank) {
    INFO("PRG addr=" << addr << " bank=" << bank);
    REQUIRE(mapper.read_prg(addr) == prg_cell(bank, addr & 0x1FFFU));
}

void expect_prg_16k(Mapper& mapper, Address addr, unsigned bank) {
    expect_prg_8k(mapper, addr, bank * 2U + ((addr >> 13U) & 1U));
}

void fill_chr_8k(Mapper& mapper) {
    mapper.write_expansion(0x4500U, 0x46U);
    mapper.write_expansion(0x43FCU, 0U);
    mapper.write_expansion(0x42FFU, 0U);
    for (unsigned bank = 0U; bank < 4U; ++bank) {
        mapper.write_prg(0x8000U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < 8192U; ++addr)
            mapper.write_chr(static_cast<Address>(addr), chr_8k_cell(bank, addr));
    }
}

void fill_chr_1k(Mapper& mapper) {
    mapper.write_expansion(0x4500U, 0x47U);
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        mapper.write_expansion(0x4510U, static_cast<Byte>(bank));
        for (unsigned addr = 0U; addr < 1024U; ++addr)
            mapper.write_chr(static_cast<Address>(addr), chr_cell(bank, addr));
    }
}

TEST_CASE("Mapper 017 initializes the extracted Super Magic Card play image", "[mapper017]") {
    for (std::size_t size : {32768UZ, 65536UZ, 524288UZ})
        for (bool legacy : {false, true})
            for (bool vertical : {false, true})
                for (Byte sub = 0U; sub < 4U; ++sub) {
                    if (legacy && sub != 0U)
                        continue;
                    auto mapper = board(sub, legacy, vertical, size);
                    const auto banks = size / 8192U;
                    for (unsigned slot = 0U; slot < 4U; ++slot)
                        expect_prg_8k(*mapper,
                                      static_cast<Address>(0x8000U + slot * 8192U),
                                      banks - 4U + slot);
                    REQUIRE(mapper->read_expansion(0x5500U) == 0x47U);
                    REQUIRE(mapper->read_expansion(0x5504U) == banks - 4U);
                    REQUIRE(mapper->read_expansion(0x5507U) == banks - 1U);
                    REQUIRE(mapper->read_expansion(0x4500U) ==
                            static_cast<Byte>(vertical ? 0x63U : 0x73U));
                    REQUIRE(mapper->read_expansion(0x4501U) == 0U);
                    REQUIRE(mapper->mirror_mode() ==
                            (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
                    REQUIRE(mapper->read_prg(0x6000U) == 0U);
                    REQUIRE(mapper->read_chr(0U) == 0U);
                    REQUIRE(mapper->battery_ram().empty());
                    REQUIRE_FALSE(mapper->has_expansion_audio());
                    REQUIRE_FALSE(mapper->irq_pending());
                    REQUIRE(state(*mapper).size() == kStateSize);
                }

    auto sparse = board(0U, false, true, 32768U);
    sparse->write_expansion(0x4504U, 63U);
    REQUIRE(sparse->read_prg(0x8000U) == 0U);
}

TEST_CASE("Mapper 017 latch modes decode every value without bus conflicts", "[mapper017]") {
    auto mapper = board();
    fill_chr_8k(*mapper);
    for (unsigned mode = 0U; mode < 8U; ++mode) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_expansion(0x43FFU, 0U);
            mapper->write_expansion(0x42FFU, static_cast<Byte>((mode << 5U) | 0x10U));
            mapper->write_prg(0x8000U, static_cast<Byte>(value));
            const auto latch = value & 0x3FU;
            switch (mode) {
            case 0U:
                expect_prg_16k(*mapper, 0x8000U, latch & 7U);
                expect_prg_16k(*mapper, 0xC000U, 7U);
                break;
            case 1U:
                expect_prg_16k(*mapper, 0x8000U, (latch >> 2U) & 15U);
                expect_prg_16k(*mapper, 0xC000U, 7U);
                break;
            case 2U:
                expect_prg_16k(*mapper, 0x8000U, latch & 15U);
                expect_prg_16k(*mapper, 0xC000U, 15U);
                break;
            case 3U:
                expect_prg_16k(*mapper, 0x8000U, 15U);
                expect_prg_16k(*mapper, 0xC000U, latch & 15U);
                break;
            case 4U:
                expect_prg_8k(*mapper, 0x8000U, ((latch >> 4U) & 3U) * 4U);
                expect_prg_8k(*mapper, 0xE000U, ((latch >> 4U) & 3U) * 4U + 3U);
                break;
            default:
                expect_prg_8k(*mapper, 0x8000U, 12U);
                expect_prg_8k(*mapper, 0xE000U, 15U);
                break;
            }
            const unsigned chr_bank = mode == 0U || mode == 2U || mode == 7U ? 0U
                                      : mode == 6U                           ? latch & 1U
                                                                             : latch & 3U;
            INFO("CHR mode=" << mode << " value=" << value << " bank=" << chr_bank);
            REQUIRE(mapper->read_chr(0x0123U) == chr_8k_cell(chr_bank, 0x0123U));
            mapper->write_chr(0x0123U, 0xA7U);
            REQUIRE(mapper->read_chr(0x0123U) ==
                    (mode < 4U ? 0xA7U : chr_8k_cell(chr_bank, 0x0123U)));
            if (mode < 4U)
                mapper->write_chr(0x0123U, chr_8k_cell(chr_bank, 0x0123U));
        }
    }
}

TEST_CASE("Mapper 017 mode aliases control protection CIRAM and shared CHR latch", "[mapper017]") {
    auto mapper = board();
    constexpr std::array<MirrorMode, 4> lower_modes = {
        MirrorMode::SingleLower,
        MirrorMode::Vertical,
        MirrorMode::SingleLower,
        MirrorMode::Vertical,
    };
    constexpr std::array<MirrorMode, 4> upper_modes = {
        MirrorMode::SingleUpper,
        MirrorMode::Horizontal,
        MirrorMode::SingleUpper,
        MirrorMode::Horizontal,
    };
    for (unsigned addr = 0x42FCU; addr <= 0x42FFU; ++addr)
        for (Byte data : {0x00U, 0x10U, 0xE0U, 0xF0U}) {
            mapper->write_expansion(static_cast<Address>(addr), data);
            REQUIRE(mapper->mirror_mode() ==
                    ((data & 0x10U) == 0U ? lower_modes[addr & 3U] : upper_modes[addr & 3U]));
            REQUIRE(mapper->read_expansion(0x4500U) ==
                    static_cast<Byte>(0x40U | data | (addr & 3U)));
        }
    for (unsigned addr = 0x43FCU; addr <= 0x43FFU; ++addr)
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->write_prg(0x8000U, 0x2CU);
            mapper->write_expansion(static_cast<Address>(addr), static_cast<Byte>(value));
            REQUIRE(mapper->read_expansion(0x4501U) ==
                    static_cast<Byte>(0xB0U | ((value & 3U) << 2U) | (addr & 3U)));
        }
}

TEST_CASE("Mapper 017 2M and 4M registers overlap and mirror exactly", "[mapper017]") {
    auto mapper = board();
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_expansion(static_cast<Address>(0x4504U + slot),
                                static_cast<Byte>(slot * 7U + 3U));
        expect_prg_8k(*mapper, static_cast<Address>(0x8000U + slot * 8192U), slot * 7U + 3U);
    }

    mapper->write_expansion(0x43FEU, 0U);
    for (unsigned value = 0U; value < 256U; ++value)
        for (unsigned slot = 0U; slot < 4U; ++slot) {
            mapper->write_prg(static_cast<Address>(0x8000U + slot * 8192U),
                              static_cast<Byte>(value));
            expect_prg_8k(*mapper, static_cast<Address>(0x8000U + slot * 8192U), value & 0x3FU);
        }
    mapper->write_expansion(0x43FCU, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot)
        expect_prg_8k(*mapper, static_cast<Address>(0x8000U + slot * 8192U), slot * 7U + 3U);

    mapper->write_expansion(0x43FEU, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot) {
        mapper->write_expansion(static_cast<Address>(0x4504U + slot),
                                static_cast<Byte>(31U - slot));
        expect_prg_8k(*mapper, static_cast<Address>(0x8000U + slot * 8192U), 31U - slot);
    }
    mapper->write_expansion(0x43FCU, 0U);
    for (unsigned slot = 0U; slot < 4U; ++slot)
        expect_prg_8k(*mapper, static_cast<Address>(0x8000U + slot * 8192U), 31U - slot);
    mapper->write_expansion(0x4504U, 0xC0U);
    expect_prg_8k(*mapper, 0x8000U, 0U);

    mapper->write_expansion(0x42FCU, 0x40U);
    mapper->write_prg(0x8000U, 0xA5U);
    REQUIRE(mapper->read_expansion(0x4501U) == 0xF0U);
    REQUIRE(mapper->read_prg(0x8000U) == 0xA5U);
}

TEST_CASE("Mapper 017 1 KiB CHR and CHR nametables reach all 256 banks", "[mapper017]") {
    auto mapper = board();
    fill_chr_1k(*mapper);
    for (unsigned slot = 0U; slot < 8U; ++slot)
        for (unsigned bank = 0U; bank < 256U; ++bank) {
            mapper->write_expansion(static_cast<Address>(0x4510U + slot), static_cast<Byte>(bank));
            for (unsigned offset : {0U, 1U, 255U, 1023U})
                REQUIRE(mapper->read_chr(static_cast<Address>(slot * 1024U + offset)) ==
                        chr_cell(bank, offset));
        }

    mapper->write_expansion(0x4500U, 0x45U);
    for (unsigned bank = 0U; bank < 256U; ++bank) {
        mapper->write_expansion(0x4518U, static_cast<Byte>(bank));
        for (unsigned offset = 0U; offset < 1024U; ++offset)
            REQUIRE(mapper->write_nametable(static_cast<Address>(0x2000U + offset),
                                            chr_cell(bank, offset)));
    }
    for (unsigned slot = 0U; slot < 4U; ++slot)
        for (unsigned bank = 0U; bank < 256U; ++bank) {
            mapper->write_expansion(static_cast<Address>(0x4518U + slot), static_cast<Byte>(bank));
            for (unsigned offset : {0U, 1U, 255U, 1023U})
                REQUIRE(mapper->read_nametable(static_cast<Address>(
                            0x2000U + slot * 1024U + offset)) == chr_cell(bank, offset));
        }
    mapper->write_expansion(0x4518U, 0U);
    mapper->write_expansion(0x42FFU, 0x90U);
    REQUIRE(mapper->write_nametable(0x2000U, 0xA7U));
    REQUIRE(mapper->read_nametable(0x2000U) == chr_cell(0U, 0U));
    mapper->write_expansion(0x42FFU, 0x10U);
    REQUIRE(mapper->write_nametable(0x2000U, 0xA7U));
    REQUIRE(mapper->read_nametable(0x2000U) == 0xA7U);
    mapper->write_expansion(0x4500U, 0x47U);
    REQUIRE_FALSE(mapper->read_nametable(0x2000U));
    REQUIRE_FALSE(mapper->write_nametable(0x2000U, 0U));
}

TEST_CASE("Mapper 017 MMC4 latches use only physical FD and FE read strobes", "[mapper017]") {
    auto mapper = board();
    fill_chr_1k(*mapper);
    mapper->write_expansion(0x4500U, 0x43U);
    for (unsigned reg = 0U; reg < 8U; ++reg)
        mapper->write_expansion(static_cast<Address>(0x4510U + reg), static_cast<Byte>(reg * 4U));

    const auto expect_half = [&](unsigned half, unsigned latch) {
        for (unsigned pair = 0U; pair < 2U; ++pair) {
            const auto reg = half * 4U + pair * 2U + latch;
            const auto addr = half * 4096U + pair * 2048U + 0x123U;
            const auto physical = reg * 4096U + pair * 2048U + 0x123U;
            REQUIRE(mapper->read_chr(static_cast<Address>(addr)) ==
                    chr_cell(physical / 1024U, physical % 1024U));
        }
    };
    expect_half(0U, 0U);
    expect_half(1U, 0U);
    for (unsigned offset = 0U; offset < 8U; ++offset) {
        mapper->observe_ppu_read(static_cast<Address>(0x0FE8U + offset));
        mapper->observe_ppu_read(static_cast<Address>(0x1FE8U + offset));
    }
    expect_half(0U, 1U);
    expect_half(1U, 1U);
    for (unsigned offset = 0U; offset < 8U; ++offset) {
        mapper->observe_ppu_read(static_cast<Address>(0x0FD8U + offset));
        mapper->observe_ppu_read(static_cast<Address>(0x1FD8U + offset));
    }
    expect_half(0U, 0U);
    expect_half(1U, 0U);
    mapper->observe_ppu_read(0x0FD7U);
    mapper->observe_ppu_read(0x0FF0U);
    mapper->observe_ppu_read(0x1FE7U);
    expect_half(0U, 0U);
    expect_half(1U, 0U);

    mapper->observe_ppu_read(0x0FE8U);
    mapper->write_expansion(0x4500U, 0x47U);
    mapper->observe_ppu_read(0x0FD8U);
    mapper->write_expansion(0x4500U, 0x43U);
    expect_half(0U, 1U);
}

TEST_CASE("Mapper 017 M2 and unfiltered PA12 IRQs wrap acknowledge and disable", "[mapper017]") {
    auto mapper = board();
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    mapper->clock_cpu();
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4501U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    for (unsigned i = 0U; i < 65536U; ++i)
        mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->write_expansion(0x4500U, 0x4FU);
    mapper->write_expansion(0x4502U, 0xFEU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(true);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(true);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    REQUIRE(mapper->irq_pending());
    mapper->write_expansion(0x4502U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 017 scratch status GUI and pass-through decode stay bounded", "[mapper017]") {
    auto mapper = board();
    for (unsigned addr = 0x5000U; addr <= 0x5FFFU; ++addr)
        mapper->write_expansion(static_cast<Address>(addr), static_cast<Byte>(addr ^ (addr >> 8U)));
    for (unsigned addr = 0x5000U; addr <= 0x5FFFU; ++addr)
        REQUIRE(mapper->read_expansion(static_cast<Address>(addr)) ==
                static_cast<Byte>(addr ^ (addr >> 8U)));
    for (Address addr : {0x42FCU, 0x43FFU, 0x4502U, 0x4508U, 0x450FU, 0x4FFFU})
        REQUIRE_FALSE(mapper->maps_expansion(addr));

    mapper->observe_cpu_write(0x4000U, 0x12U);
    mapper->observe_cpu_write(0x4015U, 0x34U);
    mapper->observe_cpu_write(0x2007U, 0x56U);
    mapper->observe_cpu_write(0x3FFFU, 0x78U);
    mapper->observe_cpu_write(0x4504U, 7U);
    mapper->write_expansion(0x4504U, 7U);
    REQUIRE(mapper->read_expansion(0x5000U) == 0x12U);
    REQUIRE(mapper->read_expansion(0x5015U) == 0x34U);
    REQUIRE(mapper->read_expansion(0x5807U) == 0x78U);
    REQUIRE(mapper->read_expansion(0x5504U) == 7U);

    mapper->write_expansion(0x4500U, 0x07U);
    mapper->write_expansion(0x4504U, 3U);
    mapper->write_expansion(0x4505U, 4U);
    mapper->write_expansion(0x4507U, 5U);
    expect_prg_8k(*mapper, 0x8000U, 3U);
    expect_prg_8k(*mapper, 0xA000U, 4U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE_FALSE(mapper->maps_prg(0xC000U));
    mapper->write_expansion(0x42FCU, 0U);
    expect_prg_8k(*mapper, 0x6000U, 5U);
    mapper->write_prg(0x6000U, 0xA7U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);

    mapper->write_expansion(0x4500U, 0xC7U);
    REQUIRE_FALSE(mapper->maps_prg(0x8000U));
    REQUIRE_FALSE(mapper->maps_expansion(0x4500U));
    REQUIRE_FALSE(mapper->maps_expansion(0x5000U));
    REQUIRE_FALSE(mapper->read_nametable(0x2000U));
}

TEST_CASE("Mapper 017 reset restores registers while retaining loaded PRG DRAM", "[mapper017]") {
    auto mapper = board();
    mapper->write_expansion(0x42FCU, 0U);
    mapper->write_prg(0x8000U, 0xA7U);
    mapper->write_prg(0x6000U, 0x5AU);
    mapper->write_expansion(0x5000U, 0xC7U);
    mapper->write_expansion(0x4500U, 0x47U);
    mapper->write_expansion(0x4510U, 3U);
    mapper->write_chr(0U, 0x39U);
    mapper->write_expansion(0x4502U, 0xFFU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->clock_cpu();

    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x6000U) == 0x5AU);
    REQUIRE(mapper->read_expansion(0x5000U) == 0xC7U);
    mapper->write_expansion(0x4500U, 0x47U);
    mapper->write_expansion(0x4510U, 3U);
    REQUIRE(mapper->read_chr(0U) == 0x39U);
    REQUIRE(mapper->read_expansion(0x5500U) == 0x47U);
    REQUIRE(mapper->read_expansion(0x5507U) == 63U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    REQUIRE(mapper->read_prg(0x8000U) == 0xA7U);
    REQUIRE(mapper->read_prg(0x6000U) == 0U);
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    REQUIRE(mapper->read_chr(0U) == 0U);
}

TEST_CASE("Mapper 017 rejects unsupported geometry memory and trainer images", "[mapper017]") {
    for (bool legacy : {false, true})
        for (std::size_t size = 32768U; size <= 524288U; size += 8192U)
            REQUIRE(FfeSuperMagicCard::validate_geometry(header(0U, legacy), size, 0U));
    for (Byte sub = 0U; sub < 4U; ++sub)
        REQUIRE(FfeSuperMagicCard::validate_geometry(header(sub), 524288U, 0U));

    for (std::size_t size : {0UZ, 8192UZ, 16384UZ, 24576UZ, 32767UZ, 32769UZ, 524289UZ})
        REQUIRE_FALSE(FfeSuperMagicCard::validate_geometry(header(), size, 0U));
    for (std::size_t size : {1UZ, 8192UZ, 32768UZ, 262144UZ})
        REQUIRE_FALSE(FfeSuperMagicCard::validate_geometry(header(), 524288U, size));
    for (Byte sub = 4U; sub < 16U; ++sub)
        REQUIRE_FALSE(FfeSuperMagicCard::validate_geometry(header(sub), 524288U, 0U));
    for (unsigned which = 0U; which < 10U; ++which) {
        auto h = header();
        switch (which) {
        case 0U:
            h.mapper_number = 6U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_nvram_size = 32768U;
            break;
        case 3U:
            h.chr_nvram_size = 262144U;
            break;
        case 4U:
            h.prg_ram_size = 0U;
            break;
        case 5U:
            h.prg_ram_size = 8192U;
            break;
        case 6U:
            h.chr_ram_size = 32768U;
            break;
        case 7U:
            h.has_trainer = true;
            break;
        case 8U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        REQUIRE_FALSE(FfeSuperMagicCard::validate_geometry(h, 524288U, 0U));
    }
    auto legacy_trainer = header(0U, true);
    legacy_trainer.has_trainer = true;
    REQUIRE_FALSE(FfeSuperMagicCard::validate_geometry(legacy_trainer, 524288U, 0U));
}

TEST_CASE("Mapper 017 snapshots replay RAM and reject malformed state atomically", "[mapper017]") {
    auto mapper = board();
    fill_chr_1k(*mapper);
    mapper->write_expansion(0x42FFU, 0x70U);
    mapper->write_expansion(0x43FEU, 1U);
    mapper->write_prg(0x8000U, 0x2AU);
    mapper->write_prg(0xA000U, 0x15U);
    mapper->write_expansion(0x4500U, 0x4BU);
    mapper->write_expansion(0x4504U, 9U);
    mapper->write_expansion(0x4510U, 201U);
    mapper->write_expansion(0x451BU, 203U);
    mapper->observe_ppu_read(0x0FE8U);
    mapper->write_expansion(0x4502U, 0xFFU);
    mapper->write_expansion(0x4503U, 0xFFU);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    mapper->write_prg(0x6123U, 0xA7U);
    mapper->write_expansion(0x5321U, 0x5AU);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(mapper->irq_pending());

    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    const auto expected = state(*mapper);
    mapper->power_on();
    StateReader replay(saved);
    mapper->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    mapper->observe_ppu_a12(false);
    mapper->observe_ppu_a12(true);
    REQUIRE(state(*mapper) == expected);
    StateReader restore(saved);
    mapper->load_state(restore);
    REQUIRE(restore.ok());

    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };
    for (const auto length : {0UZ, 1UZ, 7UZ, 35UZ, 36UZ, 43UZ, 44UZ, 100UZ, saved.size() - 1U}) {
        INFO("truncated length=" << length);
        reject(std::span(saved).first(length));
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto bad = saved;
        bad[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        INFO("tag bit=" << bit);
        reject(bad);
    }
    for (auto [offset, value] : std::array<std::pair<std::size_t, Byte>, 15>{{
             {2U, 0x10U},
             {3U, 4U},
             {4U, 1U},
             {5U, 0x40U},
             {6U, 4U},
             {7U, 4U},
             {11U, 0U},
             {11U, 2U},
             {12U, 2U},
             {13U, 2U},
             {14U, 0x40U},
             {18U, 0x40U},
             {34U, 2U},
             {35U, 2U},
             {38U, 0U},
         }}) {
        auto bad = saved;
        bad[offset] = value;
        INFO("state offset=" << offset << " value=" << unsigned(value));
        reject(bad);
    }

    auto ram_mutation = saved;
    ram_mutation[44U] ^= 0xFFU;
    StateReader accepted(ram_mutation);
    mapper->load_state(accepted);
    REQUIRE(accepted.ok());
    REQUIRE(state(*mapper) == ram_mutation);
    StateReader restore_again(saved);
    mapper->load_state(restore_again);
    REQUIRE(restore_again.ok());

    auto other = board(1U, false, false, 32768U);
    const auto before = state(*other);
    StateReader wrong(saved);
    other->load_state(wrong);
    REQUIRE_FALSE(wrong.ok());
    REQUIRE(state(*other) == before);
}

} // namespace
