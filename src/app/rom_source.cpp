#include "app/rom_source.hpp"

#include <archive.h>
#include <archive_entry.h>
#include <array>
#include <bit>
#include <cctype>
#include <memory>
#include <set>
#include <utility>

#include "app/state_storage.hpp"
#include "core/binary_file.hpp"

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

bool cancelled(const RomReadControl& control, ArchiveReadProgress progress) {
    if (control.stop.stop_requested())
        return true;
    if (control.observe)
        control.observe(progress);
    return control.stop.stop_requested();
}

auto cancelled_error() {
    return std::unexpected(std::string(kRomLoadCancelled));
}

core::Result<ArchiveReader> open_archive(const std::string& path, const RomReadControl& control) {
    if (cancelled(control, {ArchiveReadPhase::Open, 0, 0}))
        return cancelled_error();
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (control.stop.stop_requested())
        return cancelled_error();
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
        if (control.stop.stop_requested())
            return cancelled_error();
        return std::unexpected(archive_error(reader.get()));
    }
    if (control.stop.stop_requested())
        return cancelled_error();
    return reader;
}

struct ArchiveContents {
    std::vector<std::string> members;
    std::vector<core::Byte> data;
    bool found = false;
};

core::Result<ArchiveContents> scan_archive(const std::string& path,
                                           std::string_view selected,
                                           const RomReadControl& control) {
    auto opened = open_archive(path, control);
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
        if (cancelled(control, {ArchiveReadPhase::Header, entry_count, contents.data.size()}))
            return cancelled_error();
        const int status = archive_read_next_header(reader.get(), &entry);
        if (control.stop.stop_requested())
            return cancelled_error();
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
        const bool candidate = archive_entry_filetype(entry) == AE_IFREG &&
                               archive_entry_hardlink(entry) == nullptr &&
                               archive_entry_symlink(entry) == nullptr &&
                               (lower_extension(name) == ".nes" || lower_extension(name) == ".fds");
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
                if (cancelled(control, {ArchiveReadPhase::Data, entry_count, contents.data.size()}))
                    return cancelled_error();
                const auto count = archive_read_data(reader.get(), buffer.data(), buffer.size());
                if (control.stop.stop_requested())
                    return cancelled_error();
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
        } else {
            if (cancelled(control, {ArchiveReadPhase::Skip, entry_count, contents.data.size()}))
                return cancelled_error();
            const auto skipped = archive_read_data_skip(reader.get());
            if (control.stop.stop_requested())
                return cancelled_error();
            if (skipped != ARCHIVE_OK)
                return std::unexpected(archive_error(reader.get()));
        }
    }
    if (cancelled(control, {ArchiveReadPhase::Close, entry_count, contents.data.size()}))
        return cancelled_error();
    const auto closed = archive_read_close(reader.get());
    if (control.stop.stop_requested())
        return cancelled_error();
    if (closed != ARCHIVE_OK) {
        return std::unexpected(archive_error(reader.get()));
    }
    return contents;
}
} // namespace

bool is_rom_archive(const std::filesystem::path& path) {
    const auto extension = lower_extension(path);
    return extension == ".zip" || extension == ".7z";
}

core::Result<std::vector<std::string>> list_archive_roms(const std::string& path,
                                                         const RomReadControl& control) {
    auto contents = scan_archive(path, {}, control);
    if (!contents) {
        return std::unexpected(contents.error());
    }
    return std::move(contents->members);
}

namespace {
core::Result<void> load_fds_source(core::Emulator& emulator,
                                   std::span<const core::Byte> disk,
                                   const std::string& bios_path,
                                   const std::filesystem::path& persistence_directory,
                                   const RomReadControl& control) {
    if (control.stop.stop_requested())
        return cancelled_error();
    const auto bios_file = bios_path.empty() ? persistence_directory / "bios" / "disksys.rom"
                                             : std::filesystem::path(bios_path);
    auto bios = read_fds_bios(bios_file.string());
    if (control.stop.stop_requested())
        return cancelled_error();
    if (!bios) {
        if (control.on_fds_bios_error)
            control.on_fds_bios_error();
        return std::unexpected(
            bios.error() +
            "; choose an 8 KiB BIOS in Settings > General > BIOS or use --fds-bios PATH");
    }
    auto payload = disk;
    if (disk.size() >= 16 && disk[0] == 'F' && disk[1] == 'D' && disk[2] == 'S' && disk[3] == 0x1A)
        payload = disk.subspan(16);
    const auto directory = persistence_directory / "fds";
    if (auto created = ensure_state_directory(directory); !created)
        return created;
    const auto save_path = directory / (core::sha256_hex(core::rom_sha256(payload)) + ".fds-save");
    if (control.stop.stop_requested())
        return cancelled_error();
    return emulator.load_fds(*bios, disk, save_path.string());
}
} // namespace

core::Result<std::vector<core::Byte>> read_fds_bios(const std::string& path) {
    auto bios = core::read_binary_file_capped(path, 8192, "FDS BIOS");
    if (!bios)
        return std::unexpected(bios.error());
    if (bios->size() != 8192)
        return std::unexpected("FDS BIOS must contain exactly 8192 bytes.");
    return bios;
}

