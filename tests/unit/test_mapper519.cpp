#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "core/mappers/mapper519.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;
namespace {

constexpr std::size_t kMinPrgSize = 0x008000UZ;
constexpr std::size_t kMaxPrgSize = 0x100000UZ;
constexpr std::size_t kMinChrSize = 0x002000UZ;
constexpr std::size_t kMaxChrSize = 0x100000UZ;
constexpr std::size_t kPrgBankSize = 0x004000UZ;
constexpr std::size_t kChrInnerBankSize = 0x002000UZ;
constexpr std::size_t kChrOuterBankSize = 0x008000UZ;
constexpr std::size_t kAddressStateOffset = 10UZ;
constexpr std::size_t kDataStateOffset = 12UZ;
constexpr std::size_t kStateSize = 13UZ;
constexpr std::array<Address, 8> kPrgAddresses{
    0x8000U, 0x8001U, 0x9234U, 0xBFFFU, 0xC000U, 0xD234U, 0xFFFEU, 0xFFFFU};
constexpr std::array<Address, 8> kChrAddresses{
    0x0000U, 0x0001U, 0x0123U, 0x0FFFU, 0x1000U, 0x1234U, 0x1FFEU, 0x1FFFU};

INesHeader header(MirrorMode mirror = MirrorMode::Horizontal) {
    INesHeader result{};
    result.mapper_number = 519U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    result.region = Region::NTSC;
    result.prg_rom_size = kMinPrgSize * 4U;
    result.chr_rom_size = kMinChrSize * 4U;
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

std::unique_ptr<Mapper519> board(std::size_t prg_size = kMinPrgSize * 4U,
                                 std::size_t chr_size = kMinChrSize * 4U,
                                 MirrorMode mirror = MirrorMode::Horizontal,
                                 Byte dip = 0U,
                                 Byte prg_salt = 0U,
                                 Byte chr_salt = 0x40U) {
    const auto h = header(mirror);
    REQUIRE(Mapper519::validate_geometry(h, prg_size, chr_size));
    auto result = std::make_unique<Mapper519>(h, rom(prg_size, prg_salt), rom(chr_size, chr_salt));
    REQUIRE(result->configure_board_jumpers(dip));
    return result;
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

std::array<Byte, kStateSize> register_state(const Mapper& mapper) {
    const auto bytes = state(mapper);
    REQUIRE(bytes.size() == kStateSize);
    std::array<Byte, kStateSize> result{};
    std::copy(bytes.begin(), bytes.end(), result.begin());
    return result;
}

std::uint16_t latch_at(const std::array<Byte, kStateSize>& bytes) {
    return static_cast<std::uint16_t>(bytes[kAddressStateOffset] |
                                      (bytes[kAddressStateOffset + 1U] << 8U));
}

void load(Mapper519& mapper, std::span<const Byte> bytes) {
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
}

void load_rejected(Mapper519& mapper, std::span<const Byte> bytes) {
    const auto before = state(mapper);
    const auto prg = mapper.read_prg(0x9234U);
    const auto chr = mapper.read_chr(0x0123U);
    const auto mirror = mapper.mirror_mode();
    StateReader reader(bytes);
    mapper.load_state(reader);
    REQUIRE_FALSE(reader.ok());
    REQUIRE(state(mapper) == before);
    REQUIRE(mapper.read_prg(0x9234U) == prg);
    REQUIRE(mapper.read_chr(0x0123U) == chr);
    REQUIRE(mapper.mirror_mode() == mirror);
}

Byte initial_data(MirrorMode mirror) {
    return mirror == MirrorMode::Horizontal ? 0x80U : 0U;
}

std::size_t expected_prg_offset(Address addr,
                                std::uint16_t address_latch,
                                Byte dip,
                                std::size_t prg_size) {
    const auto read_address =
        (address_latch & 0x40U) != 0U ? static_cast<Address>((addr & 0xFFF0U) | dip) : addr;
    const auto bank = address_latch & 0x3FU;
    const auto offset =
        (address_latch & 0x80U) != 0U
            ? static_cast<std::size_t>(bank) * kPrgBankSize + (read_address & 0x3FFFU)
            : static_cast<std::size_t>(bank & 0x3EU) * kPrgBankSize + (read_address & 0x7FFFU);
    return offset % prg_size;
}

std::size_t expected_chr_offset(Address addr, Byte data_latch, std::size_t chr_size) {
    const auto outer = static_cast<std::size_t>((data_latch >> 2U) & 0x1FU);
    const auto inner = static_cast<std::size_t>(data_latch & 3U);
    return (outer * kChrOuterBankSize + inner * kChrInnerBankSize + addr) % chr_size;
}

void require_prg(Mapper519& mapper,
                 Address addr,
                 std::uint16_t address_latch,
                 Byte dip,
                 std::size_t prg_size,
                 Byte salt) {
    const auto physical = expected_prg_offset(addr, address_latch, dip, prg_size);
    CAPTURE(addr, address_latch, dip, prg_size, physical, salt);
    REQUIRE(mapper.read_prg(addr) == cell(physical, salt));
    REQUIRE(mapper.maps_prg(addr));
    REQUIRE(mapper.prg_read_mask(addr) == 0xFFU);
}

void require_chr(
    Mapper519& mapper, Address addr, Byte data_latch, std::size_t chr_size, Byte salt) {
    const auto physical = expected_chr_offset(addr, data_latch, chr_size);
    CAPTURE(addr, data_latch, chr_size, physical, salt);
    REQUIRE(mapper.read_chr(addr) == cell(physical, salt));
}

TEST_CASE("Mapper 519 validates ROM-only geometry and fixed bounded state",
          "[mapper519][geometry][ram][ciram][state]") {
    for (std::size_t prg_size = kMinPrgSize; prg_size <= kMaxPrgSize; prg_size *= 2U) {
        for (std::size_t chr_size = kMinChrSize; chr_size <= kMaxChrSize; chr_size *= 2U) {
            for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
                CAPTURE(prg_size, chr_size, mirror);
                REQUIRE(Mapper519::validate_geometry(header(mirror), prg_size, chr_size));
            }
        }
    }

    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize * 4U, kMinChrSize * 4U, mirror, 0x0AU, 0x35U, 0x60U);
        auto different_rom = board(kMinPrgSize * 4U, kMinChrSize * 4U, mirror, 0x0AU, 0x80U, 0xA0U);
        REQUIRE(state(*mapper) == state(*different_rom));
        REQUIRE(register_state(*mapper) == register_state(*different_rom));
        REQUIRE(latch_at(register_state(*mapper)) == 0U);
        REQUIRE(register_state(*mapper)[kDataStateOffset] == initial_data(mirror));
        REQUIRE(mapper->mirror_mode() == mirror);
        REQUIRE(mapper->battery_ram().empty());
        REQUIRE(mapper->shared_nvram().empty());
        REQUIRE_FALSE(mapper->has_flash_storage());
        REQUIRE_FALSE(mapper->irq_pending());
        REQUIRE_FALSE(mapper->has_expansion_audio());
        REQUIRE(mapper->audio_output() == 0.0F);
        require_prg(*mapper, 0x8000U, 0U, 0x0AU, kMinPrgSize * 4U, 0x35U);
        require_prg(*mapper, 0xFFFFU, 0U, 0x0AU, kMinPrgSize * 4U, 0x35U);
        require_chr(*mapper, 0U, initial_data(mirror), kMinChrSize * 4U, 0x60U);
        require_chr(*mapper, 0x1FFFU, initial_data(mirror), kMinChrSize * 4U, 0x60U);
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
        REQUIRE_FALSE(Mapper519::validate_geometry(h, prg, chr));
    };
    for (const unsigned id : {0U, 4U, 517U, 518U, 520U, 4095U}) {
        auto h = header();
        h.mapper_number = static_cast<std::uint16_t>(id);
        reject(h, kMinPrgSize, kMinChrSize);
    }
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy, kMinPrgSize, kMinChrSize);
    for (Byte submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = submapper;
        reject(h, kMinPrgSize, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x4000UZ, 0x7FFFUZ, 0x8001UZ, 0xC000UZ, 0x18000UZ, 0x200000UZ}) {
        reject(header(), size, kMinChrSize);
    }
    for (const std::size_t size :
         {0UZ, 1UZ, 0x1000UZ, 0x1FFFUZ, 0x2001UZ, 0x3000UZ, 0x18000UZ, 0x200000UZ}) {
        reject(header(), kMinPrgSize, size);
    }
    for (const auto mirror :
         {MirrorMode::FourScreen, MirrorMode::SingleLower, MirrorMode::SingleUpper}) {
        reject(header(mirror), kMinPrgSize, kMinChrSize);
    }
    for (unsigned field = 0U; field < 6U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.has_trainer = true;
            break;
        case 2U:
            h.prg_ram_size = 0x2000U;
            break;
        case 3U:
            h.prg_nvram_size = 0x2000U;
            break;
        case 4U:
            h.chr_ram_size = 0x2000U;
            break;
        default:
            h.chr_nvram_size = 0x2000U;
            break;
        }
        CAPTURE(field);
        reject(h, kMinPrgSize, kMinChrSize);
    }

    auto mapper = board();
    mapper->write_prg(0x817FU, 0xA5U);
    const auto before = state(*mapper);
    REQUIRE_FALSE(mapper->configure_board_jumpers(16U));
    REQUIRE_FALSE(mapper->configure_board_jumpers(255U));
    REQUIRE(state(*mapper) == before);
    for (Byte dip = 0U; dip < 16U; ++dip) {
        REQUIRE(mapper->configure_board_jumpers(dip));
        REQUIRE((state(*mapper) != before || dip == 0U));
    }
}

