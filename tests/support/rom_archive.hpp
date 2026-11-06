#pragma once

#include <archive.h>
#include <archive_entry.h>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/types.hpp"

namespace mapperbus::tests {
using core::Byte;

struct ArchiveMember {
    std::string name;
    std::vector<Byte> bytes;
    bool symlink = false;
};

inline void write_archive(const std::filesystem::path& path,
                          const std::vector<ArchiveMember>& members,
                          bool seven_zip = false,
                          bool encrypt = false) {
    std::unique_ptr<archive, decltype(&archive_write_free)> writer(archive_write_new(),
                                                                   archive_write_free);
    REQUIRE(writer);
    REQUIRE((seven_zip ? archive_write_set_format_7zip(writer.get())
                       : archive_write_set_format_zip(writer.get())) == ARCHIVE_OK);
    if (!seven_zip) {
        REQUIRE(archive_write_set_options(writer.get(), "zip:compression=store") == ARCHIVE_OK);
    }
    if (encrypt) {
        REQUIRE(archive_write_set_passphrase(writer.get(), "test-password") == ARCHIVE_OK);
        REQUIRE(archive_write_set_options(writer.get(), "zip:encryption=traditional") ==
                ARCHIVE_OK);
    }
    REQUIRE(archive_write_open_filename(writer.get(), path.string().c_str()) == ARCHIVE_OK);
    for (const auto& member : members) {
        std::unique_ptr<archive_entry, decltype(&archive_entry_free)> entry(archive_entry_new(),
                                                                            archive_entry_free);
        REQUIRE(entry);
        archive_entry_set_pathname(entry.get(), member.name.c_str());
        archive_entry_set_filetype(entry.get(), member.symlink ? AE_IFLNK : AE_IFREG);
        archive_entry_set_perm(entry.get(), 0644);
        archive_entry_set_size(entry.get(), static_cast<la_int64_t>(member.bytes.size()));
        if (member.symlink) {
            archive_entry_set_symlink(entry.get(), "game.nes");
        }
        REQUIRE(archive_write_header(writer.get(), entry.get()) == ARCHIVE_OK);
        if (!member.bytes.empty()) {
            REQUIRE(archive_write_data(writer.get(), member.bytes.data(), member.bytes.size()) ==
                    static_cast<la_ssize_t>(member.bytes.size()));
        }
    }
    REQUIRE(archive_write_close(writer.get()) == ARCHIVE_OK);
}

} // namespace mapperbus::tests
