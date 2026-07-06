#pragma once

#include "Theme.hpp"

class ClassicTheme : public Theme
{
    Q_OBJECT
public:
    explicit ClassicTheme(QObject* parent = nullptr);
    void populateComponentTheme() override;
};
