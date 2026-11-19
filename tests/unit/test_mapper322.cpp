#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "core/bus/memory_bus.hpp"
#include "core/cartridge/cartridge.hpp"
#include "core/mappers/mapper322.hpp"
#include "core/mappers/mapper_registry.hpp"
#include "core/state/state.hpp"

using namespace mapperbus::core;

namespace {

constexpr std::size_t kMinRomSize = 128UZ * 1024UZ;
constexpr std::size_t kMaxRomSize = 2UZ * 1024UZ * 1024UZ;
constexpr std::size_t kStateSize = 25UZ;

INesHeader header(MirrorMode mirror = MirrorMode::Vertical) {
    INesHeader result{};
    result.mapper_number = 322U;
    result.is_nes2 = true;
    result.mirror_mode = mirror;
    return result;
}

Byte prg_cell(std::size_t offset) {
    const auto bank = offset / 8192U;
    const auto within = offset & 8191U;
    return static_cast<Byte>(bank * 37U + within * 5U + (within >> 8U));
}

Byte chr_cell(std::size_t offset) {
    const auto bank = offset / 1024U;
    const auto within = offset & 1023U;
    return static_cast<Byte>(bank * 29U + within * 7U + (within >> 8U));
}

std::vector<Byte> prg_image(std::size_t size = kMaxRomSize) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = prg_cell(offset);
    }
    return data;
}

std::vector<Byte> chr_image(std::size_t size = kMaxRomSize) {
    std::vector<Byte> data(size);
    for (std::size_t offset = 0U; offset < size; ++offset) {
        data[offset] = chr_cell(offset);
    }
    return data;
}

std::unique_ptr<Mapper322> board(std::size_t prg_size = kMaxRomSize,
                                 std::size_t chr_size = kMaxRomSize,
                                 MirrorMode mirror = MirrorMode::Vertical) {
    const auto h = header(mirror);
    REQUIRE(Mapper322::validate_geometry(h, prg_size, chr_size));
    return std::make_unique<Mapper322>(h, prg_image(prg_size), chr_image(chr_size));
}

std::vector<Byte> state(const Mapper& mapper) {
    StateWriter writer;
    mapper.save_state(writer);
    return writer.take();
}

void set_bank(Mapper& mapper, Byte index, Byte value, Byte mode = 0U) {
    mapper.write_prg(0x8000U, static_cast<Byte>(index | mode));
    mapper.write_prg(0x8001U, value);
}

void set_outer(Mapper& mapper, Address addr, Byte value) {
    mapper.write_prg(0xA001U, 0x80U);
    mapper.write_prg(addr, value);
}

unsigned outer_number(Byte outer) {
    return ((outer & 0x40U) >> 4U) | ((outer & 0x18U) >> 3U);
}

std::size_t outer_size(Byte outer) {
    return ((outer & 0xA0U) == 0xA0U ? 256UZ : 128UZ) * 1024UZ;
}

std::size_t expected_prg_offset(Byte outer,
                                Byte select,
                                const std::array<Byte, 8>& banks,
                                Address addr) {
    const auto outer_base = static_cast<std::size_t>(outer_number(outer));
    if ((outer & 0x20U) == 0U) {
        const unsigned inner = outer & 7U;
        if (inner == 0U || inner == 4U) {
            return outer_base * kMinRomSize + inner * 16384U + (addr & 0x3FFFU);
        }
        return outer_base * kMinRomSize + (inner & 6U) * 16384U + (addr & 0x7FFFU);
    }

    const auto size = outer_size(outer);
    const auto mask = size / 8192U - 1U;
    const unsigned slot = (addr - 0x8000U) / 8192U;
    std::size_t inner = 0U;
    if (slot == 1U) {
        inner = banks[7] & mask;
    } else if (slot == 3U) {
        inner = mask;
    } else if ((slot == 0U) == ((select & 0x40U) == 0U)) {
        inner = banks[6] & mask;
    } else {
        inner = mask - 1U;
    }
    return outer_base * size + inner * 8192U + (addr & 0x1FFFU);
}

