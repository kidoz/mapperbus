#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "core/mappers/mapper518.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kDefaultPrgSize = 0x200000UZ;
constexpr std::size_t kPrg16Size = 0x004000UZ;
constexpr std::size_t kPrg32Size = 0x008000UZ;
constexpr std::size_t kRamSize = 0x002000UZ;
constexpr std::size_t kSecondaryRamSize = 0x020000UZ;
constexpr std::size_t kBankStateOffset = 10UZ;
constexpr std::size_t kModeStateOffset = 11UZ;
constexpr std::size_t kPinsStateOffset = 12UZ;
constexpr std::size_t kWramStateOffset = 13UZ;
constexpr std::size_t kChrRamStateOffset = kWramStateOffset + kRamSize;
constexpr std::size_t kSecondaryLengthStateOffset = kChrRamStateOffset + kRamSize;
constexpr std::size_t kFdcStateOffset = kSecondaryLengthStateOffset + 8UZ;
constexpr std::size_t kSpeechStateOffset = kFdcStateOffset + 3UZ;
constexpr std::size_t kSpeechStateSize = 129UZ;
constexpr std::size_t kStateSize = kSpeechStateOffset + kSpeechStateSize;
constexpr std::size_t kSb97StateSize = kStateSize + kSecondaryRamSize;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};
constexpr std::array<Address, 8> kChrAddresses{
    0x0000U, 0x0001U, 0x0123U, 0x0FFFU, 0x1000U, 0x1234U, 0x1FFEU, 0x1FFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 518U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::Dendy;
    result.prg_rom_size = kDefaultPrgSize;
    result.prg_ram_size = kRamSize;
    result.chr_ram_size = kRamSize;
    return result;
}

Byte cell(std::size_t offset, Byte salt = 0U) {
    return static_cast<Byte>(offset * 29U + (offset >> 8U) * 17U + (offset >> 12U) * 47U +
                             (offset >> 14U) * 59U + (offset >> 20U) * 83U + salt);
}

std::vector<Byte> rom(std::size_t size, Byte salt = 0U) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = cell(offset, salt);
    }
    return data;
}

std::unique_ptr<Mapper518> board(std::size_t prg_size = kDefaultPrgSize,
                                 MirrorMode mirror = MirrorMode::Horizontal,
                                 Byte salt = 0U,
                                 bool sb97 = false) {
    const auto h = header(mirror);
    REQUIRE(Mapper518::validate_geometry(h, prg_size, 0U));
    auto result = std::make_unique<Mapper518>(h, rom(prg_size, salt), std::vector<Byte>{});
    if (sb97) {
        REQUIRE(result->configure_board_profile("sb97"));
    }
    return result;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, kWramStateOffset> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    REQUIRE((bytes.size() == kStateSize || bytes.size() == kSb97StateSize));
    std::array<Byte, kWramStateOffset> result{};
    std::copy_n(bytes.begin(), kWramStateOffset, result.begin());
    return result;
}

void load(Mapper518& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper518& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto status = mapper.read_expansion(0x5300U);
    const auto audio = mapper.audio_output();
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_expansion(0x5300U) == status);
    REQUIRE(mapper.audio_output() == audio);
}

Byte initial_mode(MirrorMode mirror) {
    return mirror == MirrorMode::Horizontal ? 1U : 0U;
}

MirrorMode expected_mirror(Byte mode) {
    return (mode & 1U) != 0U ? MirrorMode::Horizontal : MirrorMode::Vertical;
}

std::size_t expected_rom_offset(Address addr, Byte bank, Byte mode, std::size_t prg_size) {
    if (addr >= 0xC000U && (mode & 4U) == 0U) {
        return addr - 0xC000U;
    }
    const auto unit = (mode & 4U) != 0U ? kPrg32Size : kPrg16Size;
    const auto base = static_cast<std::size_t>((bank & 0x40U) != 0U ? 0x100000U : 0U) +
                      static_cast<std::size_t>(bank & 0x3FU) * unit;
    return (base + (addr & (unit - 1U))) % prg_size;
}

