#include "app/state_storage.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <system_error>
#include <vector>

namespace mapperbus::app {
namespace {
std::filesystem::path user_home() {
    if (const auto* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        return home;
    }
    if (const auto* profile = std::getenv("USERPROFILE");
        profile != nullptr && profile[0] != '\0') {
        return profile;
    }
    return std::filesystem::current_path();
}

std::string safe_game_name(const std::filesystem::path& rom) {
    std::string name;
    for (const unsigned char ch : rom.stem().string()) {
        if (name.size() >= 48) {
            break;
        }
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            name += static_cast<char>(ch);
        } else if (!name.empty() && name.back() != '-') {
            name += '-';
        }
    }
    while (!name.empty() && name.back() == '-') {
        name.pop_back();
    }
    return name.empty() ? "game" : name;
}
} // namespace

std::filesystem::path mapperbus_data_directory() {
#if defined(_WIN32)
    if (const auto* appdata = std::getenv("APPDATA"); appdata != nullptr && appdata[0] != '\0') {
        return std::filesystem::path(appdata) / "MapperBus";
    }
    return user_home() / "AppData" / "Roaming" / "MapperBus";
#elif defined(__APPLE__)
    return user_home() / "Library" / "Application Support" / "MapperBus";
#else
    if (const auto* xdg = std::getenv("XDG_DATA_HOME"); xdg != nullptr && xdg[0] != '\0') {
        const std::filesystem::path directory(xdg);
        if (directory.is_absolute()) {
            return directory / "mapperbus";
        }
    }
    return user_home() / ".local" / "share" / "mapperbus";
#endif
}

bool valid_game_identity(std::string_view identity) {
    return identity.size() == 64 && std::ranges::all_of(identity, [](char ch) {
               return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
           });
}

std::filesystem::path state_directory_for_game(const StateStorageSettings& settings,
                                               const std::filesystem::path& rom,
                                               std::string_view identity) {
    const auto root = !settings.directory.empty() ? settings.directory
                      : settings.mode == StateStorageMode::Portable
                          ? std::filesystem::absolute(rom).parent_path() / "mapperbus-states"
                          : mapperbus_data_directory() / "states";
    // Display names are only labels. Identity alone decides which existing
    // directory to reuse after a rename or when opening a second copy.
    const std::string suffix = "--" + std::string(identity);
    std::vector<std::filesystem::path> matches;
    std::error_code error;
    auto iterator = std::filesystem::directory_iterator(root, error);
    const auto end = std::filesystem::directory_iterator{};
    while (!error && iterator != end) {
        if (iterator->path().filename().string().ends_with(suffix) &&
            iterator->is_directory(error)) {
            matches.push_back(iterator->path());
        }
        iterator.increment(error);
    }
    if (!matches.empty()) {
        std::ranges::sort(matches);
        return matches.front();
    }
    return root / (safe_game_name(rom) + suffix);
}

core::Result<void> ensure_state_directory(const std::filesystem::path& directory) {
    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        return std::unexpected("Failed to create save-state directory: " + directory.string() +
                               ": " + error.message());
    }
    return {};
}

std::filesystem::path legacy_state_path(const std::filesystem::path& rom, int slot) {
    return std::filesystem::path(rom).replace_extension(slot > 0 ? ".state" + std::to_string(slot)
                                                                 : ".state");
}

} // namespace mapperbus::app
