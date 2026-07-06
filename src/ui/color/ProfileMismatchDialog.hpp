#pragma once

#include "color/ColorProfile.hpp"

#include <QDialog>

class QCheckBox;
class QRadioButton;

enum class ProfileMismatchUserChoice {
    PreserveEmbedded,
    ConvertToWorking,
    AssignWorking
};

class ProfileMismatchDialog final : public QDialog
{
    Q_OBJECT
public:
    ProfileMismatchDialog(const ColorProfile& embeddedProfile,
                          const ColorProfile& workingSpace,
                          QWidget* parent = nullptr);

    ProfileMismatchUserChoice choice() const;
    bool dontAskAgain() const;

private:
    QRadioButton* m_preserveRadio = nullptr;
    QRadioButton* m_convertRadio = nullptr;
    QRadioButton* m_assignRadio = nullptr;
    QCheckBox* m_dontAskAgainCb = nullptr;
};
