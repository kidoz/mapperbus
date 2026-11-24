#include "app/rewind_history.hpp"

#include "core/emulator.hpp"

namespace mapperbus::app {
namespace {
constexpr std::size_t kHistoryBytes = std::size_t{64} * 1024 * 1024;
constexpr std::size_t kHistoryEntries = 100;
constexpr auto kCheckpointInterval = std::chrono::milliseconds(100);
} // namespace

void RewindHistory::record(const core::Emulator& emulator, const core::FrameBuffer& frame) {
    const auto duration = core::timing_for_region(emulator.region()).frame_duration_ns();
    constexpr auto target_ns = std::chrono::nanoseconds(kCheckpointInterval).count();
    const int interval = static_cast<int>((target_ns + duration / 2) / duration);
    if (++frames_since_capture_ < interval && !entries_.empty())
        return;
    frames_since_capture_ = 0;
    Checkpoint checkpoint{.state = emulator.save_state(), .frame = nullptr};
    const auto bytes = checkpoint.bytes();
    if (bytes > kHistoryBytes) {
        clear();
        return;
    }
    while (entries_.size() >= kHistoryEntries || bytes_ + bytes > kHistoryBytes) {
        bytes_ -= entries_.front().bytes();
        entries_.pop_front();
    }
    checkpoint.frame = std::make_unique<core::FrameBuffer>(frame);
    entries_.push_back(std::move(checkpoint));
    bytes_ += bytes;
}

bool RewindHistory::preview(std::chrono::steady_clock::time_point now) {
    if (entries_.empty())
        return false;
    if (!cursor_) {
        cursor_ = entries_.size() - 1;
        if (frames_since_capture_ == 0 && *cursor_ > 0)
            --*cursor_;
    } else {
        if (*cursor_ == 0 || now < next_preview_)
            return false;
        --*cursor_;
    }
    next_preview_ = now + kCheckpointInterval;
    return true;
}

const core::FrameBuffer* RewindHistory::picture() const {
    if (cursor_)
        return entries_[*cursor_].frame.get();
    if (frames_since_capture_ == 0 && !entries_.empty())
        return entries_.back().frame.get();
    return nullptr;
}

bool RewindHistory::restore(core::Emulator& emulator) {
    if (!cursor_)
        return false;
    if (!emulator.load_state(entries_[*cursor_].state)) {
        clear();
        return false;
    }
    while (entries_.size() - 1 > *cursor_) {
        bytes_ -= entries_.back().bytes();
        entries_.pop_back();
    }
    frames_since_capture_ = 0;
    cursor_.reset();
    return true;
}

void RewindHistory::clear() {
    entries_.clear();
    bytes_ = 0;
    frames_since_capture_ = 0;
    cursor_.reset();
}

} // namespace mapperbus::app
