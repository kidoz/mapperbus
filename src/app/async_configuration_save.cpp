#include "app/async_configuration_save.hpp"

#include <utility>

namespace mapperbus::app {

AsyncConfigurationSave::AsyncConfigurationSave(std::filesystem::path path, Write write)
    : path_(std::move(path)), write_(std::move(write)) {}

void AsyncConfigurationSave::start_latest() {
    auto started = task_.start(
        [path = path_, write = write_, configuration = latest_->configuration] {
            return write(configuration, path);
        },
        false);
    running_ = started.has_value();
    if (started)
        running_revision_ = latest_->revision;
    else
        error_ = started.error();
}

std::uint64_t AsyncConfigurationSave::submit(MapperBusConfiguration configuration) {
    latest_ = Request{++revision_, std::move(configuration)};
    if (!running_ && error_.empty())
        start_latest();
    return revision_;
}

std::optional<ConfigurationSaveCompletion> AsyncConfigurationSave::poll() {
    auto result = task_.poll();
    if (!result)
        return std::nullopt;
    const auto completed_revision = running_revision_;
    running_ = false;
    if (*result) {
        saved_revision_ = completed_revision;
        error_.clear();
        if (latest_->revision != saved_revision_)
            start_latest();
    } else {
        error_ = result->error();
    }
    return ConfigurationSaveCompletion{completed_revision, std::move(*result)};
}

void AsyncConfigurationSave::retry() {
    if (running_ || !latest_)
        return;
    error_.clear();
    if (unsaved())
        start_latest();
}

void AsyncConfigurationSave::replace_failed(MapperBusConfiguration configuration) {
    if (running_)
        return;
    error_.clear();
    (void)submit(std::move(configuration));
}

} // namespace mapperbus::app
