#include <array>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <vector>

#include "core/mappers/hp_multicart260.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
constexpr std::size_t kPrgSize = 1024U * 1024U;
constexpr std::size_t kChrSize = 1024U * 1024U;
constexpr std::size_t kStateSize = 8212U;
constexpr std::size_t kControl = 8194U;
constexpr std::size_t kBanks = 8195U;
constexpr std::size_t kMirror = 8203U;
constexpr std::size_t kIrqCounter = 8204U;
constexpr std::size_t kIrqReload = 8205U;
constexpr std::size_t kIrqFlags = 8206U;
constexpr std::size_t kRamFlags = 8207U;
constexpr std::size_t kMode = 8208U;
constexpr std::size_t kPrgBase = 8209U;
constexpr std::size_t kChrBase = 8210U;
constexpr std::size_t kCnromLatch = 8211U;

INesHeader header(bool legacy = false, bool battery = false, bool vertical = true) {
    INesHeader h{};
    h.mapper_number = 260U;
    h.is_nes2 = !legacy;
    h.has_battery = battery;
    h.prg_ram_size = !legacy && battery ? 0U : 8192U;
    h.prg_nvram_size = !legacy && battery ? 8192U : 0U;
    h.mirror_mode = vertical ? MirrorMode::Vertical : MirrorMode::Horizontal;
    return h;
}

std::vector<Byte> rom(std::size_t size, std::size_t bank_size) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i) {
        const auto bank = i / bank_size;
        const auto offset = i % bank_size;
        data[i] = offset == 0U   ? static_cast<Byte>(bank)
                  : offset == 1U ? static_cast<Byte>(bank >> 8U)
                                 : static_cast<Byte>(bank * 29U + offset + (offset >> 8U));
    }
    return data;
}

std::unique_ptr<HpMulticart260> board(bool legacy = false,
                                      bool battery = false,
                                      bool vertical = true,
                                      std::size_t prg_size = kPrgSize,
                                      std::size_t chr_size = kChrSize) {
    return std::make_unique<HpMulticart260>(
        header(legacy, battery, vertical), rom(prg_size, 8192U), rom(chr_size, 1024U));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void mmc3_bank(Mapper& mapper, Byte reg, Byte value, Byte control = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(reg | control));
    mapper.write_prg(0x8001U, value);
}

TEST_CASE("Mapper 260 exposes WRAM DIP switches and deterministic reset policies", "[mapper260]") {
    for (bool legacy : {false, true})
        for (bool battery : {false, true})
            for (bool vertical : {false, true}) {
                auto mapper = board(legacy, battery, vertical);
                REQUIRE(state(*mapper).size() == kStateSize);
                REQUIRE(mapper->battery_ram().size() == (battery ? 8192U : 0U));
                REQUIRE(mapper->mirror_mode() ==
                        (vertical ? MirrorMode::Vertical : MirrorMode::Horizontal));
                REQUIRE(mapper->read_prg(0x8000U) == 0U);
                REQUIRE(mapper->read_prg(0xA000U) == 0U);
                REQUIRE(mapper->read_prg(0xC000U) == 30U);
                REQUIRE(mapper->read_prg(0xE000U) == 31U);
                REQUIRE(mapper->read_chr(0U) == 0U);
                REQUIRE_FALSE(mapper->has_expansion_audio());
                REQUIRE_FALSE(mapper->configure_board_profile("other"));
                REQUIRE_FALSE(mapper->configure_board_jumpers(4U));
                for (Byte dip = 0U; dip < 4U; ++dip) {
                    REQUIRE(mapper->configure_board_jumpers(dip));
                    REQUIRE(mapper->read_expansion(0x5000U) == dip);
                    REQUIRE(mapper->expansion_read_mask(0x5000U) == 3U);
                }
            }
}

TEST_CASE("Mapper 260 outer registers use all F003 aliases and lock only low writes",
          "[mapper260]") {
    auto mapper = board();
    for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_expansion(static_cast<Address>(addr), 0xB6U);
        const auto decoded = addr & 0xF003U;
        REQUIRE(state(*mapper)[kMode] == (decoded == 0x5000U ? 0x86U : 0U));
        REQUIRE(state(*mapper)[kPrgBase] == (decoded == 0x5001U ? 0x36U : 0U));
        REQUIRE(state(*mapper)[kChrBase] == (decoded == 0x5002U ? 0x36U : 0U));
        REQUIRE(mapper->maps_expansion(static_cast<Address>(addr)) == (addr == 0x5000U));
    }
    mapper->write_expansion(0x5000U, 0x85U);
    for (unsigned addr = 0x5000U; addr < 0x6000U; ++addr) {
        mapper->write_expansion(static_cast<Address>(addr), 0U);
        REQUIRE(state(*mapper)[kMode] == 0x85U);
        REQUIRE(state(*mapper)[kPrgBase] == 0U);
        REQUIRE(state(*mapper)[kChrBase] == 0U);
    }
    mmc3_bank(*mapper, 6U, 9U);
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    REQUIRE(mapper->read_expansion(0x5000U) == 0U);
    mapper->reset();
    REQUIRE(state(*mapper)[kMode] == 0U);
}

