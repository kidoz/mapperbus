#pragma once

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

#include "app/background_task.hpp"
#include "app/state_storage.hpp"

namespace mapperbus::app {

enum class StateSlotStatus { Saved, Empty, Unavailable };
using StateSlotStatuses = std::array<StateSlotStatus, kSaveStateSlotCount>;

struct StateSlotScanRequest {
    std::filesystem::path directory;
    std::filesystem::path rom;
    bool allow_legacy = false;
};

struct PreparedStateStorage {
    StateStorageSettings settings;
    std::filesystem::path directory;
};

[[nodiscard]] core::Result<StateSlotStatuses> scan_state_slots(const StateSlotScanRequest& request);
[[nodiscard]] core::Result<PreparedStateStorage> prepare_state_storage(
    const StateStorageSettings& settings,
    const std::filesystem::path& rom,
    const std::string& identity);
[[nodiscard]] core::Result<void> open_state_directory(const std::filesystem::path& directory);

/// Chooser jobs own immutable paths/settings. Closing a chooser cancels its
/// results without joining I/O; an explicitly requested folder launch may finish.
class StateStorageTasks {
  public:
    using Scan = std::function<core::Result<StateSlotStatuses>(const StateSlotScanRequest&)>;
    using Prepare = std::function<core::Result<PreparedStateStorage>(
        const StateStorageSettings&, const std::filesystem::path&, const std::string&)>;
    using Open = std::function<core::Result<void>(const std::filesystem::path&)>;

    explicit StateStorageTasks(Scan scan = scan_state_slots,
                               Prepare prepare = prepare_state_storage,
                               Open open = open_state_directory);
    [[nodiscard]] core::Result<void> scan(StateSlotScanRequest request);
    [[nodiscard]] core::Result<void> prepare(StateStorageSettings settings,
                                             std::filesystem::path rom,
                                             std::string identity);
    [[nodiscard]] core::Result<void> open(std::filesystem::path directory);
    void cancel_chooser();
    [[nodiscard]] std::optional<core::Result<StateSlotStatuses>> poll_slots();
    [[nodiscard]] std::optional<core::Result<PreparedStateStorage>> poll_location();
    [[nodiscard]] std::optional<core::Result<void>> poll_open();
    [[nodiscard]] bool scanning() const {
        return scanning_;
    }
    [[nodiscard]] bool preparing() const {
        return preparing_;
    }
    [[nodiscard]] bool opening() const {
        return opening_;
    }

  private:
    Scan scan_;
    Prepare prepare_;
    Open open_;
    BackgroundTask<StateSlotStatuses> scan_task_;
    BackgroundTask<PreparedStateStorage> prepare_task_;
    BackgroundTask<void> open_task_;
    bool scanning_ = false;
    bool preparing_ = false;
    bool opening_ = false;
};

} // namespace mapperbus::app
