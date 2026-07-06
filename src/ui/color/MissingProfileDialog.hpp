#pragma once

#include "color/ColorProfile.hpp"

#include <QDialog>

class QCheckBox;
class QRadioButton;

enum class MissingProfileUserChoice {
    LeaveUntagged,
    AssignWorking,
    AssignSRgb
};

class MissingProfileDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit MissingProfileDialog(const ColorProfile& workingSpace, QWidget* parent = nullptr);

    MissingProfileUserChoice choice() const;
    bool dontAskAgain() const;

private:
    QRadioButton* m_leaveUntaggedRadio = nullptr;
    QRadioButton* m_assignWorkingRadio = nullptr;
    QRadioButton* m_assignSRgbRadio = nullptr;
    QCheckBox* m_dontAskAgainCb = nullptr;
};
