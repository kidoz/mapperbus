#include "core/logger.hpp"

#include <print>

namespace mapperbus::core::logger {

namespace {

Level current_level = Level::Info;

constexpr std::string_view level_tag(Level level) {
    switch (level) {
    case Level::Debug:
        return "[DEBUG]";
    case Level::Info:
        return "[INFO] ";
    case Level::Warn:
        return "[WARN] ";
    case Level::Error:
        return "[ERROR]";
    default:
        return "[?????]";
    }
}

} // namespace

void set_level(Level level) {
    current_level = level;
}

Level get_level() {
    return current_level;
}

namespace detail {

void write(Level level, std::string_view message) {
    std::println(stderr, "{} {}", level_tag(level), message);
}

} // namespace detail

} // namespace mapperbus::core::logger