core::Result<std::vector<core::Byte>> read_dcs_option_rom(const std::string& path) {
    auto data =
        core::read_binary_file_capped(path, core::kMaxDcsOptionBytes + 16U, "DCS option ROM");
    if (!data)
        return std::unexpected(data.error());
    if (data->size() % 0x4000U != 0U && data->size() >= 4U && (*data)[0U] == 'N' &&
        (*data)[1U] == 'E' && (*data)[2U] == 'S' && (*data)[3U] == 0x1AU) {
        auto header = core::parse_ines_header(*data);
        if (!header)
            return std::unexpected("Invalid DCS option image: " + header.error());
        if (header->has_trainer || header->chr_rom_size != 0U ||
            header->prg_rom_size != data->size() - 16U)
            return std::unexpected("DCS option image must contain only its header and PRG ROM");
        data->erase(data->begin(), data->begin() + 16U);
    }
    if (data->size() < 0x4000U || data->size() > core::kMaxDcsOptionBytes ||
        !std::has_single_bit(data->size()))
        return std::unexpected("DCS option ROM must be 16, 32, 64, or 128 KiB of PRG data");
    return data;
}

core::Result<std::string> load_rom_source(core::Emulator& emulator,
                                          const std::string& path,
                                          std::string_view member,
                                          const std::string& fds_bios_path,
                                          const std::filesystem::path& persistence_directory,
                                          const RomReadControl& control,
                                          const std::string& dcs_option_path,
                                          const std::string& datach_save_path) {
    if (control.stop.stop_requested())
        return cancelled_error();
    const auto storage =
        persistence_directory.empty() ? mapperbus_data_directory() : persistence_directory;
    const auto shared_path = [&storage, &datach_save_path](
                                 const core::Cartridge& cartridge) -> core::Result<std::string> {
        if (cartridge.shared_nvram().empty()) {
            if (!datach_save_path.empty())
                return std::unexpected("--datach-save requires mapper 157");
            return std::string{};
        }
        const auto target = datach_save_path.empty() ? storage / "devices" / "datach.sav"
                                                     : std::filesystem::path(datach_save_path);
        if (!target.parent_path().empty()) {
            if (auto made = ensure_state_directory(target.parent_path()); !made)
                return std::unexpected(made.error());
        }
        return target.string();
    };
    std::vector<core::Byte> option;
    if (!dcs_option_path.empty()) {
        auto read = read_dcs_option_rom(dcs_option_path);
        if (control.stop.stop_requested())
            return cancelled_error();
        if (!read)
            return std::unexpected(read.error());
        option = std::move(*read);
    }
    if (!is_rom_archive(path)) {
        if (!member.empty()) {
            return std::unexpected("A ROM member can only be selected in a .zip or .7z archive");
        }
        core::Result<void> loaded;
        if (lower_extension(path) == ".fds") {
            if (!option.empty())
                return std::unexpected("DCS option ROM cannot be attached to an FDS disk");
            auto disk = core::read_binary_file_capped(
                path, core::Fds::kMaxDiskImageBytes + 16, "FDS disk image");
            if (control.stop.stop_requested())
                return cancelled_error();
            if (!disk)
                return std::unexpected(disk.error());
            if (!datach_save_path.empty())
                return std::unexpected("--datach-save requires mapper 157");
            loaded = load_fds_source(emulator, *disk, fds_bios_path, storage, control);
        } else {
            auto cartridge = core::Cartridge::from_file(path, option);
            if (control.stop.stop_requested())
                return cancelled_error();
            if (!cartridge)
                return std::unexpected(cartridge.error());
            auto shared = shared_path(*cartridge);
            if (!shared)
                return std::unexpected(shared.error());
            loaded = emulator.load_cartridge(
                std::move(*cartridge),
                std::filesystem::path(path).replace_extension(".sav").string(),
                *shared);
        }
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        return std::string{};
    }
    std::string selected(member);
    if (selected.empty()) {
        auto members = list_archive_roms(path, control);
        if (!members) {
            return std::unexpected(members.error());
        }
        if (members->empty()) {
            return std::unexpected("ROM archive contains no .nes or .fds files");
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
    auto contents = scan_archive(path, selected, control);
    if (!contents) {
        return std::unexpected(contents.error());
    }
    if (!contents->found) {
        return std::unexpected("ROM archive member not found: " + selected);
    }
    if (lower_extension(selected) == ".fds") {
        if (!option.empty())
            return std::unexpected("DCS option ROM cannot be attached to an FDS disk");
        if (!datach_save_path.empty())
            return std::unexpected("--datach-save requires mapper 157");
        auto loaded = load_fds_source(emulator, contents->data, fds_bios_path, storage, control);
        if (!loaded)
            return std::unexpected(loaded.error());
        return selected;
    }
    if (control.stop.stop_requested())
        return cancelled_error();
    auto cartridge = core::Cartridge::from_data(contents->data, selected, option);
    if (control.stop.stop_requested())
        return cancelled_error();
    if (!cartridge) {
        return std::unexpected(cartridge.error());
    }
    const auto battery_path =
        storage / "battery" / (core::sha256_hex(cartridge->base_content_hash()) + ".sav");
    if (cartridge->has_battery()) {
        auto created = ensure_state_directory(battery_path.parent_path());
        if (!created) {
            return std::unexpected(created.error());
        }
    }
    // The synchronous install is a commit boundary. Cancellation after it is
    // handled by the staging loader, never reported as a rollback of this machine.
    if (control.stop.stop_requested())
        return cancelled_error();
    auto shared = shared_path(*cartridge);
    if (!shared)
        return std::unexpected(shared.error());
    auto loaded = emulator.load_cartridge(std::move(*cartridge), battery_path.string(), *shared);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    return selected;
}

} // namespace mapperbus::app