TEST_CASE("Mapper 260 combines MMC3 PRG registers with every outer mode", "[mapper260]") {
    auto mapper = board();
    for (Byte mode = 0U; mode < 4U; ++mode)
        for (Byte base : {Byte{0U}, Byte{1U}, Byte{7U}, Byte{0x3FU}})
            for (Byte control : {Byte{0U}, Byte{0x40U}})
                for (unsigned value = 0U; value < 256U; ++value) {
                    mapper->write_expansion(0x5000U, mode);
                    mapper->write_expansion(0x5001U, base);
                    mmc3_bank(*mapper, 6U, static_cast<Byte>(value), control);
                    mmc3_bank(*mapper, 7U, static_cast<Byte>(value ^ 0xA7U), control);
                    const unsigned mask = (mode & 2U) != 0U ? 0x0FU : 0x1FU;
                    const unsigned outer = (static_cast<unsigned>(base) << 1U) & ~mask;
                    const std::array<unsigned, 4> inner = {
                        control == 0U ? value & 0x3FU : mask - 1U,
                        (value ^ 0xA7U) & 0x3FU,
                        control == 0U ? mask - 1U : value & 0x3FU,
                        mask,
                    };
                    for (unsigned slot = 0U; slot < 4U; ++slot) {
                        const auto bank = (inner[slot] & mask) | outer;
                        const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
                        REQUIRE(mapper->read_prg(addr) == static_cast<Byte>(bank));
                        REQUIRE(mapper->read_prg(addr + 1U) == static_cast<Byte>(bank >> 8U));
                    }
                }
}

TEST_CASE("Mapper 260 combines aligned and inverted MMC3 CHR banks with outer pins",
          "[mapper260]") {
    auto mapper = board();
    for (Byte mode = 0U; mode < 4U; ++mode)
        for (Byte base : {Byte{0U}, Byte{1U}, Byte{7U}, Byte{0x7FU}})
            for (Byte inversion : {Byte{0U}, Byte{0x80U}})
                for (Byte reg = 0U; reg < 6U; ++reg)
                    for (unsigned value = 0U; value < 256U; ++value) {
                        mapper->write_expansion(0x5000U, mode);
                        mapper->write_expansion(0x5002U, base);
                        mmc3_bank(*mapper, reg, static_cast<Byte>(value), inversion);
                        const unsigned start = reg < 2U ? reg * 2U + (inversion == 0U ? 0U : 4U)
                                                        : reg - 2U + (inversion == 0U ? 4U : 0U);
                        const unsigned mask = (mode & 1U) != 0U ? 0x7FU : 0xFFU;
                        const unsigned outer = (static_cast<unsigned>(base) << 3U) & ~mask;
                        for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                            const unsigned inner = reg < 2U ? (value & 0xFEU) | half : value;
                            const auto bank = (inner & mask) | outer;
                            const auto addr = static_cast<Address>((start + half) * 1024U);
                            REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(bank));
                            REQUIRE(mapper->read_chr(addr + 1U) == static_cast<Byte>(bank >> 8U));
                            mapper->write_chr(addr, 0xFFU);
                            REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(bank));
                        }
                    }
}

