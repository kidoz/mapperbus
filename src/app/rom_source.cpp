#include "app/rom_source.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <cctype>
#include <memory>
#include <set>
#include <utility>

#include "app/state_storage.hpp"

namespace mapperbus::app {
namespace {
using ArchiveReader = std::unique_ptr<archive, decltype(&archive_read_free)>;
constexpr std::size_t kMaxMemberNameBytes = 4096;
constexpr std::size_t kMaxMemberNamesBytes = 1024UZ * 1024;

std::string lower_extension(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    for (auto& ch : extension) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }
    return extension;
}

std::string archive_error(archive* reader) {
    const auto* message = archive_error_string(reader);
    return std::string("Cannot read ROM archive: ") +
           (message != nullptr ? message : "unknown error");
}

core::Result<ArchiveReader> open_archive(const std::string& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        return std::unexpected("Cannot open ROM archive: " + path + ": " + error.message());
    }
    if (size > kMaxArchiveBytes) {
        return std::unexpected("ROM archive exceeds maximum size (256 MiB)");
    }
    ArchiveReader reader(archive_read_new(), archive_read_free);
    if (!reader) {
        return std::unexpected("Cannot allocate ROM archive reader");
    }
    // Enable only container formats we expose; no external filter programs.
    if (archive_read_support_format_zip(reader.get()) != ARCHIVE_OK ||
        archive_read_support_format_7zip(reader.get()) != ARCHIVE_OK ||
        archive_read_open_filename(reader.get(), path.c_str(), 64UZ * 1024) != ARCHIVE_OK) {
        return std::unexpected(archive_error(reader.get()));
    }
    return reader;
}

struct ArchiveContents {
    std::vector<std::string> members;
    std::vector<core::Byte> data;
    bool found = false;
};

core::Result<ArchiveContents> scan_archive(const std::string& path, std::string_view selected) {
    auto opened = open_archive(path);
    if (!opened) {
        return std::unexpected(opened.error());
    }
    auto& reader = *opened;
    ArchiveContents contents;
    std::set<std::string> names;
    std::size_t entry_count = 0;
    std::size_t name_bytes = 0;
    std::uintmax_t declared_bytes = 0;
    archive_entry* entry = nullptr;
    while (true) {
        const int status = archive_read_next_header(reader.get(), &entry);
        if (status == ARCHIVE_EOF) {
            break;
        }
        if (status != ARCHIVE_OK) {
            return std::unexpected(archive_error(reader.get()));
        }
        if (++entry_count > kMaxArchiveEntries) {
            return std::unexpected("ROM archive exceeds maximum entry count (1024)");
        }
        if (archive_entry_is_encrypted(entry) > 0) {
            return std::unexpected("Encrypted ROM archives are not supported");
        }
        const char* pathname = archive_entry_pathname_utf8(entry);
        if (pathname == nullptr) {
            pathname = archive_entry_pathname(entry);
        }
        if (pathname == nullptr) {
            return std::unexpected("ROM archive has an unreadable member name");
        }
        const std::string name(pathname);
        name_bytes += name.size();
        if (name.size() > kMaxMemberNameBytes || name_bytes > kMaxMemberNamesBytes) {
            return std::unexpected("ROM archive member names exceed maximum size");
        }
        const auto declared_size = archive_entry_size(entry);
        if (declared_size < 0 ||
            static_cast<std::uintmax_t>(declared_size) > kMaxArchiveBytes - declared_bytes) {
            return std::unexpected("ROM archive contents exceed maximum size (256 MiB)");
        }
        declared_bytes += static_cast<std::uintmax_t>(declared_size);
        const bool candidate =
            archive_entry_filetype(entry) == AE_IFREG && archive_entry_hardlink(entry) == nullptr &&
            archive_entry_symlink(entry) == nullptr && lower_extension(name) == ".nes";
        if (candidate) {
            if (!names.insert(name).second) {
                return std::unexpected("ROM archive contains duplicate member names: " + name);
            }
            contents.members.push_back(name);
        }
        if (candidate && name == selected) {
            if (std::cmp_greater(declared_size, core::kMaxRomBytes)) {
                return std::unexpected("Archived ROM exceeds maximum size (16 MiB)");
            }
            std::array<core::Byte, 64UZ * 1024> buffer{};
            while (true) {
                const auto count = archive_read_data(reader.get(), buffer.data(), buffer.size());
                if (count < 0) {
                    return std::unexpected(archive_error(reader.get()));
                }
                if (count == 0) {
                    break;
                }
                const auto bytes = static_cast<std::size_t>(count);
                if (bytes > core::kMaxRomBytes - contents.data.size()) {
                    return std::unexpected("Archived ROM exceeds maximum size (16 MiB)");
                }
                contents.data.insert(contents.data.end(), buffer.begin(), buffer.begin() + bytes);
            }
            if (contents.data.size() != static_cast<std::uintmax_t>(declared_size)) {
                return std::unexpected("Archived ROM size does not match its header");
            }
            contents.found = true;
        } else if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) {
            return std::unexpected(archive_error(reader.get()));
        }
    }
    if (archive_read_close(reader.get()) != ARCHIVE_OK) {
        return std::unexpected(archive_error(reader.get()));
    }
    return contents;
}
} // namespace

bool is_rom_archive(const std::filesystem::path& path) {
    const auto extension = lower_extension(path);
    return extension == ".zip" || extension == ".7z";
}

core::Result<std::vector<std::string>> list_archive_roms(const std::string& path) {
    auto contents = scan_archive(path, {});
    if (!contents) {
        return std::unexpected(contents.error());
    }
    return std::move(contents->members);
}

core::Result<std::string> load_rom_source(core::Emulator& emulator,
                                          const std::string& path,
                                          std::string_view member) {
    if (!is_rom_archive(path)) {
        if (!member.empty()) {
            return std::unexpected("A ROM member can only be selected in a .zip or .7z archive");
        }
        auto loaded = emulator.load_cartridge(path);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        return std::string{};
    }
    std::string selected(member);
    if (selected.empty()) {
        auto members = list_archive_roms(path);
        if (!members) {
            return std::unexpected(members.error());
        }
        if (members->empty()) {
            return std::unexpected("ROM archive contains no .nes files");
        }
        if (members->size() != 1) {
            std::string message = "ROM archive contains multiple ROMs; select one with --entry:";
            for (const auto& name : *members) {
                message += "\n  " + name;
            }
            return std::unexpected(message);
        }
        selected = members->front();
    }
    auto contents = scan_archive(path, selected);
    if (!contents) {
        return std::unexpected(contents.error());
    }
    if (!contents->found) {
        return std::unexpected("ROM archive member not found: " + selected);
    }
    auto cartridge = core::Cartridge::from_data(contents->data, selected);
    if (!cartridge) {
        return std::unexpected(cartridge.error());
    }
    const auto battery_path = mapperbus_data_directory() / "battery" /
                              (core::sha256_hex(cartridge->content_hash()) + ".sav");
    if (cartridge->has_battery()) {
        auto created = ensure_state_directory(battery_path.parent_path());
        if (!created) {
            return std::unexpected(created.error());
        }
    }
    auto loaded = emulator.load_cartridge(std::move(*cartridge), battery_path.string());
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    return selected;
}

} // namespace mapperbus::app
