#pragma once

#include <QIcon>
#include <QIconEngine>
#include <QPixmap>
#include <QPainter>
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

class OpacityIconEngine : public QIconEngine {
public:
    explicit OpacityIconEngine(const QPixmap& pix) : m_pix(pix) {}

    void paint(QPainter* p, const QRect& r, QIcon::Mode, QIcon::State) override {
        float op = ThemeManager::instance()->current()->iconOpacity;
        QPixmap scaled = m_pix.scaled(r.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        p->save();
        p->setOpacity(static_cast<double>(op));
        p->drawPixmap(r.topLeft(), scaled);
        p->restore();
    }

    QPixmap pixmap(const QSize& s, QIcon::Mode m, QIcon::State st) override {
        float op = ThemeManager::instance()->current()->iconOpacity;
        QPixmap scaled = m_pix.scaled(s, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (op >= 1.0f)
            return scaled;
        QPixmap out(scaled.size());
        out.fill(Qt::transparent);
        QPainter p(&out);
        p.setOpacity(static_cast<double>(op));
        p.drawPixmap(0, 0, scaled);
        return out;
    }

    QIconEngine* clone() const override { return new OpacityIconEngine(m_pix); }

private:
    QPixmap m_pix;
};

inline QIcon makeIcon(const QString& path)
{
    QPixmap pix(path);
    if (pix.isNull())
        return QIcon(path);
    return QIcon(new OpacityIconEngine(pix));
}