TEST_CASE("Mapper 260 NROM and CNROM modes map complete aligned windows", "[mapper260]") {
    auto mapper = board();
    for (Byte mode = 4U; mode < 8U; ++mode)
        for (unsigned prg_base = 0U; prg_base < 64U; ++prg_base)
            for (unsigned chr_base = 0U; chr_base < 128U; ++chr_base) {
                mapper->write_expansion(0x5000U, mode);
                mapper->write_expansion(0x5001U, static_cast<Byte>(prg_base));
                mapper->write_expansion(0x5002U, static_cast<Byte>(chr_base));
                mapper->write_prg(0x8000U, 7U);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    const unsigned bank = mode == 4U ? (prg_base << 1U) | (slot & 1U)
                                                     : ((prg_base & 0x3EU) << 1U) | (slot >> 1U);
                    const auto addr = static_cast<Address>(0x8000U + slot * 8192U);
                    REQUIRE(mapper->read_prg(addr) == static_cast<Byte>(bank));
                    REQUIRE(mapper->read_prg(addr + 1U) == static_cast<Byte>(bank >> 8U));
                }
                const unsigned mask = mode == 6U ? 1U : mode == 7U ? 3U : 0U;
                const unsigned base = mode < 6U ? chr_base : (3U & mask) | (chr_base & ~mask);
                const unsigned first_bank = base << 3U;
                const unsigned last_bank = first_bank | 7U;
                REQUIRE(mapper->read_chr(0U) == static_cast<Byte>(first_bank));
                REQUIRE(mapper->read_chr(1U) == static_cast<Byte>(first_bank >> 8U));
                REQUIRE(mapper->read_chr(0x1FFFU) == static_cast<Byte>(last_bank * 29U + 1026U));
                REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
                mapper->write_prg(0xFFFFU, 0U);
                REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
                REQUIRE(mapper->read_chr(0U) == static_cast<Byte>((chr_base & ~mask) << 3U));
                REQUIRE(mapper->read_chr(0x1FFFU) ==
                        static_cast<Byte>((((chr_base & ~mask) << 3U) | 7U) * 29U + 1026U));
            }
}

TEST_CASE("Mapper 260 keeps MMC3 registers live behind discrete modes", "[mapper260]") {
    auto mapper = board();
    for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
        mapper->reset();
        mapper->write_expansion(0x5000U, 4U);
        mapper->write_prg(static_cast<Address>(addr), 0xDBU);
        const auto bytes = state(*mapper);
        const auto port = addr & 0xE001U;
        REQUIRE(bytes[kCnromLatch] == 3U);
        REQUIRE(bytes[kControl] == (port == 0x8000U ? 0xC3U : 0U));
        REQUIRE(bytes[kBanks] == (port == 0x8001U ? 0xDBU : 0U));
        REQUIRE(bytes[kMirror] == (port == 0xA000U ? 0U : 1U));
        REQUIRE(bytes[kRamFlags] == (port == 0xA001U ? 0xC0U : 0x80U));
        REQUIRE(bytes[kIrqReload] == (port == 0xC000U ? 0xDBU : 0U));
        REQUIRE(bytes[kIrqCounter] == 0U);
        REQUIRE(bytes[kIrqFlags] == (port == 0xC001U ? 1U : port == 0xE001U ? 2U : 0U));
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }
    mapper->write_expansion(0x5000U, 0U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    mapper->write_prg(0xA000U, 1U);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 260 WRAM gates persistence reset and exact battery imports", "[mapper260]") {
    auto mapper = board(false, true);
    mapper->write_prg(0x6000U, 0xA7U);
    mapper->write_prg(0x7FFFU, 0x39U);
    REQUIRE(mapper->maps_prg(0x6000U));
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0x6000U, 0U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    mapper->write_prg(0xA001U, 0U);
    REQUIRE_FALSE(mapper->maps_prg(0x6000U));
    mapper->write_prg(0x6000U, 0U);
    mapper->write_prg(0xA001U, 0x80U);
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6000U) == 0xA7U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 8191>{});
    REQUIRE(state(*mapper) == before);
    std::array<Byte, 8192> imported{};
    imported.fill(0x5CU);
    mapper->set_battery_ram(imported);
    REQUIRE(mapper->read_prg(0x6123U) == 0x5CU);

    auto volatile_ram = board();
    volatile_ram->write_prg(0x6000U, 0xFFU);
    volatile_ram->reset();
    REQUIRE(volatile_ram->read_prg(0x6000U) == 0xFFU);
    volatile_ram->power_on();
    REQUIRE(volatile_ram->read_prg(0x6000U) == 0U);
    REQUIRE(volatile_ram->battery_ram().empty());
    volatile_ram->set_battery_ram(imported);
    REQUIRE(volatile_ram->read_prg(0x6000U) == 0U);
}

TEST_CASE("Mapper 260 MMC3 IRQ reload zero repetition acknowledgment and reset", "[mapper260]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
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
    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->write_prg(0xE000U, 0U);
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    mapper->write_prg(0xE001U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE(mapper->irq_pending());
    mapper->reset();
    REQUIRE_FALSE(mapper->irq_pending());
}

TEST_CASE("Mapper 260 snapshots replay partial IRQ and RAM state", "[mapper260]") {
    auto mapper = board(false, true, true);
    REQUIRE(mapper->configure_board_jumpers(2U));
    mapper->write_expansion(0x5001U, 0x23U);
    mapper->write_expansion(0x5002U, 0x45U);
    mapper->write_expansion(0x5000U, 0x86U);
    mmc3_bank(*mapper, 6U, 0x2AU, 0xC0U);
    mapper->write_prg(0x6001U, 0x91U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    auto copy = board(false, true, true);
    REQUIRE(copy->configure_board_jumpers(2U));
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6001U) == 0x91U);
    REQUIRE(copy->read_prg(0x8000U) == mapper->read_prg(0x8000U));
    REQUIRE(copy->read_chr(0U) == mapper->read_chr(0U));
    copy->clock_irq_counter();
    REQUIRE(copy->irq_pending());
    copy->write_expansion(0x5001U, 0U);
    REQUIRE(state(*copy)[kPrgBase] == 0x23U);
}

