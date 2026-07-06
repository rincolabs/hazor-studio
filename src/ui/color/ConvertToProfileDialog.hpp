#pragma once

#include "color/ColorProfile.hpp"

#include <QDialog>

class QComboBox;
class QCheckBox;

class ConvertToProfileDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit ConvertToProfileDialog(const ColorProfile& sourceProfile, QWidget* parent = nullptr);

    ColorProfile destinationProfile() const;
    ColorConversionOptions conversionOptions() const;

private:
    QComboBox* m_destinationCombo = nullptr;
    QComboBox* m_intentCombo = nullptr;
    QCheckBox* m_blackPointCb = nullptr;
    QCheckBox* m_ditherCb = nullptr;
};
