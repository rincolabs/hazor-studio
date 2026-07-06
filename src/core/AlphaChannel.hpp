#pragma once

#include <QImage>
#include <QString>
#include <vector>

struct AlphaChannel {
    QString name;
    QImage mask;
    bool visible = true;
};