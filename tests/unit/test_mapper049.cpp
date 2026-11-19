#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper049.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

INesHeader header(bool legacy = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 49U;
    h.is_nes2 = !legacy;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    h.prg_ram_size = legacy ? 8192U : 0U;
    return h;
}

Byte cell(unsigned bank, unsigned offset) {
    return static_cast<Byte>(bank * 37U + offset + (offset >> 8U));
}

std::vector<Byte> rom(std::size_t size, std::size_t page_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = cell(static_cast<unsigned>(i / page_size), static_cast<unsigned>(i % page_size));
    return data;
}

std::unique_ptr<Mapper049> board(bool legacy = false,
                                 bool vertical = true,
                                 std::size_t prg_size = 524288U,
                                 std::size_t chr_size = 524288U) {
    const auto h = header(legacy, vertical);
    REQUIRE(Mapper049::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper049>(h, rom(prg_size, 8192U), rom(chr_size, 1024U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 049 powers up in 32 KiB mode without board RAM", "[mapper049]") {
    for (bool legacy : {false, true})
        for (bool vertical : {false, true}) {
            auto mapper = board(legacy, vertical);
            REQUIRE(state(*mapper).size() == 18U);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(mapper->mirror_mode() ==
                    (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
            for (unsigned slot = 0U; slot < 4U; ++slot)
                REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                        cell(slot, 0U));
            REQUIRE(mapper->read_chr(0U) == 0U);
            REQUIRE(mapper->maps_prg(0x6000U));
            REQUIRE(mapper->maps_prg(0x7FFFU));
            REQUIRE(mapper->maps_prg(0x8000U));
            REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
            REQUIRE(mapper->prg_read_mask(0x6000U) == 0U);
            REQUIRE(mapper->prg_read_mask(0x7FFFU) == 0U);
            REQUIRE(mapper->prg_read_mask(0x8000U) == 0xFFU);
            REQUIRE(mapper->read_prg(0x6000U) == 0U);
            REQUIRE(mapper->read_prg(0x7FFFU) == 0U);
        }
}

TEST_CASE("Mapper 049 outer latch honors every alias and all A001 gate values", "[mapper049]") {
    auto mapper = board();
    for (unsigned gate = 0U; gate < 256U; ++gate) {
        mapper->reset();
        mapper->write_prg(0xA001U, static_cast<Byte>(gate));
        mapper->write_prg(0x6000U, 0xF1U);
        REQUIRE(state(*mapper)[2U] == ((gate & 0xC0U) == 0x80U ? 0xF1U : 0U));
    }

    mapper->reset();
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        const auto value = static_cast<Byte>((addr ^ (addr >> 8U)) & 0xFFU);
        mapper->write_prg(static_cast<Address>(addr), value);
        REQUIRE(state(*mapper)[2U] == (value & 0xF1U));
    }

    const auto before = state(*mapper);
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU})
        mapper->write_prg(addr, 0xFFU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 049 32 KiB mode ignores inner PRG registers and mode", "[mapper049]") {
    auto mapper = board();
    bank(*mapper, 6U, 0xABU, 0xC0U);
    bank(*mapper, 7U, 0xD4U, 0xC0U);
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        mapper->write_prg(0x7FFFU, static_cast<Byte>(raw));
        const auto outer = static_cast<Byte>(raw & 0xF1U);
        if ((outer & 1U) != 0U)
            continue;
        const unsigned first_bank = ((outer >> 6U) * 4U + ((outer >> 4U) & 3U)) * 4U;
        for (unsigned offset = 0U; offset < 32768U; ++offset)
            REQUIRE(mapper->read_prg(static_cast<Address>(0x8000U + offset)) ==
                    cell(first_bank + offset / 8192U, offset % 8192U));
    }
}

TEST_CASE("Mapper 049 MMC3 mode applies outer blocks to switchable and fixed PRG banks",
          "[mapper049]") {
    auto mapper = board();
    for (unsigned block = 0U; block < 4U; ++block) {
        mapper->write_prg(0x6000U, static_cast<Byte>((block << 6U) | 1U));
        for (Byte mode : {0U, 0x40U})
            for (unsigned value = 0U; value < 256U; ++value) {
                bank(*mapper, 6U, static_cast<Byte>(value), mode);
                bank(*mapper, 7U, static_cast<Byte>(value ^ 0x35U), mode);
                const std::array<unsigned, 4> inner = {mode == 0U ? value & 0x0FU : 14U,
                                                       (value ^ 0x35U) & 0x0FU,
                                                       mode == 0U ? 14U : value & 0x0FU,
                                                       15U};
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    INFO("block=" << block << " mode=" << +mode << " value=" << value
                                  << " slot=" << slot);
                    const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
                    INFO("actual=" << +mapper->read_prg(addr)
                                   << " expected=" << +cell(block * 16U + inner[slot], 0U)
                                   << " outer=" << +state(*mapper)[2U] << " r6="
                                   << +state(*mapper)[11U] << " r7=" << +state(*mapper)[12U]);
                    REQUIRE(mapper->read_prg(addr) == cell(block * 16U + inner[slot], 0U));
                    REQUIRE(mapper->read_prg(static_cast<Address>(addr + 0x1FFFU)) ==
                            cell(block * 16U + inner[slot], 0x1FFFU));
                }
            }
    }
}

