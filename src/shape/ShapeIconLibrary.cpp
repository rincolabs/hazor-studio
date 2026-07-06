#include "ShapeIconLibrary.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QRegularExpression>
#include <QXmlStreamReader>
#include <algorithm>
#include <utility>

namespace {

double parseSvgLength(const QString& value)
{
    const auto match = QRegularExpression(QStringLiteral("^\\s*([+-]?(?:\\d+\\.?\\d*|\\.\\d+))"))
        .match(value);
    if (!match.hasMatch())
        return 0.0;
    return match.captured(1).toDouble();
}

QSizeF readViewBoxSize(const QString& resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        if (xml.name() != QLatin1String("svg"))
            return {};

        const auto attrs = xml.attributes();
        const QString viewBox = attrs.value(QStringLiteral("viewBox")).toString();
        if (!viewBox.isEmpty()) {
            const QStringList parts = viewBox.split(QRegularExpression(QStringLiteral("[,\\s]+")),
                                                    Qt::SkipEmptyParts);
            if (parts.size() == 4)
                return QSizeF(parts[2].toDouble(), parts[3].toDouble());
        }

        const double width = parseSvgLength(attrs.value(QStringLiteral("width")).toString());
        const double height = parseSvgLength(attrs.value(QStringLiteral("height")).toString());
        return QSizeF(width, height);
    }
    return {};
}

QString titleCaseWords(const QString& text)
{
    QStringList out;
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    for (QString word : words) {
        word = word.toLower();
        if (!word.isEmpty())
            word[0] = word[0].toUpper();
        out.push_back(word);
    }
    return out.join(QLatin1Char(' '));
}

QString displayNameForFile(const QString& baseName)
{
    QString name = baseName;
    if (name.startsWith(QLatin1String("ic_")))
        name = name.mid(3);
    name.replace(QRegularExpression(QStringLiteral("_(?:18|24|36|48)px$")), QString());
    name.replace(QRegularExpression(QStringLiteral("[_-]+")), QStringLiteral(" "));
    return titleCaseWords(name);
}

QString categoryNameForRelativePath(const QString& relativePath)
{
    const int slash = relativePath.indexOf(QLatin1Char('/'));
    if (slash <= 0)
        return QStringLiteral("Custom");
    QString category = relativePath.left(slash);
    category.replace(QRegularExpression(QStringLiteral("[_-]+")), QStringLiteral(" "));
    return titleCaseWords(category);
}

QString iconIdForRelativePath(QString relativePath)
{
    if (relativePath.endsWith(QLatin1String(".svg"), Qt::CaseInsensitive))
        relativePath.chop(4);
    relativePath.replace(QLatin1Char('/'), QLatin1Char('.'));
    return relativePath;
}

} // namespace

QList<ShapeIconInfo> ShapeIconLibrary::loadBuiltInIcons()
{
    QList<ShapeIconInfo> icons;
    QDirIterator it(QStringLiteral(":/shapes"),
                    QStringList{QStringLiteral("*.svg")},
                    QDir::Files,
                    QDirIterator::Subdirectories);

    while (it.hasNext()) {
        const QString resourcePath = it.next();
        const QString relativePath = QDir(QStringLiteral(":/shapes")).relativeFilePath(resourcePath);

        ShapeIconInfo icon;
        icon.id = iconIdForRelativePath(relativePath);
        icon.name = displayNameForFile(QFileInfo(relativePath).completeBaseName());
        icon.category = categoryNameForRelativePath(relativePath);
        icon.resourcePath = resourcePath;
        icon.viewBoxSize = readViewBoxSize(resourcePath);
        icons.push_back(icon);
    }

    std::sort(icons.begin(), icons.end(), [](const ShapeIconInfo& a, const ShapeIconInfo& b) {
        if (a.category != b.category)
            return a.category < b.category;
        return a.name < b.name;
    });

    m_icons = icons;
    return m_icons;
}

void ShapeIconLibrary::setIcons(QList<ShapeIconInfo> icons)
{
    m_icons = std::move(icons);
}

QList<ShapeIconInfo> ShapeIconLibrary::search(const QString& query) const
{
    const QString needle = query.trimmed();
    if (needle.isEmpty())
        return m_icons;

    QList<ShapeIconInfo> matches;
    for (const ShapeIconInfo& icon : m_icons) {
        if (icon.name.contains(needle, Qt::CaseInsensitive)
            || icon.id.contains(needle, Qt::CaseInsensitive)
            || icon.category.contains(needle, Qt::CaseInsensitive)) {
            matches.push_back(icon);
        }
    }
    return matches;
}
