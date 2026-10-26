#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
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

/// Atomically replaces a binary file through a same-directory temporary file.
[[nodiscard]] inline Result<void> write_binary_file_atomic(const std::string& path,
                                                           std::span<const Byte> data,
                                                           std::string_view label) {
    const std::filesystem::path destination(path);
    const auto temporary = destination.parent_path() / (destination.filename().string() + ".tmp");
    const auto fail = [&](std::string_view action) {
        std::error_code cleanup_error;
        std::filesystem::remove(temporary, cleanup_error);
        return std::unexpected("Failed to " + std::string(action) + " " + std::string(label) +
                               ": " + path);
    };

    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
        return std::unexpected("Failed to open " + std::string(label) + " for writing: " + path);
    }
    if (!data.empty()) {
        file.write(reinterpret_cast<const char*>(data.data()),
                   static_cast<std::streamsize>(data.size()));
    }
    if (!file) {
        return fail("write");
    }
    file.close();
    if (!file) {
        return fail("close");
    }

    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    if (error) {
        return fail("replace");
    }
    return {};
}

} // namespace mapperbus::core
