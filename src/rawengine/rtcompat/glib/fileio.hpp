#pragma once
// rtcompat/glib/fileio.hpp
//
// Stand-in for <glib/gstdio.h>: g_fopen/g_open/g_stat/g_remove exist
// upstream specifically because they take a UTF-8 path and, on Windows,
// convert it to UTF-16 before calling _wfopen/_wopen/_wstat64/_wremove —
// plain fopen() et al. take the current ANSI codepage on Windows, which
// silently mangles non-ASCII (accented, CJK, …) paths. Replacing these
// with bare fopen() would reintroduce exactly that bug on Windows, so the
// shim replicates the real behaviour: POSIX calls straight through,
// Windows goes through rtcompat::utf8ToUtf16 first.
//
// Also hosts g_file_read_link() (glib/gstdio.h-adjacent; used by
// dynamicprofile.cc to resolve a symlinked config file before saving).
#include <cstdio>

#if defined(_WIN32)
#include <sys/stat.h>
using GStatBuf = struct __stat64;
#else
#include <sys/stat.h>
using GStatBuf = struct stat;
#endif

FILE* g_fopen(const char* filename, const char* mode);
int g_open(const char* filename, int flags, int mode = 0);
int g_stat(const char* filename, GStatBuf* buf);
int g_remove(const char* filename);

// Returns a malloc()'d copy of the symlink target, or nullptr if
// `filename` is not a symlink (or on any other failure). `error` is
// accepted for source compatibility (every call site in the vendored tree
// passes NULL) and is never written.
char* g_file_read_link(const char* filename, void* error);
