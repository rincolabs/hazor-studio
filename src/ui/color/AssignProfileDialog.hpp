#pragma once

#include "color/ColorProfile.hpp"

#include <QDialog>

class QComboBox;

class AssignProfileDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit AssignProfileDialog(const ColorProfile& currentProfile, QWidget* parent = nullptr);

    ColorProfile selectedProfile() const;

private:
    ColorProfile m_currentProfile;
    QComboBox* m_profileCombo = nullptr;
};