Byte expected_chr_bank(Byte select, const std::array<Byte, 8>& banks, Address addr) {
    unsigned slot = (addr / 1024U) & 7U;
    if ((select & 0x80U) != 0U) {
        slot ^= 4U;
    }
    if (slot < 4U) {
        return (banks[slot / 2U] & 0xFEU) | (slot & 1U);
    }
    return banks[slot - 2U];
}

std::size_t expected_chr_offset(Byte outer,
                                Byte select,
                                const std::array<Byte, 8>& banks,
                                Address addr) {
    const auto size = outer_size(outer);
    const auto mask = size / 1024U - 1U;
    return static_cast<std::size_t>(outer_number(outer)) * size +
           (expected_chr_bank(select, banks, addr) & mask) * 1024U + (addr & 0x3FFU);
}

std::vector<Byte> ines_image() {
    std::vector<Byte> data(16U, 0U);
    data[0] = 'N';
    data[1] = 'E';
    data[2] = 'S';
    data[3] = 0x1AU;
    data[4] = 8U;
    data[5] = 16U;
    data[6] = 0x21U;
    data[7] = 0x48U;
    data[8] = 0x01U;
    const auto prg = prg_image(kMinRomSize);
    const auto chr = chr_image(kMinRomSize);
    data.insert(data.end(), prg.begin(), prg.end());
    data.insert(data.end(), chr.begin(), chr.end());
    return data;
}

TEST_CASE("Mapper 322 validates mapper, submapper, ROM bounds and memory population",
          "[mapper322]") {
    for (std::size_t size = kMinRomSize; size <= kMaxRomSize; size *= 2U) {
        for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
            REQUIRE(Mapper322::validate_geometry(header(mirror), size, size));
        }
    }

    const auto reject = [](const INesHeader& h,
                           std::size_t prg_size = kMaxRomSize,
                           std::size_t chr_size = kMaxRomSize) {
        REQUIRE_FALSE(Mapper322::validate_geometry(h, prg_size, chr_size));
    };
    auto wrong_id = header();
    wrong_id.mapper_number = 321U;
    reject(wrong_id);
    auto legacy = header();
    legacy.is_nes2 = false;
    reject(legacy);
    for (unsigned submapper = 1U; submapper < 16U; ++submapper) {
        auto h = header();
        h.submapper = static_cast<Byte>(submapper);
        reject(h);
    }
    for (std::size_t size :
         {0UZ, 1UZ, 8192UZ, 65536UZ, 131071UZ, 131073UZ, 196608UZ, 2097153UZ, 4194304UZ}) {
        reject(header(), size, kMaxRomSize);
        reject(header(), kMaxRomSize, size);
    }
    for (unsigned field = 0U; field < 8U; ++field) {
        auto h = header();
        switch (field) {
        case 0U:
            h.has_battery = true;
            break;
        case 1U:
            h.prg_ram_size = 8192U;
            break;
        case 2U:
            h.prg_nvram_size = 8192U;
            break;
        case 3U:
            h.chr_ram_size = 8192U;
            break;
        case 4U:
            h.chr_nvram_size = 8192U;
            break;
        case 5U:
            h.mirror_mode = MirrorMode::FourScreen;
            break;
        case 6U:
            h.mirror_mode = MirrorMode::SingleLower;
            break;
        default:
            h.mirror_mode = MirrorMode::SingleUpper;
            break;
        }
        INFO(field);
        reject(h);
    }
}

