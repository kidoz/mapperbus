#include "frontends/sdl3/gpu_upscaler.hpp"

#include <cstring>
#include <vector>

#include "frontends/sdl3/shaders/xbrz_shader.hpp"

namespace {

// ARGB (0xAARRGGBB) → RGBA (0xRRGGBBAA) for GPU upload
void argb_to_rgba(std::span<const uint32_t> src, std::span<uint32_t> dst) {
    for (std::size_t i = 0; i < src.size(); ++i) {
        uint32_t c = src[i];
        uint8_t a = static_cast<uint8_t>((c >> 24) & 0xFF);
        uint8_t r = static_cast<uint8_t>((c >> 16) & 0xFF);
        uint8_t g = static_cast<uint8_t>((c >> 8) & 0xFF);
        uint8_t b = static_cast<uint8_t>(c & 0xFF);
        dst[i] = (r << 24) | (g << 16) | (b << 8) | a;
    }
}

// RGBA (0xRRGGBBAA) → ARGB (0xAARRGGBB) for readback
void rgba_to_argb(std::span<const uint32_t> src, std::span<uint32_t> dst) {
    for (std::size_t i = 0; i < src.size(); ++i) {
        uint32_t c = src[i];
        uint8_t r = static_cast<uint8_t>((c >> 24) & 0xFF);
        uint8_t g = static_cast<uint8_t>((c >> 16) & 0xFF);
        uint8_t b = static_cast<uint8_t>((c >> 8) & 0xFF);
        uint8_t a = static_cast<uint8_t>(c & 0xFF);
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

GpuUpscaler::GpuUpscaler(int scale) : scale_(scale) {
    if (scale_ < 2)
        scale_ = 2;
    if (scale_ > 6)
        scale_ = 6;
}

GpuUpscaler::~GpuUpscaler() {
    cleanup_gpu();
}

bool GpuUpscaler::init_gpu(int src_width, int src_height) {
    src_w_ = src_width;
    src_h_ = src_height;

    // Create GPU device preferring Metal on macOS
    device_ = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL, false, nullptr);
    if (!device_) {
        return false;
    }

    // Create compute pipeline from embedded MSL source
    SDL_GPUComputePipelineCreateInfo pipeline_info{};
    pipeline_info.code = reinterpret_cast<const uint8_t*>(shaders::kXbrzComputeMsl);
    pipeline_info.code_size = std::strlen(shaders::kXbrzComputeMsl);
    pipeline_info.entrypoint = "xbrz_upscale";
    pipeline_info.format = SDL_GPU_SHADERFORMAT_MSL;
    pipeline_info.num_readonly_storage_textures = 1;
    pipeline_info.num_readwrite_storage_textures = 1;
    pipeline_info.num_uniform_buffers = 1;
    pipeline_info.threadcount_x = 16;
    pipeline_info.threadcount_y = 16;
    pipeline_info.threadcount_z = 1;

    pipeline_ = SDL_CreateGPUComputePipeline(device_, &pipeline_info);
    if (!pipeline_) {
        cleanup_gpu();
        return false;
    }

    // Create source texture (256x240)
    // Use R8G8B8A8_UNORM (universal GPU storage format).
    // FrameBuffer is ARGB8888 — we swizzle on upload/download.
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

    // Create destination texture (scaled)
    SDL_GPUTextureCreateInfo dst_tex_info{};
    dst_tex_info.type = SDL_GPU_TEXTURETYPE_2D;
    dst_tex_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    dst_tex_info.usage =
        SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
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

void GpuUpscaler::cleanup_gpu() {
    if (device_) {
        if (dst_texture_)
            SDL_ReleaseGPUTexture(device_, dst_texture_);
        if (src_texture_)
            SDL_ReleaseGPUTexture(device_, src_texture_);
        if (pipeline_)
            SDL_ReleaseGPUComputePipeline(device_, pipeline_);
        SDL_DestroyGPUDevice(device_);
    }
    device_ = nullptr;
    pipeline_ = nullptr;
    src_texture_ = nullptr;
    dst_texture_ = nullptr;
    initialized_ = false;
}

void GpuUpscaler::scale(std::span<const std::uint32_t> source,
                        int src_width,
                        int src_height,
                        std::span<std::uint32_t> target) {
    // Lazy initialization
    if (!initialized_) {
        if (!init_gpu(src_width, src_height)) {
            // GPU init failed — fill with nearest-neighbor fallback
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

    // Acquire command buffer
    SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
    if (!cmd)
        return;

    // Upload source pixels to GPU
    SDL_GPUTransferBufferCreateInfo upload_buf_info{};
    upload_buf_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    upload_buf_info.size = static_cast<uint32_t>(source.size() * sizeof(uint32_t));

    SDL_GPUTransferBuffer* upload_buf = SDL_CreateGPUTransferBuffer(device_, &upload_buf_info);
    if (upload_buf) {
        void* mapped = SDL_MapGPUTransferBuffer(device_, upload_buf, false);
        if (mapped) {
            // Swizzle ARGB → RGBA for GPU texture format
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

    // Dispatch compute shader
    SDL_GPUStorageTextureReadWriteBinding rw_tex_bindings[1]{};
    rw_tex_bindings[0].texture = dst_texture_;

    SDL_GPUComputePass* compute = SDL_BeginGPUComputePass(cmd, rw_tex_bindings, 1, nullptr, 0);

    SDL_BindGPUComputePipeline(compute, pipeline_);

    // Bind source texture as read-only storage
    SDL_GPUTexture* read_textures[] = {src_texture_};
    SDL_BindGPUComputeStorageTextures(compute, 0, read_textures, 1);

    // Push uniform parameters
    GpuParams params{scale_, src_width, src_height, 0};
    SDL_PushGPUComputeUniformData(cmd, 0, &params, sizeof(params));

    // Dispatch workgroups
    uint32_t groups_x = (static_cast<uint32_t>(dst_w) + 15) / 16;
    uint32_t groups_y = (static_cast<uint32_t>(dst_h) + 15) / 16;
    SDL_DispatchGPUCompute(compute, groups_x, groups_y, 1);
    SDL_EndGPUComputePass(compute);

    // Download result from GPU
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

    // Submit and wait for completion
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
    SDL_WaitForGPUFences(device_, true, &fence, 1);
    SDL_ReleaseGPUFence(device_, fence);

    // Read back result and swizzle RGBA → ARGB
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
