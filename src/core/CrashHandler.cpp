#include "CrashHandler.hpp"

#include <atomic>
#include <exception>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#define ORION_CRASH_POSIX_BACKTRACE
#include <execinfo.h>
#include <unistd.h>
#endif

static constexpr int kBacktraceMax = 128;
static constexpr int kBreadcrumbCount = 64;

namespace {

struct Breadcrumb {
    const char* what = nullptr;
    long long detail = 0;
};

// Lock-free by construction: only the UI thread writes markers in practice, and
// the signal handler reads whatever happens to be there. A torn read is
// acceptable — this is diagnostics, not bookkeeping.
Breadcrumb g_breadcrumbs[kBreadcrumbCount];
std::atomic<unsigned> g_breadcrumbNext{0};

#ifdef ORION_CRASH_POSIX_BACKTRACE

// ── Why this file avoids stdio inside the handler ──────────────────────────
// The previous version reported through fprintf + backtrace_symbols +
// __cxa_demangle, all of which allocate. A crash caused by heap corruption
// (double free, overrun) then kills the handler inside malloc before it prints
// anything, which is exactly the "banner, then nothing" output this code used
// to produce. write(2) is async-signal-safe and allocates nothing, so the
// report survives a corrupted heap, a corrupted stack and a held malloc lock.
void writeStr(const char* s)
{
    if (!s) return;
    const size_t len = strlen(s);
    ssize_t written = 0;
    while (written < static_cast<ssize_t>(len)) {
        const ssize_t n = write(STDERR_FILENO, s + written,
                                len - static_cast<size_t>(written));
        if (n <= 0) return;
        written += n;
    }
}

void writeHex(unsigned long long value)
{
    char buf[19] = "0x";
    int at = 2;
    bool leading = true;
    for (int shift = 60; shift >= 0; shift -= 4) {
        const int digit = static_cast<int>((value >> shift) & 0xF);
        if (digit == 0 && leading && shift > 0)
            continue;
        leading = false;
        buf[at++] = "0123456789abcdef"[digit];
    }
    buf[at] = '\0';
    writeStr(buf);
}

void writeDec(long long value)
{
    char buf[24];
    int at = sizeof(buf);
    buf[--at] = '\0';
    const bool negative = value < 0;
    unsigned long long magnitude = negative
        ? ~static_cast<unsigned long long>(value) + 1ULL
        : static_cast<unsigned long long>(value);
    do {
        buf[--at] = static_cast<char>('0' + (magnitude % 10));
        magnitude /= 10;
    } while (magnitude != 0 && at > 1);
    if (negative)
        buf[--at] = '-';
    writeStr(buf + at);
}

#else
// Windows has no async-signal-safety contract to honour here, so plain stdio
// (flushed immediately) is the equivalent.
void writeStr(const char* s)
{
    if (!s) return;
    fputs(s, stderr);
    fflush(stderr);
}

void writeHex(unsigned long long value)
{
    fprintf(stderr, "0x%llx", value);
    fflush(stderr);
}

void writeDec(long long value)
{
    fprintf(stderr, "%lld", value);
    fflush(stderr);
}
#endif

} // namespace

void CrashHandler::breadcrumb(const char* what, long long detail)
{
    const unsigned slot = g_breadcrumbNext.fetch_add(1, std::memory_order_relaxed);
    Breadcrumb& crumb = g_breadcrumbs[slot % kBreadcrumbCount];
    crumb.what = what;
    crumb.detail = detail;
}

void CrashHandler::install()
{
#if defined(_WIN32)
    const auto legacy = [](int sig) { signalHandler(sig, nullptr, nullptr); };
    signal(SIGSEGV, legacy);
    signal(SIGABRT, legacy);
    signal(SIGFPE, legacy);
    signal(SIGILL, legacy);
    signal(SIGTERM, legacy);
#else
    // A stack overflow (runaway recursion) also arrives as SIGSEGV, and the
    // kernel cannot push a signal frame onto the stack that just ran out — the
    // process dies silently unless the handler has a stack of its own. Hence
    // sigaltstack + SA_ONSTACK: without them that whole class of crash reports
    // nothing at all.
    // A literal rather than SIGSTKSZ: glibc 2.34 turned SIGSTKSZ into a
    // sysconf() call, so it is no longer usable as an array bound.
    static char alternateStack[512 * 1024];
    stack_t altStack;
    altStack.ss_sp = alternateStack;
    altStack.ss_size = sizeof(alternateStack);
    altStack.ss_flags = 0;
    sigaltstack(&altStack, nullptr);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = [](int sig, siginfo_t* info, void* context) {
        signalHandler(sig, info, context);
    };
    sigfillset(&sa.sa_mask);

    // SA_RESETHAND so the re-raise at the end reaches the default handler and
    // still produces a core dump.
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);

    // SIGTERM: graceful, no reset needed
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigaction(SIGTERM, &sa, nullptr);
#endif

#ifdef ORION_CRASH_POSIX_BACKTRACE
    // glibc's backtrace() lazily dlopens libgcc_s on its first call, which
    // allocates. Called for the first time from inside the handler, a crash
    // caused by heap corruption dies right there and reports nothing — so warm
    // it up now, while the heap is still intact.
    void* warmup[4];
    backtrace(warmup, 4);
