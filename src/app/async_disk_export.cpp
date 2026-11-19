#include "app/async_disk_export.hpp"

#include <filesystem>
#include <utility>

#include "core/binary_file.hpp"

namespace mapperbus::app {
core::Result<void> write_disk_export(const DiskExportRequest& request) {
    const std::filesystem::path destination(request.path);
    const std::filesystem::path source(request.source_path);
    std::error_code error;
    const auto canonical_source = std::filesystem::weakly_canonical(source, error);
    if (error || source.empty())
        return std::unexpected("Cannot check the original disk path: " + request.source_path);
    const auto canonical_destination = std::filesystem::weakly_canonical(destination, error);
    if (error || destination.empty())
        return std::unexpected("Cannot check the export path: " + request.path);
    if (canonical_source == canonical_destination)
        return std::unexpected("Choose a different path to preserve the original disk image.");
    const bool destination_exists = std::filesystem::exists(destination, error);
    if (error)
        return std::unexpected("Cannot check the export destination: " + request.path);
    if (destination_exists) {
        if (std::filesystem::equivalent(source, destination, error))
            return std::unexpected("Choose a different path to preserve the original disk image.");
        if (error)
            return std::unexpected("Cannot check the export destination: " + request.path);
    }

    return core::write_binary_file_atomic(request.path, request.data, "FDS export");
}

AsyncDiskExport::AsyncDiskExport(Write write) : write_(std::move(write)) {}

core::Result<void> AsyncDiskExport::start(DiskExportRequest request) {
    auto started = task_.start(
        [write = write_, request = std::move(request)]() -> core::Result<std::string> {
            if (auto result = write(request); !result)
                return std::unexpected(result.error());
            return request.path;
        },
        false);
    if (started)
        busy_ = true;
    return started;
}

std::optional<core::Result<std::string>> AsyncDiskExport::poll() {
    auto result = task_.poll();
    if (result)
        busy_ = false;
    return result;
}

} // namespace mapperbus::app
