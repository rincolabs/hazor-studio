#pragma once

#include <QString>
#include <QVector>

#include <cstdint>

// Backend/device discovery for the local Stable Diffusion module.
//
// Three different things are easy to confuse here, and conflating them is how a
// CPU run gets reported as GPU acceleration:
//
//   * the COMPILED backends — which ggml backends were built into the package
//                             this binary links (HAZOR_SD_BACKENDS at configure
//                             time, normally CPU + Vulkan + CUDA). A fixed
//                             property of the build; it cannot change at run
//                             time and it proves nothing about the machine.
//   * the AVAILABLE devices — what ggml actually finds on THIS machine now.
//                             The backends ship as separate dlopen'd modules,
//                             so a CUDA module with no NVIDIA driver present,
//                             or a Vulkan module with no loader, simply does
//                             not register and its devices never appear.
//   * the EFFECTIVE device  — the one the loaded context was built against and
//                             the one the sampling really ran on. The only
//                             honest answer to "was that GPU accelerated?".
//
// Everything in this header is a query: nothing loads a model or creates a
// context. Safe to call before a context exists, which is what lets the runtime
// resolve a device before paying for a 4 GiB load.
namespace sdlocal {

// A ggml backend that may be compiled into the package this binary links.
enum class SdCompiledBackend { Cpu, Vulkan, Cuda };

// What a request asks for. Auto prefers a GPU when the build and the machine
// both have one, which is the sensible default; Cpu pins the CPU device even on
// a GPU build (the escape hatch when a GPU run misbehaves).
enum class SdDevicePreference { Auto, Gpu, Cpu };

// One ggml device as the library reports it on this machine.
struct SdDevice {
    // The ggml device name ("CPU", "Vulkan0", "CUDA0"). This is the exact token
    // sd_ctx_params_t::backend accepts, so it is carried around rather than a
    // friendlier label: resolving once and pinning the name is what makes the
    // effective device knowable instead of a guess.
    QString name;
    QString description;   // "NVIDIA GeForce GTX 1050 Ti", "llvmpipe", ...
    bool gpu = false;      // discrete GPU, integrated GPU or accelerator
    uint64_t freeBytes = 0;
    uint64_t totalBytes = 0;

    bool isValid() const { return !name.isEmpty(); }
};

// The outcome of resolving a preference against the devices actually present.
struct SdDeviceChoice {
    SdDevice device;
    bool gpuRequested = false;   // the preference asked for (or would accept) a GPU
    bool gpuAvailable = false;   // this build found at least one GPU device
    bool downgraded = false;     // a GPU was asked for and a CPU device was chosen
    QString reason;              // why, in one line, for the log and the result

    bool isValid() const { return device.isValid(); }
};

// Every backend built into the linked package, from HAZOR_SD_BACKENDS. CPU is
// always among them. This is a build property: it says what the binary *can*
// use, never what this machine will actually load.
QVector<SdCompiledBackend> compiledBackends();
bool hasCompiledBackend(SdCompiledBackend backend);
// True when any GPU backend was compiled in, which is the only question most
// callers have. Still not a promise that a GPU is reachable here.
bool hasCompiledGpuBackend();
QString compiledBackendsName();         // "CPU VULKAN CUDA"
QString backendName(SdCompiledBackend backend);

// Every ggml device on this machine, GPUs first. Loads the dynamic ggml backend
// modules on first call, so it is not free — but it does not touch a model.
QVector<SdDevice> availableDevices();

// The CPU device, which ggml always registers. Used by the fallback path: a
// build that cannot reach its GPU has to be able to name a CPU target.
SdDevice cpuDevice();

// The ggml device the user pinned ("CUDA0"), or empty for "resolve by
// preference". A machine-level choice, not a workflow one: a package that
// loads both the CUDA and the Vulkan module would otherwise run on whichever
// ggml happens to register first. Persisted with QSettings.
QString pinnedDeviceName();
void setPinnedDeviceName(const QString& name);

// Resolves `preference` against availableDevices(), honouring the pin above.
// Never returns a GPU that is not there, and never silently calls a CPU device
// a GPU: when a GPU was asked for and none is usable the choice comes back with
// downgraded=true and a reason.
SdDeviceChoice chooseDevice(SdDevicePreference preference);

QString devicePreferenceName(SdDevicePreference preference);
// Parses "auto" / "gpu" / "cpu" (case-insensitive) out of a workflow property.
bool devicePreferenceFromName(const QString& name, SdDevicePreference& out);

// One line naming all three layers, for the log at the start of a run.
QString describeBackendEnvironment();

} // namespace sdlocal
