#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "app/background_task.hpp"
#include "core/types.hpp"

namespace mapperbus::app {

struct DiskExportRequest {
    std::string path;
    std::string source_path; // Raw disk or containing archive; never replace it.
    std::vector<core::Byte> data;
};

[[nodiscard]] core::Result<void> write_disk_export(const DiskExportRequest& request);

/// One noncancellable export; owns bytes/paths and never accesses a live machine.
/// Normal GUI Close/Quit must poll completion before allowing process exit.
class AsyncDiskExport {
  public:
    using Write = std::function<core::Result<void>(const DiskExportRequest&)>;
    explicit AsyncDiskExport(Write write = write_disk_export);
    [[nodiscard]] core::Result<void> start(DiskExportRequest request);
    [[nodiscard]] std::optional<core::Result<std::string>> poll();
    [[nodiscard]] bool busy() const {
        return busy_;
    }

  private:
    Write write_;
    BackgroundTask<std::string> task_;
    bool busy_ = false;
};

} // namespace mapperbus::app
