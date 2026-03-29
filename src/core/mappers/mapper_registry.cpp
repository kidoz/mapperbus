#include "core/mappers/mapper_registry.hpp"

#include "core/mappers/axrom.hpp"
#include "core/mappers/cnrom.hpp"
#include "core/mappers/color_dreams.hpp"
#include "core/mappers/mmc1.hpp"
#include "core/mappers/mmc3.hpp"
#include "core/mappers/mmc5.hpp"
#include "core/mappers/namco163.hpp"
#include "core/mappers/nrom.hpp"
#include "core/mappers/sunsoft5b.hpp"
#include "core/mappers/uxrom.hpp"
#include "core/mappers/vrc6.hpp"
#include "core/mappers/vrc7.hpp"

namespace mapperbus::core {

MapperRegistry& MapperRegistry::instance() {
    static MapperRegistry registry;
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
    registry.register_mapper(11, make_factory<ColorDreams>());
    registry.register_mapper(19, make_factory<Namco163>());
    registry.register_mapper(24, make_factory<Vrc6>());
    registry.register_mapper(26, make_factory<Vrc6>());
    registry.register_mapper(69, make_factory<Sunsoft5b>());
    registry.register_mapper(85, make_factory<Vrc7>());
}

} // namespace mapperbus::core
