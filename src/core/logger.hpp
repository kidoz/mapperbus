#pragma once

#include <cstdio>
#include <format>
#include <string_view>
#include <utility>

namespace mapperbus::core::logger {

enum class Level : int {
    Debug = 0,
    Info = 1,
    Warn = 2,
    Error = 3,
    Off = 4,
};

/// Set the minimum log level. Messages below this level are silently dropped.
void set_level(Level level);

/// Get the current log level.
Level get_level();

namespace detail {

void write(Level level, std::string_view message);

} // namespace detail

/// Log a formatted message at the given level.
template <typename... Args> void log(Level level, std::format_string<Args...> fmt, Args&&... args) {
    if (level < get_level())
        return;
    detail::write(level, std::format(fmt, std::forward<Args>(args)...));
}

template <typename... Args> void debug(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Debug, fmt, std::forward<Args>(args)...);
}

template <typename... Args> void info(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Info, fmt, std::forward<Args>(args)...);
}

template <typename... Args> void warn(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Warn, fmt, std::forward<Args>(args)...);
}

template <typename... Args> void error(std::format_string<Args...> fmt, Args&&... args) {
    log(Level::Error, fmt, std::forward<Args>(args)...);
}

} // namespace mapperbus::core::logger
