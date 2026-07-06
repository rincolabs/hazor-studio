#pragma once

#include "core/ShapeTypes.hpp"

#include <QString>

struct SvgShapeConversionResult {
    bool success = false;
    ShapeData shapeData;
    QString errorMessage;
};

class SvgShapeConverter {
public:
    SvgShapeConversionResult convertFromResource(const QString& resourcePath);
};
