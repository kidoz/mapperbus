#include "platform/video/fsr1.hpp"

#include <algorithm>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace mapperbus::platform {
namespace {
inline Rgb unpack(std::uint32_t color) {
    constexpr float inv255 = 1.0f / 255.0f;
    return {static_cast<float>((color >> 16) & 0xFF) * inv255,
            static_cast<float>((color >> 8) & 0xFF) * inv255,
            static_cast<float>(color & 0xFF) * inv255};
}

inline std::uint32_t pack(const Rgb& color) {
    auto clamp255 = [](float v) {
        return static_cast<std::uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f);
    };
    return 0xFF000000 | (clamp255(color.r) << 16) | (clamp255(color.g) << 8) | clamp255(color.b);
}

inline float min5(float a, float b, float c, float d, float e) {
    return std::min(a, std::min(b, std::min(c, std::min(d, e))));
}

inline float max5(float a, float b, float c, float d, float e) {
    return std::max(a, std::max(b, std::max(c, std::max(d, e))));
}
} // namespace

class ThreadPool {
  public:
    ThreadPool() {
        num_threads_ = std::max(1u, std::thread::hardware_concurrency());
        workers_.reserve(num_threads_ - 1);
        for (unsigned i = 1; i < num_threads_; ++i) {
            workers_.emplace_back([this, i] { worker_loop(i); });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            worker.join();
        }
    }

    template <class F> void parallel_for(F&& f) {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            work_ = std::forward<F>(f);
            remaining_ = static_cast<int>(workers_.size());
            ++generation_;
        }
        condition_.notify_all();
        work_(0);
        std::unique_lock<std::mutex> lock(mutex_);
        completed_.wait(lock, [this] { return remaining_ == 0; });
    }

    unsigned num_threads() const {
        return num_threads_;
    }

  private:
    void worker_loop(unsigned index) {
        std::uint64_t observed_generation = 0;
        std::unique_lock<std::mutex> lock(mutex_);
        while (true) {
            condition_.wait(lock, [this, observed_generation] {
                return stop_ || generation_ != observed_generation;
            });
            if (stop_)
                return;
            observed_generation = generation_;
            lock.unlock();
            work_(index);
            lock.lock();
            if (--remaining_ == 0)
                completed_.notify_one();
        }
    }

    std::vector<std::thread> workers_;
    std::function<void(int)> work_;
    std::mutex mutex_;
    std::condition_variable condition_;
    std::condition_variable completed_;
    bool stop_ = false;
    std::uint64_t generation_ = 0;
    int remaining_ = 0;
    unsigned num_threads_ = 1;
};

Fsr1Upscaler::Fsr1Upscaler(int scale)
    : scale_(scale), thread_pool_(std::make_unique<ThreadPool>()) {}

Fsr1Upscaler::~Fsr1Upscaler() = default;

int Fsr1Upscaler::scale_factor() const {
    return scale_;
}