#endif

    // C++ terminate handler for unhandled exceptions
    std::set_terminate(terminateHandler);
}

void CrashHandler::signalHandler(int sig, void* info, void* context)
{
    (void)context;

    writeStr("\n═══════════════════════════════════════════\n");
    writeStr("  APP CRASH DETECTED\n");
    writeStr("  Signal: ");
    writeStr(signalName(sig));
    writeStr(" (");
    writeDec(sig);
    writeStr(")\n");

#ifdef ORION_CRASH_POSIX_BACKTRACE
    // The faulting address classifies the bug before the stack is even read:
    // 0x0 (or a small offset from it) is a null dereference, while a large
    // garbage value is a freed or corrupted pointer.
    if (info && (sig == SIGSEGV || sig == SIGBUS)) {
        const auto* si = static_cast<const siginfo_t*>(info);
        writeStr("  Fault address: ");
        writeHex(reinterpret_cast<unsigned long long>(si->si_addr));
        if (si->si_addr == nullptr)
            writeStr("  (null dereference)");
        else if (reinterpret_cast<unsigned long long>(si->si_addr) < 0x10000ULL)
            writeStr("  (near-null: member access through a null pointer)");
        writeStr("\n");
    }
#endif
    writeStr("═══════════════════════════════════════════\n");

    if (sig == SIGTERM) {
        writeStr("  (exiting)\n\n");
        std::_Exit(0);
    }

    printBacktrace();
    printBreadcrumbs();

    // Restore default handler and re-raise to get core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

void CrashHandler::printBacktrace()
{
#ifdef ORION_CRASH_POSIX_BACKTRACE
    void* buffer[kBacktraceMax];
    const int count = backtrace(buffer, kBacktraceMax);

    writeStr("\nStack trace (");
    writeDec(count);
    writeStr(" frames):\n");

    // backtrace_symbols_fd resolves out of the ELF symbol tables and writes
    // straight to the descriptor — no malloc, unlike backtrace_symbols. Names
    // come out mangled (pipe through `c++filt`), and a frame with no dynamic
    // symbol prints as `Hazor(+0xOFFSET)` — that offset is module-relative, so
    // it feeds addr2line directly even though the binary is position
    // independent. The absolute addresses below are only useful alongside
    // /proc/<pid>/maps, so the offsets are the ones to reach for.
    backtrace_symbols_fd(buffer, count, STDERR_FILENO);

    writeStr("\n  Symbolize with:"
             " addr2line -Cfpie build/linux-x86_64-debug/Hazor <the +0x… offsets above>\n");

    writeStr("\nAbsolute addresses:\n ");
    for (int i = 0; i < count; ++i) {
        writeStr(" ");
        writeHex(reinterpret_cast<unsigned long long>(buffer[i]));
    }
    writeStr("\n");
#else
    writeStr("\nStack trace unavailable on this platform.\n\n");
#endif
}

void CrashHandler::printBreadcrumbs()
{
    const unsigned total = g_breadcrumbNext.load(std::memory_order_relaxed);
    if (total == 0)
        return;

    const unsigned shown = total < kBreadcrumbCount ? total : kBreadcrumbCount;
    writeStr("\nBreadcrumbs (oldest first, ");
    writeDec(shown);
    writeStr(" of ");
    writeDec(total);
    writeStr(" recorded):\n");

    for (unsigned i = total - shown; i < total; ++i) {
        const Breadcrumb& crumb = g_breadcrumbs[i % kBreadcrumbCount];
        writeStr("  ");
        writeDec(static_cast<long long>(i));
        writeStr("  ");
        writeStr(crumb.what ? crumb.what : "(null)");
        if (crumb.detail != 0) {
            writeStr("  detail=");
            writeDec(crumb.detail);
        }
        writeStr("\n");
    }
    writeStr("\n");
}

void CrashHandler::terminateHandler()
{
    writeStr("\n═══════════════════════════════════════════\n");
    writeStr("  APP CRASH DETECTED\n");
    writeStr("  Type: Unhandled C++ exception\n");

    // Safe here, unlike in the signal handler: terminate() runs on a healthy
    // stack with the heap in whatever state the throw left it.
    try {
        if (auto eptr = std::current_exception())
            std::rethrow_exception(eptr);
    } catch (const std::exception& e) {
        writeStr("  what(): ");
        writeStr(e.what());
        writeStr("\n");
    } catch (...) {
        writeStr("  what(): (unknown type)\n");
    }

    writeStr("═══════════════════════════════════════════\n");
    printBacktrace();
    printBreadcrumbs();
    std::abort();
}

const char* CrashHandler::signalName(int sig)
{
    switch (sig) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (abort)";
#ifdef SIGBUS
    case SIGBUS:  return "SIGBUS (bus error)";
#endif
    case SIGFPE:  return "SIGFPE (floating point exception)";
    case SIGILL:  return "SIGILL (illegal instruction)";
    case SIGTERM: return "SIGTERM (termination request)";
    default:      return "UNKNOWN";
    }
}