void require_prg(
    Mapper518& mapper, Address addr, Byte bank, Byte mode, std::size_t prg_size, Byte salt) {
    const auto physical = expected_rom_offset(addr, bank, mode, prg_size);
    CAPTURE(addr, bank, mode, prg_size, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void observe_nametable(Mapper518& mapper, Byte nametable) {
    mapper.observe_ppu_address(0U);
    mapper.observe_ppu_address(static_cast<Address>(0x205AU | ((nametable & 3U) << 10U)));
}

class SpeechStream {
  public:
    void bits(unsigned value, unsigned width) {
        for (unsigned index = width; index > 0U; --index) {
            bits_.push_back(static_cast<Byte>((value >> (index - 1U)) & 1U));
        }
    }

    void frame(bool d6,
               unsigned energy = 14U,
               unsigned pitch = 20U,
               bool repeat = false,
               const std::array<unsigned, 10>& coefficients = {
                   23U, 9U, 8U, 6U, 7U, 6U, 6U, 3U, 3U, 3U}) {
        bits(energy, 4U);
        if (energy == 0U || energy == 15U) {
            return;
        }
        bits(repeat ? 1U : 0U, 1U);
        bits(pitch, d6 ? 7U : 6U);
        if (repeat) {
            return;
        }
        static constexpr std::array<unsigned, 10> d6_widths{6U, 6U, 5U, 5U, 4U, 4U, 4U, 3U, 3U, 3U};
        static constexpr std::array<unsigned, 10> pe_widths{5U, 5U, 4U, 4U, 4U, 4U, 4U, 3U, 3U, 3U};
        for (unsigned index = 0U; index < (pitch == 0U ? 4U : 10U); ++index) {
            bits(coefficients[index], d6 ? d6_widths[index] : pe_widths[index]);
        }
    }

    std::vector<Byte> bytes() const {
        std::vector<Byte> result((bits_.size() + 7U) / 8U, 0U);
        for (std::size_t index = 0U; index < bits_.size(); ++index) {
            result[index / 8U] |= static_cast<Byte>(bits_[index] << (index % 8U));
        }
        return result;
    }

  private:
    std::vector<Byte> bits_{};
};

std::vector<Byte> message(bool d6, bool include_stop = true) {
    SpeechStream stream;
    stream.frame(d6);
    if (include_stop) {
        stream.frame(d6, 15U);
    }
    auto result = stream.bytes();
    result.insert(result.begin(), d6 ? 0x0AU : 0x0BU);
    return result;
}

void write_speech(Mapper518& mapper, std::span<const Byte> bytes) {
    for (const auto value : bytes) {
        mapper.write_expansion(0x5300U, value);
    }
}

std::uint32_t cpu_clock(Region region) {
    return static_cast<std::uint32_t>(std::lround(timing_for_region(region).cpu_clock_hz()));
}

void clock_chip_ticks(Mapper518& mapper, std::uint64_t ticks, Region region = Region::Dendy) {
    const auto cycles = (ticks * cpu_clock(region) + 999999U) / 1000000U;
    for (std::uint64_t cycle = 0U; cycle < cycles; ++cycle) {
        mapper.clock_audio();
    }
}

std::vector<float> speech_samples(Mapper518& mapper,
                                  unsigned count,
                                  Region region = Region::Dendy) {
    clock_chip_ticks(mapper, 16000U, region);
    std::vector<float> result;
    result.reserve(count);
    for (unsigned index = 0U; index < count; ++index) {
        clock_chip_ticks(mapper, 100U, region);
        result.push_back(mapper.audio_output());
    }
    return result;
}

TEST_CASE("Mapper 518 validates geometry and bounds cartridge and SB-97 state without ROM",
          "[mapper518][geometry][ram][state]") {
    for (const std::size_t size : {0x008000UZ, 0x010000UZ, 0x100000UZ, 0x180000UZ, 0x200000UZ}) {
        for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            for (const bool sb97 : {false, true}) {
                CAPTURE(size, mirror, sb97);
                auto mapper = board(size, mirror, 0x35U, sb97);
                auto different_rom = board(size, mirror, 0x80U, sb97);
                REQUIRE(state(*mapper) == state(*different_rom));
                REQUIRE(state(*mapper).size() == (sb97 ? kSb97StateSize : kStateSize));
                REQUIRE(register_state(*mapper)[kBankStateOffset] == 0U);
                REQUIRE(register_state(*mapper)[kModeStateOffset] == initial_mode(mirror));
                REQUIRE(register_state(*mapper)[kPinsStateOffset] == 0U);
                REQUIRE(mapper->mirror_mode() == mirror);
                REQUIRE(mapper->battery_ram().empty());
                REQUIRE(mapper->shared_nvram().empty());
                REQUIRE_FALSE(mapper->has_flash_storage());
                REQUIRE_FALSE(mapper->irq_pending());
                REQUIRE(mapper->has_expansion_audio());
                REQUIRE(mapper->audio_output() == 0.0F);
                REQUIRE(mapper->read_expansion(0x5300U) == 0U);
                REQUIRE(mapper->read_prg(0x6000U) == 0U);
                REQUIRE(mapper->read_prg(0x7FFFU) == 0U);
                REQUIRE(mapper->read_chr(0U) == 0U);
                REQUIRE(mapper->read_chr(0x1FFFU) == 0U);
                require_prg(*mapper, 0x8000U, 0U, initial_mode(mirror), size, 0x35U);
                require_prg(*mapper, 0xFFFFU, 0U, initial_mode(mirror), size, 0x35U);
            }
        }
    }

    const auto reject = [](const INesHeader& h, std::size_t prg, std::size_t chr) {
        CAPTURE(h.mapper_number,
                h.is_nes2,
                h.submapper,
                h.has_battery,
                h.has_trainer,
                h.prg_ram_size,
                h.prg_nvram_size,
                h.chr_ram_size,
                h.chr_nvram_size,
                h.mirror_mode,
                prg,
                chr);
        REQUIRE_FALSE(Mapper518::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 514U, 515U, 517U, 519U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kDefaultPrgSize, 0U);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kDefaultPrgSize, 0U);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kDefaultPrgSize, 0U);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0x200001UZ, 0x204000UZ, 0x400000UZ}) {
        reject(header(), size, 0U);
    }
    for (const std::size_t size : {1UZ, 0x1000UZ, 0x2000UZ, 0x8000UZ, 0x100000UZ}) {
        reject(header(), kDefaultPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kDefaultPrgSize, 0U);
    }
    for (unsigned field = 0U; field < 10U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0U;
            break;
        case 3U:
            h.prg_ram_size = 0x1000U;
            break;
        case 4U:
            h.prg_ram_size = kSecondaryRamSize;
            break;
        case 5U:
            h.prg_nvram_size = kRamSize;
            break;
        case 6U:
            h.chr_ram_size = 0U;
            break;
        case 7U:
            h.chr_ram_size = 0x1000U;
            break;
        case 8U:
            h.chr_ram_size = 0x4000U;
            break;
        default:
            h.chr_nvram_size = kRamSize;
            break;
        }
        CAPTURE(field);
        reject(h, kDefaultPrgSize, 0U);
    }

    auto mapper = board();
    mapper->write_expansion(0x5000U, 0xA5U);
    mapper->write_prg(0x6123U, 0x5AU);
    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->configure_board_profile("cartridge"));
}