TEST_CASE("Mapper 519 exhaustively decodes CPU writes and floating low buses",
          "[mapper519][decoding][bus][ram]") {
    auto mapper = board();
    const auto cold = register_state(*mapper);
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        mapper->reset();
        auto expected = cold;
        const auto addr = static_cast<Address>(raw);
        const auto value = static_cast<Byte>(raw * 41U + (raw >> 8U) + 0xA5U);
        if (raw >= 0x8000U) {
            expected[kAddressStateOffset] = static_cast<Byte>(raw & 0xFFU);
            expected[kAddressStateOffset + 1U] = static_cast<Byte>((raw >> 8U) & 1U);
            expected[kDataStateOffset] = value;
        }

        mapper->write_expansion(addr, static_cast<Byte>(value ^ 0xFFU));
        mapper->write_prg(addr, value);
        CAPTURE(raw, value);
        REQUIRE(register_state(*mapper) == expected);
        REQUIRE(mapper->maps_prg(addr) == (raw >= 0x8000U));
        REQUIRE(mapper->prg_read_mask(addr) == (raw >= 0x8000U ? 0xFFU : 0U));
        REQUIRE_FALSE(mapper->maps_expansion(addr));
        REQUIRE(mapper->expansion_read_mask(addr) == 0xFFU);
        REQUIRE(mapper->read_expansion(addr) == 0U);
        if (raw < 0x8000U) {
            REQUIRE(mapper->read_prg(addr) == 0U);
        }
    }
    REQUIRE(mapper->cpu_open_bus_mask() == 0xFFU);

    mapper->reset();
    mapper->write_prg_cpu(0x8000U, 0x12U, true);
    REQUIRE(register_state(*mapper)[kDataStateOffset] == 0x12U);
    mapper->write_prg_cpu(0xFFFFU, 0x34U, false);
    REQUIRE(register_state(*mapper)[kDataStateOffset] == 0x34U);
}

