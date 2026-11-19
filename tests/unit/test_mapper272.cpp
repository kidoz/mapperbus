#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper272.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/ppu/ppu.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader make_header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader header{};
    header.prg_rom_banks = 8U;
    header.chr_rom_banks = 16U;
    header.mapper_number = 272U;
    header.mirror_mode = mirror;
    header.is_nes2 = true;
    header.prg_rom_size = 131072U;
    header.chr_rom_size = 131072U;
    return header;
}

std::vector<Byte> make_prg() {
    std::vector<Byte> data(131072U);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i >> 13U) ^ i ^ (i >> 8U));
    return data;
}

std::vector<Byte> make_chr() {
    std::vector<Byte> data(131072U);
    for (std::size_t i = 0U; i < data.size(); ++i)
        data[i] = static_cast<Byte>((i >> 10U) ^ (i * 3U) ^ (i >> 7U));
    return data;
}

std::unique_ptr<Mapper272> make_board(MirrorMode mirror = MirrorMode::Vertical) {
    return std::make_unique<Mapper272>(make_header(mirror), make_prg(), make_chr());
}

std::vector<Byte> save(const Mapper272& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper272& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void falling_edge(Mapper272& mapper) {
    mapper.observe_ppu_address(0x2000U);
    mapper.observe_ppu_address(0U);
}

std::vector<Byte> make_ines_image() {
    std::vector<Byte> image = {
        0x4EU, 0x45U, 0x53U, 0x1AU, 8U, 16U, 1U, 0x18U, 0x01U, 0U, 0U, 0U, 0U, 0U, 0U, 0U};
    const auto prg = make_prg();
    const auto chr = make_chr();
    image.insert(image.end(), prg.begin(), prg.end());
    image.insert(image.end(), chr.begin(), chr.end());
    return image;
}

TEST_CASE("Mapper 272 accepts only the measured NES 2.0 ROM-only population", "[mapper272]") {
    const auto valid = make_header();
    REQUIRE(Mapper272::validate_geometry(valid, 131072U, 131072U));
    REQUIRE(Mapper272::validate_geometry(make_header(MirrorMode::Horizontal), 131072U, 131072U));

    auto header = valid;
    header.is_nes2 = false;
    REQUIRE_FALSE(Mapper272::validate_geometry(header, 131072U, 131072U));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        header = valid;
        header.submapper = submapper;
        REQUIRE_FALSE(Mapper272::validate_geometry(header, 131072U, 131072U));
    }
    for (const std::size_t size : {0U, 8192U, 32768U, 65536U, 131071U, 131073U, 262144U}) {
        REQUIRE_FALSE(Mapper272::validate_geometry(valid, size, 131072U));
        REQUIRE_FALSE(Mapper272::validate_geometry(valid, 131072U, size));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        header = valid;
        switch (field) {
        case 0:
            header.has_battery = true;
            break;
        case 1:
            header.prg_ram_size = 8192U;
            break;
        case 2:
            header.prg_nvram_size = 8192U;
            break;
        case 3:
            header.chr_ram_size = 8192U;
            break;
        default:
            header.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(Mapper272::validate_geometry(header, 131072U, 131072U));
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        header = valid;
        header.mirror_mode = mirror;
        REQUIRE_FALSE(Mapper272::validate_geometry(header, 131072U, 131072U));
    }

    auto mapper = make_board();
    REQUIRE(save(*mapper).size() == 18U);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(mapper->observes_ppu_address());
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    for (const Address addr : {0x4020U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        mapper->write_prg(addr, 0xFFU);
    }
    REQUIRE(save(*mapper) == save(*make_board()));
}

TEST_CASE("Mapper 272 VRC2b PRG ports include all PAL overlaps and fixed pages", "[mapper272]") {
    auto mapper = make_board();
    const auto image = make_prg();
    for (const unsigned group : {8U, 10U}) {
        const Address window = group == 8U ? 0x8000U : 0xA000U;
        for (unsigned port = 0U; port < 4U; ++port) {
            for (const unsigned suffix : {0U, 4U, 0x0FFCU}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->power_on();
                    mapper->write_prg(static_cast<Address>((group << 12U) | port | suffix),
                                      static_cast<Byte>(value));
                    const std::size_t bank = (value & 0x1FU) % 16U;
                    for (const Address addr : {window,
                                               static_cast<Address>(window + 0x0123U),
                                               static_cast<Address>(window + 0x1FFFU)}) {
                        INFO("group=" << group << " port=" << port << " suffix=" << suffix
                                      << " value=" << value);
                        REQUIRE(mapper->read_prg(addr) == image[bank * 0x2000U + (addr & 0x1FFFU)]);
                    }
                }
            }
        }
    }
    for (unsigned value = 0U; value < 256U; ++value) {
        mapper->write_prg(0x8000U, static_cast<Byte>(value));
        mapper->write_prg(0xA000U, static_cast<Byte>(value + 1U));
        REQUIRE(mapper->read_prg(0xC123U) == image[14U * 0x2000U + 0x0123U]);
        REQUIRE(mapper->read_prg(0xE123U) == image[15U * 0x2000U + 0x0123U]);
    }
}

TEST_CASE("Mapper 272 CHR nibble ports retain halves and wrap the fitted ROM", "[mapper272]") {
    auto mapper = make_board();
    const auto image = make_chr();
    for (unsigned group = 11U; group <= 14U; ++group) {
        for (unsigned port = 0U; port < 4U; ++port) {
            for (const unsigned suffix : {0U, 4U, 0x0FFCU}) {
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->power_on();
                    mapper->write_prg(static_cast<Address>((group << 12U) | port | suffix),
                                      static_cast<Byte>(value));
                    const unsigned slot = (group - 11U) * 2U + (port >> 1U);
                    const unsigned bank = (value & 0x0FU) << ((port & 1U) * 4U);
                    const Address addr = static_cast<Address>(slot * 0x400U + 0x0155U);
                    REQUIRE(mapper->read_chr(addr) ==
                            image[(bank * 0x400U + 0x0155U) % image.size()]);
                }
            }
        }
    }
    mapper->write_prg(0xB000U, 0x0AU);
    mapper->write_prg(0xB001U, 0x0BU);
    REQUIRE(mapper->read_chr(0x0155U) == image[(0xBAU * 0x400U + 0x0155U) % image.size()]);
    mapper->write_prg(0xB000U, 0x05U);
    REQUIRE(mapper->read_chr(0x0155U) == image[(0xB5U * 0x400U + 0x0155U) % image.size()]);
    const auto before = save(*mapper);
    for (Address addr = 0U; addr < 0x2000U; ++addr)
        mapper->write_chr(addr, static_cast<Byte>(addr));
    REQUIRE(save(*mapper) == before);
}

