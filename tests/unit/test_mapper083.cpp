#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "core/mappers/mapper083.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kStateBytes = 32U;
constexpr std::size_t kNvramStateBytes = kStateBytes + 32768U;

INesHeader header(uint16_t mapper = 83U, Byte submapper = 0U, bool legacy = false) {
    INesHeader h{};
    h.mapper_number = mapper;
    h.submapper = submapper;
    h.is_nes2 = mapper == 264U || !legacy;
    h.has_battery = mapper == 83U && submapper == 2U;
    h.mirror_mode = MirrorMode::Vertical;
    h.prg_ram_size = h.is_nes2 ? 0U : 8192U;
    h.prg_nvram_size = h.is_nes2 && h.has_battery ? 32768U : 0U;
    return h;
}

std::pair<std::size_t, std::size_t> sizes(uint16_t mapper, Byte submapper) {
    if (mapper == 83U && submapper == 2U)
        return {262144U, 262144U};
    if (mapper == 83U && submapper == 3U)
        return {131072U, 131072U};
    return {1048576U, mapper == 264U || submapper == 1U ? 524288U : 262144U};
}

Byte prg_cell(std::size_t offset) {
    return static_cast<Byte>((offset / 8192U) * 37U + offset + (offset >> 8U));
}

Byte chr_cell(std::size_t offset) {
    return static_cast<Byte>((offset / 1024U) * 13U + offset + (offset >> 8U));
}

Address yoko_address(Address addr) {
    return static_cast<Address>((addr & 0xF000U) | ((addr & 0x0300U) << 2U) | (addr & 0x00FFU));
}

Address asic_address(uint16_t mapper, Address addr) {
    return mapper == 264U ? static_cast<Address>((addr & 0xF000U) | ((addr & 0x0C00U) >> 2U) |
                                                 (addr & 0x00FFU))
                          : addr;
}

std::unique_ptr<ConyYoko> board(uint16_t mapper = 83U,
                                Byte submapper = 0U,
                                Byte jumpers = 0U,
                                bool legacy = false,
                                std::size_t prg_size = 0U,
                                std::size_t chr_size = 0U) {
    const auto [default_prg, default_chr] = sizes(mapper, submapper);
    prg_size = prg_size == 0U ? default_prg : prg_size;
    chr_size = chr_size == 0U ? default_chr : chr_size;
    const auto h = header(mapper, submapper, legacy);
    REQUIRE(ConyYoko::validate_geometry(h, prg_size, chr_size));
    std::vector<Byte> prg(prg_size), chr(chr_size);
    for (std::size_t i = 0U; i < prg.size(); ++i)
        prg[i] = prg_cell(i);
    for (std::size_t i = 0U; i < chr.size(); ++i)
        chr[i] = chr_cell(i);
    auto m = std::make_unique<ConyYoko>(h, std::move(prg), std::move(chr));
    REQUIRE(m->configure_board_jumpers(jumpers));
    return m;
}

std::vector<Byte> state(const Mapper& m) {
    StateWriter writer;
    m.save_state(writer);
    return writer.take();
}

void write_reg(Mapper& m, uint16_t mapper, Address addr, Byte value) {
    m.write_prg(mapper == 264U ? yoko_address(addr) : addr, value);
}

void require_prg_bank(Mapper& m, Address addr, std::size_t bank, std::size_t rom_size) {
    const std::size_t offset = (bank * 8192U + (addr & 0x1FFFU)) % rom_size;
    INFO("addr=" << addr << " bank=" << bank << " offset=" << offset);
    REQUIRE(m.read_prg(addr) == prg_cell(offset));
}