TEST_CASE("Mapper 518 decodes exact expansion and FDC ports with masked open bus",
          "[mapper518][decoding][bus][input]") {
    for (const bool sb97 : {false, true}) {
        auto mapper = board(kDefaultPrgSize, MirrorMode::Horizontal, 0U, sb97);
        const auto cold = register_state(*mapper);
        const auto fdc_offset = kFdcStateOffset + (sb97 ? kSecondaryRamSize : 0UZ);
        for (unsigned raw = 0x4020U; raw < 0x6000U; ++raw) {
            mapper->reset();
            auto expected = cold;
            const auto addr = static_cast<Address>(raw);
            const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
            if (raw == 0x5000U) {
                expected[kBankStateOffset] = value;
            } else if (raw == 0x5200U) {
                expected[kModeStateOffset] = value & 7U;
            }

            mapper->write_expansion(addr, value);
            CAPTURE(raw, value, sb97);
            if (raw != 0x5300U) {
                const auto bytes = state(*mapper);
                REQUIRE(std::equal(expected.begin(), expected.end(), bytes.begin()));
                if (sb97 && (raw == 0x5500U || raw == 0x5501U || raw == 0x5505U)) {
                    REQUIRE(bytes[fdc_offset + (raw == 0x5500U   ? 0U
                                                : raw == 0x5501U ? 1U
                                                                 : 2U)] == value);
                } else {
                    REQUIRE(bytes[fdc_offset] == 0U);
                    REQUIRE(bytes[fdc_offset + 1U] == 0U);
                    REQUIRE(bytes[fdc_offset + 2U] == 0U);
                }
            }
            const bool common = raw == 0x5000U || raw == 0x5200U || raw == 0x5300U;
            const bool fdc = raw == 0x5500U || raw == 0x5501U || raw == 0x5505U || raw == 0x5604U ||
                             raw == 0x5605U;
            REQUIRE(mapper->maps_expansion(addr) == (common || (sb97 && fdc)));
            const auto mask = raw == 0x5300U                               ? 0x8FU
                              : sb97 && (raw == 0x5604U || raw == 0x5605U) ? 0xFFU
                                                                           : 0U;
            REQUIRE(mapper->expansion_read_mask(addr) == mask);
            if (raw == 0x5604U) {
                REQUIRE(mapper->read_expansion(addr) == (sb97 ? 0x80U : 0U));
            }
        }
        REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);
        REQUIRE(mapper->read_expansion(0x4FFFU) == 0U);
        REQUIRE(mapper->read_expansion(0x5502U) == 0U);
        REQUIRE(mapper->read_expansion(0x56FFU) == 0U);
    }

    auto mapper = board(kDefaultPrgSize, MirrorMode::Horizontal, 0U, true);
    mapper->write_expansion(0x5505U, 0xA6U);
    REQUIRE(mapper->read_expansion(0x5605U) == 0xA6U);
    const auto before = state(*mapper);
    mapper->write_expansion(0x5604U, 0xFFU);
    mapper->write_expansion(0x5605U, 0xFFU);
    REQUIRE(state(*mapper) == before);
}