TEST_CASE("Mapper 272 PAL and VRC2 controls produce all four CIRAM modes", "[mapper272]") {
    for (unsigned port = 0U; port < 4U; ++port) {
        for (unsigned value = 0U; value < 256U; ++value) {
            auto mapper = make_board();
            mapper->write_prg(static_cast<Address>(0x9000U | port), static_cast<Byte>(value));
            REQUIRE(mapper->mirror_mode() ==
                    ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
        }
    }
    for (unsigned group = 8U; group <= 11U; ++group) {
        for (unsigned low = 4U; low < 8U; ++low) {
            for (unsigned mode = 0U; mode < 4U; ++mode) {
                auto mapper = make_board();
                mapper->write_prg(0x9000U, 1U);
                mapper->write_prg(static_cast<Address>((group << 12U) | 0x05A0U | low),
                                  static_cast<Byte>(0xFCU | mode));
                const auto expected = mode == 2U                  ? MirrorMode::SingleLower
                                      : mode == 3U                ? MirrorMode::SingleUpper
                                      : group == 9U && mode == 0U ? MirrorMode::Vertical
                                                                  : MirrorMode::Horizontal;
                REQUIRE(mapper->mirror_mode() == expected);
                if (mode < 2U) {
                    mapper->write_prg(0x9001U, 0U);
                    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
                }
            }
        }
    }
}

TEST_CASE("Mapper 272 PA13 counter clocks only enabled falling edges and keeps running",
          "[mapper272]") {
    auto mapper = make_board();
    mapper->observe_ppu_address(0x2000U);
    mapper->observe_ppu_address(0x2000U);
    mapper->observe_ppu_address(0U);
    REQUIRE(save(*mapper)[14] == 0U);

    mapper->write_prg(0xC008U, 0xA7U);
    mapper->observe_ppu_address(0U);
    mapper->observe_ppu_address(0x2000U);
    mapper->observe_ppu_address(0x2000U);
    mapper->observe_ppu_address(0U);
    mapper->observe_ppu_address(0U);
    mapper->observe_ppu_address(0x2000U);
    REQUIRE(save(*mapper)[14] == 1U);
    for (unsigned count = 1U; count < 85U; ++count)
        falling_edge(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(save(*mapper)[14] == 85U);
    falling_edge(*mapper);
    REQUIRE(mapper->irq_pending());
    REQUIRE(save(*mapper)[14] == 0U);

    for (unsigned count = 0U; count < 86U; ++count)
        falling_edge(*mapper);
    REQUIRE(mapper->irq_pending());
    REQUIRE(save(*mapper)[14] == 0U);
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    mapper->write_prg(0xC008U, 0U);
    for (unsigned count = 0U; count < 42U; ++count)
        falling_edge(*mapper);
    mapper->write_prg(0xC008U, 0U);
    REQUIRE(save(*mapper)[14] == 42U);
    mapper->write_prg(0x800CU, 0U);
    REQUIRE(mapper->irq_pending());
    REQUIRE(save(*mapper)[14] == 0U);
    for (unsigned count = 0U; count < 42U; ++count)
        falling_edge(*mapper);
    mapper->write_prg(0xC004U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(save(*mapper)[14] == 42U);
    mapper->write_prg(0xC00CU, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE(save(*mapper)[14] == 0U);
    for (unsigned count = 0U; count < 86U; ++count)
        falling_edge(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 272 every PAL IRQ alias has its measured side effect", "[mapper272]") {
    for (unsigned group = 8U; group <= 11U; ++group) {
        for (unsigned low = 12U; low < 16U; ++low) {
            auto mapper = make_board();
            mapper->write_prg(static_cast<Address>((group << 12U) | 0x03A0U | low), 0x5AU);
            REQUIRE(mapper->irq_pending());
            if (group == 8U)
                REQUIRE(mapper->read_prg(0x8000U) == make_prg()[(0x1AU % 16U) * 0x2000U]);
        }
    }
    for (unsigned group = 12U; group <= 15U; ++group) {
        for (unsigned low = 4U; low < 8U; ++low) {
            auto mapper = make_board();
            mapper->write_prg(0x800CU, 0U);
            mapper->write_prg(static_cast<Address>((group << 12U) | 0x07A0U | low), 0xFFU);
            REQUIRE_FALSE(mapper->irq_pending());
        }
        for (unsigned low = 8U; low < 12U; ++low) {
            auto mapper = make_board();
            mapper->write_prg(static_cast<Address>((group << 12U) | 0x03A0U | low), 0U);
            falling_edge(*mapper);
            REQUIRE(save(*mapper)[14] == 1U);
        }
        for (unsigned low = 12U; low < 16U; ++low) {
            auto mapper = make_board();
            mapper->write_prg(0xC008U, 0U);
            mapper->write_prg(0x800CU, 0U);
            mapper->write_prg(static_cast<Address>((group << 12U) | 0x07A0U | low), 0U);
            REQUIRE_FALSE(mapper->irq_pending());
            falling_edge(*mapper);
            REQUIRE(save(*mapper)[14] == 0U);
        }
    }
}

TEST_CASE("Mapper 272 reset restores deterministic defaults without backing memory",
          "[mapper272]") {
    for (const auto initial : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = make_board(initial);
        mapper->write_prg(0x8000U, 7U);
        mapper->write_prg(0xA000U, 9U);
        mapper->write_prg(0xB001U, 0x0CU);
        mapper->write_prg(0x9000U, initial == MirrorMode::Vertical ? 1U : 0U);
        mapper->write_prg(0x8004U, 3U);
        mapper->write_prg(0xC008U, 0U);
        mapper->write_prg(0x800CU, 0U);
        mapper->observe_ppu_address(0x2000U);
        for (bool cold : {false, true}) {
            if (cold)
                mapper->power_on();
            else
                mapper->reset();
            REQUIRE(mapper->mirror_mode() == initial);
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(save(*mapper)[14] == 0U);
            REQUIRE(save(*mapper)[15] == 0U);
            REQUIRE(save(*mapper)[17] == 0U);
            REQUIRE(mapper->read_prg(0x8000U) == make_prg()[0U]);
            REQUIRE(mapper->read_prg(0xA000U) == make_prg()[0U]);
            REQUIRE(mapper->read_chr(0U) == make_chr()[0U]);
            mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA7U));
            REQUIRE(mapper->battery_ram().empty());
        }
    }
}

TEST_CASE("Mapper 272 snapshots stage all fields and reject damage atomically", "[mapper272]") {
    auto mapper = make_board();
    mapper->write_prg(0x8000U, 7U);
    mapper->write_prg(0xA000U, 9U);
    mapper->write_prg(0xB000U, 0x0AU);
    mapper->write_prg(0xB001U, 0x0BU);
    mapper->write_prg(0x9000U, 1U);
    mapper->write_prg(0x8004U, 3U);
    mapper->write_prg(0xC008U, 0U);
    mapper->write_prg(0x800CU, 0U);
    for (unsigned count = 0U; count < 37U; ++count)
        falling_edge(*mapper);
    mapper->observe_ppu_address(0x2000U);
    const auto saved = save(*mapper);
    REQUIRE(saved.size() == 18U);

    auto replay = make_board();
    load(*replay, saved);
    REQUIRE(save(*replay) == saved);
    REQUIRE(replay->irq_pending());
    REQUIRE(replay->mirror_mode() == MirrorMode::SingleUpper);
    for (unsigned count = 37U; count < 86U; ++count)
        falling_edge(*replay);
    REQUIRE(replay->irq_pending());
    REQUIRE(save(*replay)[14] == 0U);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        const auto before = save(*replay);
        StateReader reader(std::span(saved).first(size));
        replay->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*replay) == before);
        load(*replay, saved);
    }

    const std::array<std::pair<std::size_t, Byte>, 10> invalid = {std::pair{0U, 0xFFU},
                                                                  {2U, 32U},
                                                                  {3U, 32U},
                                                                  {12U, 2U},
                                                                  {13U, 4U},
                                                                  {14U, 86U},
                                                                  {15U, 2U},
                                                                  {16U, 2U},
                                                                  {17U, 2U},
                                                                  {15U, 0U}};
    for (const auto [offset, value] : invalid) {
        auto damaged = saved;
        damaged[offset] = value;
        StateReader reader(damaged);
        replay->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(save(*replay) == saved);
    }
    auto disabled_with_count = saved;
    disabled_with_count[14] = 1U;
    disabled_with_count[15] = 0U;
    StateReader disabled_reader(disabled_with_count);
    replay->load_state(disabled_reader);
    REQUIRE_FALSE(disabled_reader.ok());
    REQUIRE(save(*replay) == saved);

    auto horizontal = make_board(MirrorMode::Horizontal);
    const auto before = save(*horizontal);
    StateReader mismatch(saved);
    horizontal->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(save(*horizontal) == before);

    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    replay->power_on();
    replay->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(save(*replay) == saved);
}

