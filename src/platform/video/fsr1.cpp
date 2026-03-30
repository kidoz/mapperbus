#include "platform/video/fsr1.hpp"

#include <algorithm>
#include <cmath>
#include <future>
#include <thread>
#include <vector>
#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace mapperbus::platform {

namespace {

inline Rgb unpack(std::uint32_t color) {
    constexpr float inv255 = 1.0f / 255.0f;
    return {
        static_cast<float>((color >> 16) & 0xFF) * inv255,
        static_cast<float>((color >> 8) & 0xFF) * inv255,
        static_cast<float>(color & 0xFF) * inv255
    };
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
    using Task = std::function<void()>;
public:
    ThreadPool() {
        num_threads_ = std::max(1u, std::thread::hardware_concurrency());
        for (unsigned i = 0; i < num_threads_; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    Task task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });
                        if (stop_ && tasks_.empty()) return;
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            worker.join();
        }
    }

    template<class F>
    std::future<void> enqueue(F&& f) {
        auto task = std::make_shared<std::packaged_task<void()>>(std::forward<F>(f));
        std::future<void> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return res;
    }

    unsigned num_threads() const { return num_threads_; }

private:
    std::vector<std::thread> workers_;
    std::queue<Task> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_ = false;
    unsigned num_threads_ = 1;
};

Fsr1Upscaler::Fsr1Upscaler(int scale) : scale_(scale), thread_pool_(std::make_unique<ThreadPool>()) {}

Fsr1Upscaler::~Fsr1Upscaler() = default;

int Fsr1Upscaler::scale_factor() const {
    return scale_;
}

void Fsr1Upscaler::scale(std::span<const std::uint32_t> source,
                         int src_width,
                         int src_height,
                         std::span<std::uint32_t> target) {
    int dst_width = src_width * scale_;
    int dst_height = src_height * scale_;

    const float inv_scale = 1.0f / static_cast<float>(scale_);

    unsigned num_threads = thread_pool_->num_threads();
    int chunk_size = (dst_height + num_threads - 1) / num_threads;

    auto process_chunk = [src_width, src_height, dst_width, dst_height, &source, target, inv_scale](int y_start, int y_end) {
        if (y_start >= y_end) return;

        std::vector<Rgb> rolling_lines[3];
        for (int i = 0; i < 3; ++i) {
            rolling_lines[i].resize(dst_width);
        }

        auto compute_easu_line = [&](int y, std::vector<Rgb>& line_out) {
            for (int x = 0; x < dst_width; ++x) {
                float src_x = (static_cast<float>(x) + 0.5f) * inv_scale - 0.5f;
                float src_y = (static_cast<float>(y) + 0.5f) * inv_scale - 0.5f;

                int ix = std::max(0, std::min(src_width - 1, static_cast<int>(src_x)));
                int iy = std::max(0, std::min(src_height - 1, static_cast<int>(src_y)));
                int nx = std::max(0, std::min(src_width - 1, ix + 1));
                int ny = std::max(0, std::min(src_height - 1, iy + 1));

                float fx = std::max(0.0f, src_x - ix);
                float fy = std::max(0.0f, src_y - iy);

                Rgb c00 = unpack(source[iy * src_width + ix]);
                Rgb c10 = unpack(source[iy * src_width + nx]);
                Rgb c01 = unpack(source[ny * src_width + ix]);
                Rgb c11 = unpack(source[ny * src_width + nx]);

                float w00 = (1.0f - fx) * (1.0f - fy);
                float w10 = fx * (1.0f - fy);
                float w01 = (1.0f - fx) * fy;
                float w11 = fx * fy;

                line_out[x] = {
                    c00.r * w00 + c10.r * w10 + c01.r * w01 + c11.r * w11,
                    c00.g * w00 + c10.g * w10 + c01.g * w01 + c11.g * w11,
                    c00.b * w00 + c10.b * w10 + c01.b * w01 + c11.b * w11
                };
            }
        };

        // Bootstrap line buffers
        int pre_y = std::max(0, y_start - 1);
        compute_easu_line(pre_y, rolling_lines[pre_y % 3]);

        int curr_y = y_start;
        compute_easu_line(curr_y, rolling_lines[curr_y % 3]);

        const float sharpness = 0.2f;

        // Fused RCAS Phase
        for (int y = y_start; y < y_end; ++y) {
            int next_y = std::min(dst_height - 1, y + 1);
            compute_easu_line(next_y, rolling_lines[next_y % 3]);

            const auto& L_up = rolling_lines[std::max(0, y - 1) % 3];
            const auto& L_c  = rolling_lines[y % 3];
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

                // Replaced slow division with reciprocal multiplication
                float lob_r = limit_r * (1.0f / (max_r + 1e-5f));
                float lob_g = limit_g * (1.0f / (max_g + 1e-5f));
                float lob_b = limit_b * (1.0f / (max_b + 1e-5f));

                float w_r = sharpness * lob_r;
                float w_g = sharpness * lob_g;
                float w_b = sharpness * lob_b;

                // Replaced slow division with reciprocal multiplication for weighting
                float denom_r = 1.0f / (1.0f + 4.0f * w_r);
                float denom_g = 1.0f / (1.0f + 4.0f * w_g);
                float denom_b = 1.0f / (1.0f + 4.0f * w_b);

                Rgb rcas_out = {
                    (c.r + w_r * (n.r + s.r + w.r + e.r)) * denom_r,
                    (c.g + w_g * (n.g + s.g + w.g + e.g)) * denom_g,
                    (c.b + w_b * (n.b + s.b + w.b + e.b)) * denom_b
                };

                target[y * dst_width + x] = pack(rcas_out);
            }
        }
    };

    std::vector<std::future<void>> futures;
    futures.reserve(num_threads);

    for (int y = 0; y < dst_height; y += chunk_size) {
        int chunk_end = std::min(y + chunk_size, dst_height);
        futures.push_back(thread_pool_->enqueue([y, chunk_end, &process_chunk]() {
            process_chunk(y, chunk_end);
        }));
    }

    for (auto& f : futures) {
        f.get();
    }
}

} // namespace mapperbus::platform