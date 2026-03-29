#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

#include "platform/video/upscaler.hpp"

namespace mapperbus::frontend {

/// GPU-accelerated xBRZ upscaler using SDL3 GPU compute shaders.
/// Uses Metal on macOS. Falls back to CPU xBRZ if GPU init fails.
class GpuUpscaler : public platform::Upscaler {
  public:
    explicit GpuUpscaler(int scale);
    ~GpuUpscaler() override;

    GpuUpscaler(const GpuUpscaler&) = delete;
    GpuUpscaler& operator=(const GpuUpscaler&) = delete;

    int scale_factor() const override {
        return scale_;
    }
    void scale(std::span<const std::uint32_t> source,
               int src_width,
               int src_height,
               std::span<std::uint32_t> target) override;

    bool is_gpu_available() const {
        return device_ != nullptr;
    }

  private:
    bool init_gpu(int src_width, int src_height);
    void cleanup_gpu();

    int scale_;
    int src_w_ = 0;
    int src_h_ = 0;

    SDL_GPUDevice* device_ = nullptr;
    SDL_GPUComputePipeline* pipeline_ = nullptr;
    SDL_GPUTexture* src_texture_ = nullptr;
    SDL_GPUTexture* dst_texture_ = nullptr;
    bool initialized_ = false;
};

} // namespace mapperbus::frontend