TEST_CASE("Mapper 260 malformed snapshots reject without mutation", "[mapper260]") {
    auto mapper = board(false, true, true);
    REQUIRE(mapper->configure_board_jumpers(3U));
    mapper->write_expansion(0x5001U, 0x31U);
    mapper->write_expansion(0x5000U, 0x82U);
    mapper->write_prg(0x6123U, 0x67U);
    mmc3_bank(*mapper, 6U, 0x3FU, 0xC0U);
    mapper->write_prg(0xC000U, 1U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader reader(std::span(saved).first(size));
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    const std::array<std::pair<std::size_t, Byte>, 12> invalid = {
        std::pair{0U, Byte{0x27U}},
        std::pair{kControl, Byte{0x08U}},
        std::pair{kBanks + 6U, Byte{0x40U}},
        std::pair{kBanks + 7U, Byte{0x80U}},
        std::pair{kMirror, Byte{2U}},
        std::pair{kIrqFlags, Byte{8U}},
        std::pair{kIrqFlags, Byte{4U}},
        std::pair{kRamFlags, Byte{1U}},
        std::pair{kMode, Byte{8U}},
        std::pair{kPrgBase, Byte{0x40U}},
        std::pair{kChrBase, Byte{0x80U}},
        std::pair{kCnromLatch, Byte{8U}},
    };
    for (const auto& [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        StateReader reader(bad);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    }
    for (bool battery : {false, true})
        for (bool vertical : {false, true})
            for (Byte dip = 0U; dip < 4U; ++dip) {
                auto other = board(false, battery, vertical);
                REQUIRE(other->configure_board_jumpers(dip));
                const auto before = state(*other);
                if (before[0] == saved[0] && before[1] == saved[1])
                    continue;
                StateReader reader(saved);
                other->load_state(reader);
                REQUIRE_FALSE(reader.ok());
                REQUIRE(state(*other) == before);
            }
}

TEST_CASE("Mapper 260 geometry validation rejects unsupported memory and metadata", "[mapper260]") {
    const auto valid = [](const INesHeader& h, std::size_t p, std::size_t c) {
        return HpMulticart260::validate_geometry(h, p, c).has_value();
    };
    for (bool legacy : {false, true})
        for (bool battery : {false, true}) {
            REQUIRE(valid(header(legacy, battery), 16384U, 8192U));
            REQUIRE(valid(header(legacy, battery), 49152U, 24576U));
            REQUIRE(valid(header(legacy, battery), kPrgSize, kChrSize));
        }
    for (auto size : std::array<std::size_t, 6>{0U, 1U, 8192U, 16383U, 16385U, kPrgSize + 16384U})
        REQUIRE_FALSE(valid(header(), size, 8192U));
    for (auto size : std::array<std::size_t, 6>{0U, 1U, 4096U, 8191U, 8193U, kChrSize + 8192U})
        REQUIRE_FALSE(valid(header(), 16384U, size));
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        REQUIRE_FALSE(valid(h, 16384U, 8192U));
    }
    for (auto mirror : {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto h = header();
        h.mirror_mode = mirror;
        REQUIRE_FALSE(valid(h, 16384U, 8192U));
    }
    for (unsigned field = 0U; field < 5U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.prg_ram_size = 0U;
            break;
        case 1U:
            h.prg_ram_size = 4096U;
            h.prg_nvram_size = 4096U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        default:
            h.chr_nvram_size = 8192U;
            break;
        }
        REQUIRE_FALSE(valid(h, 16384U, 8192U));
    }
    auto wrong_battery = header();
    wrong_battery.has_battery = true;
    REQUIRE_FALSE(valid(wrong_battery, 16384U, 8192U));

    auto wrapped = board(false, false, true, 49152U, 24576U);
    wrapped->write_expansion(0x5001U, 0x3FU);
    wrapped->write_expansion(0x5002U, 0x7FU);
    REQUIRE(wrapped->read_prg(0xE000U) == 1U);
    REQUIRE(wrapped->read_chr(0U) == 0U);
}

} // namespace
