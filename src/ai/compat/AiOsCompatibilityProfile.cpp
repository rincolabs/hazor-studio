#include "ai/compat/AiOsCompatibilityProfile.hpp"

#include <QCoreApplication>

namespace AiOsCompatibilityProfile {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("AiOsCompatibilityProfile", s); }

// Extracts the leading major number of a version string ("24.04" -> 24).
int majorVersion(const QString& version)
{
    QString digits;
    for (const QChar c : version) {
        if (c.isDigit()) digits.append(c);
        else break;
    }
    return digits.isEmpty() ? -1 : digits.toInt();
}

// ── Linux: officially validated distributions (spec §5) ──
// Returns true and sets *experimental when the distro+version is recognised.
bool classifyLinux(const AiPlatformInfo& info, bool* experimental, QString* note)
{
    const QString id = info.distroId;
    const int major = majorVersion(info.distroVersion);
    *experimental = false;

    if (id == QLatin1String("ubuntu"))
        return major == 22 || major == 24;
    if (id == QLatin1String("debian"))
        return major == 12 || major == 13;
    if (id == QLatin1String("linuxmint")) {
        // Mint tracks Ubuntu LTS; 21/22 map onto Ubuntu 22/24.
        return major == 21 || major == 22;
    }
    if (id == QLatin1String("fedora"))
        return major >= 39; // "current Fedora"
    if (id == QLatin1String("arch") || id == QLatin1String("archlinux") || id == QLatin1String("manjaro")) {
        *note = tr("Rolling-release distribution — validated as a rolling target.");
        return true;
    }
    if (id == QLatin1String("opensuse-tumbleweed") || id == QLatin1String("opensuse")) {
        if (id.contains(QLatin1String("tumbleweed")))
            return true;
        *experimental = true; // Leap is close but not the validated Tumbleweed
        return false;
    }
    return false;
}

} // namespace

AiPlatformCompatibility classify(const AiPlatformInfo& info)
{
    AiPlatformCompatibility out;
    out.osName = info.osName;
    out.osVersion = info.osVersion;
    out.architecture = info.architecture;

    bool supported = false;
    bool experimental = false;
    QString note;

    if (info.platform == QLatin1String("linux")) {
        supported = classifyLinux(info, &experimental, &note);
    } else if (info.platform == QLatin1String("windows")) {
        // Windows 10 and 11 (spec §2). Build numbers ≥ 10.
        const int major = majorVersion(info.osVersion);
        supported = major >= 10;
    } else if (info.platform == QLatin1String("macos")) {
        // macOS 13 Ventura+ official; macOS 12 Monterey experimental (spec §2).
        const int major = majorVersion(info.osVersion);
        if (major >= 13) supported = true;
        else if (major == 12) experimental = true;
    }

    out.officiallySupported = supported;
    out.experimental = experimental;

    if (supported) {
        out.status = AiCompatibilityStatus::Compatible;
        out.messages.push_back({ AiCompatibilitySeverity::Ok,
                                 tr("Supported platform"),
                                 note.isEmpty()
                                     ? tr("This platform is officially validated for AI features.")
                                     : note,
                                 QString(), QString(), QString() });
    } else if (experimental) {
        out.status = AiCompatibilityStatus::PartiallyCompatible;
        out.messages.push_back({ AiCompatibilitySeverity::Warning,
                                 tr("Experimental platform"),
                                 tr("AI features may work on this platform, but it is not officially supported."),
                                 QString(), QString(), QString() });
    } else {
        out.status = AiCompatibilityStatus::Unsupported;
        const QString msg = info.platform == QLatin1String("linux")
            ? tr("This Linux distribution is not officially validated for AI features. "
                 "The editor will try to use ONNX Runtime, but compatibility is not guaranteed.")
            : tr("This platform is not officially validated for AI features. "
                 "The editor will still try to use ONNX Runtime, but compatibility is not guaranteed.");
        out.messages.push_back({ AiCompatibilitySeverity::Warning,
                                 tr("Untested platform"), msg, QString(), QString(), QString() });
    }

    return out;
}

} // namespace AiOsCompatibilityProfile
