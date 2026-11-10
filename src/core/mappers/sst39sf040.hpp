#pragma once

#include <span>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::core {
class StateWriter;
class StateReader;

/// SST39SF040 command/address decoder and persistent 512 KiB flash array.
class Sst39sf040 {
  public:
    static constexpr std::size_t kSize = 512U * 1024U;
    explicit Sst39sf040(std::vector<Byte> image);
    Byte read(uint32_t address);
    void write(uint32_t address, Byte data);
    void clock_cpu();
    std::span<const Byte> nvram() const {
        return data_;
    }
    void restore_nvram(std::span<const Byte> data);
    void save_state(StateWriter& writer) const;
    void load_state(StateReader& reader);

  private:
    enum class Command : Byte { Read, Unlock1, Unlock2, Program, Erase1, Erase2, Erase3 };
    enum class Operation : Byte { None, Program, Sector, Chip };
    void begin(Operation operation, uint32_t address, Byte data, uint32_t cycles);
    std::vector<Byte> data_;
    Command command_ = Command::Read;
    Operation operation_ = Operation::None;
    Byte id_mode_ = 0U;
    Byte toggle_ = 0U;
    Byte pending_data_ = 0U;
    uint32_t pending_address_ = 0U;
    uint32_t remaining_ = 0U;
};
} // namespace mapperbus::core