TEST_CASE("Mapper 322 NROM mode implements every inner and outer bank combination", "[mapper322]") {
    auto mapper = board();
    const std::array<Byte, 8> banks{};
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        const auto outer = static_cast<Byte>(raw);
        set_outer(*mapper, 0x6000U, outer);
        for (unsigned addr = 0x8000U; addr <= 0xFFFFU; ++addr) {
            const auto cpu = static_cast<Address>(addr);
            CAPTURE(raw, addr);
            REQUIRE(mapper->read_prg(cpu) == prg_cell(expected_prg_offset(outer, 0U, banks, cpu)));
        }
        for (Address addr : {0U, 0x400U, 0x1000U, 0x1FFFU}) {
            REQUIRE(mapper->read_chr(addr) ==
                    chr_cell(expected_chr_offset(outer, 0U, banks, addr)));
        }
    }
}

TEST_CASE("Mapper 322 MMC3 PRG mode applies inner masks, fixed banks and outer pins",
          "[mapper322]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        const auto outer = static_cast<Byte>(raw | 0x20U);
        set_outer(*mapper, 0x7FFFU, outer);
        for (Byte mode : {0U, 0x40U}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                std::array<Byte, 8> banks{};
                banks[6] = value & 0x3FU;
                banks[7] = (value ^ 0xA5U) & 0x3FU;
                set_bank(*mapper, 6U, static_cast<Byte>(value), mode);
                set_bank(*mapper, 7U, static_cast<Byte>(value ^ 0xA5U), mode);
                for (unsigned slot = 0U; slot < 4U; ++slot) {
                    for (Address offset : {0U, 1U, 0x1234U, 0x1FFFU}) {
                        const auto addr = static_cast<Address>(0x8000U + slot * 8192U + offset);
                        CAPTURE(raw, mode, value, slot, offset);
                        REQUIRE(mapper->read_prg(addr) ==
                                prg_cell(expected_prg_offset(outer, mode, banks, addr)));
                    }
                }
            }
        }
    }
}

TEST_CASE("Mapper 322 CHR banking retains MMC3 pairing and inversion in both PRG modes",
          "[mapper322]") {
    auto mapper = board();
    for (unsigned raw = 0U; raw < 256U; ++raw) {
        const auto outer = static_cast<Byte>(raw);
        set_outer(*mapper, 0x6000U, outer);
        for (Byte mode : {0U, 0x80U}) {
            for (unsigned value = 0U; value < 256U; ++value) {
                std::array<Byte, 8> banks{};
                for (unsigned index = 0U; index < 6U; ++index) {
                    banks[index] = static_cast<Byte>(value + index * 41U);
                    set_bank(*mapper, static_cast<Byte>(index), banks[index], mode);
                }
                for (Address addr : {0U,
                                     0x3FFU,
                                     0x400U,
                                     0x800U,
                                     0xC00U,
                                     0x1000U,
                                     0x1400U,
                                     0x1800U,
                                     0x1C00U,
                                     0x1FFFU}) {
                    CAPTURE(raw, mode, value, addr);
                    REQUIRE(mapper->read_chr(addr) ==
                            chr_cell(expected_chr_offset(outer, mode, banks, addr)));
                    mapper->write_chr(addr, static_cast<Byte>(value ^ 0xFFU));
                    REQUIRE(mapper->read_chr(addr) ==
                            chr_cell(expected_chr_offset(outer, mode, banks, addr)));
                }
            }
        }
    }
}

