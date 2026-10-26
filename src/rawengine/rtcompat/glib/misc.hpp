#pragma once
// rtcompat/glib/misc.hpp
//
// Catch-all shim for the remaining glibmm/giomm pieces rtengine touches:
// Error/Exception hierarchy (incl. KeyFileError, FileError), ArrayHandle,
// TimeVal (+ the tiny Gio::File::query_info() slice used for mtime
// comparisons in metadata.cc), Thread (+ the two sigc:: call sites that
// feed it) and Regex::match_simple. Maps from <glibmm/arrayhandle.h>,
// <glibmm/thread.h>, <glibmm/timer.h>, <glibmm/regex.h>, <glibmm.h> and
// <giomm.h>.
#include <filesystem>
#include <functional>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "rtcompat/glib/fileio.hpp"
#include "rtcompat/glib/ustring.hpp"

// <winbase.h> and <fileapi.h> define THREAD_PRIORITY_NORMAL and
// FILE_TYPE_UNKNOWN as plain integer macros, and dcraw.cc reaches them
// through winutils.h/leanwindows.h before including this header. The
// enumerators below carry the names real glibmm/giomm use, which rtengine's
// own call sites spell out (Glib::THREAD_PRIORITY_NORMAL,
// Gio::FILE_TYPE_DIRECTORY), so the macros have to go rather than the names:
// left in place they expand mid-enum and the declaration fails to parse.
//
// Nothing in rtengine calls SetThreadPriority() or GetFileType(), which are
// the only reason those macros exist.
#ifdef _WIN32
#undef THREAD_PRIORITY_LOW
#undef THREAD_PRIORITY_NORMAL
#undef THREAD_PRIORITY_HIGH
#undef THREAD_PRIORITY_URGENT
#undef FILE_TYPE_UNKNOWN
#undef FILE_TYPE_REGULAR
#undef FILE_TYPE_DIRECTORY
#undef FILE_TYPE_SYMBOLIC_LINK
#undef FILE_TYPE_SPECIAL
#endif

namespace Glib {

// --- Error hierarchy -------------------------------------------------
//
// Real glibmm's Glib::Exception is its own base (not std::exception) with
// a pure-virtual `ustring what() const`; Glib::Error and the GLib-domain
// error types derive from it. rtengine only ever catches these and either
// ignores them or calls .what()/.code() for a diagnostic printf, so this
// reproduces just that surface.

class Exception {
public:
    virtual ~Exception() = default;
    virtual ustring what() const = 0;
};

class Error : public Exception {
public:
    Error() : code_(0) {}
    Error(int code, const ustring& message) : code_(code), message_(message) {}

    ustring what() const override { return message_; }
    int code() const { return code_; }

private:
    int code_;
    ustring message_;
};

class FileError : public Exception {
public:
    enum Code {
        ERROR_EXIST,
        ERROR_ISDIR,
        ERROR_ACCES,
        ERROR_NAMETOOLONG,
        ERROR_NOENT,
        ERROR_NOTDIR,
        ERROR_NXIO,
        ERROR_NODEV,
        ERROR_ROFS,
        ERROR_TOOMANYLINKS,
        ERROR_FAILED,
    };

    FileError(Code code, const ustring& message) : code_(code), message_(message) {}

    ustring what() const override { return message_; }
    Code code() const { return code_; }

private:
    Code code_;
    ustring message_;
};

class KeyFileError : public Error {
public:
    enum Code {
        UNKNOWN_ENCODING,
        PARSE,
        NOT_FOUND,
        KEY_NOT_FOUND,
        GROUP_NOT_FOUND,
        INVALID_VALUE,
    };

    KeyFileError(Code code, const ustring& message) : Error(static_cast<int>(code), message), code_(code) {}

    Code code() const { return code_; }

private:
    Code code_;
};

// --- ArrayHandle -------------------------------------------------------
//
// Real glibmm's ArrayHandle is a lightweight view that can either own or
// borrow a C array. Here it is just a small value type that normalizes
// everything to a std::vector<T>, which is all rtengine's two use sites
// (procparams.cc channel-mixer triplets, rtthumbnail.cc color matrix, and
// the IPTC string-list save path) need.

enum ArrayHandleOwnership { OWNERSHIP_NONE, OWNERSHIP_SHALLOW, OWNERSHIP_DEEP };

template <typename T>
class ArrayHandle {
public:
    ArrayHandle(const std::vector<T>& v) : data_(v) {}

