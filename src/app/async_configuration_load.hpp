#pragma once

#include <filesystem>
#include <functional>
#include <optional>

#include "app/background_task.hpp"
#include "app/configuration.hpp"

namespace mapperbus::app {

/// Cancellable startup reads own only request paths and result values.
class AsyncConfigurationLoad {
  public:
    using Read = std::function<core::Result<LoadedConfiguration>(const ConfigurationLoadRequest&)>;
    explicit AsyncConfigurationLoad(Read read = read_startup_configuration);
    [[nodiscard]] core::Result<void> start(ConfigurationLoadRequest request);
    [[nodiscard]] std::optional<core::Result<LoadedConfiguration>> poll();
    bool cancel();

  private:
    Read read_;
    BackgroundTask<LoadedConfiguration> task_;
};

} // namespace mapperbus::app