TEST_CASE("Mapper 519 selects NROM-128 and NROM-256 banks with DIP substitution",
          "[mapper519][banking][prg][bus]") {
    for (std::size_t prg_size = kMinPrgSize; prg_size <= kMaxPrgSize; prg_size *= 2U) {
        auto mapper = board(prg_size, kMinChrSize, MirrorMode::Vertical, 0x0BU, 0x35U);
        for (const Byte dip : {Byte{0U}, Byte{0x0BU}}) {
            REQUIRE(mapper->configure_board_jumpers(dip));
            for (const std::uint16_t mode : {0U, 0x80U}) {
                for (std::uint16_t bank = 0U; bank < 64U; ++bank) {
                    const auto address_latch =
                        static_cast<std::uint16_t>(mode | bank | (dip != 0U ? 0x40U : 0U));
                    mapper->write_prg(static_cast<Address>(0x8000U | address_latch), 0U);
                    CAPTURE(prg_size, dip, address_latch);
                    REQUIRE(latch_at(register_state(*mapper)) == address_latch);
                    for (const auto addr : kPrgAddresses) {
                        require_prg(*mapper, addr, address_latch, dip, prg_size, 0x35U);
                    }
                }
            }
        }
    }

    auto mapper = board(kMaxPrgSize, kMinChrSize, MirrorMode::Horizontal, 0x0FU, 0x65U);
    mapper->write_prg(0x81FFU, 0U);
    for (Address addr = 0x8000U;; ++addr) {
        require_prg(*mapper, addr, 0x01FFU, 0x0FU, kMaxPrgSize, 0x65U);
        if (addr == 0xFFFFU) {
            break;
        }
    }
}

