#include "ai/sd/SdBackend.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QStringList>

#include <algorithm>

// ggml is a PUBLIC link dependency of the stable-diffusion target, so its
// headers resolve here. The device registry is read through ggml rather than
// through sd_list_devices(): that one returns "name<TAB>description" lines and
// would leave the CPU/GPU question to string matching on the device name, which
// is exactly the kind of guess this module exists to avoid. ggml_backend_dev_type()
// is the library's own answer.
#include <ggml-backend.h>

namespace sdlocal {
namespace {

// The backends the linked package was built with, space separated, turned into
// this macro by CMakeLists.txt out of ggml's own GGML_AVAILABLE_BACKENDS. The
// default keeps a build that somehow missed the definition honest: CPU is the
// only backend ggml always has.
#ifndef HAZOR_SD_BACKENDS
#define HAZOR_SD_BACKENDS "CPU"
#endif

// `enum` is required, not stylistic: ggml-backend.h declares both an enum
// ggml_backend_dev_type and a function of the same name, and in C++ the function
// declaration hides the type, so only the elaborated form names it.
bool deviceTypeIsGpu(enum ggml_backend_dev_type type)
{
    switch (type) {
    case GGML_BACKEND_DEVICE_TYPE_GPU:
    case GGML_BACKEND_DEVICE_TYPE_IGPU:
        return true;
    // ACCEL is a BLAS/AMX-style helper for the CPU backend, and META only wraps
    // other devices. Neither is "the GPU ran this", so neither counts.
    case GGML_BACKEND_DEVICE_TYPE_CPU:
    case GGML_BACKEND_DEVICE_TYPE_ACCEL:
    case GGML_BACKEND_DEVICE_TYPE_META:
        break;
    }
    return false;
}

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

} // namespace

QVector<SdCompiledBackend> compiledBackends()
{
    // Parsed once: the macro is a compile-time constant, so the answer cannot
    // change, and chooseDevice() asks for it on every resolve.
    static const QVector<SdCompiledBackend> backends = [] {
        QVector<SdCompiledBackend> out;
        const QStringList names = QString::fromLatin1(HAZOR_SD_BACKENDS)
                                      .split(QLatin1Char(' '), Qt::SkipEmptyParts);
        for (const QString& name : names) {
            if (name.compare(QLatin1String("VULKAN"), Qt::CaseInsensitive) == 0)
                out.push_back(SdCompiledBackend::Vulkan);
            else if (name.compare(QLatin1String("CUDA"), Qt::CaseInsensitive) == 0)
                out.push_back(SdCompiledBackend::Cuda);
            else if (name.compare(QLatin1String("CPU"), Qt::CaseInsensitive) == 0)
                out.push_back(SdCompiledBackend::Cpu);
        }
        // ggml cannot run an operator without a CPU backend, and the package
        // is never built without one, so an empty or unparsable list means the
        // define is wrong rather than that the CPU is really absent.
        if (out.isEmpty())
            out.push_back(SdCompiledBackend::Cpu);
        return out;
    }();
    return backends;
}

bool hasCompiledBackend(SdCompiledBackend backend)
{
    const QVector<SdCompiledBackend> backends = compiledBackends();
    return std::find(backends.begin(), backends.end(), backend) != backends.end();
}

bool hasCompiledGpuBackend()
{
    return hasCompiledBackend(SdCompiledBackend::Vulkan)
        || hasCompiledBackend(SdCompiledBackend::Cuda);
}

QString backendName(SdCompiledBackend backend)
{
    switch (backend) {
    case SdCompiledBackend::Vulkan: return QStringLiteral("VULKAN");
    case SdCompiledBackend::Cuda:   return QStringLiteral("CUDA");
    case SdCompiledBackend::Cpu:    break;
    }
    return QStringLiteral("CPU");
}

QString compiledBackendsName()
{
    QStringList names;
    for (SdCompiledBackend backend : compiledBackends())
        names << backendName(backend);
    return names.join(QLatin1Char(' '));
}

QVector<SdDevice> availableDevices()
{
    // Dynamic-backend builds register their modules lazily; without this a
    // Vulkan build reports the CPU device only and the GPU looks absent.
    // ggml_backend_load_all() is idempotent but not free, so it runs once the
    // registry is still empty rather than on every query.
    if (ggml_backend_dev_count() == 0)
        ggml_backend_load_all();

    QVector<SdDevice> out;
    const size_t count = ggml_backend_dev_count();
    out.reserve(static_cast<int>(count));
    for (size_t i = 0; i < count; ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        if (!dev)
            continue;
        SdDevice d;
        const char* name = ggml_backend_dev_name(dev);
        const char* desc = ggml_backend_dev_description(dev);
        d.name = name ? QString::fromUtf8(name) : QString();
        d.description = desc ? QString::fromUtf8(desc) : QString();
        d.gpu = deviceTypeIsGpu(ggml_backend_dev_type(dev));
        size_t freeBytes = 0;
        size_t totalBytes = 0;
        ggml_backend_dev_memory(dev, &freeBytes, &totalBytes);
        d.freeBytes = freeBytes;
        d.totalBytes = totalBytes;
        if (d.isValid())
            out.push_back(d);
    }

    // GPUs first, so "the first usable GPU" is a stable notion and matches what
    // the library's own default-device resolution would pick.
    std::stable_sort(out.begin(), out.end(),
                     [](const SdDevice& a, const SdDevice& b) { return a.gpu && !b.gpu; });
    return out;
}

SdDevice cpuDevice()
{
    for (const SdDevice& d : availableDevices()) {
        if (!d.gpu && d.name.compare(QLatin1String("CPU"), Qt::CaseInsensitive) == 0)
            return d;
    }
    // Fall back to any non-GPU device, then to a synthesised entry: "CPU" is
    // the name ggml's own CPU backend registers under, so naming it explicitly
    // is still better than handing the library an empty spec and hoping.
    for (const SdDevice& d : availableDevices()) {
        if (!d.gpu)
            return d;
    }
    SdDevice d;
    d.name = QStringLiteral("CPU");
    d.description = tr_("ggml CPU backend");
    return d;
}

SdDeviceChoice chooseDevice(SdDevicePreference preference)
{
    const QVector<SdDevice> devices = availableDevices();

    SdDeviceChoice choice;
    choice.gpuRequested = preference != SdDevicePreference::Cpu;
    choice.gpuAvailable = std::any_of(devices.begin(), devices.end(),
                                      [](const SdDevice& d) { return d.gpu; });

    if (preference == SdDevicePreference::Cpu) {
        choice.device = cpuDevice();
        choice.reason = tr_("CPU requested explicitly.");
        return choice;
    }

    for (const SdDevice& d : devices) {
        if (!d.gpu)
            continue;
        choice.device = d;
        choice.reason = tr_("GPU device %1 (%2) selected.")
                            .arg(d.name, d.description);
        return choice;
    }

    // Asked for a GPU and there is none to be had. Saying so — with the reason
    // — is the whole point: the caller reports a CPU run as a CPU run.
    choice.device = cpuDevice();
    choice.downgraded = true;
    if (!hasCompiledGpuBackend()) {
        choice.reason = tr_("No GPU backend is compiled into this build "
                            "(HAZOR_SD_BACKENDS=CPU); running on the CPU.");
    } else {
        // The GPU backends ship as separate modules, so this is the common
        // case on a machine with no driver for any of them: the modules are
        // there, none of them loaded, and saying which were tried is the
        // difference between a diagnosable report and a shrug.
        choice.reason = tr_("No GPU device was found on this machine for any "
                            "compiled backend (%1); running on the CPU.")
                            .arg(compiledBackendsName());
    }
    if (preference == SdDevicePreference::Gpu)
        qWarning().noquote() << "[SD]" << choice.reason;
    return choice;
}

QString devicePreferenceName(SdDevicePreference preference)
{
    switch (preference) {
    case SdDevicePreference::Gpu: return QStringLiteral("gpu");
    case SdDevicePreference::Cpu: return QStringLiteral("cpu");
    case SdDevicePreference::Auto: break;
    }
    return QStringLiteral("auto");
}

bool devicePreferenceFromName(const QString& name, SdDevicePreference& out)
{
    const QString n = name.trimmed().toLower();
    if (n.isEmpty() || n == QLatin1String("auto")) {
        out = SdDevicePreference::Auto;
        return true;
    }
    if (n == QLatin1String("gpu")) {
        out = SdDevicePreference::Gpu;
        return true;
    }
    if (n == QLatin1String("cpu")) {
        out = SdDevicePreference::Cpu;
        return true;
    }
    return false;
}

QString describeBackendEnvironment()
{
    QStringList parts;
    for (const SdDevice& d : availableDevices()) {
        parts << QStringLiteral("%1 (%2%3)")
                     .arg(d.name,
                          d.gpu ? QStringLiteral("GPU") : QStringLiteral("CPU"),
                          d.totalBytes > 0
                              ? QStringLiteral(", %1 MiB")
                                    .arg(d.totalBytes / (1024ull * 1024ull))
                              : QString());
    }
    return QStringLiteral("compiled=%1 | devices: %2")
        .arg(compiledBackendsName(),
             parts.isEmpty() ? QStringLiteral("none") : parts.join(QStringLiteral(", ")));
}

} // namespace sdlocal
