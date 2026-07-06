#pragma once

#include <QString>
#include "QtTheme.hpp"

class ThemeBuilder
{
public:
    static QString buildStyleSheet(const QtTheme& theme);
};
