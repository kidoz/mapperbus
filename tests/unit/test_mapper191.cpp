#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper191.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

INesHeader make_header(bool legacy = false, Byte submapper = 1U, bool vertical = true) {
    INesHeader header{};
    header.mapper_number = 191U;
    header.submapper = submapper;
    header.is_nes2 = !legacy;
    header.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    header.prg_ram_size = legacy ? 8192U : 0U;
    header.chr_ram_size = legacy ? 0U : 2048U;
    return header;
}

Byte prg_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 3U + offset + (offset >> 8U));
}

Byte chr_cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 5U + offset + (offset >> 8U));
}

std::unique_ptr<Mapper191> make_board(bool legacy = false,
                                      Byte submapper = 1U,
                                      std::size_t chr_size = 262144U,
                                      bool vertical = true) {
    std::vector<Byte> prg(262144U), chr(chr_size);
    for (std::size_t offset = 0; offset < prg.size(); ++offset)
        prg[offset] = prg_cell(offset / 8192U, offset % 8192U);
    for (std::size_t offset = 0; offset < chr.size(); ++offset)
        chr[offset] = chr_cell(offset / 1024U, offset % 1024U);
    const auto header = make_header(legacy, submapper, vertical);
    REQUIRE(Mapper191::validate_geometry(header, prg.size(), chr.size()));
    return std::make_unique<Mapper191>(header, std::move(prg), std::move(chr));
}

