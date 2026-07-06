#include "ai/compat/AiPlatformDetector.hpp"

#include <QFile>
#include <QHash>
#include <QSysInfo>
#include <QTextStream>

namespace AiPlatformDetector {

namespace {

#if defined(Q_OS_LINUX)
// Parses the small KEY=VALUE format of /etc/os-release (values may be quoted).
QHash<QString, QString> readOsRelease()
{
    QHash<QString, QString> kv;
    QFile f(QStringLiteral("/etc/os-release"));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return kv;
    QTextStream ts(&f);
    while (!ts.atEnd()) {
        const QString line = ts.readLine().trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || line.startsWith(QLatin1Char('#')))
            continue;
        QString key = line.left(eq).trimmed();
        QString val = line.mid(eq + 1).trimmed();
        if (val.size() >= 2 && (val.front() == QLatin1Char('"') || val.front() == QLatin1Char('\''))
            && val.back() == val.front())
            val = val.mid(1, val.size() - 2);
        kv.insert(key, val);
    }
    return kv;
}
#endif

QString detectArchitecture()
{
#if defined(Q_PROCESSOR_ARM_64)
    return QStringLiteral("arm64");
#elif defined(Q_PROCESSOR_X86_64)
    return QStringLiteral("x86_64");
#else
    return QSysInfo::currentCpuArchitecture();
#endif
}

} // namespace

AiPlatformInfo detect()
{
    AiPlatformInfo info;
    info.architecture = detectArchitecture();

#if defined(Q_OS_LINUX)
    info.platform = QStringLiteral("linux");
    const QHash<QString, QString> os = readOsRelease();
    info.distroId      = os.value(QStringLiteral("ID")).trimmed().toLower();
    info.distroVersion = os.value(QStringLiteral("VERSION_ID")).trimmed();
    info.osName        = os.value(QStringLiteral("PRETTY_NAME"));
    info.osVersion     = info.distroVersion;
    if (info.osName.isEmpty())
        info.osName = QSysInfo::prettyProductName();
    if (info.osName.isEmpty())
        info.osName = QStringLiteral("Linux");
#elif defined(Q_OS_WIN)
    info.platform  = QStringLiteral("windows");
    info.osName    = QSysInfo::prettyProductName();
    info.osVersion = QSysInfo::productVersion();
#elif defined(Q_OS_MACOS)
    info.platform  = QStringLiteral("macos");
    info.osName    = QStringLiteral("macOS");
    info.osVersion = QSysInfo::productVersion();
    if (!QSysInfo::prettyProductName().isEmpty())
        info.osName = QSysInfo::prettyProductName();
#else
    info.platform  = QSysInfo::productType();
    info.osName    = QSysInfo::prettyProductName();
    info.osVersion = QSysInfo::productVersion();
#endif

    return info;
}

} // namespace AiPlatformDetector
