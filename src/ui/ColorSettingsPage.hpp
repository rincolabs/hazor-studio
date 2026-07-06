#pragma once

#include <QWidget>

class QCheckBox;
class QComboBox;
class QScrollArea;

class ColorSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ColorSettingsPage(QWidget* parent = nullptr);

    void loadSettings();
    void saveSettings();

private:
    void applyTheme();
    void addProfileItems(QComboBox* combo) const;
    void selectComboData(QComboBox* combo, int value) const;

    QScrollArea* m_scroll = nullptr;
    QWidget* m_content = nullptr;
    QComboBox* m_workingRgbCombo = nullptr;
    QComboBox* m_missingPolicyCombo = nullptr;
    QComboBox* m_mismatchPolicyCombo = nullptr;
    QComboBox* m_intentCombo = nullptr;
    QCheckBox* m_blackPointCb = nullptr;
    QCheckBox* m_convertNewDocumentsCb = nullptr;
    QCheckBox* m_displayManagementCb = nullptr;
    QCheckBox* m_systemMonitorProfileCb = nullptr;
    QComboBox* m_fallbackDisplayCombo = nullptr;
    QCheckBox* m_softProofCb = nullptr;
};