TEST_CASE("Mapper 519 combines outer and inner CHR-ROM banks and controls CIRAM",
          "[mapper519][banking][chr][ciram][bus]") {
    for (std::size_t chr_size = kMinChrSize; chr_size <= kMaxChrSize; chr_size *= 2U) {
        auto mapper = board(kMinPrgSize, chr_size, MirrorMode::Vertical, 0U, 0U, 0x60U);
        for (Byte outer = 0U; outer < 32U; ++outer) {
            for (Byte inner = 0U; inner < 4U; ++inner) {
                const auto data = static_cast<Byte>((outer << 2U) | inner);
                mapper->write_prg(0x8000U, data);
                REQUIRE(register_state(*mapper)[kDataStateOffset] == data);
                REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
                for (const auto addr : kChrAddresses) {
                    require_chr(*mapper, addr, data, chr_size, 0x60U);
                }
            }
        }
        mapper->write_prg(0x8000U, 0x80U);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        mapper->write_prg(0x8000U, 0x7FU);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Vertical);
    }

    auto mapper = board(kMinPrgSize, kMaxChrSize, MirrorMode::Horizontal, 0U, 0U, 0x75U);
    mapper->write_prg(0x8000U, 0xFFU);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        require_chr(*mapper, addr, 0xFFU, kMaxChrSize, 0x75U);
    }
    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        mapper->write_chr(addr, static_cast<Byte>(addr ^ 0xA5U));
    }
    REQUIRE(state(*mapper) == before);
    for (Address addr = 0U; addr < 0x2000U; ++addr) {
        require_chr(*mapper, addr, 0xFFU, kMaxChrSize, 0x75U);
    }
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
    REQUIRE_FALSE(mapper->observes_ppu_address());
    REQUIRE_FALSE(mapper->observes_ppu_a12());
    REQUIRE_FALSE(mapper->observes_ppu_reads());
    REQUIRE_FALSE(mapper->read_ppu_register(0x2007U, 0xA5U));
    REQUIRE_FALSE(mapper->write_ppu_register(0x2007U, 0xA5U));
}

TEST_CASE("Mapper 519 lock freezes starred fields and permits only inner CHR updates",
          "[mapper519][decoding][banking][ciram][state]") {
    auto mapper = board(kMaxPrgSize, kMaxChrSize, MirrorMode::Vertical, 0x0EU, 0x35U, 0x60U);
    constexpr std::uint16_t kLockedAddress = 0x01FFU;
    constexpr Byte kLockedData = 0xD4U;
    mapper->write_prg(static_cast<Address>(0x8000U | kLockedAddress), kLockedData);
    REQUIRE(latch_at(register_state(*mapper)) == kLockedAddress);
    REQUIRE(register_state(*mapper)[kDataStateOffset] == kLockedData);
    REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);

    auto expected_data = kLockedData;
    for (unsigned raw = 0U; raw <= 0xFFFFU; ++raw) {
        const auto value = static_cast<Byte>(raw * 73U + (raw >> 8U) + 0x19U);
        mapper->write_prg(static_cast<Address>(raw), value);
        if (raw >= 0x8000U) {
            expected_data = static_cast<Byte>(kLockedData | (value & 3U));
        }
        CAPTURE(raw, value, expected_data);
        REQUIRE(latch_at(register_state(*mapper)) == kLockedAddress);
        REQUIRE(register_state(*mapper)[kDataStateOffset] == expected_data);
        REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
        require_prg(*mapper, 0x9234U, kLockedAddress, 0x0EU, kMaxPrgSize, 0x35U);
        require_chr(*mapper, 0x0123U, expected_data, kMaxChrSize, 0x60U);
    }
}

TEST_CASE("Mapper 519 exposes no RAM IRQ audio or unrelated observer side effects",
          "[mapper519][ram][irq][audio][bus]") {
    auto mapper = board();
    mapper->write_prg(0x817FU, 0xA5U);
    const auto before = state(*mapper);
    for (Address addr = 0U; addr < 0x8000U; ++addr) {
        mapper->write_prg(addr, static_cast<Byte>(addr));
        REQUIRE(mapper->read_prg(addr) == 0U);
        REQUIRE_FALSE(mapper->maps_prg(addr));
        REQUIRE(mapper->prg_read_mask(addr) == 0U);
    }
    mapper->set_battery_ram(std::array<Byte, 4>{1U, 2U, 3U, 4U});
    mapper->set_shared_nvram(std::array<Byte, 4>{5U, 6U, 7U, 8U});
    mapper->observe_cpu_read(0x8000U, 0xA5U);
    mapper->observe_cpu_write(0x8000U, 0xA5U);
    mapper->observe_ppu_a12(true);
    mapper->observe_ppu_address(0x2012U);
    mapper->observe_ppu_read(0x2012U);
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
    REQUIRE_FALSE(mapper->configure_board_profile("unknown"));
    REQUIRE(mapper->led_outputs() == std::nullopt);
}

