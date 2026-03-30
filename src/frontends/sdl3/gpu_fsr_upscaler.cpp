#include "frontends/sdl3/gpu_fsr_upscaler.hpp"

#include <cstring>
#include <vector>

#include "frontends/sdl3/shaders/fsr1_shader.hpp"

namespace {

// ARGB (0xAARRGGBB) → R8G8B8A8_UNORM (byte 0=R, 1=G, 2=B, 3=A)
void argb_to_rgba(std::span<const uint32_t> src, std::span<uint32_t> dst) {
    for (std::size_t i = 0; i < src.size(); ++i) {
        uint32_t c = src[i];
        uint32_t a = (c >> 24) & 0xFF;
        uint32_t r = (c >> 16) & 0xFF;
        uint32_t g = (c >> 8) & 0xFF;
        uint32_t b = c & 0xFF;
        dst[i] = r | (g << 8) | (b << 16) | (a << 24);
    }
}

// R8G8B8A8_UNORM (byte 0=R, 1=G, 2=B, 3=A) → ARGB (0xAARRGGBB)
void rgba_to_argb(std::span<const uint32_t> src, std::span<uint32_t> dst) {
    for (std::size_t i = 0; i < src.size(); ++i) {
        uint32_t c = src[i];
        uint32_t r = c & 0xFF;
        uint32_t g = (c >> 8) & 0xFF;
        uint32_t b = (c >> 16) & 0xFF;
        uint32_t a = (c >> 24) & 0xFF;
        dst[i] = (a << 24) | (r << 16) | (g << 8) | b;
    }
}

} // namespace

