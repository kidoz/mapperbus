#pragma once

#include <SDL3/SDL.h>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "platform/video/upscaler.hpp"
#include "platform/video/video_backend.hpp"

namespace mapperbus::frontend {

class Sdl3Video : public platform::VideoBackend {
  public:
    ~Sdl3Video() override;

    bool initialize(int width, int height) override;
    void render(const core::FrameBuffer& frame) override;
    void shutdown() override;

    void set_upscaler(std::unique_ptr<platform::Upscaler> upscaler) override;
    void set_window_status(std::string_view status);
    void toggle_fullscreen() override;
    void set_vsync(bool on) override;

  private:
    [[nodiscard]] std::string window_title() const;
    std::string window_status_;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;

    std::unique_ptr<platform::Upscaler> upscaler_;
    std::vector<std::uint32_t> scaled_buffer_;
    int src_width_ = 0;
    int src_height_ = 0;
    bool using_zero_copy_ = false;
    bool fullscreen_ = false;
    bool vsync_ = false;
};

} // namespace mapperbus::frontend