std::vector<Byte> state(const Mapper191& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void write_register(Mapper191& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 191 validates only the documented populations", "[mapper191]") {
    for (bool legacy : {false, true}) {
        for (std::size_t chr_size : {131072U, 262144U}) {
            const auto header = make_header(legacy, 0U);
            REQUIRE(Mapper191::validate_geometry(header, 262144U, chr_size));
        }
    }
    REQUIRE(Mapper191::validate_geometry(make_header(false, 0U), 262144U, 131072U));
    REQUIRE(Mapper191::validate_geometry(make_header(false, 0U), 262144U, 262144U));
    REQUIRE(Mapper191::validate_geometry(make_header(false, 1U), 262144U, 262144U));

    for (std::size_t size : {0U, 8192U, 131071U, 131073U, 262143U, 262145U, 524288U}) {
        REQUIRE_FALSE(Mapper191::validate_geometry(make_header(), size, 262144U));
        REQUIRE_FALSE(Mapper191::validate_geometry(make_header(), 262144U, size));
    }
    REQUIRE_FALSE(Mapper191::validate_geometry(make_header(false, 1U), 262144U, 131072U));
    for (unsigned submapper = 2U; submapper < 16U; ++submapper)
        REQUIRE_FALSE(Mapper191::validate_geometry(
            make_header(false, static_cast<Byte>(submapper)), 262144U, 262144U));

    for (unsigned variant = 0U; variant < 8U; ++variant) {
        auto header = make_header();
        switch (variant) {
        case 0U:
            header.mapper_number = 4U;
            break;
        case 1U:
            header.has_battery = true;
            break;
        case 2U:
            header.prg_ram_size = 8192U;
            break;
        case 3U:
            header.prg_nvram_size = 8192U;
            break;
        case 4U:
            header.chr_ram_size = 0U;
            break;
        case 5U:
            header.chr_ram_size = 8192U;
            break;
        case 6U:
            header.chr_nvram_size = 2048U;
            break;
        default:
            header.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(Mapper191::validate_geometry(header, 262144U, 262144U));
    }
    auto legacy_battery = make_header(true);
    legacy_battery.has_battery = true;
    REQUIRE_FALSE(Mapper191::validate_geometry(legacy_battery, 262144U, 131072U));
}

TEST_CASE("Mapper 191 submapper and legacy headers select the documented outer policy",
          "[mapper191]") {
    for (bool vertical : {false, true}) {
        auto trimmed = make_board(false, 0U, 131072U, vertical);
        REQUIRE(trimmed->read_prg(0xC000U) == prg_cell(30U, 0U));
        REQUIRE(trimmed->read_prg(0xE000U) == prg_cell(31U, 0U));
        REQUIRE(trimmed->mirror_mode() ==
                (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
        REQUIRE(trimmed->battery_ram().empty());
        REQUIRE_FALSE(trimmed->has_expansion_audio());

        auto full = make_board(false, 1U, 262144U, vertical);
        REQUIRE(full->read_prg(0xC000U) == prg_cell(24U, 0U));
        REQUIRE(full->read_prg(0xE000U) == prg_cell(25U, 0U));

        auto legacy_trimmed = make_board(true, 0U, 131072U, vertical);
        REQUIRE(legacy_trimmed->read_prg(0xE000U) == prg_cell(31U, 0U));
        auto legacy_full = make_board(true, 0U, 262144U, vertical);
        REQUIRE(legacy_full->read_prg(0xE000U) == prg_cell(25U, 0U));
    }
}

TEST_CASE("Mapper 191 decodes all outer aliases and retains the MMC3 bank-select write",
          "[mapper191]") {
    auto mapper = make_board();
    for (unsigned alias = 0U; alias < 16U; ++alias) {
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            write_register(*mapper, 2U, 0x80U);
            mapper->write_prg(static_cast<Address>(0x90AAU + alias * 0x100U),
                              static_cast<Byte>(value));
            const unsigned outer = value & 3U;
            INFO("alias=" << alias << " value=" << value);
            const Address second_last = (value & 0x40U) != 0U ? 0x8000U : 0xC000U;
            REQUIRE(mapper->read_prg(second_last) == prg_cell(24U + outer * 2U, 0U));
            REQUIRE(mapper->read_prg(0xE000U) == prg_cell(25U + outer * 2U, 0U));
            REQUIRE(state(*mapper)[3U] == (value & 7U));
            REQUIRE(state(*mapper)[4U] == (value & 0xC0U));
            const Address chr_addr = (value & 0x80U) != 0U ? 0x0000U : 0x1000U;
            mapper->write_chr(chr_addr, 0xA7U);
            if (outer == 3U) {
                REQUIRE(mapper->read_chr(chr_addr) == 0xA7U);
            } else {
                REQUIRE(mapper->read_chr(chr_addr) == chr_cell(128U, 0U));
            }
        }
    }

    mapper->reset();
    mapper->write_prg(0x90AAU, 2U);
    for (Address addr : {0x90A8U, 0x90A9U, 0x90ABU, 0x90ACU, 0x80AAU, 0xA0AAU}) {
        mapper->write_prg(addr, 1U);
        REQUIRE(state(*mapper)[2U] == 2U);
    }

    auto disabled = make_board(false, 0U);
    for (unsigned value = 0U; value < 256U; ++value) {
        disabled->write_prg(0x90AAU, static_cast<Byte>(value));
        REQUIRE(state(*disabled)[2U] == 0U);
        REQUIRE(disabled->read_prg(0xE000U) == prg_cell(31U, 0U));
    }
}

TEST_CASE("Mapper 191 preserves every ordinary MMC3 register alias without bus conflicts",
          "[mapper191]") {
    auto actual = make_board(false, 0U);
    auto expected = make_board(false, 0U);
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        actual->reset();
        expected->reset();
        actual->write_prg(static_cast<Address>(addr), 0xA5U);
        expected->write_prg(static_cast<Address>(0x8000U | (addr & 0xE001U)), 0xA5U);
        INFO("address=" << addr);
        REQUIRE(state(*actual) == state(*expected));
        REQUIRE(actual->maps_prg(static_cast<Address>(addr)));
        REQUIRE_FALSE(actual->maps_expansion(static_cast<Address>(addr)));
    }
    for (Address addr : {0x4020U, 0x5000U, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(actual->maps_prg(addr));
        REQUIRE(actual->prg_read_mask(addr) == 0xFFU);
    }
}

TEST_CASE("Mapper 191 maps all PRG bank values in both modes and all outer banks", "[mapper191]") {
    auto mapper = make_board();
    for (unsigned outer = 0U; outer < 4U; ++outer) {
        for (Byte mode : {0U, 0x40U}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->write_prg(0x90AAU, static_cast<Byte>(outer));
                write_register(*mapper, 6U, static_cast<Byte>(value), mode);
                write_register(*mapper, 7U, static_cast<Byte>(value ^ 0xFFU), mode);
                const std::array<unsigned, 4> banks{mode == 0U ? value & 0x1FU : 24U + outer * 2U,
                                                    (value ^ 0xFFU) & 0x1FU,
                                                    mode == 0U ? 24U + outer * 2U : value & 0x1FU,
                                                    25U + outer * 2U};
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    for (unsigned offset : {0U, 1U, 8191U}) {
                        INFO("outer=" << outer << " mode=" << +mode << " value=" << value);
                        REQUIRE(mapper->read_prg(
                                    static_cast<Address>(0x8000U + slot * 8192U + offset)) ==
                                prg_cell(banks[slot], offset));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 191 maps paired and inverted CHR banks before ROM/RAM decoding", "[mapper191]") {
    auto mapper = make_board();
    for (unsigned outer = 0U; outer < 4U; ++outer) {
        for (Byte mode : {0U, 0x80U}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                mapper->write_prg(0x90AAU, static_cast<Byte>(outer));
                std::array<unsigned, 6> banks{};
                for (Byte index = 0U; index < 6U; ++index) {
                    banks[index] = (value + index * 17U) & 0xFFU;
                    write_register(*mapper, index, static_cast<Byte>(banks[index]), mode);
                }
                const std::array<unsigned, 8> pages{banks[0] & 0xFEU,
                                                    (banks[0] & 0xFEU) + 1U,
                                                    banks[1] & 0xFEU,
                                                    (banks[1] & 0xFEU) + 1U,
                                                    banks[2],
                                                    banks[3],
                                                    banks[4],
                                                    banks[5]};
                for (unsigned slot = 0U; slot < 8U; ++slot) {
                    const unsigned bank = pages[slot ^ (mode == 0U ? 0U : 4U)];
                    for (unsigned offset : {0U, 1U, 1023U}) {
                        const auto addr = static_cast<Address>(slot * 1024U + offset);
                        const auto written = static_cast<Byte>(value ^ 0x5AU);
                        mapper->write_chr(addr, written);
                        INFO("outer=" << outer << " mode=" << +mode << " bank=" << bank);
                        if (outer == 3U && bank >= 128U) {
                            REQUIRE(mapper->read_chr(addr) == written);
                        } else {
                            const unsigned rom_bank = outer == 3U ? bank : (bank & 0x7FU) | 0x80U;
                            REQUIRE(mapper->read_chr(addr) == chr_cell(rom_bank, offset));
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 191 mirrors exactly two CHR RAM pages and ignores ROM writes", "[mapper191]") {
    auto mapper = make_board();
    mapper->write_prg(0x90AAU, 3U);
    for (unsigned bank = 128U; bank < 256U; ++bank) {
        write_register(*mapper, 2U, static_cast<Byte>(bank));
        const auto value = static_cast<Byte>(bank ^ 0xA5U);
        mapper->write_chr(0x1000U, value);
        for (unsigned alias = 128U + (bank & 1U); alias < 256U; alias += 2U) {
            write_register(*mapper, 2U, static_cast<Byte>(alias));
            REQUIRE(mapper->read_chr(0x1000U) == value);
        }
    }

    mapper->write_prg(0x90AAU, 0U);
    write_register(*mapper, 2U, 0x80U);
    for (unsigned offset = 0U; offset < 1024U; ++offset) {
        mapper->write_chr(static_cast<Address>(0x1000U + offset), 0xFFU);
        REQUIRE(mapper->read_chr(static_cast<Address>(0x1000U + offset)) == chr_cell(128U, offset));
    }
}

TEST_CASE("Mapper 191 reset restores outer and MMC3 state while power clears CHR RAM",
          "[mapper191]") {
    auto mapper = make_board(false, 1U, 262144U, false);
    mapper->write_prg(0x90AAU, 3U);
    write_register(*mapper, 2U, 0x80U);
    mapper->write_chr(0x1000U, 0xA7U);
    write_register(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);

    mapper->reset();
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
    REQUIRE(mapper->read_prg(0xE000U) == prg_cell(25U, 0U));
    REQUIRE(state(*mapper)[2U] == 0U);
    REQUIRE(state(*mapper)[4U] == 0U);
    mapper->write_prg(0x90AAU, 3U);
    write_register(*mapper, 2U, 0x80U);
    REQUIRE(mapper->read_chr(0x1000U) == 0xA7U);
    REQUIRE_FALSE(mapper->irq_pending());

    mapper->power_on();
    REQUIRE(mapper->read_prg(0xE000U) == prg_cell(25U, 0U));
    mapper->write_prg(0x90AAU, 3U);
    write_register(*mapper, 2U, 0x80U);
    REQUIRE(mapper->read_chr(0x1000U) == 0U);
}

TEST_CASE("Mapper 191 clocks acknowledges and replays the MMC3 IRQ counter", "[mapper191]") {
    auto mapper = make_board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto partial = state(*mapper);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    StateReader restore(partial);
    mapper->load_state(restore);
    REQUIRE(restore.ok());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned clock = 0U; clock < 4U; ++clock) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 191 bounded state rejection is atomic and binds immutable wiring",
          "[mapper191]") {
    auto mapper = make_board();
    mapper->write_prg(0x90AAU, 3U);
    write_register(*mapper, 2U, 0x81U);
    mapper->write_chr(0x1000U, 0xA7U);
    write_register(*mapper, 6U, 9U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 2065U);

    for (std::size_t length = 0; length < saved.size(); ++length) {
        StateReader reader{std::span(saved).first(length)};
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    constexpr std::array<std::pair<std::size_t, Byte>, 9> kInvalid{{
        {2U, 4U},
        {3U, 8U},
        {4U, 1U},
        {11U, 64U},
        {12U, 64U},
        {13U, 2U},
        {16U, 8U},
        {16U, 4U},
        {16U, 0xFFU},
    }};
    for (const auto& [offset, value] : kInvalid) {
        auto corrupt = saved;
        corrupt[offset] = value;
        StateReader reader(corrupt);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (unsigned bit = 0U; bit < 16U; ++bit) {
        auto corrupt = saved;
        corrupt[bit / 8U] ^= static_cast<Byte>(1U << (bit & 7U));
        StateReader reader(corrupt);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }

    for (auto&& other : {make_board(false, 0U),
                         make_board(false, 1U, 262144U, false),
                         make_board(false, 0U, 131072U)}) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    }

    mapper->power_on();
    StateReader reader(saved);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*mapper) == saved);
    REQUIRE(mapper->read_chr(0x0000U) == 0xA7U);

    auto with_tail = saved;
    with_tail.push_back(0x5AU);
    StateReader tail_reader(with_tail);
    mapper->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 1U);
}

} // namespace