TEST_CASE("Mapper 322 decodes every outer alias and gates it only with A001 D7", "[mapper322]") {
    auto mapper = board();
    const std::array<Byte, 8> banks{};
    for (unsigned addr = 0x6000U; addr < 0x8000U; ++addr) {
        const auto value = static_cast<Byte>(addr ^ (addr >> 8U));
        set_outer(*mapper, static_cast<Address>(addr), value);
        CAPTURE(addr, value);
        REQUIRE(mapper->read_prg(0xE123U) ==
                prg_cell(expected_prg_offset(value, 0U, banks, 0xE123U)));
        REQUIRE(mapper->read_chr(0x1000U) ==
                chr_cell(expected_chr_offset(value, 0U, banks, 0x1000U)));
    }

    for (unsigned gate = 0U; gate < 256U; ++gate) {
        mapper->reset();
        mapper->write_prg(0xA001U, static_cast<Byte>(gate));
        mapper->write_prg(0x6000U, 0xFFU);
        const auto expected = static_cast<Byte>((gate & 0x80U) != 0U ? 0xFFU : 0U);
        CAPTURE(gate);
        REQUIRE(mapper->read_prg(0xE000U) ==
                prg_cell(expected_prg_offset(expected, 0U, banks, 0xE000U)));
        REQUIRE(state(*mapper)[24] == (gate & 0xC0U));
    }

    const auto initial = state(*mapper);
    for (unsigned addr = 0U; addr < 0x6000U; ++addr) {
        mapper->write_prg(static_cast<Address>(addr), static_cast<Byte>(addr));
    }
    REQUIRE(state(*mapper) == initial);
}

TEST_CASE("Mapper 322 decodes every MMC3 register alias", "[mapper322]") {
    auto mapper = board();
    for (unsigned raw = 0x8000U; raw <= 0xFFFFU; ++raw) {
        const auto addr = static_cast<Address>(raw);
        mapper->reset();
        CAPTURE(raw);
        if (raw < 0xA000U) {
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xC6U);
                REQUIRE(state(*mapper)[11] == 0xC6U);
            } else {
                mapper->write_prg(0x8000U, 6U);
                mapper->write_prg(addr, 0xFFU);
                REQUIRE(state(*mapper)[18] == 0x3FU);
            }
        } else if (raw < 0xC000U) {
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 1U);
                REQUIRE(mapper->mirror_mode() == MirrorMode::Horizontal);
            } else {
                mapper->write_prg(addr, 0x80U);
                mapper->write_prg(0x6000U, 0xFFU);
                REQUIRE(state(*mapper)[10] == 0xFFU);
            }
        } else if (raw < 0xE000U) {
            if ((raw & 1U) == 0U) {
                mapper->write_prg(addr, 0xABU);
                REQUIRE(state(*mapper)[22] == 0xABU);
            } else {
                mapper->write_prg(0xC000U, 3U);
                mapper->clock_irq_counter();
                mapper->write_prg(addr, 0U);
                const auto bytes = state(*mapper);
                REQUIRE(bytes[21] == 0U);
                REQUIRE((bytes[23] & 1U) != 0U);
            }
        } else if ((raw & 1U) == 0U) {
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(0xE001U, 0U);
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending());
            mapper->write_prg(addr, 0U);
            REQUIRE_FALSE(mapper->irq_pending());
        } else {
            mapper->write_prg(0xC000U, 0U);
            mapper->write_prg(addr, 0U);
            mapper->clock_irq_counter();
            REQUIRE(mapper->irq_pending());
        }
    }
}