TEST_CASE("Mapper 049 maps every CHR bank in both inversions and protects ROM", "[mapper049]") {
    auto mapper = board();
    for (unsigned block = 0U; block < 4U; ++block) {
        mapper->write_prg(0x6000U, static_cast<Byte>((block << 6U) | 1U));
        for (Byte inversion : {0U, 0x80U})
            for (Byte reg = 0U; reg < 6U; ++reg)
                for (unsigned value = 0U; value < 256U; ++value) {
                    bank(*mapper, reg, static_cast<Byte>(value), inversion);
                    const unsigned first_slot = reg < 2U ? reg * 2U : reg + 2U;
                    for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                        const unsigned slot = (first_slot + half) ^ (inversion == 0U ? 0U : 4U);
                        const unsigned inner = reg < 2U ? (value & 0xFEU) | half : value;
                        const auto addr = static_cast<Address>(slot * 1024U);
                        mapper->write_chr(addr, 0xFFU);
                        REQUIRE(mapper->read_chr(addr) == cell(block * 128U + inner, 0U));
                        REQUIRE(mapper->read_chr(static_cast<Address>(addr + 0x3FFU)) ==
                                cell(block * 128U + inner, 0x3FFU));
                    }
                }
    }
}

TEST_CASE("Mapper 049 decodes every MMC3 register alias", "[mapper049]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        constexpr Byte kValue = 0xA5U;
        mapper->write_prg(static_cast<Address>(addr), kValue);
        const auto saved = state(*mapper);
        if (addr < 0xA000U) {
            if ((addr & 1U) == 0U) {
                REQUIRE(saved[3U] == 5U);
                REQUIRE(saved[4U] == 0x80U);
            } else {
                REQUIRE(saved[5U] == kValue);
            }
        } else if (addr < 0xC000U) {
            if ((addr & 1U) == 0U)
                REQUIRE(saved[13U] == static_cast<Byte>(MirrorMode::Horizontal));
            else
                REQUIRE(saved[17U] == 0x80U);
        } else if (addr < 0xE000U) {
            if ((addr & 1U) == 0U)
                REQUIRE(saved[15U] == kValue);
            else {
                REQUIRE(saved[14U] == 0U);
                REQUIRE(saved[16U] == 1U);
            }
        } else if ((addr & 1U) == 0U) {
            REQUIRE(saved[16U] == 0U);
        } else {
            REQUIRE(saved[16U] == 2U);
        }
    }
}