    template <typename U, typename = std::enable_if_t<std::is_convertible<U, T>::value>>
    ArrayHandle(const std::vector<U>& v) : data_(v.begin(), v.end()) {}

    ArrayHandle(const T* data, std::size_t count, ArrayHandleOwnership /*ownership*/)
        : data_(data, data + count)
    {
    }

    operator std::vector<T>() const { return data_; }

    typename std::vector<T>::const_iterator begin() const { return data_.begin(); }
    typename std::vector<T>::const_iterator end() const { return data_.end(); }
    std::size_t size() const { return data_.size(); }
    const T* data() const { return data_.data(); }

private:
    std::vector<T> data_;
};

// --- TimeVal -------------------------------------------------------------

class TimeVal {
public:
    TimeVal() : sec_(0), usec_(0) {}
    TimeVal(long sec, long usec) : sec_(sec), usec_(usec) {}

    bool operator>=(const TimeVal& other) const
    {
        return sec_ > other.sec_ || (sec_ == other.sec_ && usec_ >= other.usec_);
    }

    long sec_;
    long usec_;
};

// --- Thread --------------------------------------------------------------
//
// Real glibmm's Glib::Thread::create() is deprecated-but-present API over
// GThread; both rtengine call sites (simpleprocess.cc, improccoordinator.cc)
// only ever pass joinable=true, so that is the only path that needs to be
// exact. Thread::join() self-deletes, matching real glibmm semantics (the
// callers never touch the pointer again after join()).

enum ThreadPriority {
    THREAD_PRIORITY_LOW,
    THREAD_PRIORITY_NORMAL,
    THREAD_PRIORITY_HIGH,
    THREAD_PRIORITY_URGENT,
};

class Thread {
public:
    template <typename Slot>
    static Thread* create(Slot slot, bool joinable)
    {
        return create(std::move(slot), 0, joinable, true, THREAD_PRIORITY_NORMAL);
    }

    template <typename Slot>
    static Thread* create(Slot slot, std::size_t /*stackSize*/, bool joinable, bool /*bound*/, ThreadPriority /*priority*/)
    {
        if (joinable) {
            return new Thread(std::move(slot));
        }
        std::thread([slot = std::move(slot)]() mutable { slot(); }).detach();
        return nullptr;
    }

    void join()
    {
        impl_.join();
        delete this;
    }

private:
    template <typename Slot>
    explicit Thread(Slot slot) : impl_([slot = std::move(slot)]() mutable { slot(); })
    {
    }

    std::thread impl_;
};

inline void usleep(unsigned long microseconds)
{
    std::this_thread::sleep_for(std::chrono::microseconds(microseconds));
}

// --- Regex -----------------------------------------------------------

enum RegexCompileFlags { REGEX_CASELESS = 1 << 0 };

class Regex {
public:
    static bool match_simple(const std::string& pattern, const std::string& str, int compileFlags = 0, int /*matchFlags*/ = 0)
    {
        auto flags = std::regex::ECMAScript;
        if (compileFlags & REGEX_CASELESS) {
            flags |= std::regex::icase;
        }
        try {
            return std::regex_search(str, std::regex(pattern, flags));
        } catch (const std::regex_error&) {
            return false;
        }
    }
};

} // namespace Glib

// sigc:: is libsigc++'s namespace; rtengine only uses it to build the two
// background-thread entry points passed to Glib::Thread::create(), so this
// implements exactly that (bind arguments to a free function, or bind an
// object to a zero-arg member function) rather than vendoring libsigc++.
namespace sigc {

template <typename Ret, typename... Args>
auto ptr_fun(Ret (*fn)(Args...))
{
    return fn;
}

template <typename F, typename... BoundArgs>
auto bind(F fn, BoundArgs... args)
{
    return [fn, args...]() { fn(args...); };
}

template <typename T, typename Ret, typename C>
auto mem_fun(T& obj, Ret (C::*fn)())
{
    return [&obj, fn]() { (obj.*fn)(); };
}

} // namespace sigc

