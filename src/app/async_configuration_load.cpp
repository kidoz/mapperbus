#include "app/async_configuration_load.hpp"

#include <utility>

namespace mapperbus::app {
AsyncConfigurationLoad::AsyncConfigurationLoad(Read read) : read_(std::move(read)) {}

core::Result<void> AsyncConfigurationLoad::start(ConfigurationLoadRequest request) {
    return task_.start([read = read_, request = std::move(request)] { return read(request); },
                       true);
}

std::optional<core::Result<LoadedConfiguration>> AsyncConfigurationLoad::poll() {
    return task_.poll();
}

bool AsyncConfigurationLoad::cancel() {
    return task_.cancel();
}

} // namespace mapperbus::app
