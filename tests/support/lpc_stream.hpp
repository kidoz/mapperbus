#pragma once

#include <array>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
/// Original synthetic LPC parameters, packed independently of the decoder.
class LpcStream {
  public:
    void bits(unsigned value, unsigned width) {
        for (unsigned i = width; i > 0U; --i)
            bits_.push_back(static_cast<core::Byte>((value >> (i - 1U)) & 1U));
    }
    void frame(unsigned energy = 14U,
               unsigned pitch = 20U,
               bool repeat = false,
               std::array<unsigned, 10> coefficients = {23U, 9U, 8U, 6U, 7U, 6U, 6U, 3U, 3U, 3U}) {
        bits(energy, 4U);
        if (energy == 0U || energy == 15U)
            return;
        bits(repeat ? 1U : 0U, 1U);
        bits(pitch, 6U);
        if (!repeat)
            for (unsigned i = 0; i < (pitch == 0U ? 4U : 10U); ++i)
                bits(coefficients[i], i < 2U ? 5U : i < 7U ? 4U : 3U);
    }
    std::vector<core::Byte> nibbles() const {
        std::vector<core::Byte> data((bits_.size() + 3U) / 4U, 0U);
        for (std::size_t i = 0; i < bits_.size(); ++i)
            data[i / 4U] |= static_cast<core::Byte>(bits_[i] << (i % 4U));
        return data;
    }

  private:
    std::vector<core::Byte> bits_;
};
} // namespace mapperbus::tests
