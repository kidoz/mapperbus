#include "core/binary_file.hpp"

#include <chrono>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

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

bool sync_file(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto handle = CreateFileW(path.c_str(),
                                    GENERIC_WRITE,
                                    FILE_SHARE_READ,
                                    nullptr,
                                    OPEN_EXISTING,
                                    FILE_FLAG_OPEN_REPARSE_POINT,
                                    nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return false;
    const bool synced = FlushFileBuffers(handle) != 0;
    CloseHandle(handle);
#else
    const int fd = open(path.c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0)
        return false;
    const bool synced = fsync(fd) == 0;
    close(fd);
#endif
    return synced;
}
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
#if !defined(_WIN32)
    std::filesystem::permissions(temporary.path,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace,
                                 error);
    if (error)
        return fail("set permissions on");
#endif
    write(temporary.file);
    const bool written = temporary.file.good();
    temporary.file.close();
    if (!written || !temporary.file)
        return fail("write");
    if (!sync_file(temporary.path))
        return fail("sync");
    std::filesystem::rename(temporary.path, destination, error);
    if (error)
        return fail("replace");
    temporary.owned = false;
    return {};
}

} // namespace mapperbus::core
