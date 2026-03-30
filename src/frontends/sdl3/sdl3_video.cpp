#include "frontends/sdl3/sdl3_video.hpp"

#include "core/types.hpp"

namespace mapperbus::frontend {

Sdl3Video::~Sdl3Video() {
    shutdown();
}

bool Sdl3Video::initialize(int width, int height) {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return false;
    }

    src_width_ = width;
    src_height_ = height;

    int display_scale = upscaler_ ? upscaler_->scale_factor() : 2;
    int win_w = width * display_scale;
    int win_h = height * display_scale;

    window_ = SDL_CreateWindow("MapperBus", win_w, win_h, SDL_WINDOW_RESIZABLE);
    if (!window_) {
        return false;
    }

    // Try the experimental 'gpu' backend first for zero-copy sharing!
    renderer_ = SDL_CreateRenderer(window_, "gpu");
    if (!renderer_) {
        // Fallback to default (usually 'metal' or 'direct3d11')
        renderer_ = SDL_CreateRenderer(window_, nullptr);
    }
    if (!renderer_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
        return false;
    }

    // Pass GPU Device natively to upscaler if supported
    using_zero_copy_ = false;
    if (upscaler_ && upscaler_->supports_gpu_texture()) {
        SDL_PropertiesID props = SDL_GetRendererProperties(renderer_);
        void* device = SDL_GetPointerProperty(props, SDL_PROP_RENDERER_GPU_DEVICE_POINTER, nullptr);
        if (device) {
            upscaler_->set_gpu_device(device);
            using_zero_copy_ = true;
        }
    }

    int tex_w = width;
    int tex_h = height;

    if (upscaler_) {
        tex_w = width * upscaler_->scale_factor();
        tex_h = height * upscaler_->scale_factor();
        if (!using_zero_copy_) {
            scaled_buffer_.resize(static_cast<std::size_t>(tex_w) * tex_h);
        }

        // Let SDL fit the upscaled texture into the window
        SDL_SetRenderLogicalPresentation(
            renderer_, tex_w, tex_h, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    } else {
        SDL_SetRenderLogicalPresentation(
            renderer_, width, height, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    }

    if (!using_zero_copy_) {
        texture_ = SDL_CreateTexture(
            renderer_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, tex_w, tex_h);
        if (!texture_) {
            SDL_DestroyRenderer(renderer_);
            SDL_DestroyWindow(window_);
            renderer_ = nullptr;
            window_ = nullptr;
            return false;
        }
    }

    return true;
}

void Sdl3Video::render(const core::FrameBuffer& frame) {
    if (upscaler_) {
        int scale = upscaler_->scale_factor();
        int dst_w = src_width_ * scale;
        int dst_h = src_height_ * scale;

        if (using_zero_copy_) {
            // GPU path: scale dispatches compute shaders, target span unused
            upscaler_->scale(frame.pixels, src_width_, src_height_, {});
            if (!texture_) {
                void* gpu_tex = upscaler_->get_gpu_texture();
                if (gpu_tex) {
                    SDL_PropertiesID tex_props = SDL_CreateProperties();
                    SDL_SetNumberProperty(tex_props, SDL_PROP_TEXTURE_CREATE_FORMAT_NUMBER, SDL_PIXELFORMAT_RGBA32);
                    SDL_SetNumberProperty(tex_props, SDL_PROP_TEXTURE_CREATE_ACCESS_NUMBER, SDL_TEXTUREACCESS_STATIC);
                    SDL_SetNumberProperty(tex_props, SDL_PROP_TEXTURE_CREATE_WIDTH_NUMBER, dst_w);
                    SDL_SetNumberProperty(tex_props, SDL_PROP_TEXTURE_CREATE_HEIGHT_NUMBER, dst_h);
                    SDL_SetPointerProperty(tex_props, SDL_PROP_TEXTURE_CREATE_GPU_TEXTURE_POINTER, gpu_tex);
                    texture_ = SDL_CreateTextureWithProperties(renderer_, tex_props);
                    if (!texture_) {
                        SDL_LogCritical(SDL_LOG_CATEGORY_VIDEO, "Failed to create GPU Texture wrapper: %s", SDL_GetError());
                    } else {
                        SDL_SetTextureBlendMode(texture_, SDL_BLENDMODE_NONE);
                    }
                    SDL_DestroyProperties(tex_props);
                }
            }
        } else {
            upscaler_->scale(frame.pixels, src_width_, src_height_, scaled_buffer_);
            SDL_UpdateTexture(texture_,
                              nullptr,
                              scaled_buffer_.data(),
                              dst_w * static_cast<int>(sizeof(std::uint32_t)));
        }
    } else {
        SDL_UpdateTexture(texture_,
                          nullptr,
                          frame.pixels.data(),
                          core::kScreenWidth * static_cast<int>(sizeof(std::uint32_t)));
    }

    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    SDL_RenderTexture(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

void Sdl3Video::set_upscaler(std::unique_ptr<platform::Upscaler> upscaler) {
    upscaler_ = std::move(upscaler);
}

void Sdl3Video::shutdown() {
    if (upscaler_) {
        upscaler_.reset();
    }
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    if (window_) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    scaled_buffer_.clear();
}

} // namespace mapperbus::frontend
