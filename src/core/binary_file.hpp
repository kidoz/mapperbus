#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::core {

/// Reads a whole binary file, rejecting anything larger than `max_bytes`
/// before allocating the buffer. `label` is the noun used in error text
/// ("ROM file", "save-state file").
[[nodiscard]] inline Result<std::vector<Byte>> read_binary_file_capped(const std::string& path,
                                                                       std::size_t max_bytes,
                                                                       std::string_view label) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return std::unexpected("Failed to open " + std::string(label) + ": " + path +
                               " (not an accessible regular file)");
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected("Failed to open " + std::string(label) + ": " + path);
    }

    file.seekg(0, std::ios::end);
    const auto end = file.tellg();
    if (end < 0) {
        return std::unexpected("Failed to read " + std::string(label) + ": " + path);
    }
    const auto size = static_cast<std::uint64_t>(end);
    if (size > max_bytes) {
        return std::unexpected(std::string(label) + " exceeds maximum size: " + path);
    }

    file.seekg(0, std::ios::beg);
    std::vector<Byte> data(static_cast<std::size_t>(size));
    if (!data.empty()) {
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file) {
            return std::unexpected("Failed to read " + std::string(label) + ": " + path);
        }
    }
    return data;
}

/// Publishes the completed stream through an exclusively created sibling file.
[[nodiscard]] Result<void> write_file_atomic(const std::filesystem::path& path,
                                             std::string_view label,
                                             const std::function<void(std::ostream&)>& write);

[[nodiscard]] inline Result<void> write_binary_file_atomic(const std::string& path,
                                                           std::span<const Byte> data,
                                                           std::string_view label) {
    return write_file_atomic(path, label, [data](std::ostream& file) {
        if (!data.empty())
            file.write(reinterpret_cast<const char*>(data.data()),
                       static_cast<std::streamsize>(data.size()));
    });
}

} // namespace mapperbus::core
