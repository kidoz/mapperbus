#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "platform/video/upscaler.hpp"

namespace mapperbus::platform {

struct XbrzConfig {
    double luminance_weight = 1.0;
    double equal_color_tolerance = 30.0;
    double dominant_direction_threshold = 3.6;
    double steep_direction_threshold = 2.2;
};

class XbrzUpscaler : public Upscaler {
  public:
    explicit XbrzUpscaler(int scale, XbrzConfig config = {});

    int scale_factor() const override {
        return scale_;
    }
    void scale(std::span<const std::uint32_t> source,
               int src_width,
               int src_height,
               std::span<std::uint32_t> target) override;

  private:
    int scale_;
    XbrzConfig config_;
    std::vector<uint8_t> blend_result_; // Preprocessing buffer
};

// Standalone function for direct use without the interface
void xbrz_scale(int scale_factor,
                std::span<const std::uint32_t> source,
                int src_width,
                int src_height,
                std::span<std::uint32_t> target,
                const XbrzConfig& config = {});

} // namespace mapperbus::platform
