#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/emulator.hpp"
#include "core/input/controller.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {
INesHeader header(uint16_t id, std::size_t ram = 2048U) {
    INesHeader h{};
    h.mapper_number = id;
    h.is_nes2 = true;
    h.mirror_mode = MirrorMode::Vertical;
    h.chr_ram_size = ram;
    return h;
}
std::vector<Byte> banks(std::size_t size, std::size_t unit) {
    std::vector<Byte> data(size);
    for (std::size_t i = 0U; i < size; ++i)
        data[i] = static_cast<Byte>(i / unit);
    return data;
}
std::unique_ptr<Mapper> board(INesHeader h, std::size_t p = 0x80000U, std::size_t c = 0x40000U) {
    register_builtin_mappers();
    auto r = MapperRegistry::instance().create(h, banks(p, 8192U), banks(c, 1024U));
    REQUIRE(r);
    return std::move(*r);
}
void bank(Mapper& m, Byte reg, Byte value, Byte mode = 0U) {
    m.write_prg(0x8000U, static_cast<Byte>(reg | mode));
    m.write_prg(0x8001U, value);
}
TEST_CASE("Waixing 74 legacy banks 8-9 are RAM and bank 10 remains ROM",
          "[mappers][waixing-mixed]") {
    auto h = header(74U);
    h.is_nes2 = false;
    h.chr_ram_size = 0U;
    auto m = board(h);
    for (Byte page : {8U, 9U}) {
        bank(*m, 2U, page);
        m->write_chr(0x1000U, 0xA7U);
        REQUIRE(m->read_chr(0x1000U) == 0xA7U);
    }
    bank(*m, 2U, 10U);
    m->write_chr(0x1000U, 0U);
    REQUIRE(m->read_chr(0x1000U) == 10U);
}
TEST_CASE("Waixing 192 legacy banks 8-11 are RAM and bank 12 remains ROM",
          "[mappers][waixing-mixed]") {
    auto h = header(192U);
    h.is_nes2 = false;
    h.chr_ram_size = 0U;
    auto m = board(h);
    for (Byte page : {8U, 9U, 10U, 11U}) {
        bank(*m, 2U, page);
        m->write_chr(0x1000U, 0xB9U);
        REQUIRE(m->read_chr(0x1000U) == 0xB9U);
    }
    bank(*m, 2U, 12U);
    m->write_chr(0x1000U, 0U);
    REQUIRE(m->read_chr(0x1000U) == 12U);
}
TEST_CASE("Mapper 194 legacy banks zero and one are writable while bank two remains ROM",
          "[mappers][waixing-mixed][mapper194]") {
    auto h = header(194U);
    h.is_nes2 = false;
    h.chr_ram_size = 0U;
    auto m = board(h);
    for (Byte page : {0U, 1U}) {
        bank(*m, 2U, page);
        m->write_chr(0x1000U, static_cast<Byte>(0xA0U + page));
        REQUIRE(m->read_chr(0x1000U) == 0xA0U + page);
    }
    bank(*m, 2U, 2U);
    m->write_chr(0x1000U, 0U);
    REQUIRE(m->read_chr(0x1000U) == 2U);
}
TEST_CASE("Mapper 194 without CHR ROM mirrors the selected physical RAM page",
          "[mappers][waixing-mixed][mapper194]") {
    auto m = board(header(194U), 32768U, 0U);
    bank(*m, 2U, 1U);
    m->write_chr(0x13FFU, 0x73U);
    bank(*m, 2U, 0xFFU);
    REQUIRE(m->read_chr(0x13FFU) == 0x73U);
}
std::vector<Byte> state(const Mapper& m) {
    StateWriter w;
    m.save_state(w);
    return w.take();
}
TEST_CASE("Mapper 194 all CHR bytes respect bank-zero RAM, pairing and inversion",
          "[mappers][waixing-mixed][mapper194]") {
    constexpr std::array<Address, 6> kSlots = {0U, 0x800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
    for (std::size_t size : {2048UZ, 4096UZ})
        for (bool ram_only : {false, true}) {
            auto m = board(header(194U, size), 0x80000U, ram_only ? 0U : 0x40000U);
            std::array<Byte, 4> ram{};
            for (Byte mode : {0U, 0x80U})
                for (Byte reg = 0U; reg < 6U; ++reg)
                    for (unsigned value = 0U; value < 256U; ++value) {
                        bank(*m, reg, static_cast<Byte>(value), mode);
                        const Address base =
                            static_cast<Address>(kSlots[reg] ^ (mode == 0U ? 0U : 0x1000U));
                        for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                            const unsigned page = reg < 2U ? (value & 0xFEU) + half : value;
                            const Address a = static_cast<Address>(base + half * 1024U + 0x3FFU);
                            const bool writable = ram_only || page < size / 1024U;
                            const auto physical = page % (size / 1024U);
                            REQUIRE(m->read_chr(a) == (writable ? ram[physical] : page));
                            const auto data = static_cast<Byte>(value ^ 0xB9U);
                            m->write_chr(a, data);
                            if (writable)
                                ram[physical] = data;
                            REQUIRE(m->read_chr(a) == (writable ? data : page));
                        }
                    }
        }
}
TEST_CASE("Mapper 194 mixed RAM does not alias through wrapped ROM banks or WRAM gates",
          "[mappers][waixing-mixed][mapper194]") {
    for (std::size_t size : {2048UZ, 4096UZ}) {
        auto m = board(header(194U, size), 98304U, 16384U);
        for (Byte gate : {0U, 0x80U, 0xC0U}) {
            m->write_prg(0xA001U, gate);
            bank(*m, 2U, 0U);
            m->write_chr(0x1000U, 0xA7U);
            for (Byte page : {0x10U, 0x20U, 0x80U, 0xF0U}) {
                bank(*m, 2U, page);
                REQUIRE(m->read_chr(0x1000U) == 0U);
                m->write_chr(0x1000U, 0x31U);
                REQUIRE(m->read_chr(0x1000U) == 0U);
            }
            bank(*m, 2U, 0U);
            REQUIRE(m->read_chr(0x1000U) == 0xA7U);
        }
        const auto boundary = static_cast<Byte>(size / 1024U);
        bank(*m, 2U, boundary);
        m->write_chr(0x1000U, 0x31U);
        REQUIRE(m->read_chr(0x1000U) == boundary);
        m->reset();
        REQUIRE(m->read_chr(0U) == 0xA7U);
    }
}
TEST_CASE("Mapper 194 snapshots reject other RAM origins and ROM-presence configurations",
          "[mappers][waixing-mixed][mapper194]") {
    for (std::size_t size : {2048UZ, 4096UZ})
        for (bool ram_only : {false, true}) {
            auto h = header(194U, size);
            h.has_battery = true;
            h.prg_nvram_size = 8192U;
            const auto chr_size = ram_only ? 0U : 0x40000U;
            auto m = board(h, 0x80000U, chr_size);
            for (unsigned page = 0U; page < size / 1024U; ++page) {
                bank(*m, 2U, static_cast<Byte>(page));
                m->write_chr(0x13FFU, static_cast<Byte>(0x50U + page));
            }
            bank(*m, 2U, 1U, 0x80U);
            m->write_prg(0x6000U, 0x71U);
            m->write_prg(0xC000U, 2U);
            m->write_prg(0xC001U, 0U);
            m->write_prg(0xE001U, 0U);
            m->clock_irq_counter();
            const auto saved = state(*m);
            auto copy = board(h, 0x80000U, chr_size);
            StateReader r(saved);
            copy->load_state(r);
            REQUIRE(r.ok());
            REQUIRE(r.remaining() == 0U);
            REQUIRE((state(*copy) == saved));
            REQUIRE(copy->read_chr(0x3FFU) == 0x51U);
            REQUIRE(copy->read_prg(0x6000U) == 0x71U);
            copy->clock_irq_counter();
            REQUIRE_FALSE(copy->irq_pending());
            copy->clock_irq_counter();
            REQUIRE(copy->irq_pending());
            copy->write_prg(0xE000U, 0U);
            REQUIRE_FALSE(copy->irq_pending());
            copy->write_prg(0xC000U, 0U);
            copy->write_prg(0xE001U, 0U);
            for (unsigned i = 0U; i < 3U; ++i) {
                copy->clock_irq_counter();
                REQUIRE(copy->irq_pending());
                copy->acknowledge_irq();
            }
            for (unsigned page = 0U; page < size / 1024U; ++page) {
                bank(*copy, 2U, static_cast<Byte>(page));
                REQUIRE(copy->read_chr(0x13FFU) == 0x50U + page);
            }
            for (auto incompatible : {header(74U, size),
                                      header(192U, size),
                                      header(194U, size == 2048U ? 4096U : 2048U)}) {
                auto other = board(incompatible);
                StateReader cross(saved);
                other->load_state(cross);
                REQUIRE_FALSE(cross.ok());
            }
            auto other = board(h, 0x80000U, ram_only ? 0x40000U : 0U);
            StateReader cross(saved);
            other->load_state(cross);
            REQUIRE_FALSE(cross.ok());
            auto original_74 = board(header(74U, size));
            const auto before_74 = state(*original_74);
            StateReader old_payload(before_74);
            copy->load_state(old_payload);
            REQUIRE_FALSE(old_payload.ok());
            for (std::size_t offset :
                 {16384UZ, 16385UZ, 16392UZ, 16394UZ, saved.size() - 2U, saved.size() - 1U}) {
                auto bad = saved;
                bad[offset] = 0xFFU;
                StateReader invalid(bad);
                copy->load_state(invalid);
                REQUIRE_FALSE(invalid.ok());
            }
            StateReader truncated(std::span{saved}.first(saved.size() - 1U));
            copy->load_state(truncated);
            REQUIRE_FALSE(truncated.ok());
        }
}
TEST_CASE("Mapper 194 validates populations, submapper and RAM-only declarations",
          "[mappers][waixing-mixed][mapper194]") {
    register_builtin_mappers();
    for (unsigned kind = 0U; kind < 17U; ++kind) {
        auto h = header(194U);
        std::size_t p = 0x80000U, c = 0x40000U;
        switch (kind) {
        case 0U:
            h.submapper = 1U;
            break;
        case 1U:
            h.chr_ram_size = 0U;
            break;
        case 2U:
            h.chr_ram_size = 1024U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        case 4U:
            h.chr_nvram_size = 2048U;
            break;
        case 5U:
            h.has_battery = true;
            break;
        case 6U:
            h.prg_nvram_size = 8192U;
            break;
        case 7U:
            h.prg_ram_size = 4096U;
            break;
        case 8U:
            h.prg_ram_size = 16384U;
            break;
        case 9U:
            h.has_battery = true;
            h.prg_ram_size = h.prg_nvram_size = 8192U;
            break;
        case 10U:
            p = 16384U;
            break;
        case 11U:
            p = 0x82000U;
            break;
        case 12U:
            p = 32769U;
            break;
        case 13U:
            c = 1024U;
            break;
        case 14U:
            c = 0x40400U;
            break;
        case 15U:
            c = 8193U;
            break;
        default:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        }
        REQUIRE_FALSE(
            MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
    }
    for (bool legacy : {false, true})
        for (std::size_t size : {2048UZ, 4096UZ}) {
            auto h = header(194U, size);
            h.is_nes2 = !legacy;
            auto m = board(h, 32768U, 0U);
            REQUIRE_FALSE(m->maps_prg(0x6000U));
            bank(*m, 2U, 0U);
            m->write_chr(0x1000U, 0x5DU);
            bank(*m, 2U, static_cast<Byte>((legacy ? 2048U : size) / 1024U));
            REQUIRE(m->read_chr(0x1000U) == 0x5DU);
        }
}
TEST_CASE("Waixing all CHR bytes select exact RAM pages after pairing and inversion",
          "[mappers][waixing-mixed]") {
    constexpr std::array<Address, 6> kSlots = {0U, 0x800U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U};
    for (uint16_t id : {74U, 192U})
        for (std::size_t size : {2048UZ, 4096UZ}) {
            auto m = board(header(id, size));
            std::array<Byte, 4> ram{};
            for (Byte mode : {0U, 0x80U})
                for (Byte reg = 0U; reg < 6U; ++reg)
                    for (unsigned value = 0U; value < 256U; ++value) {
                        bank(*m, reg, static_cast<Byte>(value), mode);
                        const Address base =
                            static_cast<Address>(kSlots[reg] ^ (mode == 0U ? 0U : 0x1000U));
                        for (unsigned half = 0U; half < (reg < 2U ? 2U : 1U); ++half) {
                            const auto page = reg < 2U ? (value & 0xFEU) + half : value;
                            const auto address = static_cast<Address>(base + half * 1024U + 0x3FFU);
                            const bool writable = page >= 8U && page < 8U + size / 1024U;
                            REQUIRE(m->read_chr(address) == (writable ? ram[page - 8U] : page));
                            const auto data = static_cast<Byte>(value ^ 0xA7U);
                            m->write_chr(address, data);
                            if (writable)
                                ram[page - 8U] = data;
                            REQUIRE(m->read_chr(address) == (writable ? data : page));
                        }
                    }
        }
}
TEST_CASE("Waixing raw RAM decoder precedes ROM wrapping and never mirrors RAM into other banks",
          "[mappers][waixing-mixed]") {
    for (uint16_t id : {74U, 192U})
        for (std::size_t size : {2048UZ, 4096UZ}) {
            auto m = board(header(id, size), 98304U, 16384U);
            for (unsigned page = 8U; page < 8U + size / 1024U; ++page) {
                bank(*m, 2U, static_cast<Byte>(page));
                m->write_chr(0x1000U, static_cast<Byte>(0x90U + page));
            }
            for (Byte page : {0U, 7U, 10U, 11U, 12U, 16U, 24U, 0x88U, 0x89U, 0xF8U}) {
                bank(*m, 2U, page);
                const bool writable = page >= 8U && page < 8U + size / 1024U;
                REQUIRE(m->read_chr(0x1000U) == (writable ? 0x90U + page : page % 16U));
                if (!writable) {
                    m->write_chr(0x1000U, 0U);
                    REQUIRE(m->read_chr(0x1000U) == page % 16U);
                }
            }
            for (Byte gate : {0U, 0x80U, 0xC0U}) {
                m->write_prg(0xA001U, gate);
                bank(*m, 2U, 8U);
                REQUIRE(m->read_chr(0x1000U) == 0x98U);
                m->write_chr(0x1000U, 0x31U);
                REQUIRE(m->read_chr(0x1000U) == 0x31U);
                m->write_chr(0x1000U, 0x98U);
            }
            m->reset();
            bank(*m, 2U, 8U);
            REQUIRE(m->read_chr(0x1000U) == 0x98U);
        }
}
TEST_CASE("Waixing NES 2.0 uses the same memory population on both IDs",
          "[mappers][waixing-mixed]") {
    for (std::size_t size : {2048UZ, 4096UZ}) {
        auto a = board(header(74U, size)), b = board(header(192U, size));
        for (Byte page : {7U, 8U, 9U, 10U, 11U, 12U, 0x88U}) {
            bank(*a, 2U, page, 0x80U);
            bank(*b, 2U, page, 0x80U);
            a->write_chr(0U, static_cast<Byte>(page + 0x40U));
            b->write_chr(0U, static_cast<Byte>(page + 0x40U));
            REQUIRE(a->read_chr(0U) == b->read_chr(0U));
        }
        REQUIRE((state(*a) == state(*b)));
    }
}
TEST_CASE("Waixing standard PRG modes, register aliases and H/V mirroring remain active",
          "[mappers][waixing-mixed]") {
    for (uint16_t id : {74U, 192U})
        for (std::size_t bytes : {0x80000UZ, 98304UZ}) {
            auto m = board(header(id), bytes);
            for (Byte mode : {0U, 0x40U})
                for (unsigned value = 0U; value < 256U; ++value) {
                    bank(*m, 6U, static_cast<Byte>(value), mode);
                    m->write_prg(0x9FFEU, static_cast<Byte>(mode | 7U));
                    m->write_prg(0x9FFFU, 0xA5U);
                    const auto count = bytes / 8192U;
                    const std::array<std::size_t, 4> expected =
                        mode == 0U
                            ? std::array<std::size_t, 4>{(value & 63U) % count,
                                                         37U % count,
                                                         count - 2U,
                                                         count - 1U}
                            : std::array<std::size_t, 4>{
                                  count - 2U, 37U % count, (value & 63U) % count, count - 1U};
                    for (unsigned slot = 0U; slot < 4U; ++slot)
                        REQUIRE(m->read_prg(static_cast<Address>(0x8000U + slot * 8192U)) ==
                                expected[slot]);
                }
            for (unsigned value = 0U; value < 256U; ++value) {
                m->write_prg(0xBFFEU, static_cast<Byte>(value));
                REQUIRE(m->mirror_mode() ==
                        ((value & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical));
            }
            m->reset();
            REQUIRE(m->mirror_mode() == MirrorMode::Vertical);
        }
}
TEST_CASE("Waixing snapshots retain every RAM page and validate the immutable population",
          "[mappers][waixing-mixed]") {
    for (uint16_t id : {74U, 192U})
        for (std::size_t size : {2048UZ, 4096UZ}) {
            auto h = header(id, size);
            h.has_battery = true;
            h.prg_nvram_size = 8192U;
            auto m = board(h);
            for (unsigned page = 8U; page < 8U + size / 1024U; ++page) {
                bank(*m, 2U, static_cast<Byte>(page));
                m->write_chr(0x13FFU, static_cast<Byte>(0x50U + page));
            }
            bank(*m, 2U, 8U, 0x80U);
            m->write_prg(0x6000U, 0x71U);
            m->write_prg(0xC000U, 2U);
            m->write_prg(0xC001U, 0U);
            m->write_prg(0xE001U, 0U);
            m->clock_irq_counter();
            const auto saved = state(*m);
            auto copy = board(h);
            StateReader r(saved);
            copy->load_state(r);
            REQUIRE(r.ok());
            REQUIRE(r.remaining() == 0U);
            REQUIRE((state(*copy) == saved));
            REQUIRE(copy->read_chr(0x3FFU) == 0x58U);
            REQUIRE(copy->read_prg(0x6000U) == 0x71U);
            copy->clock_irq_counter();
            REQUIRE_FALSE(copy->irq_pending());
            copy->clock_irq_counter();
            REQUIRE(copy->irq_pending());
            copy->write_prg(0xE000U, 0U);
            REQUIRE_FALSE(copy->irq_pending());
            copy->write_prg(0xC000U, 0U);
            copy->write_prg(0xE001U, 0U);
            for (unsigned i = 0U; i < 3U; ++i) {
                copy->clock_irq_counter();
                REQUIRE(copy->irq_pending());
                copy->acknowledge_irq();
            }
            for (unsigned page = 8U; page < 8U + size / 1024U; ++page) {
                bank(*copy, 2U, static_cast<Byte>(page));
                REQUIRE(copy->read_chr(0x13FFU) == 0x50U + page);
            }
            auto other = board(header(id, size == 2048U ? 4096U : 2048U));
            StateReader cross(saved);
            other->load_state(cross);
            REQUIRE_FALSE(cross.ok());
            for (std::size_t offset :
                 {16384UZ, 16385UZ, 16392UZ, 16394UZ, saved.size() - 2U, saved.size() - 1U}) {
                auto bad = saved;
                bad[offset] = 0xFFU;
                StateReader invalid(bad);
                copy->load_state(invalid);
                REQUIRE_FALSE(invalid.ok());
            }
            StateReader truncated(std::span{saved}.first(saved.size() - 1U));
            copy->load_state(truncated);
            REQUIRE_FALSE(truncated.ok());
        }
}
TEST_CASE("Waixing optional WRAM gates and NVRAM persistence do not affect CHR RAM",
          "[mappers][waixing-mixed]") {
    for (uint16_t id : {74U, 192U})
        for (bool persistent : {false, true}) {
            auto h = header(id);
            h.has_battery = persistent;
            h.prg_ram_size = persistent ? 0U : 8192U;
            h.prg_nvram_size = persistent ? 8192U : 0U;
            auto m = board(h);
            m->write_prg(0x6000U, 0x31U);
            bank(*m, 2U, 8U);
            m->write_chr(0x1000U, 0x73U);
            m->write_prg(0xA001U, 0xC0U);
            m->write_prg(0x6000U, 0U);
            REQUIRE(m->read_prg(0x6000U) == 0x31U);
            m->write_prg(0xA001U, 0U);
            REQUIRE_FALSE(m->maps_prg(0x6000U));
            m->write_prg(0xA001U, 0x80U);
            REQUIRE(m->read_prg(0x6000U) == 0x31U);
            REQUIRE(m->battery_ram().size() == (persistent ? 8192U : 0U));
            m->set_battery_ram(std::vector<Byte>(8192U, 0xD9U));
            REQUIRE(m->read_prg(0x6000U) == (persistent ? 0xD9U : 0x31U));
            REQUIRE(m->read_chr(0x1000U) == 0x73U);
        }
}
TEST_CASE("Waixing headers reject unsupported RAM populations and ROM geometry",
          "[mappers][waixing-mixed]") {
    register_builtin_mappers();
    for (uint16_t id : {74U, 192U})
        for (unsigned kind = 0U; kind < 17U; ++kind) {
            auto h = header(id);
            std::size_t p = 0x80000U, c = 0x40000U;
            switch (kind) {
            case 0U:
                h.submapper = 1U;
                break;
            case 1U:
                h.chr_ram_size = 0U;
                break;
            case 2U:
                h.chr_ram_size = 1024U;
                break;
            case 3U:
                h.chr_ram_size = 8192U;
                break;
            case 4U:
                h.chr_nvram_size = 2048U;
                break;
            case 5U:
                h.has_battery = true;
                break;
            case 6U:
                h.prg_nvram_size = 8192U;
                break;
            case 7U:
                h.prg_ram_size = 4096U;
                break;
            case 8U:
                h.prg_ram_size = 16384U;
                break;
            case 9U:
                h.has_battery = true;
                h.prg_ram_size = h.prg_nvram_size = 8192U;
                break;
            case 10U:
                p = 16384U;
                break;
            case 11U:
                p = 0x82000U;
                break;
            case 12U:
                p = 32769U;
                break;
            case 13U:
                c = 0U;
                break;
            case 14U:
                c = 0x40400U;
                break;
            case 15U:
                c = 8193U;
                break;
            default:
                h.mirror_mode = MirrorMode::FourScreen;
                break;
            }
            REQUIRE_FALSE(
                MapperRegistry::instance().create(h, std::vector<Byte>(p), std::vector<Byte>(c)));
        }
    for (uint16_t id : {74U, 192U}) {
        auto h = header(id);
        h.is_nes2 = false;
        h.chr_ram_size = 0U;
        h.prg_ram_size = 8192U;
        auto m = board(h, 32768U, 8192U);
        REQUIRE_FALSE(m->maps_prg(0x6000U));
        h.has_battery = true;
        m = board(h);
        REQUIRE(m->battery_ram().size() == 8192U);
    }
}
TEST_CASE("Waixing power-on ROM clears cannot overwrite nonzero pattern data",
          "[mappers][waixing-mixed]") {
    register_builtin_mappers();
    for (uint16_t id : {74U, 192U}) {
        auto chr = banks(16384U, 1024U);
        std::fill_n(chr.begin(), 1024U, 0x5DU);
        auto r =
            MapperRegistry::instance().create(header(id), banks(32768U, 8192U), std::move(chr));
        REQUIRE(r);
        auto& m = **r;
        REQUIRE(m.read_chr(0U) == 0x5DU);
        for (unsigned a = 0U; a < 0x2000U; ++a)
            m.write_chr(static_cast<Address>(a), 0U);
        REQUIRE(m.read_chr(0U) == 0x5DU);
        REQUIRE(m.read_chr(0x400U) == 1U);
    }
}
std::vector<Byte> image(uint16_t id,
                        std::size_t ram,
                        bool legacy = false,
                        bool persistent = false,
                        bool ram_only = false) {
    auto p = banks(0x80000U, 8192U), c = banks(ram_only ? 0U : 0x40000U, 1024U);
    if (!c.empty())
        std::fill_n(c.begin(), 1024U, 0x5DU);
    std::vector<Byte> code{0x78U};
    const auto store = [&code](Address a, Byte v) {
        code.insert(code.end(),
                    {0xA9U, v, 0x8DU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U)});
    };
    const auto expect = [&code](Address a, Byte v) {
        code.insert(
            code.end(),
            {0xADU, static_cast<Byte>(a), static_cast<Byte>(a >> 8U), 0xC9U, v, 0xD0U, 0xFEU});
    };
    const auto set_bank = [&store](Byte r, Byte v, Byte mode = 0U) {
        store(0x8000U, static_cast<Byte>(r | mode));
        store(0x8001U, v);
    };
    const auto address = [&store](Address a) {
        store(0x2006U, static_cast<Byte>(a >> 8U));
        store(0x2006U, static_cast<Byte>(a));
    };
    const auto pwrite = [&address, &store](Address a, Byte v) {
        address(a);
        store(0x2007U, v);
    };
    const auto pread = [&code, &address, &expect](Address a, Byte v) {
        address(a);
        code.insert(code.end(), {0xADU, 7U, 0x20U});
        expect(0x2007U, v);
    };
    store(0x4017U, 0x40U);
    store(0x11U, 0U);
    store(0x12U, 0U);
    if (id == 194U) {
        pwrite(0U, 0xA7U);
        pread(0U, 0xA7U);
    } else {
        pread(0U, 0x5DU);
        pwrite(0U, 0U);
        pwrite(0x1000U, 0U);
        pwrite(0x400U, 0U);
        pread(0U, 0x5DU);
        pread(0x400U, 1U);
    }
    set_bank(6U, 35U);
    set_bank(7U, 37U);
    expect(0x9A00U, 35U);
    expect(0xBA00U, 37U);
    expect(0xDA00U, 62U);
    expect(0xFA00U, 63U);
    set_bank(6U, 35U, 0x40U);
    expect(0x9A00U, 62U);
    expect(0xDA00U, 35U);
    const unsigned first = id == 194U ? 0U : 8U;
    for (unsigned page = first; page < first + ram / 1024U; ++page) {
        set_bank(2U, static_cast<Byte>(page));
        pwrite(0x1000U, static_cast<Byte>(0xA0U + page));
    }
    set_bank(0U, static_cast<Byte>(first + 1U));
    pread(0U, static_cast<Byte>(0xA0U + first));
    pread(0x400U, static_cast<Byte>(0xA1U + first));
    set_bank(1U, static_cast<Byte>(first + 3U));
    pread(0x800U, static_cast<Byte>(ram == 4096U ? 0xA2U + first : ram_only ? 0xA0U : first + 2U));
    pread(0xC00U, static_cast<Byte>(ram == 4096U ? 0xA3U + first : ram_only ? 0xA1U : first + 3U));
    set_bank(2U, static_cast<Byte>(first), 0x80U);
    pread(0U, static_cast<Byte>(0xA0U + first));
    pwrite(0U, 0x31U);
    pread(0x1000U, 0x31U);
    pread(0x1400U, static_cast<Byte>(0xA1U + first));
    const auto high = static_cast<Byte>(first + 0x80U);
    set_bank(2U, high, 0x80U);
    pread(0U, ram_only ? 0x31U : high);
    pwrite(0U, 0U);
    pread(0U, ram_only ? 0U : high);
    set_bank(2U, static_cast<Byte>(first), 0x80U);
    pread(0U, ram_only ? 0U : 0x31U);
    pwrite(0U, 0x31U);
    for (Byte gate : {0U, 0xC0U}) {
        store(0xA001U, gate);
        pwrite(0U, gate);
        pread(0U, gate);
    }
    const auto boundary = static_cast<Byte>(first + ram / 1024U);
    set_bank(2U, boundary);
    pread(0x1000U, ram_only ? 0xC0U : boundary);
    pwrite(0x1000U, 0U);
    pread(0x1000U, ram_only ? 0U : boundary);
    pwrite(0x2000U, 0x19U);
    pwrite(0x2400U, 0x27U);
    store(0xA000U, 1U);
    pread(0x2400U, 0x19U);
    store(0xA000U, 0U);
    pread(0x2400U, 0x27U);
    store(0xC000U, 2U);
    store(0xC001U, 0U);
    store(0xE001U, 0U);
    store(0x2000U, 8U);
    store(0x2001U, 0x18U);
    code.insert(code.end(), {0x58U, 0xEAU, 0xA5U, 0x11U, 0xC9U, 1U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    store(0xC000U, 0U);
    store(0xE001U, 0U);
    code.insert(code.end(), {0xA5U, 0x11U, 0xC9U, 3U, 0x90U, 0xFAU});
    store(0xE000U, 0U);
    if (persistent) {
        store(0xA001U, 0x80U);
        store(0x6000U, 0xA7U);
    }
    store(0x12U, 0xC7U);
    const auto loop = static_cast<Address>(0xE000U + code.size());
    code.insert(code.end(),
                {0xE6U, 0x13U, 0x4CU, static_cast<Byte>(loop), static_cast<Byte>(loop >> 8U)});
    REQUIRE(code.size() < 0x1800U);
    constexpr std::array<Byte, 13> kIrq = {
        0x48U, 0xA9U, 0U, 0x8DU, 0U, 0xE0U, 0x8DU, 1U, 0xE0U, 0xE6U, 0x11U, 0x68U, 0x40U};
    std::ranges::copy(code, p.begin() + 0x7E000U);
    std::ranges::copy(kIrq, p.begin() + 0x7F800U);
    for (std::size_t v = 0x7FFFAU; v < 0x80000U; v += 2U) {
        p[v] = 0U;
        p[v + 1U] = v == 0x7FFFCU ? 0xE0U : 0xF8U;
    }
    std::vector<Byte> rom(16U + p.size() + c.size());
    rom[0] = 'N';
    rom[1] = 'E';
    rom[2] = 'S';
    rom[3] = 0x1AU;
    rom[4] = 32U;
    rom[5] = ram_only ? 0U : 32U;
    rom[6] = static_cast<Byte>((id << 4U) | 1U | (persistent ? 2U : 0U));
    rom[7] = static_cast<Byte>((id & 0xF0U) | (legacy ? 0U : 8U));
    rom[10] = persistent && !legacy ? 0x70U : 0U;
    rom[11] = legacy ? 0U : ram == 2048U ? 5U : 6U;
    std::ranges::copy(p, rom.begin() + 16U);
    std::ranges::copy(c, rom.begin() + 16U + p.size());
    return rom;
}
Cartridge cartridge(uint16_t id,
                    std::size_t ram,
                    bool legacy = false,
                    bool persistent = false,
                    bool ram_only = false) {
    register_builtin_mappers();
    auto r = Cartridge::from_data(image(id, ram, legacy, persistent, ram_only));
    REQUIRE(r);
    return std::move(*r);
}
TEST_CASE("Waixing absent WRAM and expansion ports preserve all CPU open-bus bits",
          "[mappers][waixing-mixed]") {
    for (uint16_t id : {74U, 192U}) {
        auto c = cartridge(id, 2048U);
        MemoryBus bus;
        bus.connect_cartridge(&c);
        for (Address a : {0x5000U, 0x5800U, 0x6000U, 0x6001U, 0x7FFFU})
            for (Byte value : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 255U}) {
                bus.write(0x10U, value);
                REQUIRE(bus.read(a) == value);
            }
        REQUIRE_FALSE(c.has_battery());
        REQUIRE(c.battery_ram().empty());
    }
}
TEST_CASE("Waixing original CPU fixtures verify mixed CHR, PRG, CIRAM and IRQs before replay",
          "[mappers][waixing-mixed]") {
    for (uint16_t id : {74U, 192U})
        for (unsigned profile = 0U; profile < 3U; ++profile) {
            const bool legacy = profile == 0U, persistent = profile == 2U;
            const std::size_t ram = legacy          ? (id == 74U ? 2048U : 4096U)
                                    : profile == 1U ? 2048U
                                                    : 4096U;
            Emulator e;
            REQUIRE(e.load_cartridge(cartridge(id, ram, legacy, persistent), ""));
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x11U) >= 3U);
            if (persistent)
                REQUIRE(e.read_cpu(0x6000U) == 0xA7U);
            const auto early = e.save_state();
            e.step_frame();
            const auto future = e.save_state();
            REQUIRE(e.load_state(early));
            e.step_frame();
            REQUIRE((e.save_state() == future));
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            auto bad = early;
            bad[bad.size() - tail.data().size() - 1U] = 0xFFU;
            const auto before = e.save_state();
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE((e.save_state() == before));
        }
}
TEST_CASE("Mapper 194 original CPU fixtures cover mixed and RAM-only patterns before replay",
          "[mappers][waixing-mixed][mapper194]") {
    for (unsigned profile = 0U; profile < 3U; ++profile)
        for (bool ram_only : {false, true}) {
            const bool legacy = profile == 0U, persistent = profile == 2U;
            const std::size_t ram = profile == 2U ? 4096U : 2048U;
            auto c = cartridge(194U, ram, legacy, persistent, ram_only);
            const auto payload_size =
                state(*board(header(194U, ram), 0x80000U, ram_only ? 0U : 0x40000U)).size();
            Emulator e;
            REQUIRE(e.load_cartridge(std::move(c), ""));
            e.reset();
            e.step_frame();
            REQUIRE(e.read_cpu(0x12U) == 0xC7U);
            REQUIRE(e.read_cpu(0x11U) >= 3U);
            if (persistent)
                REQUIRE(e.read_cpu(0x6000U) == 0xA7U);
            const auto early = e.save_state();
            e.step_frame();
            const auto future = e.save_state();
            REQUIRE(e.load_state(early));
            e.step_frame();
            REQUIRE((e.save_state() == future));
            StateWriter tail;
            Controller{}.save_state(tail);
            Controller{}.save_state(tail);
            auto bad = early;
            const auto mapper_end = bad.size() - tail.data().size();
            bad[mapper_end - 1U] = 0xFFU;
            bad[mapper_end - payload_size + 8192U + 13U] ^= 0xA5U; // Mutate RAM before the bad tag.
            const auto before = e.save_state();
            REQUIRE_FALSE(e.load_state(bad));
            REQUIRE((e.save_state() == before));
        }
}
TEST_CASE("Mapper 194 optional WRAM does not change pattern RAM and expansion reads stay open",
          "[mappers][waixing-mixed][mapper194]") {
    for (bool persistent : {false, true}) {
        auto h = header(194U);
        h.has_battery = persistent;
        h.prg_ram_size = persistent ? 0U : 8192U;
        h.prg_nvram_size = persistent ? 8192U : 0U;
        auto m = board(h);
        m->write_prg(0x6000U, 0x31U);
        m->write_chr(0U, 0x73U);
        m->write_prg(0xA001U, 0xC0U);
        m->write_prg(0x6000U, 0U);
        REQUIRE(m->read_prg(0x6000U) == 0x31U);
        m->write_prg(0xA001U, 0U);
        REQUIRE_FALSE(m->maps_prg(0x6000U));
        m->write_chr(0U, 0x59U);
        REQUIRE(m->read_chr(0U) == 0x59U);
        m->write_prg(0xA001U, 0x80U);
        REQUIRE(m->read_prg(0x6000U) == 0x31U);
        REQUIRE(m->battery_ram().size() == (persistent ? 8192U : 0U));
        m->set_battery_ram(std::vector<Byte>(8192U, 0xD9U));
        REQUIRE(m->read_prg(0x6000U) == (persistent ? 0xD9U : 0x31U));
        REQUIRE(m->read_chr(0U) == 0x59U);
    }
    auto c = cartridge(194U, 2048U);
    MemoryBus bus;
    bus.connect_cartridge(&c);
    for (Address a : {0x5000U, 0x5800U, 0x6000U, 0x6001U, 0x7FFFU})
        for (Byte value : {0U, 1U, 2U, 4U, 8U, 16U, 32U, 64U, 128U, 255U}) {
            bus.write(0x10U, value);
            REQUIRE(bus.read(a) == value);
        }
}
} // namespace