TEST_CASE("Cony/Yoko decodes every mapper 83 and 264 register alias", "[mapper083][mapper264]") {
    for (uint16_t mapper : {83U, 264U}) {
        auto m = board(mapper);
        for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            m->power_on();
            auto expected = state(*m);
            const auto reg = asic_address(mapper, addr);
            if ((reg & 0x8300U) == 0x8000U)
                expected[10] = 0xA5U;
            else if ((reg & 0x8300U) == 0x8100U)
                expected[11] = 0xA5U;
            else if ((reg & 0x8301U) == 0x8200U)
                expected[24] = 0xA5U;
            else if ((reg & 0x8301U) == 0x8201U)
                expected[25] = 0xA5U;
            else if ((reg & 0x8318U) == 0x8318U)
                expected[26] = 0xA5U;
            else if ((reg & 0x8318U) == 0x8310U)
                expected[16U + (reg & 7U)] = 0xA5U;
            else if ((reg & 0x8310U) == 0x8300U)
                expected[12U + (reg & 3U)] = 0xA5U;
            m->write_prg(addr, 0xA5U);
            REQUIRE(state(*m) == expected);
        }
        for (Address addr : {0x0000U, 0x401FU, 0x5000U, 0x5103U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
            m->power_on();
            const auto before = state(*m);
            m->write_prg(addr, 0xA5U);
            REQUIRE(state(*m) == before);
        }
    }
}

TEST_CASE("Cony/Yoko PRG modes preserve the documented inner-bank width",
          "[mapper083][mapper264]") {
    for (uint16_t mapper : {83U, 264U}) {
        auto m = board(mapper);
        const auto prg_size = sizes(mapper, 0U).first;
        write_reg(*m, mapper, 0x8000U, 0x05U);
        write_reg(*m, mapper, 0x8100U, 0U);
        require_prg_bank(*m, 0x8000U, 10U, prg_size);
        require_prg_bank(*m, 0xA000U, 11U, prg_size);
        require_prg_bank(*m, 0xC000U, mapper == 264U ? 14U : 30U, prg_size);
        require_prg_bank(*m, 0xE000U, mapper == 264U ? 15U : 31U, prg_size);

        write_reg(*m, mapper, 0x8000U, 0x0BU);
        write_reg(*m, mapper, 0x8100U, 0x10U);
        for (Address addr : {0x8000U, 0xA000U, 0xC000U, 0xE000U, 0xFFFFU})
            REQUIRE(m->read_prg(addr) == prg_cell(0x0BU * 16384U + (addr & 0x3FFFU)));

        for (Byte mode : {0x20U, 0x30U}) {
            write_reg(*m, mapper, 0x8000U, 0x10U);
            write_reg(*m, mapper, 0x8100U, mode);
            for (unsigned slot = 0U; slot < 4U; ++slot)
                write_reg(*m,
                          mapper,
                          static_cast<Address>(0x8300U + slot),
                          static_cast<Byte>(0xE1U + slot));
            for (unsigned slot = 0U; slot < 3U; ++slot)
                require_prg_bank(
                    *m, static_cast<Address>(0x8000U + slot * 8192U), 33U + slot, prg_size);
            require_prg_bank(*m, 0xE000U, mapper == 264U ? 47U : 63U, prg_size);
            require_prg_bank(*m, 0x6000U, 36U, prg_size);
            REQUIRE(m->maps_prg(0x6000U));
        }

        write_reg(*m, mapper, 0x8100U, 0U);
        REQUIRE_FALSE(m->maps_prg(0x6000U));
        REQUIRE(m->read_prg(0x6000U) == 0U);
        REQUIRE(m->prg_read_mask(0x6000U) == 0xFFU);
        REQUIRE(m->prg_read_mask(0x8000U) == 0xFFU);
    }
}

