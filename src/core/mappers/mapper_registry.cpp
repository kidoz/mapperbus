#include "core/mappers/mapper_registry.hpp"

#include "core/mappers/axrom.hpp"
#include "core/mappers/cnrom.hpp"
#include "core/mappers/color_dreams.hpp"
#include "core/mappers/discrete.hpp"
#include "core/mappers/konami_vrc.hpp"
#include "core/mappers/mmc1.hpp"
#include "core/mappers/mmc2.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/mmc4.hpp"
#include "core/mappers/mmc5.hpp"
#include "core/mappers/namco108.hpp"
#include "core/mappers/namco163.hpp"
#include "core/mappers/nrom.hpp"
#include "core/mappers/sunsoft5b.hpp"
#include "core/mappers/uxrom.hpp"
#include "core/mappers/vrc6.hpp"
#include "core/mappers/vrc7.hpp"

namespace mapperbus::core {
namespace {

[[nodiscard]] Result<void> validate_geometry(const INesHeader& header,
                                             std::size_t prg_size,
                                             std::size_t chr_size) {
    if (header.mapper_number == 31 && prg_size < 4096) {
        return std::unexpected("Mapper 31 requires at least a 4 KiB PRG ROM");
    }
    if (header.mapper_number != 31 && prg_size < 16 * 1024) {
        return std::unexpected("Mapper requires at least a 16 KiB PRG ROM");
    }
    if (header.mapper_number == 0 && prg_size != 16 * 1024 && prg_size != 32 * 1024) {
        return std::unexpected("NROM requires a 16 KiB or 32 KiB PRG ROM");
    }
    if (header.mapper_number == 4 && prg_size > 512 * 1024) {
        return std::unexpected("MMC3 PRG ROM exceeds 512 KiB");
    }
    if (header.mapper_number == 4 && chr_size > 256 * 1024) {
        return std::unexpected("MMC3 CHR ROM exceeds 256 KiB");
    }
    if (header.mapper_number == 4 && chr_size > 0 && chr_size < 1024) {
        return std::unexpected("MMC3 CHR ROM is smaller than one bank");
    }
    if (header.mapper_number == 3 && chr_size > 0 && chr_size < 8 * 1024) {
        return std::unexpected("CNROM CHR ROM is smaller than one bank");
    }
    if (header.mapper_number == 7 && prg_size < 32 * 1024) {
        return std::unexpected("AxROM requires at least a 32 KiB PRG ROM");
    }
    if (header.mapper_number == 9 && prg_size < 32 * 1024) {
        return std::unexpected("MMC2 requires at least a 32 KiB PRG ROM");
    }
    switch (header.mapper_number) {
    case 10:
    case 21:
    case 22:
    case 23:
    case 25:
    case 75:
    case 76:
    case 88:
    case 95:
    case 154:
    case 193:
    case 206:
        if (prg_size < 32 * 1024)
            return std::unexpected("Mapper requires at least a 32 KiB PRG ROM");
        break;
    case 13:
        if (chr_size != 0)
            return std::unexpected("CPROM requires CHR RAM, not CHR ROM");
        break;
    default:
        break;
    }
    if ((header.mapper_number == 21 || header.mapper_number == 22 || header.mapper_number == 23 ||
         header.mapper_number == 25 || header.mapper_number == 73 || header.mapper_number == 75) &&
        (header.prg_ram_size > 8192 || header.prg_nvram_size > 8192 ||
         header.prg_ram_size + header.prg_nvram_size > 8192)) {
        return std::unexpected("VRC board PRG RAM exceeds 8 KiB");
    }
    if (header.is_nes2) {
        if (header.mapper_number == 21 && header.submapper > 2)
            return std::unexpected("Unsupported VRC4 submapper");
        if ((header.mapper_number == 23 || header.mapper_number == 25) && header.submapper > 3)
            return std::unexpected("Unsupported VRC2/VRC4 submapper");
        if (header.mapper_number == 34 && header.submapper > 2)
            return std::unexpected("Unsupported mapper 34 submapper");
        if (header.mapper_number == 71 && header.submapper > 1)
            return std::unexpected("Unsupported mapper 71 submapper");
        if (header.mapper_number == 78 && header.submapper != 0 && header.submapper != 1 &&
            header.submapper != 3)
            return std::unexpected("Unsupported mapper 78 submapper");
        if ((header.mapper_number == 61 || header.mapper_number == 200 ||
             header.mapper_number == 206 || header.mapper_number == 232) &&
            header.submapper > 1)
            return std::unexpected("Unsupported board submapper");
    }
    return {};
}

} // namespace

MapperRegistry& MapperRegistry::instance() {
    // Readers retaining ownership may continue during static teardown. The
    // cached reference does not access the static shared_ptr after its lifetime.
    static auto& registry = *retain();
    return registry;
}

std::shared_ptr<MapperRegistry> MapperRegistry::retain() {
    struct SharedRegistry final : MapperRegistry {};
    static const auto registry = std::make_shared<SharedRegistry>();
    return registry;
}

void MapperRegistry::register_mapper(uint16_t mapper_number, MapperFactory factory) {
    factories_[mapper_number] = std::move(factory);
}

Result<std::unique_ptr<Mapper>> MapperRegistry::create(const INesHeader& header,
                                                       std::vector<Byte> prg_rom,
                                                       std::vector<Byte> chr_rom) const {
    auto it = factories_.find(header.mapper_number);
    if (it == factories_.end()) {
        return std::unexpected("Unsupported mapper: " + std::to_string(header.mapper_number));
    }
    if (auto valid = validate_geometry(header, prg_rom.size(), chr_rom.size()); !valid) {
        return std::unexpected(valid.error());
    }
    return it->second(header, std::move(prg_rom), std::move(chr_rom));
}

namespace {

template <typename T> MapperFactory make_factory() {
    return [](const INesHeader& header,
              std::vector<Byte> prg_rom,
              std::vector<Byte> chr_rom) -> std::unique_ptr<Mapper> {
        return std::make_unique<T>(header, std::move(prg_rom), std::move(chr_rom));
    };
}

} // namespace

void register_builtin_mappers() {
    auto& registry = MapperRegistry::instance();
    registry.register_mapper(0, make_factory<Nrom>());
    registry.register_mapper(1, make_factory<Mmc1>());
    registry.register_mapper(2, make_factory<Uxrom>());
    registry.register_mapper(3, make_factory<Cnrom>());
    registry.register_mapper(4, make_factory<Mmc3>());
    registry.register_mapper(7, make_factory<Axrom>());
    registry.register_mapper(5, make_factory<Mmc5>());
    registry.register_mapper(9, make_factory<Mmc2>());
    registry.register_mapper(11, make_factory<ColorDreams>());
    registry.register_mapper(19, make_factory<Namco163>());
    registry.register_mapper(24, make_factory<Vrc6>());
    registry.register_mapper(26, make_factory<Vrc6>());
    registry.register_mapper(69, make_factory<Sunsoft5b>());
    registry.register_mapper(85, make_factory<Vrc7>());
    registry.register_mapper(10, make_factory<Mmc4>());
    registry.register_mapper(28, make_factory<DiscreteMapper>());
    registry.register_mapper(31, make_factory<DiscreteMapper>());
    for (const uint16_t id : {21, 22, 23, 25, 73, 75})
        registry.register_mapper(id, make_factory<KonamiVrc>());
    for (const uint16_t id :
         {13, 34, 66, 70, 71, 78, 79, 87, 89, 93, 94, 97, 113, 140, 152, 180, 184}) {
        registry.register_mapper(id, make_factory<DiscreteMapper>());
    }
    for (const uint16_t id : {76, 88, 95, 154, 206}) {
        registry.register_mapper(id, make_factory<Namco108>());
    }
    for (const uint16_t id : {38,  46,  58,  61,  62,  81,  107, 145, 148, 149, 193,
                              200, 201, 203, 212, 213, 225, 226, 229, 231, 232, 240}) {
        registry.register_mapper(id, make_factory<DiscreteMapper>());
    }
}

} // namespace mapperbus::core
