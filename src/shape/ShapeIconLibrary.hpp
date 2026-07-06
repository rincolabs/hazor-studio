#pragma once

#include <QList>
#include <QMetaType>
#include <QSizeF>
#include <QString>

struct ShapeIconInfo {
    QString id;
    QString name;
    QString category;
    QString resourcePath;
    QSizeF viewBoxSize;
};

Q_DECLARE_METATYPE(ShapeIconInfo)

class ShapeIconLibrary {
public:
    QList<ShapeIconInfo> loadBuiltInIcons();
    QList<ShapeIconInfo> search(const QString& query) const;
    QList<ShapeIconInfo> icons() const { return m_icons; }
    void setIcons(QList<ShapeIconInfo> icons);

private:
    QList<ShapeIconInfo> m_icons;
};