TEST_CASE("Cony/Yoko multicart base bits select NVRAM without widening fitted ROMs",
          "[mapper083]") {
    auto nvram = board(83U, 2U);
    write_reg(*nvram, 83U, 0x8000U, 0x3AU);
    write_reg(*nvram, 83U, 0x8100U, 0x10U);
    const auto base_image = state(*nvram);
    REQUIRE(nvram->read_prg(0x8123U) == prg_cell((0x3AU * 16384U + 0x123U) % 262144U));
    write_reg(*nvram, 83U, 0x8000U, 0xFAU);
    REQUIRE(nvram->read_prg(0x8123U) == prg_cell((0x3AU * 16384U + 0x123U) % 262144U));
    REQUIRE(nvram->read_prg(0x6123U) == 0U);
    REQUIRE(base_image != state(*nvram));

    auto small = board(83U, 3U);
    for (Byte base : {0x0AU, 0x4AU, 0x8AU, 0xCAU}) {
        write_reg(*small, 83U, 0x8000U, base);
        write_reg(*small, 83U, 0x8100U, 0U);
        require_prg_bank(*small, 0x8000U, 20U, 131072U);
        require_prg_bank(*small, 0xC000U, 14U, 131072U);
    }
}

TEST_CASE("Cony/Yoko maps all 1 KiB CHR registers and protects ROM", "[mapper083][mapper264]") {
    for (Byte submapper : {0U, 2U, 3U}) {
        auto m = board(83U, submapper);
        const auto chr_size = sizes(83U, submapper).second;
        for (unsigned slot = 0U; slot < 8U; ++slot) {
            for (unsigned bank = 0U; bank < 256U; ++bank) {
                write_reg(*m, 83U, static_cast<Address>(0x8310U + slot), static_cast<Byte>(bank));
                for (Address addr : {static_cast<Address>(slot * 1024U),
                                     static_cast<Address>(slot * 1024U + 1023U)}) {
                    const auto before = m->read_chr(addr);
                    m->write_chr(addr, static_cast<Byte>(bank ^ 0xFFU));
                    REQUIRE(m->read_chr(addr) == before);
                    REQUIRE(before == chr_cell((bank * 1024U + (addr & 0x3FFU)) % chr_size));
                }
            }
        }
    }
}

TEST_CASE("Large-CHR Cony/Yoko boards use registers 0 1 6 7 as four 2 KiB windows",
          "[mapper083][mapper264]") {
    for (uint16_t mapper : {83U, 264U}) {
        auto m = board(mapper, mapper == 83U ? 1U : 0U);
        constexpr std::array<unsigned, 4> kRegisters = {0U, 1U, 6U, 7U};
        for (unsigned window = 0U; window < 4U; ++window) {
            for (unsigned bank = 0U; bank < 256U; ++bank) {
                write_reg(*m,
                          mapper,
                          static_cast<Address>(0x8310U + kRegisters[window]),
                          static_cast<Byte>(bank));
                for (unsigned offset : {0U, 1023U, 1024U, 2047U}) {
                    const auto addr = static_cast<Address>(window * 2048U + offset);
                    REQUIRE(m->read_chr(addr) == chr_cell(bank * 2048U + offset));
                }
            }
        }
        for (unsigned reg : {2U, 3U, 4U, 5U}) {
            const auto before = state(*m);
            std::array<Byte, 4> mapped{};
            for (unsigned window = 0U; window < 4U; ++window)
                mapped[window] = m->read_chr(static_cast<Address>(window * 2048U));
            const auto value = static_cast<Byte>(0xA0U + reg);
            write_reg(*m, mapper, static_cast<Address>(0x8310U + reg), value);
            auto expected = before;
            expected[16U + reg] = value;
            REQUIRE(state(*m) == expected);
            for (unsigned window = 0U; window < 4U; ++window)
                REQUIRE(m->read_chr(static_cast<Address>(window * 2048U)) == mapped[window]);
        }
    }
}