TEST_CASE("Mapper 518 selects 16/32 KiB ROM banks with expansion A20 and fixed main bank zero",
          "[mapper518][banking][prg][bus]") {
    for (const std::size_t size : {0x008000UZ, 0x010000UZ, 0x100000UZ, 0x180000UZ, 0x200000UZ}) {
        auto mapper = board(size, MirrorMode::Vertical, 0x35U);
        for (const Byte mode : {Byte{0U}, Byte{1U}, Byte{4U}, Byte{5U}}) {
            mapper->write_expansion(0x5200U, mode);
            for (const Byte bank : {Byte{0U},
                                    Byte{1U},
                                    Byte{2U},
                                    Byte{0x3FU},
                                    Byte{0x40U},
                                    Byte{0x7FU},
                                    Byte{0x80U},
                                    Byte{0xC0U},
                                    Byte{0xFFU}}) {
                mapper->write_expansion(0x5000U, bank);
                REQUIRE(register_state(*mapper)[kBankStateOffset] == bank);
                for (const auto addr : kPrgAddresses) {
                    const bool secondary =
                        (bank & 0x80U) != 0U && (addr < 0xC000U || (mode & 4U) != 0U);
                    if (secondary) {
                        REQUIRE(mapper->read_prg(addr) == 0U);
                        REQUIRE(mapper->prg_read_mask(addr) == 0U);
                    } else {
                        require_prg(*mapper, addr, bank, mode, size, 0x35U);
                    }
                }
            }
        }
        mapper->write_expansion(0x5200U, 4U);
        mapper->write_expansion(0x5000U, 0x7FU);
        for (Address addr = 0x8000U;; ++addr) {
            require_prg(*mapper, addr, 0x7FU, 4U, size, 0x35U);
            if (addr == 0xFFFFU) {
                break;
            }
        }
    }
}