TEST_CASE("Mapper 049 switches H V mirroring and reset restores documented defaults",
          "[mapper049]") {
    for (bool vertical : {false, true}) {
        auto mapper = board(false, vertical);
        mapper->write_prg(0xA000U, 0U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
        mapper->write_prg(0xBFFEU, 1U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->write_prg(0x6000U, 0xF1U);
        bank(*mapper, 6U, 0x3FU, 0xC0U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        mapper->write_prg(0xA001U, 0U);
        mapper->reset();
        REQUIRE(state(*mapper) == state(*board(false, vertical)));
        mapper->write_prg(0x6000U, 0xF1U);
        mapper->power_on();
        REQUIRE(state(*mapper) == state(*board(false, vertical)));
    }
}

TEST_CASE("Mapper 049 retains the Sharp MMC3 IRQ counter and acknowledgement", "[mapper049]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto partial = state(*mapper);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->acknowledge_irq();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());

    StateReader reader(partial);
    mapper->load_state(reader);
    REQUIRE(reader.ok());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());

    mapper->write_prg(0xE000U, 0U);
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 3U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 049 state replays partial IRQs and rejects malformed input atomically",
          "[mapper049]") {
    auto mapper = board();
    mapper->write_prg(0x6000U, 0xF1U);
    bank(*mapper, 6U, 0x2AU, 0xC0U);
    bank(*mapper, 2U, 0xD5U, 0xC0U);
    mapper->write_prg(0xA000U, 1U);
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    mapper->write_prg(0xA001U, 0x80U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == 18U);

    auto copy = board();
    StateReader replay(saved);
    copy->load_state(replay);
    REQUIRE(replay.ok());
    REQUIRE(replay.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));
    REQUIRE(copy->read_chr(0U) == mapper->read_chr(0U));
    copy->clock_irq_counter();
    mapper->clock_irq_counter();
    copy->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(state(*copy) == state(*mapper));
    REQUIRE(copy->irq_pending());
    const auto before_rejection = state(*mapper);

    for (unsigned length = 0U; length < saved.size(); ++length) {
        StateReader short_reader{std::span(saved).first(length)};
        mapper->load_state(short_reader);
        REQUIRE_FALSE(short_reader.ok());
        REQUIRE(state(*mapper) == before_rejection);
    }

    constexpr std::array<std::pair<unsigned, Byte>, 10> kBad = {{{2U, 2U},
                                                                 {3U, 8U},
                                                                 {4U, 1U},
                                                                 {11U, 64U},
                                                                 {12U, 64U},
                                                                 {13U, 2U},
                                                                 {16U, 8U},
                                                                 {16U, 4U},
                                                                 {17U, 1U},
                                                                 {13U, 4U}}};
    for (const auto& [offset, value] : kBad) {
        auto bad = saved;
        bad[offset] = value;
        StateReader bad_reader(bad);
        mapper->load_state(bad_reader);
        REQUIRE_FALSE(bad_reader.ok());
        REQUIRE(state(*mapper) == before_rejection);
    }

    auto wrong_tag = saved;
    wrong_tag[0U] ^= 0xFFU;
    StateReader tag_reader(wrong_tag);
    mapper->load_state(tag_reader);
    REQUIRE_FALSE(tag_reader.ok());
    REQUIRE(state(*mapper) == before_rejection);

    auto other = board(false, false);
    const auto other_state = state(*other);
    StateReader mismatch(saved);
    other->load_state(mismatch);
    REQUIRE_FALSE(mismatch.ok());
    REQUIRE(state(*other) == other_state);
}

TEST_CASE("Mapper 049 validates geometry and wraps fitted ROMs after outer decoding",
          "[mapper049]") {
    auto mapper = board(false, true, 98304U, 24576U);
    mapper->write_prg(0x6000U, 0xC1U);
    bank(*mapper, 6U, 0x3FU);
    bank(*mapper, 2U, 0xFFU);
    REQUIRE(mapper->read_prg(0x8000U) == cell(3U, 0U));
    REQUIRE(mapper->read_prg(0xC000U) == cell(2U, 0U));
    REQUIRE(mapper->read_prg(0xE000U) == cell(3U, 0U));
    REQUIRE(mapper->read_chr(0x1000U) == cell(15U, 0U));

    const auto reject = [](INesHeader h, std::size_t prg_size, std::size_t chr_size) {
        REQUIRE_FALSE(Mapper049::validate_geometry(h, prg_size, chr_size));
    };
    for (std::size_t size : {0U, 16384U, 32768U + 8192U, 524288U + 32768U})
        reject(header(), size, 524288U);
    for (std::size_t size : {0U, 4096U, 8192U + 512U, 524288U + 1024U})
        reject(header(), 524288U, size);
    for (unsigned variant = 0U; variant < 8U; ++variant) {
        auto h = header();
        switch (variant) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.has_battery = true;
            break;
        case 2U:
            h.prg_ram_size = 8192U;
            break;
        case 3U:
            h.prg_nvram_size = 8192U;
            break;
        case 4U:
            h.chr_ram_size = 8192U;
            break;
        case 5U:
            h.chr_nvram_size = 8192U;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        }
        reject(h, 524288U, 524288U);
    }
    auto legacy_battery = header(true);
    legacy_battery.has_battery = true;
    reject(legacy_battery, 524288U, 524288U);
}

} // namespace