void Fsr1Upscaler::scale(std::span<const std::uint32_t> source,
                         int src_width,
                         int src_height,
                         std::span<std::uint32_t> target) {
    const int dst_width = src_width * scale_;
    const int dst_height = src_height * scale_;
    const float inv_scale = 1.0f / static_cast<float>(scale_);
    const int num_threads = static_cast<int>(thread_pool_->num_threads());
    const int chunk_size = (dst_height + num_threads - 1) / num_threads;

    if (x_samples_.size() != static_cast<std::size_t>(dst_width)) {
        x_samples_.resize(dst_width);
        for (int x = 0; x < dst_width; ++x) {
            const float src_x = (static_cast<float>(x) + 0.5f) * inv_scale - 0.5f;
            const int ix = std::clamp(static_cast<int>(src_x), 0, src_width - 1);
            const int nx = std::clamp(ix + 1, 0, src_width - 1);
            x_samples_[x] = {ix, nx, std::max(0.0f, src_x - ix)};
        }
    }
    if (rolling_lines_.size() != static_cast<std::size_t>(num_threads))
        rolling_lines_.resize(num_threads);
    for (auto& lines : rolling_lines_) {
        for (auto& line : lines) {
            if (line.size() != static_cast<std::size_t>(dst_width))
                line.resize(dst_width);
        }
    }

    auto process_chunk =
        [this, src_width, src_height, dst_width, dst_height, &source, target, inv_scale](
            int y_start, int y_end, auto& rolling_lines) {
            if (y_start >= y_end)
                return;

            auto compute_easu_line = [&](int y, std::vector<Rgb>& line_out) {
                const float src_y = (static_cast<float>(y) + 0.5f) * inv_scale - 0.5f;
                const int iy = std::clamp(static_cast<int>(src_y), 0, src_height - 1);
                const int ny = std::clamp(iy + 1, 0, src_height - 1);
                const float fy = std::max(0.0f, src_y - iy);
                const auto* row0 = source.data() + iy * src_width;
                const auto* row1 = source.data() + ny * src_width;

                for (int x = 0; x < dst_width; ++x) {
                    const auto& sample = x_samples_[x];
                    const float fx = sample.fraction;
                    Rgb c00 = unpack(row0[sample.source]);
                    Rgb c10 = unpack(row0[sample.next]);
                    Rgb c01 = unpack(row1[sample.source]);
                    Rgb c11 = unpack(row1[sample.next]);

                    float w00 = (1.0f - fx) * (1.0f - fy);
                    float w10 = fx * (1.0f - fy);
                    float w01 = (1.0f - fx) * fy;
                    float w11 = fx * fy;

                    line_out[x] = {c00.r * w00 + c10.r * w10 + c01.r * w01 + c11.r * w11,
                                   c00.g * w00 + c10.g * w10 + c01.g * w01 + c11.g * w11,
                                   c00.b * w00 + c10.b * w10 + c01.b * w01 + c11.b * w11};
                }
            };

            int pre_y = std::max(0, y_start - 1);
            compute_easu_line(pre_y, rolling_lines[pre_y % 3]);

            int curr_y = y_start;
            compute_easu_line(curr_y, rolling_lines[curr_y % 3]);

            const float sharpness = 0.2f;

            for (int y = y_start; y < y_end; ++y) {
                int next_y = std::min(dst_height - 1, y + 1);
                compute_easu_line(next_y, rolling_lines[next_y % 3]);

                const auto& L_up = rolling_lines[std::max(0, y - 1) % 3];
                const auto& L_c = rolling_lines[y % 3];
                const auto& L_dn = rolling_lines[next_y % 3];

                for (int x = 0; x < dst_width; ++x) {
                    int left = std::max(0, x - 1);
                    int right = std::min(dst_width - 1, x + 1);

                    Rgb c = L_c[x];
                    Rgb n = L_up[x];
                    Rgb s = L_dn[x];
                    Rgb w = L_c[left];
                    Rgb e = L_c[right];

                    float min_r = min5(c.r, n.r, s.r, w.r, e.r);
                    float max_r = max5(c.r, n.r, s.r, w.r, e.r);
                    float min_g = min5(c.g, n.g, s.g, w.g, e.g);
                    float max_g = max5(c.g, n.g, s.g, w.g, e.g);
                    float min_b = min5(c.b, n.b, s.b, w.b, e.b);
                    float max_b = max5(c.b, n.b, s.b, w.b, e.b);

                    float limit_r = std::min(min_r, 1.0f - max_r);
                    float limit_g = std::min(min_g, 1.0f - max_g);
                    float limit_b = std::min(min_b, 1.0f - max_b);

                    float lob_r = limit_r * (1.0f / (max_r + 1e-5f));
                    float lob_g = limit_g * (1.0f / (max_g + 1e-5f));
                    float lob_b = limit_b * (1.0f / (max_b + 1e-5f));

                    float w_r = sharpness * lob_r;
                    float w_g = sharpness * lob_g;
                    float w_b = sharpness * lob_b;

                    float denom_r = 1.0f / (1.0f + 4.0f * w_r);
                    float denom_g = 1.0f / (1.0f + 4.0f * w_g);
                    float denom_b = 1.0f / (1.0f + 4.0f * w_b);

                    Rgb rcas_out = {(c.r + w_r * (n.r + s.r + w.r + e.r)) * denom_r,
                                    (c.g + w_g * (n.g + s.g + w.g + e.g)) * denom_g,
                                    (c.b + w_b * (n.b + s.b + w.b + e.b)) * denom_b};

                    target[y * dst_width + x] = pack(rcas_out);
                }
            }
        };

    thread_pool_->parallel_for([&](int chunk_index) {
        const int y_start = chunk_index * chunk_size;
        const int y_end = std::min(y_start + chunk_size, dst_height);
        process_chunk(y_start, y_end, rolling_lines_[chunk_index]);
    });
}
} // namespace mapperbus::platform