// --- giomm.h --------------------------------------------------------------
//
// rtengine's two uses of Gio::File are: metadata.cc's mtime-based EXIF
// cache invalidation (query_info() + modification_time()), and
// ffmanager.cc/dfmanager.cc's flat-field/dark-frame directory scans
// (query_exists(), enumerate_children(), FileInfo::get_name()/
// get_file_type()/is_hidden()). Implemented over std::filesystem plus
// rtcompat/glib/fileio.hpp's g_stat() for the mtime.

#define G_FILE_ATTRIBUTE_TIME_MODIFIED "time::modified"

namespace Gio {

enum FileType {
    FILE_TYPE_UNKNOWN,
    FILE_TYPE_REGULAR,
    FILE_TYPE_DIRECTORY,
    FILE_TYPE_SYMBOLIC_LINK,
    FILE_TYPE_SPECIAL,
};

class Error : public Glib::Error {
public:
    using Glib::Error::Error;
};

class FileInfo {
public:
    FileInfo(std::string name, FileType type, bool hidden, Glib::TimeVal mtime)
        : name_(std::move(name)), type_(type), hidden_(hidden), mtime_(mtime)
    {
    }

    std::string get_name() const { return name_; }
    FileType get_file_type() const { return type_; }
    bool is_hidden() const { return hidden_; }
    Glib::TimeVal modification_time() const { return mtime_; }

private:
    std::string name_;
    FileType type_;
    bool hidden_;
    Glib::TimeVal mtime_;
};

namespace detail {

inline std::shared_ptr<FileInfo> statFileInfo(const std::string& path)
{
    const std::filesystem::path p(path);
    std::error_code ec;
    const auto status = std::filesystem::status(p, ec);

    FileType type = FILE_TYPE_UNKNOWN;
    if (!ec) {
        if (std::filesystem::is_directory(status)) {
            type = FILE_TYPE_DIRECTORY;
        } else if (std::filesystem::is_regular_file(status)) {
            type = FILE_TYPE_REGULAR;
        } else if (std::filesystem::is_symlink(std::filesystem::symlink_status(p, ec))) {
            type = FILE_TYPE_SYMBOLIC_LINK;
        }
    }

    const std::string name = p.filename().string();
    const bool hidden = !name.empty() && name[0] == '.';

    GStatBuf st{};
    Glib::TimeVal mtime;
    if (g_stat(path.c_str(), &st) == 0) {
        mtime = Glib::TimeVal(static_cast<long>(st.st_mtime), 0);
    }

    return std::make_shared<FileInfo>(name, type, hidden, mtime);
}

} // namespace detail

class FileEnumerator {
public:
    explicit FileEnumerator(const std::string& dir)
    {
        std::error_code ec;
        it_ = std::filesystem::directory_iterator(dir, ec);
        if (ec) {
            throw Error(Glib::FileError::ERROR_NOENT, "Gio::File::enumerate_children: cannot open '" + dir + "'");
        }
    }

    std::shared_ptr<FileInfo> next_file()
    {
        if (it_ == std::filesystem::end(it_)) {
            return nullptr;
        }
        auto info = detail::statFileInfo(it_->path().string());
        std::error_code ec;
        it_.increment(ec);
        return info;
    }

private:
    std::filesystem::directory_iterator it_;
};

class File {
public:
    static std::shared_ptr<File> create_for_path(const std::string& path)
    {
        return std::shared_ptr<File>(new File(path));
    }

    bool query_exists() const
    {
        std::error_code ec;
        return std::filesystem::exists(path_, ec);
    }

    std::shared_ptr<FileEnumerator> enumerate_children(const char* /*attributes*/) const
    {
        return std::make_shared<FileEnumerator>(path_);
    }

    // The attribute string is ignored: every attribute rtengine ever
    // requests (name/type/is-hidden/time::modified) is cheap enough to
    // always compute together.
    std::shared_ptr<FileInfo> query_info(const char* /*attributes*/) const
    {
        return detail::statFileInfo(path_);
    }

private:
    explicit File(std::string path) : path_(std::move(path)) {}

    std::string path_;
};

} // namespace Gio