namespace mapperbus::frontend {

struct GpuParams {
    int32_t scale_factor;
    int32_t src_width;
    int32_t src_height;
    int32_t padding; // align to 16 bytes
};

GpuFsr1Upscaler::GpuFsr1Upscaler(int scale) : scale_(scale) {
    if (scale_ < 2)
        scale_ = 2;
    if (scale_ > 6)
        scale_ = 6;
}

GpuFsr1Upscaler::~GpuFsr1Upscaler() {
    cleanup_gpu();
}

void GpuFsr1Upscaler::set_gpu_device(void* device_handle) {
    if (!device_) {
        device_ = static_cast<SDL_GPUDevice*>(device_handle);
        external_device_ = true;
    }
}

bool GpuFsr1Upscaler::init_gpu(int src_width, int src_height) {
    src_w_ = src_width;
    src_h_ = src_height;

    if (!device_) {
        device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
        if (!device_) {
            return false;
        }
        external_device_ = false;
    }

    // Pipeline 1: EASU Compute
    SDL_GPUComputePipelineCreateInfo easu_info{};
    easu_info.code = reinterpret_cast<const uint8_t*>(shaders::kFsr1EasuComputeMsl);
    easu_info.code_size = std::strlen(shaders::kFsr1EasuComputeMsl);
    easu_info.entrypoint = "fsr1_easu";
    easu_info.format = SDL_GPU_SHADERFORMAT_MSL;
    easu_info.num_readonly_storage_textures = 1;
    easu_info.num_readwrite_storage_textures = 1;
    easu_info.num_uniform_buffers = 1;
    easu_info.threadcount_x = 16;
    easu_info.threadcount_y = 16;
    easu_info.threadcount_z = 1;

    easu_pipeline_ = SDL_CreateGPUComputePipeline(device_, &easu_info);
    if (!easu_pipeline_) {
        cleanup_gpu();
        return false;
    }
    
    // Pipeline 2: RCAS Compute
    SDL_GPUComputePipelineCreateInfo rcas_info{};
    rcas_info.code = reinterpret_cast<const uint8_t*>(shaders::kFsr1RcasComputeMsl);
    rcas_info.code_size = std::strlen(shaders::kFsr1RcasComputeMsl);
    rcas_info.entrypoint = "fsr1_rcas";
    rcas_info.format = SDL_GPU_SHADERFORMAT_MSL;
    rcas_info.num_readonly_storage_textures = 1;
    rcas_info.num_readwrite_storage_textures = 1;
    rcas_info.num_uniform_buffers = 1;
    rcas_info.threadcount_x = 16;
    rcas_info.threadcount_y = 16;
    rcas_info.threadcount_z = 1;

    rcas_pipeline_ = SDL_CreateGPUComputePipeline(device_, &rcas_info);
    if (!rcas_pipeline_) {
        cleanup_gpu();
        return false;
    }

    // Source Texture
    SDL_GPUTextureCreateInfo src_tex_info{};
    src_tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    src_tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    src_tex_info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
    src_tex_info.width = static_cast<uint32_t>(src_width);
    src_tex_info.height = static_cast<uint32_t>(src_height);
    src_tex_info.layer_count_or_depth = 1;
    src_tex_info.num_levels = 1;

    src_texture_ = SDL_CreateGPUTexture(device_, &src_tex_info);
    if (!src_texture_) {
        cleanup_gpu();
        return false;
    }

    // Intermediate Texture (EASU output)
    SDL_GPUTextureCreateInfo tmp_tex_info{};
    tmp_tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    tmp_tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    tmp_tex_info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    tmp_tex_info.width = static_cast<uint32_t>(src_width * scale_);
    tmp_tex_info.height = static_cast<uint32_t>(src_height * scale_);
    tmp_tex_info.layer_count_or_depth = 1;
    tmp_tex_info.num_levels = 1;

    temp_texture_ = SDL_CreateGPUTexture(device_, &tmp_tex_info);
    if (!temp_texture_) {
        cleanup_gpu();
        return false;
    }

    // Dst Texture (RCAS output)
    SDL_GPUTextureCreateInfo dst_tex_info{};
    dst_tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    dst_tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    dst_tex_info.usage = SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_TEXTUREUSAGE_SAMPLER;
    dst_tex_info.width = static_cast<uint32_t>(src_width * scale_);
    dst_tex_info.height = static_cast<uint32_t>(src_height * scale_);
    dst_tex_info.layer_count_or_depth = 1;
    dst_tex_info.num_levels = 1;

    dst_texture_ = SDL_CreateGPUTexture(device_, &dst_tex_info);
    if (!dst_texture_) {
        cleanup_gpu();
        return false;
    }

    initialized_ = true;
    return true;
}

void GpuFsr1Upscaler::cleanup_gpu() {
    if (device_) {
        if (dst_texture_)
            SDL_ReleaseGPUTexture(device_, dst_texture_);
        if (temp_texture_)
            SDL_ReleaseGPUTexture(device_, temp_texture_);
        if (src_texture_)
            SDL_ReleaseGPUTexture(device_, src_texture_);
        if (easu_pipeline_)
            SDL_ReleaseGPUComputePipeline(device_, easu_pipeline_);
        if (rcas_pipeline_)
            SDL_ReleaseGPUComputePipeline(device_, rcas_pipeline_);
        if (!external_device_) {
            SDL_DestroyGPUDevice(device_);
        }
    }
    device_ = nullptr;
    easu_pipeline_ = nullptr;
    rcas_pipeline_ = nullptr;
    src_texture_ = nullptr;
    temp_texture_ = nullptr;
    dst_texture_ = nullptr;
    initialized_ = false;
}

void GpuFsr1Upscaler::scale(std::span<const std::uint32_t> source,
                            int src_width,
                            int src_height,
                            std::span<std::uint32_t> target) {
    if (!initialized_) {
        if (!init_gpu(src_width, src_height)) {
            // CPU Nearest-Neighbor fallback
            for (int y = 0; y < src_height * scale_; ++y) {
                for (int x = 0; x < src_width * scale_; ++x) {
                    int sx = x / scale_;
                    int sy = y / scale_;
                    target[y * src_width * scale_ + x] = source[sy * src_width + sx];
                }
            }
            return;
        }
    }

    int dst_w = src_width * scale_;
    int dst_h = src_height * scale_;

    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd)
        return;

    // --- Upload CPU pixels to GPU src_texture ---
    SDL_GPUTransferBufferCreateInfo upload_buf_info{};
    upload_buf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    upload_buf_info.size = static_cast<uint32_t>(source.size() * sizeof(uint32_t));

    SDL_GPUTransferBuffer* upload_buf = SDL_CreateGPUTransferBuffer(device_, &upload_buf_info);
    if (upload_buf) {
        void* mapped = SDL_MapGPUTransferBuffer(device_, upload_buf, false);
        if (mapped) {
            auto* dst_pixels = static_cast<uint32_t*>(mapped);
            argb_to_rgba(source, {dst_pixels, source.size()});
            SDL_UnmapGPUTransferBuffer(device_, upload_buf);
        }

        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo src_transfer{};
        src_transfer.transfer_buffer = upload_buf;
        src_transfer.offset = 0;

        SDL_GPUTextureRegion src_region{};
        src_region.texture = src_texture_;
        src_region.w = static_cast<uint32_t>(src_width);
        src_region.h = static_cast<uint32_t>(src_height);
        src_region.d = 1;

        SDL_UploadToGPUTexture(copy_pass, &src_transfer, &src_region, false);
        SDL_EndGPUCopyPass(copy_pass);
    }