TEST_CASE("Cony/Yoko solder pads and scratch RAM decode with partial open bus",
          "[mapper083][mapper264]") {
    for (uint16_t mapper : {83U, 264U}) {
        auto m = board(mapper, 0U, 2U);
        for (unsigned raw = 0x4020U; raw <= 0x5FFFU; ++raw) {
            const auto addr = static_cast<Address>(raw);
            const auto reg = asic_address(mapper, addr);
            const bool pad = (reg & 0xF100U) == 0x5000U;
            const bool scratch = (reg & 0xF100U) == 0x5100U;
            m->power_on();
            REQUIRE(m->maps_expansion(addr) == (pad || scratch));
            REQUIRE(m->expansion_read_mask(addr) == (scratch ? 0xFFU : pad ? 0x03U : 0U));
            REQUIRE(m->read_expansion(addr) == (pad ? 2U : 0U));
            m->write_expansion(addr, 0xA5U);
            REQUIRE(m->read_expansion(addr) == (scratch ? 0xA5U : pad ? 2U : 0U));
        }
        for (Byte value = 0U; value < 4U; ++value) {
            REQUIRE(m->configure_board_jumpers(value));
            REQUIRE((m->read_expansion(mapper == 264U ? 0x5000U : 0x5200U) & 3U) == value);
        }
        const auto before = state(*m);
        REQUIRE_FALSE(m->configure_board_jumpers(4U));
        REQUIRE(state(*m) == before);
    }
}

TEST_CASE("Mapper 83.2 exposes four 8 KiB NVRAM banks and retains persistent bytes",
          "[mapper083]") {
    auto m = board(83U, 2U);
    REQUIRE(m->battery_ram().size() == 32768U);
    REQUIRE(state(*m).size() == kNvramStateBytes);
    for (Byte bank = 0U; bank < 4U; ++bank) {
        write_reg(*m, 83U, 0x8000U, static_cast<Byte>(bank << 6U));
        write_reg(*m, 83U, 0x8100U, 0U);
        REQUIRE(m->maps_prg(0x6000U));
        for (Address addr : {0x6000U, 0x6123U, 0x7FFFU})
            m->write_prg(addr, static_cast<Byte>(0x40U + bank * 16U + (addr & 3U)));
    }
    m->write_expansion(0x5100U, 0xA5U);
    m->reset();
    REQUIRE(m->read_expansion(0x5100U) == 0xA5U);
    m->power_on();
    REQUIRE(m->read_expansion(0x5100U) == 0U);
    for (Byte bank = 0U; bank < 4U; ++bank) {
        write_reg(*m, 83U, 0x8000U, static_cast<Byte>(bank << 6U));
        for (Address addr : {0x6000U, 0x6123U, 0x7FFFU})
            REQUIRE(m->read_prg(addr) == static_cast<Byte>(0x40U + bank * 16U + (addr & 3U)));
    }

    std::vector<Byte> imported(32768U);
    for (std::size_t i = 0U; i < imported.size(); ++i)
        imported[i] = static_cast<Byte>(i * 11U);
    m->set_battery_ram(imported);
    REQUIRE(std::vector<Byte>(m->battery_ram().begin(), m->battery_ram().end()) == imported);
    imported.push_back(0xA5U);
    m->set_battery_ram(imported);
    REQUIRE(m->battery_ram().size() == 32768U);
    for (Byte submapper : {0U, 1U, 3U}) {
        auto rom_only = board(83U, submapper);
        REQUIRE(rom_only->battery_ram().empty());
    }
}

TEST_CASE("Cony/Yoko nametable modes and deterministic reset match the mode register",
          "[mapper083][mapper264]") {
    auto m = board();
    constexpr std::array<MirrorMode, 4> kModes = {MirrorMode::Vertical,
                                                  MirrorMode::Horizontal,
                                                  MirrorMode::SingleLower,
                                                  MirrorMode::SingleUpper};
    for (Byte mode = 0U; mode < 4U; ++mode) {
        write_reg(*m, 83U, 0x8100U, mode);
        REQUIRE(m->mirror_mode() == kModes[mode]);
    }
    write_reg(*m, 83U, 0x8000U, 0xFFU);
    write_reg(*m, 83U, 0x8318U, 0xFFU);
    m->write_expansion(0x5100U, 0xA5U);
    m->reset();
    REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(m->read_expansion(0x5100U) == 0xA5U);
    REQUIRE_FALSE(m->irq_pending());
    REQUIRE_FALSE(m->has_expansion_audio());
    m->power_on();
    REQUIRE(m->read_expansion(0x5100U) == 0U);
}

