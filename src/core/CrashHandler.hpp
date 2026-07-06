#pragma once

class CrashHandler {
public:
    static void install();

private:
    static void signalHandler(int sig);
    static void terminateHandler();
    static void printBacktrace();
    static const char* signalName(int sig);
};