TEST_CASE("Mapper 518 maps independent primary and bounded SB-97 secondary RAM",
          "[mapper518][ram][banking][bus][reset]") {
    auto mapper = board(kDefaultPrgSize, MirrorMode::Horizontal, 0U, true);
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        mapper->write_prg(addr, cell(addr, 0x45U));
    }
    for (const Byte mode : {Byte{0U}, Byte{4U}}) {
        mapper->write_expansion(0x5200U, mode);
        for (const Byte bank : {Byte{0x80U}, Byte{0x81U}, Byte{0xBFU}, Byte{0xFFU}}) {
            mapper->write_expansion(0x5000U, bank);
            for (const auto addr : kPrgAddresses) {
                if (addr >= 0xC000U && (mode & 4U) == 0U) {
                    continue;
                }
                const auto value = cell(addr + bank * 0x101U + mode, 0x72U);
                mapper->write_prg(addr, value);
                CAPTURE(mode, bank, addr);
                REQUIRE(mapper->read_prg(addr) == value);
                REQUIRE(mapper->prg_read_mask(addr) == 0xFFU);
            }
        }
    }
    for (Address addr = 0x6000U; addr < 0x8000U; ++addr) {
        REQUIRE(mapper->read_prg(addr) == cell(addr, 0x45U));
    }

    mapper->write_expansion(0x5000U, 0U);
    mapper->write_expansion(0x5200U, 0U);
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
    mapper->write_expansion(0x5000U, 0x80U);
    const auto secondary_before = mapper->read_prg(0x8123U);
    mapper->reset();
    REQUIRE(mapper->read_prg(0x8000U) == 0U);
    mapper->write_expansion(0x5000U, 0x80U);
    REQUIRE(mapper->read_prg(0x8123U) == secondary_before);
    mapper->power_on();
    REQUIRE(mapper->read_prg(0x6123U) == 0U);
    mapper->write_expansion(0x5000U, 0x80U);
    REQUIRE(mapper->read_prg(0x8123U) == 0U);
}

