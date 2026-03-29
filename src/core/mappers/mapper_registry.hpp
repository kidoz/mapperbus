#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

using MapperFactory = std::function<std::unique_ptr<Mapper>(
    const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)>;

class MapperRegistry {
  public:
    static MapperRegistry& instance();

    void register_mapper(uint16_t mapper_number, MapperFactory factory);

    Result<std::unique_ptr<Mapper>> create(const INesHeader& header,
                                           std::vector<Byte> prg_rom,
                                           std::vector<Byte> chr_rom) const;

  private:
    MapperRegistry() = default;
    std::unordered_map<uint16_t, MapperFactory> factories_;
};

void register_builtin_mappers();

} // namespace mapperbus::core
