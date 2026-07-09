#pragma once

#include <memory>

#include "core/types.hpp"
#include "platform/video/upscaler.hpp"

namespace mapperbus::platform {

class VideoBackend {
  public:
    virtual ~VideoBackend() = default;

    virtual bool initialize(int width, int height) = 0;
    virtual void render(const core::FrameBuffer& frame) = 0;
    virtual void shutdown() = 0;

    virtual void set_upscaler(std::unique_ptr<Upscaler> /*upscaler*/) {}

    /// Toggle borderless fullscreen. Default no-op for backends without a window.
    virtual void toggle_fullscreen() {}
    /// Enable/disable vsync. May require a renderer reinitialization.
    virtual void set_vsync(bool /*on*/) {}
};

} // namespace mapperbus::platform