TEST_CASE("Mapper 519 reset restores unlocked bank zero and retains DIP wiring",
          "[mapper519][reset][banking][ciram][state]") {
    for (const auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMinPrgSize * 4U, kMinChrSize * 4U, mirror, 0x0DU, 0x35U, 0x60U);
        const auto cold = state(*mapper);
        mapper->write_prg(0x81FFU, static_cast<Byte>(initial_data(mirror) ^ 0x80U));
        REQUIRE(state(*mapper) != cold);
        REQUIRE(mapper->mirror_mode() != mirror);

        mapper->reset();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
        require_prg(*mapper, 0xFFFFU, 0U, 0x0DU, kMinPrgSize * 4U, 0x35U);
        require_chr(*mapper, 0x0123U, initial_data(mirror), kMinChrSize * 4U, 0x60U);

        mapper->write_prg(0x81FFU, static_cast<Byte>(initial_data(mirror) ^ 0x80U));
        mapper->power_on();
        REQUIRE(state(*mapper) == cold);
        REQUIRE(mapper->mirror_mode() == mirror);
    }
}

TEST_CASE("Mapper 519 snapshots replay latches without ROM bytes",
          "[mapper519][state][atomic][banking][ciram]") {
    auto mapper = board(kMinPrgSize * 8U, kMinChrSize * 8U, MirrorMode::Vertical, 9U, 0x35U, 0x60U);
    mapper->write_prg(0x81B7U, 0xD6U);
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);
    REQUIRE(latch_at(register_state(*mapper)) == 0x01B7U);
    REQUIRE(saved[kDataStateOffset] == 0xD6U);

    auto copy = board(kMinPrgSize * 8U, kMinChrSize * 8U, MirrorMode::Vertical, 9U, 0x80U, 0xA0U);
    load(*copy, saved);
    REQUIRE(state(*copy) == saved);
    REQUIRE(copy->mirror_mode() == MirrorMode::Horizontal);
    for (const auto addr : kPrgAddresses) {
        require_prg(*copy, addr, 0x01B7U, 9U, kMinPrgSize * 8U, 0x80U);
    }
    for (const auto addr : kChrAddresses) {
        require_chr(*copy, addr, 0xD6U, kMinChrSize * 8U, 0xA0U);
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

TEST_CASE("Mapper 519 rejects truncated damaged and mismatched states atomically",
          "[mapper519][state][atomic][geometry]") {
    auto mapper = board(kMinPrgSize * 4U, kMinChrSize * 4U, MirrorMode::Horizontal, 7U);
    mapper->write_prg(0x81B7U, 0xD6U);
    const auto saved = state(*mapper);

    for (std::size_t length = 0U; length < saved.size(); ++length) {
        CAPTURE(length);
        load_rejected(*mapper, std::span(saved).first(length));
    }
    for (std::size_t offset = 0U; offset < kAddressStateOffset; ++offset) {
        auto invalid = saved;
        invalid[offset] ^= 1U;
        CAPTURE(offset);
        load_rejected(*mapper, invalid);
    }
    for (const std::uint16_t invalid_latch : {0x0200U, 0x0201U, 0x8000U, 0xFFFFU}) {
        auto invalid = saved;
        invalid[kAddressStateOffset] = static_cast<Byte>(invalid_latch & 0xFFU);
        invalid[kAddressStateOffset + 1U] = static_cast<Byte>(invalid_latch >> 8U);
        CAPTURE(invalid_latch);
        load_rejected(*mapper, invalid);
    }

    auto wrong_prg = board(kMinPrgSize * 2U, kMinChrSize * 4U, MirrorMode::Horizontal, 7U);
    load_rejected(*wrong_prg, saved);
    auto wrong_chr = board(kMinPrgSize * 4U, kMinChrSize * 2U, MirrorMode::Horizontal, 7U);
    load_rejected(*wrong_chr, saved);
    auto wrong_mirror = board(kMinPrgSize * 4U, kMinChrSize * 4U, MirrorMode::Vertical, 7U);
    load_rejected(*wrong_mirror, saved);
    auto wrong_dip = board(kMinPrgSize * 4U, kMinChrSize * 4U, MirrorMode::Horizontal, 8U);
    load_rejected(*wrong_dip, saved);

    auto changed = saved;
    changed[kAddressStateOffset] = 0x55U;
    changed[kAddressStateOffset + 1U] = 0U;
    changed[kDataStateOffset] = 0x2BU;
    load(*mapper, changed);
    REQUIRE(state(*mapper) == changed);
    REQUIRE(latch_at(register_state(*mapper)) == 0x0055U);
    REQUIRE(register_state(*mapper)[kDataStateOffset] == 0x2BU);
}

} // namespace