    // --- Dispatch EASU Compute Pass ---
    {
        SDL_GPUStorageTextureReadWriteBinding easu_rw_tex_bindings[1]{};
        easu_rw_tex_bindings[0].texture = temp_texture_;

        SDL_GPUComputePass* easu_compute = SDL_BeginGPUComputePass(cmd, easu_rw_tex_bindings, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(easu_compute, easu_pipeline_);

        SDL_GPUTexture* easu_read_textures[] = {src_texture_};
        SDL_BindGPUComputeStorageTextures(easu_compute, 0, easu_read_textures, 1);

        GpuParams params{scale_, src_width, src_height, 0};
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

        uint32_t groups_x = (static_cast<uint32_t>(dst_w) + 15) / 16;
        uint32_t groups_y = (static_cast<uint32_t>(dst_h) + 15) / 16;
        SDL_DispatchGPUCompute(easu_compute, groups_x, groups_y, 1);
        SDL_EndGPUComputePass(easu_compute);
    }

    // --- Dispatch RCAS Compute Pass ---
    {
        SDL_GPUStorageTextureReadWriteBinding rcas_rw_tex_bindings[1]{};
        rcas_rw_tex_bindings[0].texture = dst_texture_;

        SDL_GPUComputePass* rcas_compute = SDL_BeginGPUComputePass(cmd, rcas_rw_tex_bindings, 1, nullptr, 0);
        SDL_BindGPUComputePipeline(rcas_compute, rcas_pipeline_);

        SDL_GPUTexture* rcas_read_textures[] = {temp_texture_};
        SDL_BindGPUComputeStorageTextures(rcas_compute, 0, rcas_read_textures, 1);

        // Parameters re-used (same scale/dims needed to keep geometry math perfect)
        GpuParams params{scale_, src_width, src_height, 0};
        SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

        uint32_t groups_x = (static_cast<uint32_t>(dst_w) + 15) / 16;
        uint32_t groups_y = (static_cast<uint32_t>(dst_h) + 15) / 16;
        SDL_DispatchGPUCompute(rcas_compute, groups_x, groups_y, 1);
        SDL_EndGPUComputePass(rcas_compute);
    }

    if (external_device_) {
        // Zero-copy! Submit GPU commands natively.
        SDL_SubmitGPUCommandBuffer(cmd);
        if (upload_buf) {
            SDL_ReleaseGPUTransferBuffer(device_, upload_buf);
        }
        return;
    }

    // --- Download dst_texture to CPU RAM ---
    SDL_GPUTransferBufferCreateInfo download_buf_info{};
    download_buf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    download_buf_info.size = static_cast<uint32_t>(target.size() * sizeof(uint32_t));

    SDL_GPUTransferBuffer* download_buf = SDL_CreateGPUTransferBuffer(device_, &download_buf_info);
    if (download_buf) {
        SDL_GPUCopyPass* copy_pass = SDL_BeginGPUCopyPass(cmd);

        SDL_GPUTextureRegion dst_region{};
        dst_region.texture = dst_texture_;
        dst_region.w = static_cast<uint32_t>(dst_w);
        dst_region.h = static_cast<uint32_t>(dst_h);
        dst_region.d = 1;

        SDL_GPUTextureTransferInfo dst_transfer{};
        dst_transfer.transfer_buffer = download_buf;
        dst_transfer.offset = 0;

        SDL_DownloadFromGPUTexture(copy_pass, &dst_region, &dst_transfer);
        SDL_EndGPUCopyPass(copy_pass);
    }

    // --- Submit and block until fence triggers ---
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);

    // Read back and un-swizzle to ARGB
    if (download_buf) {
        void* mapped = SDL_MapGPUTransferBuffer(device_, download_buf, false);
        if (mapped) {
            auto* src_pixels = static_cast<const uint32_t*>(mapped);
            rgba_to_argb({src_pixels, target.size()}, target);
            SDL_UnmapGPUTransferBuffer(device_, download_buf);
        }
        SDL_ReleaseGPUTransferBuffer(device_, download_buf);
    }

    if (upload_buf) {
        SDL_ReleaseGPUTransferBuffer(device_, upload_buf);
    }
}

} // namespace mapperbus::frontend
