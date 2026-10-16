#pragma once
// rtcompat/glib/fileutils.hpp
//
// Stand-in for <glibmm/fileutils.h> and <glibmm/miscutils.h>: Glib::Dir,
// Glib::file_test() and the handful of Glib::path_*()/build_filename()
// helpers rtengine calls, all implemented over std::filesystem.
//
// Note: rtlensfun.cc/.h pull in the *real* glib-2.0 (and lensfun.h)
// directly — see CMakeLists.txt's Lensfun::Lensfun target — so its GDir /
// GPatternSpec / g_build_filename() fallback path (taken only when the
// vendored lensfun lacks lfDatabase::LoadDirectory) resolves against the
// genuine GLib C API, not this header. Nothing here needs to match it.
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>

#include "rtcompat/glib/misc.hpp"
#include "rtcompat/glib/ustring.hpp"

namespace Glib {

enum FileTest {
    FILE_TEST_IS_REGULAR = 1 << 0,
    FILE_TEST_IS_SYMLINK = 1 << 1,
    FILE_TEST_IS_DIR = 1 << 2,
    FILE_TEST_IS_EXECUTABLE = 1 << 3,
    FILE_TEST_EXISTS = 1 << 4,
};

inline bool file_test(const std::string& filename, int test)
{
    if (test & FILE_TEST_IS_SYMLINK) {
        std::error_code ec;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(filename, ec)) && !ec) {
            return true;
        }
    }

    std::error_code ec;
    const auto status = std::filesystem::status(filename, ec);
    if (ec) {
        return false;
    }

    if ((test & FILE_TEST_EXISTS) && std::filesystem::exists(status)) {
        return true;
    }
    if ((test & FILE_TEST_IS_DIR) && std::filesystem::is_directory(status)) {
        return true;
    }
    if ((test & FILE_TEST_IS_REGULAR) && std::filesystem::is_regular_file(status)) {
        return true;
    }
    if ((test & FILE_TEST_IS_EXECUTABLE) && std::filesystem::is_regular_file(status)) {
        const auto perms = status.permissions();
        constexpr auto anyExec = std::filesystem::perms::owner_exec | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_exec;
        if ((perms & anyExec) != std::filesystem::perms::none) {
            return true;
        }
    }

    return false;
}

namespace detail {

inline void appendPathPart(std::filesystem::path& result, const std::string& part)
{
    if (!part.empty()) {
        result /= part;
    }
}

} // namespace detail

template <typename... Parts>
ustring build_filename(Parts&&... parts)
{
    std::filesystem::path result;
    (detail::appendPathPart(result, std::string(parts)), ...);
    return ustring(result.generic_string());
}

inline ustring path_get_basename(const std::string& path)
{
    return ustring(std::filesystem::path(path).filename().generic_string());
}

inline ustring path_get_dirname(const std::string& path)
{
    std::filesystem::path dir = std::filesystem::path(path).parent_path();
    return ustring(dir.empty() ? std::string(".") : dir.generic_string());
}

inline bool path_is_absolute(const std::string& path)
{
    return std::filesystem::path(path).is_absolute();
}

class DirIterator {
public:
    DirIterator() = default;
    explicit DirIterator(std::filesystem::directory_iterator it) : it_(std::move(it)) {}

    ustring operator*() const { return ustring(it_->path().filename().generic_string()); }
    DirIterator& operator++()
    {
        std::error_code ec;
        it_.increment(ec);
        return *this;
    }
    bool operator!=(const DirIterator& other) const { return it_ != other.it_; }
    bool operator==(const DirIterator& other) const { return it_ == other.it_; }

private:
    std::filesystem::directory_iterator it_;
};

class Dir {
public:
    explicit Dir(const std::string& path) : path_(path)
    {
        std::error_code ec;
        std::filesystem::directory_iterator probe(path_, ec);
        if (ec) {
            throw FileError(FileError::ERROR_NOENT, "Glib::Dir: cannot open directory '" + path_ + "'");
        }
    }

    DirIterator begin() const { return DirIterator(std::filesystem::directory_iterator(path_)); }
    DirIterator end() const { return DirIterator(); }

private:
    std::string path_;
};

} // namespace Glib
