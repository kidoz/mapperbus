#pragma once

#include <SDL3/SDL.h>
#include <cstdint>
#include <vector>

#include "platform/video/upscaler.hpp"

namespace mapperbus::frontend {

/// GPU-accelerated FSR1 upscaler using SDL3 GPU compute shaders.
/// Dispatches EASU (scaling) and RCAS (sharpening) passes in VRAM.
class GpuFsr1Upscaler : public platform::Upscaler {
  public:
    explicit GpuFsr1Upscaler(int scale);
    ~GpuFsr1Upscaler() override;

    GpuFsr1Upscaler(const GpuFsr1Upscaler&) = delete;
    GpuFsr1Upscaler& operator=(const GpuFsr1Upscaler&) = delete;

    int scale_factor() const override {
        return scale_;
    }

    bool supports_gpu_texture() const override {
        return true;
    }
    void* get_gpu_texture() const override {
        return dst_texture_;
    }
    void set_gpu_device(void* device_handle) override;

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
    SDL_GPUComputePipeline* easu_pipeline_ = nullptr;
    SDL_GPUComputePipeline* rcas_pipeline_ = nullptr;

    SDL_GPUTexture* src_texture_ = nullptr;
    SDL_GPUTexture* temp_texture_ = nullptr;
    SDL_GPUTexture* dst_texture_ = nullptr;
    SDL_GPUTransferBuffer* upload_bufs_[2] = {nullptr, nullptr};
    SDL_GPUTransferBuffer* download_bufs_[2] = {nullptr, nullptr};
    SDL_GPUFence* fences_[2] = {nullptr, nullptr};
    uint64_t frame_index_ = 0;
    bool initialized_ = false;
    bool external_device_ = false;
};

} // namespace mapperbus::frontend
