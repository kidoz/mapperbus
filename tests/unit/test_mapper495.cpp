#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper495.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kRomSize = 0x020000UZ;
constexpr std::size_t kPrgBankSize = 0x2000UZ;
constexpr std::size_t kChrBankSize = 0x1000UZ;
constexpr std::size_t kPrgStateOffset = 10UZ;
constexpr std::size_t kChrStateOffset = 13UZ;
constexpr std::size_t kLatchStateOffset = 17UZ;
constexpr std::size_t kStateSize = 19UZ;
constexpr std::array<Address, 14> kPrgAddresses{0x8000U,
                                                0x8001U,
                                                0x9234U,
                                                0x9FFFU,
                                                0xA000U,
                                                0xB123U,
                                                0xBFFFU,
                                                0xC000U,
                                                0xD456U,
                                                0xDFFFU,
                                                0xE000U,
                                                0xF123U,
                                                0xFFFEU,
                                                0xFFFFU};
constexpr std::array<Address, 10> kChrAddresses{
    0x0000U, 0x0123U, 0x0FD0U, 0x0FFFU, 0x1000U, 0x1ABCU, 0x1FD0U, 0x1FE8U, 0x1FFFU, 0x0800U};

INesHeader header() {
    INesHeader result{};
    result.mapper_number = 495U;
    result.is_nes2 = true;
    result.mirror_mode = MirrorMode::Vertical;
    result.prg_rom_size = kRomSize;
    result.chr_rom_size = kRomSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 12U) * 13U +
                             (offset >> 16U) * 47U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper495> board(Byte prg_salt = 0U, Byte chr_salt = 0x55U) {
    const auto h = header();
    REQUIRE(Mapper495::validate_geometry(h, kRomSize, kRomSize));
    return std::make_unique<Mapper495>(h, rom(kRomSize, prg_salt), rom(kRomSize, chr_salt));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void load(Mapper495& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

std::size_t prg_offset(Address addr, const std::array<Byte, 3>& banks) {
    const auto slot = static_cast<unsigned>((addr - 0x8000U) / kPrgBankSize);
    const auto bank = slot < 3U ? banks[slot] : Byte{0x0FU};
    return static_cast<std::size_t>(bank) * kPrgBankSize + (addr & 0x1FFFU);
}

std::size_t chr_offset(Address addr,
                       const std::array<Byte, 4>& banks,
                       const std::array<Byte, 2>& latches) {
    const auto half = static_cast<unsigned>((addr >> 12U) & 1U);
    const auto index = static_cast<std::size_t>(latches[half]) * 2UZ + half;
    return static_cast<std::size_t>(banks[index] & 0x1FU) * kChrBankSize + (addr & 0x0FFFU);
}

void require_prg(Mapper495& mapper,
                 Address addr,
                 const std::array<Byte, 3>& banks,
                 Byte salt = 0U) {
    const auto physical = prg_offset(addr, banks);
    CAPTURE(addr, banks[0], banks[1], banks[2], physical, salt);
    REQUIRE(physical < kRomSize);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
}

void require_chr(Mapper495& mapper,
                 Address addr,
                 const std::array<Byte, 4>& banks,
                 const std::array<Byte, 2>& latches,
                 Byte salt = 0x55U) {
    const auto physical = chr_offset(addr, banks, latches);
    CAPTURE(addr, banks[0], banks[1], banks[2], banks[3], latches[0], latches[1], physical, salt);
    REQUIRE(physical < kRomSize);
    REQUIRE(mapper.read_chr(addr) == cell(physical, salt));
}

Address ciram_offset(Byte mode, Address addr) {
    constexpr std::array<std::array<Address, 4>, 4> kPages{{
        {0U, 0U, 0U, 1U},
        {0U, 1U, 0U, 1U},
        {0U, 0U, 1U, 1U},
        {1U, 1U, 1U, 1U},
    }};
    const auto table = static_cast<unsigned>((addr >> 10U) & 3U);
    return static_cast<Address>(kPages[mode][table] * 0x0400U + (addr & 0x03FFU));
}

TEST_CASE("Mapper 495 validates exact NES 2.0 ROM-only geometry",
          "[mapper495][geometry][ram][ciram]") {
    const auto h = header();
    REQUIRE(Mapper495::validate_geometry(h, kRomSize, kRomSize));
    auto horizontal = h;
    horizontal.mirror_mode = MirrorMode::Horizontal;
    REQUIRE(Mapper495::validate_geometry(horizontal, kRomSize, kRomSize));

    auto mapper = board(0x35U, 0x60U);
    auto different_rom = board(0x80U, 0xD5U);
    REQUIRE(state(*mapper) == state(*different_rom));
    REQUIRE(state(*mapper).size() == kStateSize);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    require_prg(*mapper, 0x8000U, {0U, 0U, 0U}, 0x35U);
    require_prg(*mapper, 0xA000U, {0U, 0U, 0U}, 0x35U);
    require_prg(*mapper, 0xC000U, {0U, 0U, 0U}, 0x35U);
    require_prg(*mapper, 0xFFFFU, {0U, 0U, 0U}, 0x35U);
    require_chr(*mapper, 0U, {}, {}, 0x60U);
    require_chr(*mapper, 0x1FFFU, {}, {}, 0x60U);

    const auto reject = [](const INesHeader& candidate, std::size_t prg, std::size_t chr) {
        CAPTURE(candidate.mapper_number,
                candidate.is_nes2,
                candidate.submapper,
                candidate.has_battery,
                candidate.has_trainer,
                candidate.prg_ram_size,
                candidate.prg_nvram_size,
                candidate.chr_ram_size,
                candidate.chr_nvram_size,
                candidate.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper495::validate_geometry(candidate, prg, chr));
    };
    for (const unsigned id : {0U, 9U, 10U, 494U, 496U, 4095U}) {
        auto invalid = h;
        invalid.mapper_number = static_cast<std::uint16_t>(id);
        reject(invalid, kRomSize, kRomSize);
    }
    auto legacy = h;
    legacy.is_nes2 = false;
    reject(legacy, kRomSize, kRomSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto invalid = h;
        invalid.submapper = submapper;
        reject(invalid, kRomSize, kRomSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x2000UZ, 0x010000UZ, kRomSize - 1U, kRomSize + 1U, kRomSize * 2U}) {
        reject(h, size, kRomSize);
        reject(h, kRomSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        auto invalid = h;
        invalid.mirror_mode = mirror;
        reject(invalid, kRomSize, kRomSize);
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto invalid = h;
        switch (field) {
        case 0U:
            invalid.has_battery = true;
            break;
        case 1U:
            invalid.has_trainer = true;
            break;
        case 2U:
            invalid.prg_ram_size = 0x2000U;
            break;
        case 3U:
            invalid.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            invalid.chr_ram_size = 0x2000U;
            break;
        default:
            invalid.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(invalid, kRomSize, kRomSize);
    }
}

TEST_CASE("Mapper 495 decodes all CPU register aliases and leaves low windows undriven",
          "[mapper495][decoding][bus][ram]") {
    auto mapper = board();
    const auto cold = state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw < 0xA000U && raw >= 0x8000U) {
            expected[kPrgStateOffset] = value & 0x0FU;
        } else if (raw < 0xC000U && raw >= 0xA000U) {
            expected[kPrgStateOffset + 1U] = value & 0x0FU;
        } else if (raw < 0xE000U && raw >= 0xC000U) {
            expected[kPrgStateOffset + 2U] = value & 0x0FU;
        } else if (raw >= 0xE000U) {
            std::size_t index = 0UZ;
            switch (raw & 0xEC00U) {
            case 0xE000U:
                index = 0UZ;
                break;
            case 0xE400U:
                index = 2UZ;
                break;
            case 0xE800U:
                index = 1UZ;
                break;
            default:
                index = 3UZ;
                break;
            }
            expected[kChrStateOffset + index] = value & 0xDFU;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(state(*mapper) == expected);
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
}

TEST_CASE("Mapper 495 independently selects three PRG banks and fixes the last bank",
          "[mapper495][banking][prg][bus]") {
    auto mapper = board(0x35U);
    for (const Byte first : {Byte{0U}, Byte{1U}, Byte{7U}, Byte{14U}, Byte{15U}}) {
        for (const Byte second : {Byte{0U}, Byte{2U}, Byte{8U}, Byte{15U}}) {
            for (const Byte third : {Byte{0U}, Byte{3U}, Byte{9U}, Byte{15U}}) {
                const std::array<Byte, 3> banks{first, second, third};
                mapper->write_prg(0x8000U, first);
                mapper->write_prg(0xA000U, second);
                mapper->write_prg(0xC000U, third);
                CAPTURE(first, second, third);
                for (const auto addr : kPrgAddresses) {
                    require_prg(*mapper, addr, banks, 0x35U);
                }
            }
        }
    }
    for (const Address addr : {Address{0x8000U}, Address{0xA000U}, Address{0xC000U}}) {
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            mapper->write_prg(addr, static_cast<Byte>(raw));
            CAPTURE(addr, raw);
            REQUIRE(state(*mapper)[kPrgStateOffset + (addr >> 13U & 3U)] == (raw & 0x0FU));
        }
    }
    const std::array<Byte, 3> banks{15U, 15U, 15U};
    for (Address addr = 0xE000U;; ++addr) {
        require_prg(*mapper, addr, banks, 0x35U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
}

TEST_CASE("Mapper 495 CHR reads are pure and physical trigger reads use the old bank",
          "[mapper495][banking][chr][bus]") {
    auto mapper = board(0U, 0x60U);
    mapper->write_prg(0xE000U, 0x01U);
    mapper->write_prg(0xE400U, 0x02U);
    mapper->write_prg(0xE800U, 0x03U);
    mapper->write_prg(0xEC00U, 0x04U);
    const std::array<Byte, 4> banks{1U, 3U, 2U, 4U};
    const auto before = state(*mapper);

    REQUIRE(mapper->observes_ppu_reads());
    REQUIRE(mapper->read_chr(0x0FD5U) == cell(chr_offset(0x0FD5U, banks, {0U, 0U}), 0x60U));
    REQUIRE(state(*mapper) == before);
    mapper->observe_ppu_read(0x0FD5U);
    REQUIRE(state(*mapper) == before);
    mapper->observe_ppu_read(0x0FE5U);
    REQUIRE(mapper->read_chr(0x0FD5U) == cell(chr_offset(0x0FD5U, banks, {1U, 0U}), 0x60U));
    mapper->observe_ppu_read(0x1FE5U);
    require_chr(*mapper, 0x1FFFU, banks, {1U, 1U}, 0x60U);

    for (unsigned raw = 0U; raw <= 0x3FFFU; ++raw) {
        mapper->reset();
        auto expected = state(*mapper);
        const auto addr = static_cast<Address>(raw);
        const auto trigger = static_cast<Address>(addr & 0x1FF0U);
        if (trigger == 0x0FD0U || trigger == 0x1FD0U) {
            expected[kLatchStateOffset + ((trigger >> 12U) & 1U)] = 0U;
        } else if (trigger == 0x0FE0U || trigger == 0x1FE0U) {
            expected[kLatchStateOffset + ((trigger >> 12U) & 1U)] = 1U;
        }
        const auto pure_before = state(*mapper);
        if (addr < 0x2000U) {
            (void)mapper->read_chr(addr);
            REQUIRE(state(*mapper) == pure_before);
        }
        mapper->observe_ppu_read(addr);
        CAPTURE(raw, trigger);
        REQUIRE(state(*mapper) == expected);
    }
}

TEST_CASE("Mapper 495 selects all four 4 KiB CHR banks through both latches",
          "[mapper495][banking][chr][decoding]") {
    auto mapper = board(0U, 0x60U);
    constexpr std::array<Address, 4> kRegisters{0xE000U, 0xE800U, 0xE400U, 0xEC00U};
    for (std::size_t index = 0U; index < kRegisters.size(); ++index) {
        for (unsigned raw = 0U; raw < 256U; ++raw) {
            mapper->reset();
            mapper->write_prg(kRegisters[index], static_cast<Byte>(raw));
            const auto latch = static_cast<Byte>(index / 2U);
            const auto half = static_cast<unsigned>(index % 2U);
            const auto trigger = static_cast<Address>(half * 0x1000U + 0x0FD0U + latch * 0x10U);
            mapper->observe_ppu_read(trigger);
            std::array<Byte, 4> banks{};
            std::array<Byte, 2> latches{};
            banks[index] = static_cast<Byte>(raw & 0xDFU);
            latches[half] = latch;
            CAPTURE(index, raw, trigger);
            REQUIRE(state(*mapper)[kChrStateOffset + index] == (raw & 0xDFU));
            for (const auto addr : kChrAddresses) {
                require_chr(*mapper, addr, banks, latches, 0x60U);
            }
        }
    }

    mapper->reset();
    for (const auto addr : kChrAddresses) {
        const auto expected = mapper->read_chr(addr);
        mapper->write_chr(addr, static_cast<Byte>(expected ^ 0xFFU));
        REQUIRE(mapper->read_chr(addr) == expected);
    }
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xFFU);
    mapper->write_chr(0xFFFFU, 0xFFU);
}

TEST_CASE("Mapper 495 independently maps normal and mirrored CIRAM in every mode",
          "[mapper495][ciram][chr][bus]") {
    auto mapper = board();
    constexpr std::array<MirrorMode, 4> kModes{MirrorMode::SingleLower,
                                               MirrorMode::Vertical,
                                               MirrorMode::Horizontal,
                                               MirrorMode::SingleUpper};
    for (Byte normal_mode = 0U; normal_mode < 4U; ++normal_mode) {
        for (Byte upper_mode = 0U; upper_mode < 4U; ++upper_mode) {
            for (Byte left_latch = 0U; left_latch < 2U; ++left_latch) {
                for (Byte right_latch = 0U; right_latch < 2U; ++right_latch) {
                    mapper->reset();
                    mapper->write_prg(0xE000U, static_cast<Byte>((normal_mode << 6U) | 1U));
                    mapper->write_prg(0xE400U, static_cast<Byte>(((3U - normal_mode) << 6U) | 2U));
                    mapper->write_prg(0xE800U, static_cast<Byte>((upper_mode << 6U) | 3U));
                    mapper->write_prg(0xEC00U, static_cast<Byte>(((3U - upper_mode) << 6U) | 4U));
                    mapper->observe_ppu_read(static_cast<Address>(0x0FD0U + left_latch * 0x10U));
                    mapper->observe_ppu_read(static_cast<Address>(0x1FD0U + right_latch * 0x10U));
                    const auto selected_normal =
                        static_cast<Byte>(left_latch == 0U ? normal_mode : 3U - normal_mode);
                    const auto selected_upper =
                        static_cast<Byte>(right_latch == 0U ? upper_mode : 3U - upper_mode);
                    CAPTURE(normal_mode,
                            upper_mode,
                            left_latch,
                            right_latch,
                            selected_normal,
                            selected_upper);
                    REQUIRE(mapper->mirror_mode() == kModes[selected_normal]);
                    for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
                        const auto mode = addr < 0x3000U ? selected_normal : selected_upper;
                        const auto mapped = mapper->nametable_address(addr);
                        REQUIRE(mapped);
                        REQUIRE(*mapped == ciram_offset(mode, addr));
                        REQUIRE(mapper->maps_nametable(addr));
                        REQUIRE_FALSE(mapper->ciram_address(addr));
                        REQUIRE_FALSE(mapper->read_nametable(addr));
                        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
                    }
                }
            }
        }
    }
    for (const Address addr : {Address{0U}, Address{0x1FFFU}, Address{0x3F00U}, Address{0xFFFFU}}) {
        REQUIRE_FALSE(mapper->nametable_address(addr));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
}

TEST_CASE("Mapper 495 reset clears banks and restores both FD latches",
          "[mapper495][reset][banking][ciram]") {
    auto mapper = board(0x35U, 0x60U);
    const auto cold = state(*mapper);
    mapper->write_prg(0x8000U, 0x0FU);
    mapper->write_prg(0xA000U, 0x0EU);
    mapper->write_prg(0xC000U, 0x0DU);
    mapper->write_prg(0xE000U, 0xDFU);
    mapper->write_prg(0xE400U, 0xDEU);
    mapper->write_prg(0xE800U, 0xDDU);
    mapper->write_prg(0xEC00U, 0xDCU);
    mapper->observe_ppu_read(0x0FE0U);
    mapper->observe_ppu_read(0x1FE0U);
    REQUIRE(state(*mapper) != cold);

    mapper->reset();
    REQUIRE(state(*mapper) == cold);
    REQUIRE(mapper->mirror_mode() == MirrorMode::SingleLower);
    for (const auto addr : kPrgAddresses) {
        require_prg(*mapper, addr, {0U, 0U, 0U}, 0x35U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*mapper, addr, {}, {}, 0x60U);
    }

    mapper->write_prg(0xE400U, 0xFFU);
    mapper->observe_ppu_read(0x0FE0U);
    mapper->power_on();
    REQUIRE(state(*mapper) == cold);
}

TEST_CASE("Mapper 495 has no cartridge RAM IRQ audio or unrelated observer effects",
          "[mapper495][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x0FU);
    mapper->write_prg(0xE400U, 0xDFU);
    mapper->observe_ppu_read(0x0FE0U);
    const auto before = state(*mapper);
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x0FE0U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->clock_audio();
    mapper->set_region(Region::PAL);
    mapper->acknowledge_irq();

    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->has_flash_storage());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->has_expansion_audio());
    REQUIRE(mapper->audio_output() == 0.0F);
    REQUIRE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
}

TEST_CASE("Mapper 495 bounded snapshots replay banks and latches without ROM",
          "[mapper495][state][banking][ram]") {
    auto mapper = board(0x35U, 0x60U);
    mapper->write_prg(0x8000U, 0x0FU);
    mapper->write_prg(0xA000U, 0x0EU);
    mapper->write_prg(0xC000U, 0x0DU);
    mapper->write_prg(0xE000U, 0x41U);
    mapper->write_prg(0xE400U, 0x82U);
    mapper->write_prg(0xE800U, 0xC3U);
    mapper->write_prg(0xEC00U, 0x04U);
    mapper->observe_ppu_read(0x0FE0U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board(0x80U, 0xD5U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    const std::array<Byte, 3> prg_banks{15U, 14U, 13U};
    const std::array<Byte, 4> chr_banks{0x41U, 0xC3U, 0x82U, 0x04U};
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, prg_banks, 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, chr_banks, {1U, 0U}, 0xD5U);
    }

    copy->power_on();
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    auto with_tail = saved;
    with_tail.insert(with_tail.end(), {1U, 2U, 3U});
    StateReader tail_reader(with_tail);
    copy->load_state(tail_reader);
    REQUIRE(tail_reader.ok());
    REQUIRE(tail_reader.remaining() == 3U);
    REQUIRE(state(*copy) == saved);
}

TEST_CASE("Mapper 495 rejects truncated damaged and mismatched states atomically",
          "[mapper495][state][ram]") {
    auto mapper = board();
    mapper->write_prg(0x8000U, 0x0FU);
    mapper->write_prg(0xA000U, 0x0EU);
    mapper->write_prg(0xC000U, 0x0DU);
    mapper->write_prg(0xE000U, 0xDFU);
    mapper->write_prg(0xE400U, 0x9EU);
    mapper->write_prg(0xE800U, 0x5DU);
    mapper->write_prg(0xEC00U, 0x1CU);
    mapper->observe_ppu_read(0x0FE0U);
    mapper->observe_ppu_read(0x1FE0U);
    const auto saved = state(*mapper);
    const auto reject = [&](std::span<const Byte> bytes) {
        StateReader reader(bytes);
        mapper->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*mapper) == saved);
    };

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        reject(std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kPrgStateOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        reject(invalid);
    }
    for (std::size_t index = 0U; index < 3U; ++index) {
        for (const Byte value : {Byte{0x10U}, Byte{0x80U}, Byte{0xFFU}}) {
            auto invalid = saved;
            invalid[kPrgStateOffset + index] = value;
            CAPTURE(index, value);
            reject(invalid);
        }
    }
    for (std::size_t index = 0U; index < 4U; ++index) {
        auto invalid = saved;
        invalid[kChrStateOffset + index] |= 0x20U;
        CAPTURE(index);
        reject(invalid);
    }
    for (std::size_t index = 0U; index < 2U; ++index) {
        for (const Byte value : {Byte{2U}, Byte{0xFFU}}) {
            auto invalid = saved;
            invalid[kLatchStateOffset + index] = value;
            CAPTURE(index, value);
            reject(invalid);
        }
    }

    const auto mismatch = [&](std::unique_ptr<Mapper495>& other) {
        const auto before = state(*other);
        StateReader reader(saved);
        other->load_state(reader);
        REQUIRE_FALSE(reader.ok());
        REQUIRE(state(*other) == before);
    };
    auto wrong_prg =
        std::make_unique<Mapper495>(header(), rom(kRomSize / 2U, 0U), rom(kRomSize, 0x55U));
    mismatch(wrong_prg);
    auto wrong_chr =
        std::make_unique<Mapper495>(header(), rom(kRomSize, 0U), rom(kRomSize / 2U, 0x55U));
    mismatch(wrong_chr);

    auto changed = saved;
    changed[kPrgStateOffset] = 0x05U;
    changed[kChrStateOffset] = 0x06U;
    changed[kLatchStateOffset] = 0U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