TEST_CASE("Cony/Yoko CPU-clock IRQs count in both directions and latch until LSB acknowledgement",
          "[mapper083][mapper264]") {
    for (Byte direction : {0U, 0x40U}) {
        auto m = board();
        write_reg(*m, 83U, 0x8100U, static_cast<Byte>(0x80U | direction));
        write_reg(*m, 83U, 0x8200U, direction == 0U ? 0xFEU : 2U);
        write_reg(*m, 83U, 0x8201U, direction == 0U ? 0xFFU : 0U);
        m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
        m->clock_cpu();
        REQUIRE(m->irq_pending());
        for (unsigned cycle = 0U; cycle < 8U; ++cycle)
            m->clock_cpu();
        REQUIRE(m->irq_pending());
        m->acknowledge_irq();
        REQUIRE(m->irq_pending());
        write_reg(*m, 83U, 0x8201U, 0U);
        REQUIRE(m->irq_pending());
        write_reg(*m, 83U, 0x8200U, 2U);
        REQUIRE_FALSE(m->irq_pending());
        REQUIRE((state(*m)[27] & 1U) != 0U);
        m->clock_cpu();
        REQUIRE_FALSE(m->irq_pending());
        if (direction != 0U) {
            m->clock_cpu();
            REQUIRE(m->irq_pending());
        }
    }

    auto disabled = board();
    write_reg(*disabled, 83U, 0x8100U, 0x80U);
    write_reg(*disabled, 83U, 0x8201U, 0U);
    write_reg(*disabled, 83U, 0x8100U, 0U);
    write_reg(*disabled, 83U, 0x8200U, 1U);
    write_reg(*disabled, 83U, 0x8201U, 0U);
    disabled->clock_cpu();
    REQUIRE_FALSE(disabled->irq_pending());
    write_reg(*disabled, 83U, 0x8200U, 0U);
    write_reg(*disabled, 83U, 0x8100U, 0x80U);
    write_reg(*disabled, 83U, 0x8201U, 0U);
    disabled->clock_cpu();
    REQUIRE_FALSE(disabled->irq_pending());
}

TEST_CASE("Cony/Yoko PPU IRQs use unfiltered rising A12 edges and replay partial pin state",
          "[mapper083][mapper264]") {
    auto m = board();
    REQUIRE(m->observes_ppu_a12());
    write_reg(*m, 83U, 0x8100U, 0xC0U);
    write_reg(*m, 83U, 0x8200U, 3U);
    write_reg(*m, 83U, 0x8201U, 0U);
    write_reg(*m, 83U, 0x8318U, 0xFFU);
    m->clock_cpu();
    REQUIRE_FALSE(m->irq_pending());
    m->observe_ppu_a12(true);
    m->observe_ppu_a12(true);
    REQUIRE_FALSE(m->irq_pending());
    const auto partial = state(*m);
    m->observe_ppu_a12(false);
    m->observe_ppu_a12(true);
    REQUIRE_FALSE(m->irq_pending());
    m->observe_ppu_a12(false);
    m->observe_ppu_a12(true);
    REQUIRE(m->irq_pending());

    m->power_on();
    StateReader reader(partial);
    m->load_state(reader);
    REQUIRE(reader.ok());
    m->observe_ppu_a12(true);
    REQUIRE_FALSE(m->irq_pending());
    m->observe_ppu_a12(false);
    m->observe_ppu_a12(true);
    REQUIRE_FALSE(m->irq_pending());
    m->observe_ppu_a12(false);
    m->observe_ppu_a12(true);
    REQUIRE(m->irq_pending());
}