TEST_CASE("Mapper 518 latches CIRAM A10 on A13 rise and banks only lower CHR reads",
          "[mapper518][banking][chr][ram][ciram][bus]") {
    auto mapper = board();
    for (Address addr = 0U; addr < kRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0x60U));
    }
    REQUIRE(mapper->observes_ppu_address());
    for (Byte mode = 0U; mode < 8U; ++mode) {
        mapper->write_expansion(0x5200U, mode);
        for (Byte nametable = 0U; nametable < 4U; ++nametable) {
            observe_nametable(*mapper, nametable);
            REQUIRE(mapper->mirror_mode() == expected_mirror(mode));
            for (const auto addr : kChrAddresses) {
                const auto physical = addr < 0x1000U && (mode & 2U) != 0U && (nametable & 1U) != 0U
                                          ? addr + 0x1000U
                                          : addr;
                CAPTURE(mode, nametable, addr, physical);
                REQUIRE(mapper->read_chr(addr) == cell(physical, 0x60U));
            }
        }
    }

    mapper->write_expansion(0x5200U, 2U);
    observe_nametable(*mapper, 1U);
    REQUIRE(register_state(*mapper)[kPinsStateOffset] == 3U);
    mapper->observe_ppu_address(0x2800U);
    REQUIRE(register_state(*mapper)[kPinsStateOffset] == 3U);
    mapper->observe_ppu_address(0x2C00U);
    REQUIRE(register_state(*mapper)[kPinsStateOffset] == 3U);
    mapper->observe_ppu_address(0x1000U);
    mapper->observe_ppu_address(0x2000U);
    REQUIRE(register_state(*mapper)[kPinsStateOffset] == 2U);
    for (Address addr = 0U; addr < kRamSize; ++addr) {
        mapper->write_chr(addr, cell(addr, 0xA5U));
    }
    mapper->write_expansion(0x5200U, 0U);
    for (Address addr = 0U; addr < kRamSize; ++addr) {
        REQUIRE(mapper->read_chr(addr) == cell(addr, 0xA5U));
    }

    const auto before = state(*mapper);
    REQUIRE(mapper->read_chr(0x2000U) == 0U);
    REQUIRE(mapper->read_chr(0xFFFFU) == 0U);
    mapper->write_chr(0x2000U, 0xA5U);
    mapper->write_chr(0xFFFFU, 0x5AU);
    REQUIRE(state(*mapper) == before);
    for (Address addr = 0x2000U; addr < 0x3F00U; ++addr) {
        REQUIRE(mapper->maps_nametable(addr));
        REQUIRE_FALSE(mapper->nametable_address(addr));
        REQUIRE_FALSE(mapper->ciram_address(addr));
        REQUIRE_FALSE(mapper->read_nametable(addr));
        REQUIRE_FALSE(mapper->write_nametable(addr, static_cast<Byte>(addr)));
    }
    REQUIRE_FALSE(mapper->unmirrored_nametables());
}

TEST_CASE("Mapper 518 has no mapper IRQ or controller overlay and ignores unrelated observers",
          "[mapper518][irq][input][audio][bus]") {
    auto mapper = board();
    mapper->write_expansion(0x5000U, 0xFFU);
    mapper->write_chr(0x0123U, 0x5AU);
    const auto before = state(*mapper);
    for (const Address addr : {0x4016U, 0x4017U, 0x5001U, 0x5201U, 0x5301U}) {
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_read(0x2012U);
    mapper->clock_cpu();
    mapper->clock_cpu_falling_edge();
    mapper->clock_irq_counter();
    mapper->on_ppu_hblank();
    mapper->on_ppu_frame_start();
    mapper->acknowledge_irq();
    REQUIRE(state(*mapper) == before);
    REQUIRE(mapper->battery_ram().empty());
    REQUIRE(mapper->shared_nvram().empty());
    REQUIRE_FALSE(mapper->irq_pending());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(0U));
}

