#pragma once

#include <array>
#include <memory>
#include <vector>

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
    struct AxisSample {
        int source;
        int next;
        float fraction;
    };

    int scale_;
    std::unique_ptr<ThreadPool> thread_pool_;
    std::vector<std::array<std::vector<Rgb>, 3>> rolling_lines_;
    std::vector<AxisSample> x_samples_;
};
} // namespace mapperbus::platform