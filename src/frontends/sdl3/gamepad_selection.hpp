#pragma once

#include <SDL3/SDL.h>
#include <algorithm>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "platform/input/gamepad_config.hpp"

namespace mapperbus::frontend {

struct Sdl3GamepadCandidate {
    SDL_JoystickID id = 0;
    int index = 0;
    platform::GamepadIdentity identity;
};

inline bool gamepad_identity_matches(const platform::GamepadIdentity& saved,
                                     const platform::GamepadIdentity& candidate) {
    return !saved.guid.empty() && saved.guid == candidate.guid &&
           (saved.serial.empty() || saved.serial == candidate.serial);
}

inline std::optional<std::size_t> unique_gamepad_match(
    const platform::GamepadIdentity& saved, std::span<const Sdl3GamepadCandidate> candidates) {
    if (saved.ambiguous)
        return std::nullopt;
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (gamepad_identity_matches(saved, candidates[i].identity)) {
            if (found)
                return std::nullopt;
            found = i;
        }
    }
    return found;
}

// Shared SDL handle/identity policy for both frontends. Inventory metadata is
// refreshed only when instance IDs change; platform input config stays SDL-free.
class Sdl3GamepadSelection {
  public:
    Sdl3GamepadSelection() = default;
    Sdl3GamepadSelection(const Sdl3GamepadSelection&) = delete;
    Sdl3GamepadSelection& operator=(const Sdl3GamepadSelection&) = delete;
    ~Sdl3GamepadSelection() {
        close();
    }

    [[nodiscard]] SDL_Gamepad* gamepad() const {
        return gamepad_;
    }
    bool consume_changed() {
        return std::exchange(changed_, false);
    }
    bool consume_inventory_changed() {
        return std::exchange(inventory_changed_, false);
    }

    void close() {
        if (gamepad_)
            SDL_CloseGamepad(gamepad_);
        gamepad_ = nullptr;
    }

    void assign(platform::GamepadInputConfig& config, int index) {
        close();
        blocked_ids_.clear();
        config.gamepad_index = std::max(0, index);
        config.identity = {};
        poll(config);
    }

    void poll(platform::GamepadInputConfig& config) {
        if (!config.enabled) {
            close();
            return;
        }
        if (gamepad_ && !SDL_GamepadConnected(gamepad_)) {
            blocked_ids_ = known_ids_;
            close();
        }
        int count = 0;
        auto* ids = SDL_GetGamepads(&count);
        if (!ids)
            return;
        std::vector<SDL_JoystickID> current(ids, ids + count);
        SDL_free(ids);
        if (current != known_ids_ || candidates_.size() != current.size()) {
            inventory_changed_ = true;
            candidates_.clear();
            for (int index = 0; index < static_cast<int>(current.size()); ++index) {
                if (auto* pad = SDL_OpenGamepad(current[index])) {
                    char guid[33]{};
                    SDL_GUIDToString(SDL_GetGamepadGUIDForID(current[index]), guid, sizeof(guid));
                    platform::GamepadIdentity identity;
                    if (std::string_view(guid) != "00000000000000000000000000000000")
                        identity.guid = guid;
                    if (const auto* serial = SDL_GetGamepadSerial(pad))
                        identity.serial = std::string(serial).substr(0, 256);
                    if (const auto* name = SDL_GetGamepadName(pad))
                        identity.name = std::string(name).substr(0, 256);
                    candidates_.push_back({current[index], index, std::move(identity)});
                    SDL_CloseGamepad(pad);
                }
            }
        }
        known_ids_ = std::move(current);
        if (!config.identity.guid.empty()) {
            const auto matches = std::ranges::count_if(candidates_, [&](const auto& candidate) {
                return gamepad_identity_matches(config.identity, candidate.identity);
            });
            if (matches > 1 && !config.identity.ambiguous) {
                config.identity.ambiguous = true;
                changed_ = true;
            }
        }
        if (gamepad_) {
            for (const auto& candidate : candidates_)
                if (candidate.id == SDL_GetGamepadID(gamepad_) &&
                    config.gamepad_index != candidate.index) {
                    config.gamepad_index = candidate.index;
                    changed_ = true;
                }
            return;
        }
        const Sdl3GamepadCandidate* target = nullptr;
        if (config.identity.guid.empty()) {
            for (const auto& candidate : candidates_)
                if (candidate.index == config.gamepad_index &&
                    std::ranges::find(blocked_ids_, candidate.id) == blocked_ids_.end())
                    target = &candidate;
        } else if (const auto match = unique_gamepad_match(config.identity, candidates_)) {
            target = &candidates_[*match];
        }
        if (!target)
            return;
        gamepad_ = SDL_OpenGamepad(target->id);
        if (!gamepad_)
            return;
        auto identity = target->identity;
        identity.ambiguous = std::ranges::count_if(candidates_, [&](const auto& candidate) {
                                 return gamepad_identity_matches(identity, candidate.identity);
                             }) > 1;
        if (config.identity != identity || config.gamepad_index != target->index) {
            config.identity = std::move(identity);
            config.gamepad_index = target->index;
            changed_ = true;
        }
    }

  private:
    SDL_Gamepad* gamepad_ = nullptr;
    std::vector<SDL_JoystickID> known_ids_;
    std::vector<SDL_JoystickID> blocked_ids_;
    std::vector<Sdl3GamepadCandidate> candidates_;
    bool changed_ = false;
    bool inventory_changed_ = false;
};

} // namespace mapperbus::frontend
