#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/mappers/fds_conversion108.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kPrgSize = 131072U;

MirrorMode default_mirror(Byte submapper) {
    return submapper == 1U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::size_t default_chr_size(Byte submapper) {
    if (submapper == 2U)
        return 65536U;
    return submapper == 4U ? 16384U : 0U;
}

INesHeader header(Byte submapper,
                  std::size_t chr_size,
                  bool legacy = false,
                  std::optional<MirrorMode> mirror = std::nullopt) {
    INesHeader h{};
    h.mapper_number = 108U;
    h.is_nes2 = !legacy;
    h.submapper = legacy ? 0U : submapper;
    h.mirror_mode = mirror.value_or(default_mirror(submapper));
    h.prg_ram_size = legacy ? 8192U : 0U;
    h.chr_ram_size = chr_size == 0U ? 8192U : 0U;
    return h;
}

Byte prg_cell(std::size_t index) {
    return static_cast<Byte>((index / 8192U) * 29U + index * 7U + (index >> 8U));
}

Byte chr_cell(std::size_t index) {
    return static_cast<Byte>((index / 8192U) * 43U + index * 11U + (index >> 9U));
}

std::vector<Byte> patterned(std::size_t size, bool chr) {
    std::vector<Byte> bytes(size);
    for (std::size_t index = 0U; index < size; ++index)
        bytes[index] = chr ? chr_cell(index) : prg_cell(index);
    return bytes;
}

std::unique_ptr<FdsConversion108> board(Byte submapper,
                                        std::size_t prg_size = kPrgSize,
                                        std::size_t chr_size = 0U,
                                        bool legacy = false,
                                        std::optional<MirrorMode> mirror = std::nullopt) {
    chr_size = chr_size == 0U ? default_chr_size(submapper) : chr_size;
    const auto h = header(submapper, chr_size, legacy, mirror);
    REQUIRE(FdsConversion108::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<FdsConversion108>(
        h, patterned(prg_size, false), patterned(chr_size, true));
}

Address register_address(Byte submapper) {
    if (submapper == 1U)
        return 0xF000U;
    return submapper == 2U ? 0xE000U : 0x8000U;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

TEST_CASE("Mapper 108 powers on with fixed upper PRG and the documented CHR populations",
          "[mapper108]") {
    for (Byte submapper = 1U; submapper <= 4U; ++submapper) {
        for (MirrorMode mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            if ((submapper == 1U || submapper == 3U) && mirror != default_mirror(submapper))
                continue;
            auto mapper = board(submapper, kPrgSize, default_chr_size(submapper), false, mirror);
            REQUIRE(mapper->mirror_mode() == mirror);
            REQUIRE(mapper->battery_ram().empty());
            REQUIRE(mapper->prg_read_mask(0x6000U) == 0xFFU);
            REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
            REQUIRE_FALSE(mapper->maps_expansion(0x5000U));
            REQUIRE_FALSE(mapper->has_expansion_audio());
            REQUIRE_FALSE(mapper->irq_pending());
            REQUIRE(state(*mapper).size() == (submapper == 1U || submapper == 3U ? 8204U : 12U));

            for (Address addr : {0x8000U, 0x9A55U, 0xBFFFU, 0xC000U, 0xE000U, 0xFFFFU}) {
                const auto index = kPrgSize - 32768U + (addr & 0x7FFFU);
                REQUIRE(mapper->read_prg(addr) == prg_cell(index));
            }
            const auto low_index = submapper == 4U ? kPrgSize - 8192U : 0U;
            REQUIRE(mapper->read_prg(0x6000U) == prg_cell(low_index));
            REQUIRE(mapper->read_prg(0x7FFFU) == prg_cell(low_index + 8191U));
            REQUIRE(mapper->read_prg(0x5FFFU) == 0U);
            REQUIRE_FALSE(mapper->maps_prg(0x5FFFU));
            REQUIRE(mapper->maps_prg(0x6000U));
            REQUIRE(mapper->read_chr(0x0123U) ==
                    (submapper == 1U || submapper == 3U ? 0U : chr_cell(0x0123U)));
            mapper->write_chr(0x0123U, 0xA7U);
            REQUIRE(mapper->read_chr(0x0123U) ==
                    (submapper == 1U || submapper == 3U ? 0xA7U : chr_cell(0x0123U)));
        }
    }
}

TEST_CASE("Mapper 108 decodes every CPU address only in its assigned submapper windows",
          "[mapper108]") {
    for (Byte submapper = 1U; submapper <= 4U; ++submapper) {
        auto mapper = board(submapper);
        for (unsigned addr = 0U; addr <= 0xFFFFU; ++addr) {
            mapper->reset();
            mapper->write_prg(static_cast<Address>(addr), 0xA5U);
            const bool prg_selected = (submapper == 1U && addr >= 0xF000U) ||
                                      (submapper == 2U && addr >= 0xE000U) ||
                                      (submapper == 3U && addr >= 0x8000U);
            const bool chr_selected =
                (submapper == 2U && addr >= 0xE000U) || (submapper == 4U && addr >= 0x8000U);
            const auto low_index =
                submapper == 4U ? kPrgSize - 8192U : (prg_selected ? 5U * 8192U : 0U);
            REQUIRE(mapper->read_prg(0x6000U) == prg_cell(low_index));
            REQUIRE(mapper->read_prg(0x8000U) == prg_cell(kPrgSize - 32768U));
            if (submapper == 2U || submapper == 4U) {
                const auto bank =
                    submapper == 2U ? (chr_selected ? 5U : 0U) : (chr_selected ? 1U : 0U);
                REQUIRE(mapper->read_chr(0U) == chr_cell(bank * 8192U));
            }
            REQUIRE(mapper->maps_prg(static_cast<Address>(addr)) == (addr >= 0x6000U));
        }
    }
}

TEST_CASE("Mapper 108 latches all data bits without bus conflicts and wraps fitted ROMs",
          "[mapper108]") {
    for (Byte submapper = 1U; submapper <= 4U; ++submapper) {
        auto mapper = board(submapper);
        for (unsigned value = 0U; value < 256U; ++value) {
            mapper->reset();
            mapper->write_prg(register_address(submapper), static_cast<Byte>(value));
            const auto saved = state(*mapper);
            REQUIRE(saved[10] == (submapper == 4U ? 0U : value));
            REQUIRE(saved[11] == (submapper == 1U || submapper == 3U ? 0U : value));

            const auto low_bank = submapper == 4U ? 15U : value & 15U;
            REQUIRE(mapper->read_prg(0x6123U) == prg_cell(low_bank * 8192U + 0x123U));
            if (submapper == 2U || submapper == 4U) {
                const auto chr_bank = value & (submapper == 2U ? 7U : 1U);
                REQUIRE(mapper->read_chr(0x0234U) == chr_cell(chr_bank * 8192U + 0x234U));
            }
        }
    }
}

TEST_CASE("Mapper 108 maps every PRG and CHR cell without serializing immutable ROM",
          "[mapper108]") {
    for (Byte submapper = 1U; submapper <= 4U; ++submapper) {
        auto mapper = board(submapper);
        for (Address addr = 0x8000U; addr >= 0x8000U; ++addr) {
            REQUIRE(mapper->read_prg(addr) == prg_cell(kPrgSize - 32768U + (addr & 0x7FFFU)));
            if (addr == 0xFFFFU)
                break;
        }
        if (submapper != 4U) {
            for (Byte bank = 0U; bank < 16U; ++bank) {
                mapper->write_prg(register_address(submapper), bank);
                for (Address addr = 0x6000U; addr < 0x8000U; ++addr)
                    REQUIRE(mapper->read_prg(addr) ==
                            prg_cell(static_cast<std::size_t>(bank) * 8192U + (addr & 0x1FFFU)));
            }
        }
        if (submapper == 2U || submapper == 4U) {
            const Byte banks = submapper == 2U ? 8U : 2U;
            for (Byte bank = 0U; bank < banks; ++bank) {
                mapper->write_prg(register_address(submapper), bank);
                for (Address addr = 0U; addr < 8192U; ++addr) {
                    mapper->write_chr(addr, static_cast<Byte>(bank ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) ==
                            chr_cell(static_cast<std::size_t>(bank) * 8192U + addr));
                }
            }
        } else {
            for (Address addr = 0U; addr < 8192U; ++addr)
                mapper->write_chr(addr, static_cast<Byte>(addr ^ (addr >> 8U)));
            for (Address addr = 0U; addr < 8192U; ++addr)
                REQUIRE(mapper->read_chr(addr) == static_cast<Byte>(addr ^ (addr >> 8U)));
        }
        auto larger =
            board(submapper, 262144U, submapper == 2U ? 131072U : default_chr_size(submapper));
        REQUIRE(state(*larger).size() == state(*mapper).size());
    }
}

TEST_CASE("Mapper 108 legacy images use only the article geometry and mirroring heuristic",
          "[mapper108]") {
    auto first = board(1U, kPrgSize, 0U, true);
    first->write_prg(0xEFFFU, 7U);
    REQUIRE(first->read_prg(0x6000U) == prg_cell(0U));
    first->write_prg(0xF000U, 7U);
    REQUIRE(first->read_prg(0x6000U) == prg_cell(7U * 8192U));

    auto third = board(3U, kPrgSize, 0U, true);
    third->write_prg(0x8000U, 6U);
    REQUIRE(third->read_prg(0x6000U) == prg_cell(6U * 8192U));

    for (std::size_t chr_size : {8192U, 16384U}) {
        auto fourth = board(4U, kPrgSize, chr_size, true);
        fourth->write_prg(0x8000U, 0xFFU);
        REQUIRE(fourth->read_prg(0x6000U) == prg_cell(kPrgSize - 8192U));
        REQUIRE(fourth->read_chr(0U) == chr_cell(chr_size == 16384U ? 8192U : 0U));
    }

    auto second = board(2U, kPrgSize, 32768U, true);
    second->write_prg(0xDFFFU, 3U);
    REQUIRE(second->read_prg(0x6000U) == prg_cell(0U));
    REQUIRE(second->read_chr(0U) == chr_cell(0U));
    second->write_prg(0xE000U, 3U);
    REQUIRE(second->read_prg(0x6000U) == prg_cell(3U * 8192U));
    REQUIRE(second->read_chr(0U) == chr_cell(3U * 8192U));
}

TEST_CASE("Mapper 108 validates submappers ROM geometry fixed mirroring and native memory",
          "[mapper108]") {
    for (Byte submapper = 1U; submapper <= 4U; ++submapper) {
        const auto chr_size = default_chr_size(submapper);
        for (std::size_t prg_size = 32768U; prg_size <= 2097152U; prg_size *= 2U)
            REQUIRE(FdsConversion108::validate_geometry(
                header(submapper, chr_size), prg_size, chr_size));
        for (std::size_t prg_size : {0U, 8192U, 16384U, 24576U, 32767U, 49152U, 2097153U, 4194304U})
            REQUIRE_FALSE(FdsConversion108::validate_geometry(
                header(submapper, chr_size), prg_size, chr_size));
        for (MirrorMode mirror :
             {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper})
            REQUIRE_FALSE(FdsConversion108::validate_geometry(
                header(submapper, chr_size, false, mirror), kPrgSize, chr_size));

        auto battery = header(submapper, chr_size);
        battery.has_battery = true;
        REQUIRE_FALSE(FdsConversion108::validate_geometry(battery, kPrgSize, chr_size));
        battery = header(submapper, chr_size, true);
        battery.has_battery = true;
        REQUIRE_FALSE(FdsConversion108::validate_geometry(battery, kPrgSize, chr_size));

        for (unsigned field = 0U; field < 4U; ++field) {
            auto invalid = header(submapper, chr_size);
            switch (field) {
            case 0U:
                invalid.prg_ram_size = 8192U;
                break;
            case 1U:
                invalid.prg_nvram_size = 8192U;
                break;
            case 2U:
                invalid.chr_ram_size = chr_size == 0U ? 0U : 8192U;
                break;
            default:
                invalid.chr_nvram_size = 8192U;
                break;
            }
            REQUIRE_FALSE(FdsConversion108::validate_geometry(invalid, kPrgSize, chr_size));
        }
    }

    for (Byte submapper : {0U, 5U, 6U, 15U, 255U})
        REQUIRE_FALSE(
            FdsConversion108::validate_geometry(header(submapper, 65536U), kPrgSize, 65536U));
    for (Byte submapper : {1U, 3U}) {
        REQUIRE_FALSE(
            FdsConversion108::validate_geometry(header(submapper, 8192U), kPrgSize, 8192U));
        const auto wrong_mirror = submapper == 1U ? MirrorMode::Vertical : MirrorMode::Horizontal;
        REQUIRE_FALSE(FdsConversion108::validate_geometry(
            header(submapper, 0U, false, wrong_mirror), kPrgSize, 0U));
    }
    for (std::size_t chr_size : {0U, 8192U, 16384U, 24576U, 2097153U, 4194304U})
        REQUIRE_FALSE(
            FdsConversion108::validate_geometry(header(2U, chr_size), kPrgSize, chr_size));
    for (std::size_t chr_size = 32768U; chr_size <= 2097152U; chr_size *= 2U)
        REQUIRE(FdsConversion108::validate_geometry(header(2U, chr_size), kPrgSize, chr_size));
    for (std::size_t chr_size : {0U, 4096U, 24576U, 32768U, 65536U})
        REQUIRE_FALSE(
            FdsConversion108::validate_geometry(header(4U, chr_size), kPrgSize, chr_size));
    for (Byte submapper : {2U, 4U})
        for (MirrorMode mirror : {MirrorMode::Horizontal, MirrorMode::Vertical})
            REQUIRE(FdsConversion108::validate_geometry(
                header(submapper, default_chr_size(submapper), false, mirror),
                kPrgSize,
                default_chr_size(submapper)));
}

TEST_CASE("Mapper 108 reset clears latches while cold power alone clears volatile CHR RAM",
          "[mapper108]") {
    for (Byte submapper = 1U; submapper <= 4U; ++submapper) {
        auto mapper = board(submapper);
        mapper->write_prg(register_address(submapper), 0xA5U);
        mapper->write_chr(0x0123U, 0x5AU);
        mapper->reset();
        REQUIRE(state(*mapper)[10] == 0U);
        REQUIRE(state(*mapper)[11] == 0U);
        REQUIRE(mapper->read_chr(0x0123U) ==
                (submapper == 1U || submapper == 3U ? 0x5AU : chr_cell(0x0123U)));
        mapper->power_on();
        REQUIRE(mapper->read_chr(0x0123U) ==
                (submapper == 1U || submapper == 3U ? 0U : chr_cell(0x0123U)));
        mapper->set_battery_ram(std::vector<Byte>(8192U, 0xA7U));
        REQUIRE(mapper->battery_ram().empty());
    }
}

TEST_CASE("Mapper 108 snapshots replay and reject malformed or mismatched state atomically",
          "[mapper108]") {
    for (Byte submapper = 1U; submapper <= 4U; ++submapper) {
        auto mapper = board(submapper);
        mapper->write_prg(register_address(submapper), 0xA5U);
        for (Address addr = 0U; addr < 8192U; ++addr)
            mapper->write_chr(addr, static_cast<Byte>(addr * 17U + (addr >> 8U)));
        const auto saved = state(*mapper);

        for (std::size_t length = 0U; length < saved.size(); ++length) {
            StateReader reader(std::span<const Byte>(saved).first(length));
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
        for (std::size_t index = 0U; index < 10U; ++index) {
            auto corrupt = saved;
            corrupt[index] ^= 0x80U;
            StateReader reader(corrupt);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }
        if (submapper == 1U || submapper == 3U || submapper == 4U) {
            auto corrupt = saved;
            corrupt[submapper == 4U ? 10U : 11U] = 1U;
            StateReader reader(corrupt);
            mapper->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == saved);
        }

        for (Byte other_submapper = 1U; other_submapper <= 4U; ++other_submapper) {
            if (other_submapper == submapper)
                continue;
            auto other = board(other_submapper);
            const auto before = state(*other);
            StateReader reader(saved);
            other->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*other) == before);
        }
        for (unsigned variant = 0U; variant < 3U; ++variant) {
            const auto other_prg_size =
                variant == 0U || (variant == 1U && submapper != 2U && submapper != 4U) ? 262144U
                : variant == 2U && (submapper == 1U || submapper == 3U)                ? 524288U
                                                                                       : kPrgSize;
            const auto other_chr_size = submapper == 2U && variant == 1U ? 131072U
                                        : submapper == 4U && variant == 1U
                                            ? 8192U
                                            : default_chr_size(submapper);
            auto other = board(
                submapper,
                other_prg_size,
                other_chr_size,
                false,
                variant == 2U && (submapper == 2U || submapper == 4U)
                    ? std::optional<MirrorMode>(default_mirror(submapper) == MirrorMode::Horizontal
                                                    ? MirrorMode::Vertical
                                                    : MirrorMode::Horizontal)
                    : std::nullopt);
            const auto before = state(*other);
            StateReader reader(saved);
            other->load_state(reader);
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*other) == before);
        }

        mapper->power_on();
        mapper->write_prg(register_address(submapper), 0x11U);
        StateReader reader(saved);
        mapper->load_state(reader);
        REQUIRE(reader.ok());
        REQUIRE(reader.remaining() == 0U);
        REQUIRE(state(*mapper) == saved);
    }
}

} // namespace
