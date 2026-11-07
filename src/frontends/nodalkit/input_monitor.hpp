#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "platform/input/input_action.hpp"

namespace mapperbus::frontend {

enum class InputMonitorConnection {
    Unavailable,
    Disabled,
    Disconnected,
    NeedsAssignment,
    Connected
};

/// A read-only host snapshot. IDs identify live instances, never saved device indices.
struct InputMonitorProfile {
    InputMonitorConnection connection = InputMonitorConnection::Unavailable;
    std::uint32_t device_id = 0;
    int device_index = 0;
    std::string device_name;
    std::array<bool, platform::kInputActionCount> held{};
    bool operator==(const InputMonitorProfile&) const = default;
};

inline std::string input_profile_name(int player, core::ControllerMode mode) {
    if (mode == core::ControllerMode::HoriFourPlayer)
        return player < 2 ? "Built-in " + std::to_string(player + 1)
                          : "Hori " + std::to_string(player - 1);
    return player < 2 || mode == core::ControllerMode::FourScore
               ? "Player " + std::to_string(player + 1)
               : "External " + std::to_string(player - 1);
}

inline std::vector<int> active_controller_profiles(core::ControllerMode mode, core::Byte attached) {
    if (mode == core::ControllerMode::HoriFourPlayer)
        return {0, 1, 2, 3, 4, 5};
    if (mode == core::ControllerMode::FourScore)
        return {0, 1, 2, 3};
    std::vector<int> profiles{0, 1};
    for (int port = 0; port < 2; ++port)
        if (attached & (1U << port))
            profiles.push_back(port + 2);
    return profiles;
}

struct InputMonitorRow {
    int profile = 0;
    std::string text;
    bool shared_device = false;
};

/// Presentation from values only: never polls devices, changes assignments or clocks turbo.
inline std::vector<InputMonitorRow> input_monitor_rows(
    core::ControllerMode mode,
    core::Byte attached,
    const std::array<InputMonitorProfile, platform::kInputProfileCount>& profiles) {
    const auto active = active_controller_profiles(mode, attached);
    constexpr std::array kActionNames = {"Up",
                                         "Down",
                                         "Left",
                                         "Right",
                                         "A",
                                         "B",
                                         "Start",
                                         "Select",
                                         "Turbo A",
                                         "Turbo B",
                                         "Microphone"};
    std::vector<InputMonitorRow> rows;
    for (const int player : active) {
        const auto& profile = profiles[player];
        std::string status;
        switch (profile.connection) {
        case InputMonitorConnection::Unavailable:
            status = "Gamepad support unavailable";
            break;
        case InputMonitorConnection::Disabled:
            status = "Gamepad input disabled";
            break;
        case InputMonitorConnection::Disconnected:
            status = "Disconnected";
            break;
        case InputMonitorConnection::NeedsAssignment:
            status = "Needs reassignment";
            break;
        case InputMonitorConnection::Connected:
            status = "Connected";
            break;
        }
        auto text = input_profile_name(player, mode) + " — Gamepad " +
                    std::to_string(static_cast<std::int64_t>(profile.device_index) + 1);
        if (!profile.device_name.empty())
            text += " · " + profile.device_name;
        text += " · " + status + "\nHolding: ";
        bool any = false;
        for (std::size_t action = 0; action < profile.held.size(); ++action)
            if (profile.held[action]) {
                if (any)
                    text += " + ";
                text += kActionNames[action];
                any = true;
            }
        if (!any)
            text += "None";
        bool shared = false;
        if (profile.connection == InputMonitorConnection::Connected && profile.device_id)
            for (const int other : active) {
                if (other == player ||
                    profiles[other].connection != InputMonitorConnection::Connected ||
                    profiles[other].device_id != profile.device_id)
                    continue;
                text += shared ? ", " : "\nShared gamepad with ";
                text += input_profile_name(other, mode);
                shared = true;
            }
        if (shared)
            text += ". Reassign if unintended.";
        rows.push_back({player, std::move(text), shared});
    }
    return rows;
}
} // namespace mapperbus::frontend
