#pragma once

class CrashHandler {
public:
    static void install();

    // Records a short free-form marker in a fixed-size ring that the signal
    // handler dumps after the stack trace. Breadcrumbs exist for the crashes a
    // stack trace alone does not explain — a use-after-free reports the frame
    // that touched the freed object, never the one that freed it — so the
    // teardown paths (tab close, document/canvas destruction) drop a marker as
    // they go. The text is NOT copied: pass a string literal only.
    static void breadcrumb(const char* what, long long detail = 0);

private:
    static void signalHandler(int sig, void* info, void* context);
    static void terminateHandler();
    // Async-signal-safe: write(2) only, no malloc. See the definition.
    static void printBacktrace();
    static void printBreadcrumbs();
    static const char* signalName(int sig);
};