TEST_CASE("Mapper 518 LPC byte input enforces readiness codec prefixes and stop termination",
          "[mapper518][audio][input][decoding][state]") {
    auto mapper = board();
    REQUIRE(mapper->read_expansion(0x5300U) == 0U);
    clock_chip_ticks(*mapper, 16000U);
    REQUIRE(mapper->read_expansion(0x5300U) == 0x80U);
    const auto empty = state(*mapper);
    mapper->write_expansion(0x5300U, 0U);
    mapper->write_expansion(0x5300U, 0xFFU);
    REQUIRE(state(*mapper) == empty);

    mapper->write_expansion(0x5300U, 0x0AU);
    for (unsigned index = 0U; index < 16U; ++index) {
        mapper->write_expansion(0x5300U, static_cast<Byte>(index * 17U));
    }
    auto full = state(*mapper);
    REQUIRE(full[kSpeechStateOffset + 19U] == 128U);
    REQUIRE((mapper->read_expansion(0x5300U) & 0x80U) == 0U);
    for (unsigned index = 0U; index < 256U; ++index) {
        mapper->write_expansion(0x5300U, static_cast<Byte>(index));
    }
    REQUIRE(state(*mapper) == full);

    mapper->reset();
    const auto d6 = message(true);
    write_speech(*mapper, d6);
    const auto samples = speech_samples(*mapper, 3U);
    REQUIRE(mapper->read_expansion(0x5300U) == 0x8EU);
    REQUIRE(std::ranges::any_of(samples, [](float sample) { return std::abs(sample) > 0.0F; }));
    clock_chip_ticks(*mapper, 19800U);
    REQUIRE(mapper->read_expansion(0x5300U) == 0x0FU);
    REQUIRE(mapper->audio_output() == 0.0F);
    mapper->write_expansion(0x5300U, 0x0AU);
    REQUIRE(mapper->read_expansion(0x5300U) == 0x0FU);
    mapper->write_expansion(0x5300U, 0x0FU);
    REQUIRE(mapper->read_expansion(0x5300U) == 0x8FU);
    write_speech(*mapper, message(false));
    (void)speech_samples(*mapper, 3U);
    REQUIRE(mapper->read_expansion(0x5300U) == 0x8EU);
}

TEST_CASE("Mapper 518 synthesizes D6 and TMS5220 PE streams consistently across regions",
          "[mapper518][audio][input][reset]") {
    for (const bool d6 : {false, true}) {
        std::vector<float> reference;
        for (const auto region : {Region::NTSC, Region::PAL, Region::Dendy}) {
            auto mapper = board();
            mapper->set_region(region);
            write_speech(*mapper, message(d6));
            const auto samples = speech_samples(*mapper, 202U, region);
            CAPTURE(d6, region);
            REQUIRE(std::ranges::any_of(samples, [](float sample) { return sample > 0.0F; }));
            REQUIRE(
                std::ranges::all_of(samples, [](float sample) { return std::isfinite(sample); }));
            if (region == Region::NTSC) {
                reference = samples;
            } else {
                REQUIRE(samples == reference);
            }
            REQUIRE(mapper->read_expansion(0x5300U) == 0x0FU);
            REQUIRE(samples[200] == 0.0F);
            REQUIRE(samples[201] == 0.0F);
            mapper->write_prg(0x6123U, 0xA5U);
            mapper->write_chr(0x0123U, 0x5AU);
            mapper->reset();
            REQUIRE(mapper->audio_output() == 0.0F);
            REQUIRE(mapper->read_expansion(0x5300U) == 0U);
            REQUIRE(mapper->read_prg(0x6123U) == 0xA5U);
            REQUIRE(mapper->read_chr(0x0123U) == 0x5AU);
        }
    }
}

TEST_CASE("Mapper 518 bounded snapshots replay registers RAM FDC and mid-speech without ROM",
          "[mapper518][state][atomic][audio][ram][ciram]") {
    auto mapper = board(kDefaultPrgSize, MirrorMode::Vertical, 0x35U, true);
    mapper->write_prg(0x6123U, 0xA6U);
    mapper->write_chr(0x1321U, 0x5AU);
    mapper->write_chr(0x1432U, 0x6BU);
    mapper->write_expansion(0x5000U, 0x9FU);
    mapper->write_expansion(0x5200U, 6U);
    mapper->write_prg(0x9432U, 0x7CU);
    mapper->write_expansion(0x5500U, 0x12U);
    mapper->write_expansion(0x5501U, 0x34U);
    mapper->write_expansion(0x5505U, 0x56U);
    observe_nametable(*mapper, 1U);
    write_speech(*mapper, message(true, false));
    (void)speech_samples(*mapper, 7U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kSb97StateSize);
    REQUIRE(saved[kBankStateOffset] == 0x9FU);
    REQUIRE(saved[kModeStateOffset] == 6U);
    REQUIRE(saved[kPinsStateOffset] == 3U);
    REQUIRE(mapper->audio_output() != 0.0F);

    auto copy = board(kDefaultPrgSize, MirrorMode::Vertical, 0x80U, true);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->read_prg(0x6123U) == 0xA6U);
    REQUIRE(copy->read_chr(0x0321U) == 0x5AU);
    REQUIRE(copy->read_chr(0x1432U) == 0x6BU);
    REQUIRE(copy->read_prg(0x9432U) == 0x7CU);
    REQUIRE(copy->read_expansion(0x5605U) == 0x56U);
    REQUIRE(copy->mirror_mode() == MirrorMode::Vertical);
    REQUIRE(copy->audio_output() == mapper->audio_output());
    for (unsigned cycle = 0U; cycle < 10000U; ++cycle) {
        mapper->clock_audio();
        copy->clock_audio();
        REQUIRE(copy->audio_output() == mapper->audio_output());
    }
    REQUIRE(state(*copy) == state(*mapper));

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