TEST_CASE("Cony/Yoko snapshots restore all mutable state and reject damage atomically",
          "[mapper083][mapper264]") {
    for (Byte submapper = 0U; submapper < 4U; ++submapper) {
        auto m = board(83U, submapper, 2U);
        write_reg(*m, 83U, 0x8000U, 0xA7U);
        write_reg(*m, 83U, 0x8100U, 0xF3U);
        for (unsigned reg = 0U; reg < 4U; ++reg)
            write_reg(*m, 83U, static_cast<Address>(0x8300U + reg), static_cast<Byte>(0x30U + reg));
        for (unsigned reg = 0U; reg < 8U; ++reg)
            write_reg(*m, 83U, static_cast<Address>(0x8310U + reg), static_cast<Byte>(0x80U + reg));
        write_reg(*m, 83U, 0x8200U, 0x34U);
        write_reg(*m, 83U, 0x8201U, 0x12U);
        write_reg(*m, 83U, 0x8318U, 0xFFU);
        m->observe_ppu_a12(true);
        m->write_expansion(0x5100U, 0xA5U);
        m->write_expansion(0x5103U, 0x5AU);
        m->write_prg(0x6123U, 0xC7U);
        const auto saved = state(*m);
        REQUIRE(saved.size() == (submapper == 2U ? kNvramStateBytes : kStateBytes));

        for (std::size_t length :
             std::array<std::size_t, 8>{0U, 1U, 2U, 9U, 27U, 28U, 31U, saved.size() - 1U}) {
            StateReader reader{std::span(saved).first(length)};
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }
        for (unsigned bit = 0U; bit < 16U; ++bit) {
            auto bad = saved;
            bad[bit / 8U] ^= static_cast<Byte>(1U << (bit % 8U));
            StateReader reader(bad);
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }
        for (unsigned offset : {2U, 6U, 27U}) {
            auto bad = saved;
            bad[offset] = 0xFFU;
            StateReader reader(bad);
            m->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*m) == saved);
        }

        m->power_on();
        StateReader reader(saved);
        m->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*m) == saved);
        REQUIRE(m->read_expansion(0x5100U) == 0xA5U);
        REQUIRE(m->read_expansion(0x5103U) == 0x5AU);
        if (submapper == 2U)
            REQUIRE(m->read_prg(0x6123U) == 0xC7U);

        auto other_pads = board(83U, submapper, 1U);
        const auto before = state(*other_pads);
        StateReader wrong_pads(saved);
        other_pads->load_state(wrong_pads);
        REQUIRE_FALSE(wrong_pads.ok());
        REQUIRE(state(*other_pads) == before);
        for (Byte other = 0U; other < 4U; ++other) {
            if (other == submapper)
                continue;
            auto different = board(83U, other, 2U);
            const auto original = state(*different);
            StateReader wrong_profile(saved);
            different->load_state(wrong_profile);
            REQUIRE_FALSE(wrong_profile.ok());
            REQUIRE(state(*different) == original);
        }
    }
}

TEST_CASE("Mapper 264 snapshots bind Yoko wiring and reject other family profiles", "[mapper264]") {
    auto m = board(264U, 0U, 3U);
    write_reg(*m, 264U, 0x8000U, 0xA7U);
    write_reg(*m, 264U, 0x8100U, 0xF3U);
    write_reg(*m, 264U, 0x8318U, 0xFFU);
    m->observe_ppu_a12(true);
    m->write_expansion(0x5400U, 0xA5U);
    const auto saved = state(*m);
    REQUIRE(saved.size() == kStateBytes);

    m->power_on();
    StateReader reader(saved);
    m->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*m) == saved);
    REQUIRE(m->read_expansion(0x5400U) == 0xA5U);

    for (std::size_t length : {0U, 10U, 27U, 31U}) {
        StateReader truncated{std::span(saved).first(length)};
        m->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*m) == saved);
    }
    auto other = board(83U, 0U, 3U);
    const auto before = state(*other);
    StateReader wrong_profile(saved);
    other->load_state(wrong_profile);
    REQUIRE_FALSE(wrong_profile.ok());
    REQUIRE(state(*other) == before);
}

