#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>

#include "core/mappers/mapper_registry.hpp"
#include "core/types.hpp"

namespace mapperbus::app {

/// Bounded file work with owned inputs/results and no frontend callbacks.
/// Destruction never joins an OS operation. A cancelled result cannot be polled.
template <typename T> class BackgroundTask {
  public:
    using Task = std::function<core::Result<T>()>;

    BackgroundTask() : workers_(std::make_shared<Workers>()) {}
    ~BackgroundTask() {
        abandon();
    }
    BackgroundTask(const BackgroundTask&) = delete;
    BackgroundTask& operator=(const BackgroundTask&) = delete;

    [[nodiscard]] core::Result<void> start(Task task, bool cancellable) {
        if (job_)
            return std::unexpected("A file operation is already in progress");
        auto job = std::make_shared<Job>();
        constexpr int kMaxWorkers = 4;
        if (workers_->count.fetch_add(1) >= kMaxWorkers) {
            --workers_->count;
            return std::unexpected("Previous file operations are still waiting. Try again later.");
        }
        try {
            std::thread([job, workers = workers_, task = std::move(task)]() mutable {
                {
                    auto result = [&]() -> core::Result<T> {
                        try {
                            return task();
                        } catch (const std::exception& error) {
                            return std::unexpected("File operation failed: " +
                                                   std::string(error.what()));
                        } catch (...) {
                            return std::unexpected("File operation failed unexpectedly");
                        }
                    }();
                    const std::lock_guard lock(job->mutex);
                    if (!job->abandoned)
                        job->result = std::move(result);
                    job->done = true;
                    job->completed.notify_all();
                }
                // Release captured machines before returning the worker permit;
                // their destructors may perform persistence during shutdown.
                task = {};
                --workers->count;
            }).detach();
        } catch (const std::exception& error) {
            --workers_->count;
            return std::unexpected("Cannot start file operation: " + std::string(error.what()));
        }
        job_ = std::move(job);
        cancellable_ = cancellable;
        return {};
    }

    [[nodiscard]] std::optional<core::Result<T>> poll() {
        if (!job_)
            return std::nullopt;
        std::optional<core::Result<T>> result;
        {
            const std::lock_guard lock(job_->mutex);
            if (!job_->result)
                return std::nullopt;
            result = std::move(job_->result);
        }
        job_.reset();
        return result;
    }

    bool cancel() {
        if (!job_ || !cancellable_)
            return false;
        abandon();
        job_.reset();
        return true;
    }

    /// Invoke on a worker before another writer takes ownership of the same file.
    /// The fence owns completion state, never the task owner or frontend.
    [[nodiscard]] std::function<void()> completion_fence() const {
        return [job = job_] {
            if (job) {
                std::unique_lock lock(job->mutex);
                job->completed.wait(lock, [&] { return job->done; });
            }
        };
    }

  private:
    struct Job {
        std::mutex mutex;
        std::condition_variable completed;
        bool done = false;
        bool abandoned = false;
        std::optional<core::Result<T>> result;
    };
    struct Workers {
        std::atomic<int> count{0};
        std::shared_ptr<const core::MapperRegistry> registry = core::MapperRegistry::retain();
    };
    void abandon() {
        if (job_) {
            const std::lock_guard lock(job_->mutex);
            job_->abandoned = true;
        }
    }
    std::shared_ptr<Workers> workers_;
    std::shared_ptr<Job> job_;
    bool cancellable_ = false;
};

} // namespace mapperbus::app
