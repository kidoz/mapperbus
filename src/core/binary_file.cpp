#include "core/binary_file.hpp"

#include <chrono>

namespace mapperbus::core {
namespace {
struct TemporaryFile {
    std::filesystem::path path;
    std::ofstream file;
    ~TemporaryFile() {
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

Result<void> write_file_atomic(const std::filesystem::path& destination,
                               std::string_view label,
                               const std::function<void(std::ostream&)>& write) {
    std::error_code error;
    TemporaryFile temporary;
    for (int attempt = 0; attempt < 8; ++attempt) {
        temporary.path =
            destination.parent_path() /
            (".mapperbus-write-" +
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
        return std::unexpected("Failed to open " + std::string(label) +
                               " for writing: " + destination.string());
    const auto fail = [&](const std::string& action) -> core::Result<void> {
        return std::unexpected("Failed to " + action + " " + std::string(label) + ": " +
                               destination.string());
    };
    write(temporary.file);
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

} // namespace mapperbus::core