TEST_CASE("Mapper 322 retains normal MMC3 IRQ reload, enable and acknowledgement behavior",
          "[mapper322]") {
    auto mapper = board();
    mapper->write_prg(0xC000U, 2U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    REQUIRE_FALSE(mapper->irq_pending());
    set_outer(*mapper, 0x6000U, 0xFFU);
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

    mapper->write_prg(0xC000U, 0U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    for (unsigned count = 0U; count < 4U; ++count) {
        mapper->clock_irq_counter();
        REQUIRE(mapper->irq_pending());
        mapper->write_prg(0xE000U, 0U);
        mapper->write_prg(0xE001U, 0U);
    }
}

TEST_CASE("Mapper 322 reset and power-on restore deterministic board state", "[mapper322]") {
    for (auto mirror : {MirrorMode::Horizontal, MirrorMode::Vertical}) {
        auto mapper = board(kMaxRomSize, kMaxRomSize, mirror);
        const auto initial = state(*mapper);
        set_outer(*mapper, 0x7FFFU, 0xFFU);
        set_bank(*mapper, 0U, 0xF1U, 0xC0U);
        set_bank(*mapper, 6U, 0x3DU, 0xC0U);
        mapper->write_prg(0xA000U, mirror == MirrorMode::Vertical ? 0U : 1U);
        mapper->write_prg(0xA001U, 0xC0U);
        mapper->write_prg(0xC000U, 0U);
        mapper->write_prg(0xE001U, 0U);
        mapper->clock_irq_counter();
        REQUIRE(state(*mapper) != initial);
        mapper->reset();
        REQUIRE(state(*mapper) == initial);
        REQUIRE(mapper->mirror_mode() == mirror);
        set_outer(*mapper, 0x6000U, 0xFFU);
        mapper->power_on();
        REQUIRE(state(*mapper) == initial);
        REQUIRE_FALSE(mapper->irq_pending());
    }
}

TEST_CASE("Mapper 322 low reads use CPU open bus and no RAM, persistence or audio exists",
          "[mapper322]") {
    MapperRegistry::instance().register_mapper(
        322U,
        [](const INesHeader& h,
           std::vector<Byte> prg,
           std::vector<Byte> chr) -> std::unique_ptr<Mapper> {
            return std::make_unique<Mapper322>(h, std::move(prg), std::move(chr));
        });
    auto cartridge = Cartridge::from_data(ines_image());
    const auto error = cartridge ? std::string{} : cartridge.error();
    INFO(error);
    REQUIRE(cartridge);
    MemoryBus bus;
    bus.connect_cartridge(&*cartridge);
    for (unsigned value = 1U; value < 256U; ++value) {
        bus.write(0x10U, static_cast<Byte>(value));
        REQUIRE(bus.read(0x6000U) == value);
        bus.write(0x10U, static_cast<Byte>(value ^ 0xA5U));
        REQUIRE(bus.read(0x7FFFU) == (value ^ 0xA5U));
    }
    bus.write(0xA001U, 0x80U);
    bus.write(0x6A55U, 0xFFU);
    const std::array<Byte, 8> banks{};
    REQUIRE(cartridge->read_prg(0xE000U) ==
            prg_cell(expected_prg_offset(0xFFU, 0U, banks, 0xE000U) & (kMinRomSize - 1U)));
    for (Address addr : {0x4020U, 0x5000U, 0x5FFFU, 0x6000U, 0x7FFFU}) {
        REQUIRE_FALSE(cartridge->maps_prg(addr));
        REQUIRE_FALSE(cartridge->maps_expansion(addr));
    }
    REQUIRE(cartridge->maps_prg(0x8000U));
    REQUIRE(cartridge->prg_read_mask(0x8000U) == 0xFFU);
    REQUIRE(cartridge->battery_ram().empty());
    REQUIRE_FALSE(cartridge->has_battery());
    REQUIRE_FALSE(cartridge->has_flash_storage());
    REQUIRE_FALSE(cartridge->has_expansion_audio());
    REQUIRE(cartridge->audio_output() == 0.0F);
    REQUIRE(cartridge->mirror_mode() == MirrorMode::Vertical);
    cartridge->write_prg(0xA000U, 1U);
    REQUIRE(cartridge->mirror_mode() == MirrorMode::Horizontal);
}

TEST_CASE("Mapper 322 snapshots replay mutable state without ROM serialization",
          "[mapper322][state]") {
    auto mapper = board();
    set_outer(*mapper, 0x6000U, 0xFFU);
    for (unsigned index = 0U; index < 8U; ++index) {
        set_bank(*mapper, static_cast<Byte>(index), static_cast<Byte>(0xA1U + index * 17U), 0xC0U);
    }
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);
    REQUIRE(saved.size() == kStateSize);

    auto copy = board();
    StateReader reader(saved);
    copy->load_state(reader);
    REQUIRE(reader.ok());
    REQUIRE(reader.remaining() == 0U);
    REQUIRE(state(*copy) == saved);
    for (Address addr : {0x8000U, 0x9234U, 0xA000U, 0xBFFFU, 0xC000U, 0xD234U, 0xE000U, 0xFFFFU}) {
        REQUIRE(copy->read_prg(addr) == mapper->read_prg(addr));
    }
    for (Address addr : {0U, 0x400U, 0x800U, 0xC00U, 0x1000U, 0x1400U, 0x1800U, 0x1C00U}) {
        REQUIRE(copy->read_chr(addr) == mapper->read_chr(addr));
    }
    REQUIRE(copy->irq_pending() == mapper->irq_pending());
    REQUIRE(copy->mirror_mode() == mapper->mirror_mode());
}

TEST_CASE("Mapper 322 rejects truncated, mismatched and invalid states atomically",
          "[mapper322][state]") {
    auto mapper = board();
    set_outer(*mapper, 0x6000U, 0xD8U);
    set_bank(*mapper, 0U, 0xF1U);
    set_bank(*mapper, 6U, 0x35U, 0xC0U);
    set_bank(*mapper, 7U, 0x1AU, 0xC0U);
    mapper->write_prg(0xA000U, 0U);
    mapper->write_prg(0xA001U, 0xC0U);
    mapper->write_prg(0xC000U, 3U);
    mapper->write_prg(0xC001U, 0U);
    mapper->write_prg(0xE001U, 0U);
    mapper->clock_irq_counter();
    const auto saved = state(*mapper);

    for (std::size_t size = 0U; size < saved.size(); ++size) {
        StateReader truncated{std::span(saved).first(size)};
        mapper->load_state(truncated);
        REQUIRE_FALSE(truncated.ok());
        REQUIRE(state(*mapper) == saved);
    }
    const std::array<std::pair<std::size_t, Byte>, 10> invalid = {
        {{0U, static_cast<Byte>(saved[0] ^ 1U)},
         {2U, static_cast<Byte>(saved[2] ^ 1U)},
         {6U, static_cast<Byte>(saved[6] ^ 1U)},
         {11U, 0x38U},
         {18U, 0x40U},
         {19U, 0x40U},
         {20U, 2U},
         {23U, 8U},
         {23U, 4U},
         {24U, 1U}}};
    for (const auto& [offset, value] : invalid) {
        auto bad = saved;
        bad[offset] = value;
        StateReader rejected(bad);
        mapper->load_state(rejected);
        REQUIRE_FALSE(rejected.ok());
        REQUIRE(state(*mapper) == saved);
    }

    auto other_mirror = board(kMaxRomSize, kMaxRomSize, MirrorMode::Horizontal);
    const auto mirror_before = state(*other_mirror);
    StateReader wrong_mirror(saved);
    other_mirror->load_state(wrong_mirror);
    REQUIRE_FALSE(wrong_mirror.ok());
    REQUIRE(state(*other_mirror) == mirror_before);

    auto other_size = board(1024U * 1024U, kMaxRomSize);
    const auto size_before = state(*other_size);
    StateReader wrong_size(saved);
    other_size->load_state(wrong_size);
    REQUIRE_FALSE(wrong_size.ok());
    REQUIRE(state(*other_size) == size_before);

    for (unsigned outer = 0U; outer < 256U; ++outer) {
        auto valid = saved;
        valid[10] = static_cast<Byte>(outer);
        StateReader accepted(valid);
        mapper->load_state(accepted);
        REQUIRE(accepted.ok());
        REQUIRE(state(*mapper) == valid);
    }
    for (unsigned select = 0U; select < 256U; ++select) {
        auto candidate = saved;
        candidate[11] = static_cast<Byte>(select);
        const auto before = state(*mapper);
        StateReader reader(candidate);
        mapper->load_state(reader);
        if ((select & 0x38U) == 0U) {
            REQUIRE(reader.ok());
            REQUIRE(state(*mapper) == candidate);
        } else {
            REQUIRE_FALSE(reader.ok());
            REQUIRE(state(*mapper) == before);
        }
    }
}

} // namespace
