#include "app/async_configuration_load.hpp"

#include <string>
#include <utility>

#include "core/binary_file.hpp"

namespace mapperbus::app {
namespace {
core::Result<bool> settings_present(const std::filesystem::path& path) {
    std::error_code error;
    const auto status = std::filesystem::symlink_status(path, error);
    if (error == std::errc::no_such_file_or_directory)
        return false;
    if (error)
        return std::unexpected("Cannot check settings file: " + path.string() + ": " +
                               error.message());
    return std::filesystem::exists(status);
}
} // namespace

core::Result<LoadedConfiguration> read_startup_configuration(
    const ConfigurationLoadRequest& request) {
    if (request.primary.empty())
        return std::unexpected("No settings file path was provided");
    auto path = request.primary;
    auto primary_exists = settings_present(path);
    if (!primary_exists)
        return std::unexpected(primary_exists.error());
    bool legacy = false;
    if (!*primary_exists) {
        auto legacy_exists =
            request.legacy.empty() ? core::Result<bool>(false) : settings_present(request.legacy);
        if (!legacy_exists)
            return std::unexpected(legacy_exists.error());
        if (!*legacy_exists)
            return LoadedConfiguration{.configuration = {}, .missing = true};
        path = request.legacy;
        legacy = true;
    }
    auto data =
        core::read_binary_file_capped(path.string(), kMaxConfigurationBytes, "settings file");
    if (!data)
        return std::unexpected(data.error());
    const std::string text(data->begin(), data->end());
    return LoadedConfiguration{parse_mapperbus_configuration(text), false, legacy};
}

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