TEST_CASE("Cony/Yoko rejects unsupported geometry and native memory declarations",
          "[mapper083][mapper264]") {
    for (uint16_t mapper : {83U, 264U}) {
        for (unsigned submapper = 0U; submapper < (mapper == 83U ? 4U : 1U); ++submapper) {
            const auto profile = static_cast<Byte>(submapper);
            const auto [prg, chr] = sizes(mapper, profile);
            REQUIRE(ConyYoko::validate_geometry(header(mapper, profile), prg, chr));
            REQUIRE_FALSE(ConyYoko::validate_geometry(header(mapper, profile), 0U, chr));
            REQUIRE_FALSE(ConyYoko::validate_geometry(header(mapper, profile), prg, 0U));
            REQUIRE_FALSE(ConyYoko::validate_geometry(header(mapper, profile), 16385U, chr));
            REQUIRE_FALSE(ConyYoko::validate_geometry(header(mapper, profile), prg, 8193U));
            for (MirrorMode mirror :
                 {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
                auto h = header(mapper, profile);
                h.mirror_mode = mirror;
                REQUIRE_FALSE(ConyYoko::validate_geometry(h, prg, chr));
            }
            for (unsigned field = 0U; field < 5U; ++field) {
                auto h = header(mapper, profile);
                switch (field) {
                case 0U:
                    h.has_battery = !h.has_battery;
                    break;
                case 1U:
                    h.prg_ram_size = 8192U;
                    break;
                case 2U:
                    h.prg_nvram_size = h.prg_nvram_size == 0U ? 32768U : 0U;
                    break;
                case 3U:
                    h.chr_ram_size = 8192U;
                    break;
                default:
                    h.chr_nvram_size = 8192U;
                    break;
                }
                REQUIRE_FALSE(ConyYoko::validate_geometry(h, prg, chr));
            }
        }
    }

    for (std::size_t prg : {16384U, 32768U, 262144U, 1048576U, 4194304U}) {
        REQUIRE(ConyYoko::validate_geometry(header(), prg, 8192U));
        REQUIRE(ConyYoko::validate_geometry(header(83U, 0U, true), prg, 262144U));
    }
    for (std::size_t chr : {8192U, 16384U, 262144U}) {
        REQUIRE(ConyYoko::validate_geometry(header(), 16384U, chr));
        REQUIRE(ConyYoko::validate_geometry(header(83U, 1U), 16384U, chr));
        REQUIRE(ConyYoko::validate_geometry(header(264U), 16384U, chr));
    }
    REQUIRE(ConyYoko::validate_geometry(header(83U, 1U), 16384U, 524288U));
    REQUIRE(ConyYoko::validate_geometry(header(264U), 16384U, 524288U));
    REQUIRE_FALSE(ConyYoko::validate_geometry(header(), 16384U, 524288U));
    REQUIRE_FALSE(ConyYoko::validate_geometry(header(), 8388608U, 262144U));

    for (Byte submapper = 4U; submapper < 16U; ++submapper)
        REQUIRE_FALSE(ConyYoko::validate_geometry(header(83U, submapper), 262144U, 262144U));
    REQUIRE_FALSE(ConyYoko::validate_geometry(header(264U, 1U), 262144U, 262144U));
    auto legacy_yoko = header(264U, 0U, true);
    legacy_yoko.is_nes2 = false;
    REQUIRE_FALSE(ConyYoko::validate_geometry(legacy_yoko, 262144U, 262144U));
    REQUIRE_FALSE(ConyYoko::validate_geometry(header(82U), 262144U, 262144U));
    for (Byte submapper : {2U, 3U}) {
        const auto [prg, chr] = sizes(83U, submapper);
        REQUIRE_FALSE(ConyYoko::validate_geometry(header(83U, submapper), prg * 2U, chr));
        REQUIRE_FALSE(ConyYoko::validate_geometry(header(83U, submapper), prg, chr * 2U));
    }
}

} // namespace
