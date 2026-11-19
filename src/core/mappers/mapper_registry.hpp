#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "core/cartridge/ines_header.hpp"
#include "core/mappers/mapper.hpp"

namespace mapperbus::core {

using MapperFactory = std::function<std::unique_ptr<Mapper>(
    const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)>;

using MapperMediaFactory = std::function<Result<std::unique_ptr<Mapper>>(
    const INesHeader&, std::vector<Byte>, std::vector<Byte>, std::span<const Byte>)>;

class MapperRegistry {
  public:
    static MapperRegistry& instance();
    /// Retain factory storage while a background media operation may outlive main.
    /// Register factories before starting concurrent readers.
    [[nodiscard]] static std::shared_ptr<MapperRegistry> retain();

    void register_mapper(uint16_t mapper_number, MapperFactory factory);

    void register_mapper(uint16_t mapper_number, MapperMediaFactory factory);

    Result<std::unique_ptr<Mapper>> create(const INesHeader& header,
                                           std::vector<Byte> prg_rom,
                                           std::vector<Byte> chr_rom,
                                           std::span<const Byte> misc_rom = {}) const;

  private:
    MapperRegistry() = default;
    std::unordered_map<uint16_t, MapperMediaFactory> factories_;
};

void register_builtin_mappers();

} // namespace mapperbus::core
