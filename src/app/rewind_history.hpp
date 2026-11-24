#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::core {
class Emulator;
} // namespace mapperbus::core

namespace mapperbus::app {

/// Frame-boundary snapshots and the corresponding presented pictures. All access
/// stays on the emulation thread. Browsing only moves a cursor over the history.
class RewindHistory {
  public:
    void record(const core::Emulator& emulator, const core::FrameBuffer& frame);
    /// Returns true when a different checkpoint must be presented.
    bool preview(std::chrono::steady_clock::time_point now);
    [[nodiscard]] bool browsing() const {
        return cursor_.has_value();
    }
    [[nodiscard]] bool at_oldest() const {
        return cursor_ == 0;
    }
    [[nodiscard]] const core::FrameBuffer* picture() const;
    bool restore(core::Emulator& emulator);
    void clear();

  private:
    struct Checkpoint {
        std::vector<core::Byte> state;
        std::unique_ptr<core::FrameBuffer> frame;

        std::size_t bytes() const {
            return state.capacity() + sizeof(Checkpoint) + sizeof(core::FrameBuffer);
        }
    };
    std::deque<Checkpoint> entries_;
    std::size_t bytes_ = 0;
    int frames_since_capture_ = 0;
    std::optional<std::size_t> cursor_;
    std::chrono::steady_clock::time_point next_preview_{};
};

} // namespace mapperbus::app
