#pragma once

#include <cstdint>
#include <span>

namespace mapperbus::platform {

class Upscaler {
  public:
    virtual ~Upscaler() = default;

    virtual int scale_factor() const = 0;
    virtual void scale(std::span<const std::uint32_t> source,
                       int src_width,
                       int src_height,
                       std::span<std::uint32_t> target) = 0;
};

} // namespace mapperbus::platform
