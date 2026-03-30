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

    // Optional hardware acceleration bindings
    virtual bool supports_gpu_texture() const {
        return false;
    }

    virtual void* get_gpu_texture() const {
        return nullptr;
    }

    virtual void set_gpu_device(void* /*device_handle*/) {}
};

} // namespace mapperbus::platform