TEST_CASE("Mapper 518 rejects truncated damaged and mismatched states atomically",
          "[mapper518][state][atomic][ram][audio]") {
    auto mapper = board();
    mapper->write_prg(0x6123U, 0xA5U);
    mapper->write_chr(0x0123U, 0x5AU);
    mapper->write_expansion(0x5000U, 0xDFU);
    mapper->write_expansion(0x5200U, 6U);
    observe_nametable(*mapper, 1U);
    write_speech(*mapper, message(false, false));
    (void)speech_samples(*mapper, 5U);
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kBankStateOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        load_rejected(*mapper, invalid);
    }
    for (const auto& [offset, value] : {std::pair{kModeStateOffset, 8U},
                                        std::pair{kPinsStateOffset, 4U},
                                        std::pair{kSpeechStateOffset, 0U},
                                        std::pair{kSpeechStateOffset + 18U, 128U},
                                        std::pair{kSpeechStateOffset + 19U, 129U},
                                        std::pair{kSpeechStateOffset + 20U, 3U},
                                        std::pair{kSpeechStateOffset + 27U, 100U},
                                        std::pair{kSpeechStateOffset + 28U, 200U},
                                        std::pair{kSpeechStateOffset + 29U, 13U},
                                        std::pair{kSpeechStateOffset + 30U, 16U},
                                        std::pair{kSpeechStateOffset + 31U, 16U}}) {
        auto invalid = saved;
        invalid[offset] = static_cast<Byte>(value);
        CAPTURE(offset, value);
        load_rejected(*mapper, invalid);
    }
    auto invalid_noise = saved;
    invalid_noise[kSpeechStateOffset + 33U] = 0U;
    invalid_noise[kSpeechStateOffset + 34U] = 0U;
    load_rejected(*mapper, invalid_noise);
    auto invalid_secondary_length = saved;
    std::fill_n(invalid_secondary_length.begin() + kSecondaryLengthStateOffset, 8U, 0xFFU);
    load_rejected(*mapper, invalid_secondary_length);

    auto wrong_prg = board(kDefaultPrgSize / 2U);
    load_rejected(*wrong_prg, saved);
    auto wrong_mirror = board(kDefaultPrgSize, MirrorMode::Vertical);
    load_rejected(*wrong_mirror, saved);
    auto wrong_profile = board(kDefaultPrgSize, MirrorMode::Horizontal, 0U, true);
    load_rejected(*wrong_profile, saved);
    auto missing_secondary = board(kDefaultPrgSize, MirrorMode::Vertical, 0U, true);
    load_rejected(*mapper, state(*missing_secondary));

    auto changed = saved;
    changed[kBankStateOffset] = 0xD3U;
    changed[kModeStateOffset] = 3U;
    changed[kPinsStateOffset] = 1U;
    changed[kWramStateOffset + 0x123U] ^= 0xFFU;
    changed[kChrRamStateOffset + 0x123U] ^= 0xFFU;
    changed[kFdcStateOffset] = 0xA5U;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
}

} // namespace
