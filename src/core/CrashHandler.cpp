#include "CrashHandler.hpp"

#include <exception>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#if defined(__unix__) || defined(__APPLE__)
#define ORION_CRASH_POSIX_BACKTRACE
#include <cxxabi.h>
#include <dlfcn.h>
#include <execinfo.h>
#endif

static constexpr int BACKTRACE_MAX = 128;

void CrashHandler::install()
{
#if defined(_WIN32)
    signal(SIGSEGV, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGFPE, signalHandler);
    signal(SIGILL, signalHandler);
    signal(SIGTERM, signalHandler);
#else
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigfillset(&sa.sa_mask);

    // For fatal signals, restore default so core dump works
    sa.sa_flags = SA_RESETHAND;
    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGBUS, &sa, nullptr);
    sigaction(SIGFPE, &sa, nullptr);
    sigaction(SIGILL, &sa, nullptr);

    // SIGTERM: graceful, no reset needed
    sa.sa_flags = 0;
    sigaction(SIGTERM, &sa, nullptr);
#endif

    // C++ terminate handler for unhandled exceptions
    std::set_terminate(terminateHandler);
}

void CrashHandler::signalHandler(int sig)
{
    fprintf(stderr, "\n═══════════════════════════════════════════\n");
    fprintf(stderr, "  APP CRASH DETECTED\n");
    fprintf(stderr, "  Signal: %s (%d)\n", signalName(sig), sig);
    fprintf(stderr, "═══════════════════════════════════════════\n");

    if (sig == SIGTERM) {
        fprintf(stderr, "  (exiting)\n\n");
        std::_Exit(0);
    }

    printBacktrace();

    // Restore default handler and re-raise to get core dump
    signal(sig, SIG_DFL);
    raise(sig);
}

void CrashHandler::printBacktrace()
{
#ifdef ORION_CRASH_POSIX_BACKTRACE
    void* buffer[BACKTRACE_MAX];
    int count = backtrace(buffer, BACKTRACE_MAX);

    char** symbols = backtrace_symbols(buffer, count);
    if (!symbols) {
        fprintf(stderr, "  (no symbols available)\n");
        return;
    }

    fprintf(stderr, "\nStack trace (%d frames):\n", count);
    for (int i = 0; i < count; ++i) {
        Dl_info info;
        if (dladdr(buffer[i], &info) && info.dli_sname
            && info.dli_sname[0] != '\0')
        {
            int status = 0;
            char* demangled = abi::__cxa_demangle(
                info.dli_sname, nullptr, nullptr, &status);
            const char* name = (status == 0 && demangled)
                ? demangled : info.dli_sname;

            // Compute offset from symbol address
            ptrdiff_t offset = static_cast<const char*>(buffer[i])
                             - static_cast<const char*>(info.dli_saddr);

            fprintf(stderr, "  #%02d  %s+0x%tx\n", i, name, offset);
            if (info.dli_fname && info.dli_fname[0] != '\0')
                fprintf(stderr, "        (%s)\n", info.dli_fname);

            free(demangled);
        } else {
            fprintf(stderr, "  #%02d  %s\n", i, symbols[i]);
        }
    }

    free(symbols);
    fprintf(stderr, "\n");
#else
    fprintf(stderr, "\nStack trace unavailable on this platform.\n\n");
#endif
}

void CrashHandler::terminateHandler()
{
    fprintf(stderr, "\n═══════════════════════════════════════════\n");
    fprintf(stderr, "  APP CRASH DETECTED\n");
    fprintf(stderr, "  Type: Unhandled C++ exception\n");

    try {
        if (auto eptr = std::current_exception())
            std::rethrow_exception(eptr);
    } catch (const std::exception& e) {
        fprintf(stderr, "  what(): %s\n", e.what());
    } catch (...) {
        fprintf(stderr, "  what(): (unknown type)\n");
    }

    fprintf(stderr, "═══════════════════════════════════════════\n");
    printBacktrace();
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
