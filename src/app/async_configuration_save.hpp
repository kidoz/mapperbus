#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "app/background_task.hpp"
#include "app/configuration.hpp"

namespace mapperbus::app {

struct ConfigurationSaveCompletion {
    std::uint64_t revision;
    core::Result<void> result;
};

/// One immutable writer and one coalesced latest request. No worker UI callbacks.
/// Failure keeps the latest request for explicit Retry, including later edits.
class AsyncConfigurationSave {
  public:
    using Write = std::function<core::Result<void>(const MapperBusConfiguration&,
                                                   const std::filesystem::path&)>;
    explicit AsyncConfigurationSave(std::filesystem::path path = mapperbus_configuration_path(),
                                    Write write = save_mapperbus_configuration_to_file);
    [[nodiscard]] std::uint64_t submit(MapperBusConfiguration configuration);
    [[nodiscard]] std::optional<ConfigurationSaveCompletion> poll();
    void retry();
    /// Discard an unapplied failed proposal in favor of current live settings.
    void replace_failed(MapperBusConfiguration configuration);
    [[nodiscard]] bool busy() const {
        return running_;
    }
    [[nodiscard]] bool unsaved() const {
        return latest_ && latest_->revision != saved_revision_;
    }
    [[nodiscard]] const std::string& error() const {
        return error_;
    }

  private:
    struct Request {
        std::uint64_t revision;
        MapperBusConfiguration configuration;
    };
    void start_latest();
    std::filesystem::path path_;
    Write write_;
    BackgroundTask<void> task_;
    std::optional<Request> latest_;
    std::uint64_t revision_ = 0;
    std::uint64_t running_revision_ = 0;
    std::uint64_t saved_revision_ = 0;
    bool running_ = false;
    std::string error_;
};

} // namespace mapperbus::app
