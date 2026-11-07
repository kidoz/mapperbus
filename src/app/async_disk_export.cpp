#include "app/async_disk_export.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <utility>

namespace mapperbus::app {
namespace {
struct ExportTemporary {
    std::filesystem::path path;
    std::ofstream file;
    ~ExportTemporary() {
        if (file.is_open())
            file.close();
        if (owned) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
    }
    bool owned = false;
};
} // namespace

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

    // Never truncate a pre-existing temporary file, symlink, or hard link. A
    // unique, exclusively created sibling also isolates export from other saves.
    ExportTemporary temporary;
    for (int attempt = 0; attempt < 8; ++attempt) {
        temporary.path =
            destination.parent_path() /
            (destination.filename().string() + ".export-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
             std::to_string(attempt) + ".tmp");
        temporary.file.open(temporary.path, std::ios::binary | std::ios::noreplace);
        if (temporary.file) {
            temporary.owned = true;
            break;
        }
        temporary.file.clear();
    }
    if (!temporary.owned)
        return std::unexpected("Failed to open FDS export for writing: " + request.path);
    const auto fail = [&](const std::string& action) -> core::Result<void> {
        return std::unexpected("Failed to " + action + " FDS export: " + request.path);
    };
    if (!request.data.empty())
        temporary.file.write(reinterpret_cast<const char*>(request.data.data()),
                             static_cast<std::streamsize>(request.data.size()));
    const bool written = temporary.file.good();
    temporary.file.close();
    if (!written || !temporary.file)
        return fail("write");
    std::filesystem::rename(temporary.path, destination, error);
    if (error)
        return fail("replace");
    temporary.owned = false;
    return {};
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
