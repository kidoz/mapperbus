#pragma once

#include <memory>

#include "platform/video/upscaler.hpp"

namespace mapperbus::platform {

struct Rgb {
    float r, g, b;
};

class ThreadPool;

class Fsr1Upscaler : public Upscaler {
  public:
    explicit Fsr1Upscaler(int scale);
    ~Fsr1Upscaler() override;

    int scale_factor() const override;
    void scale(std::span<const std::uint32_t> source,
               int src_width,
               int src_height,
               std::span<std::uint32_t> target) override;

  private:
    int scale_;
    std::unique_ptr<ThreadPool> thread_pool_;
};

} // namespace mapperbus::platform