TEST_CASE("Mapper 272 observes PA13 and CIRAM through the real cartridge and PPU path",
          "[mapper272]") {
    MapperRegistry::instance().register_mapper(
        272U,
        [](const INesHeader& header,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            return std::make_unique<Mapper272>(header, std::move(prg), std::move(chr));
        });
    auto cartridge = Cartridge::from_data(make_ines_image());
    REQUIRE(cartridge);
    cartridge->write_prg(0xC008U, 0U);
    Ppu ppu;
    ppu.connect_cartridge(&*cartridge);

    auto set_ppu_address = [&ppu](Address addr) {
        ppu.write_register(6U, static_cast<Byte>(addr >> 8U));
        ppu.write_register(6U, static_cast<Byte>(addr));
        ppu.step_dots(1U);
    };
    for (unsigned count = 0U; count < 85U; ++count) {
        set_ppu_address(0x2000U);
        set_ppu_address(0U);
    }
    REQUIRE_FALSE(cartridge->irq_pending());
    set_ppu_address(0x2000U);
    set_ppu_address(0U);
    REQUIRE(cartridge->irq_pending());
    cartridge->write_prg(0xC004U, 0U);
    REQUIRE_FALSE(cartridge->irq_pending());

    cartridge->write_prg(0x8004U, 2U);
    set_ppu_address(0x2800U);
    ppu.write_register(7U, 0xA7U);
    REQUIRE(ppu.vram()[0U] == 0xA7U);
    cartridge->write_prg(0x8004U, 3U);
    set_ppu_address(0x2000U);
    ppu.write_register(7U, 0xB8U);
    REQUIRE(ppu.vram()[0x0400U] == 0xB8U);
}

} // namespace
