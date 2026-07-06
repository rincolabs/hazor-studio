#pragma once

#include "color/SoftProofSettings.hpp"

#include <QDialog>

class QComboBox;
class QCheckBox;

// Proof Setup dialog (View > Proof Setup...). Edits the document's
// SoftProofSettings: which profile to simulate, the rendering intent, black
// point compensation, paper/ink simulation, and whether to preserve RGB
// numbers. Soft proof is a display-only preview — it never alters pixels or the
// document profile — so this dialog only returns settings; the caller applies
// them and repaints.
class SoftProofDialog final : public QDialog
{
    Q_OBJECT
public:
    explicit SoftProofDialog(const SoftProofSettings& current, QWidget* parent = nullptr);

    SoftProofSettings settings() const;

private:
    QComboBox* m_proofCombo = nullptr;
    QComboBox* m_intentCombo = nullptr;
    QCheckBox* m_enabledCb = nullptr;
    QCheckBox* m_blackPointCb = nullptr;
    QCheckBox* m_simulatePaperCb = nullptr;
    QCheckBox* m_simulateBlackCb = nullptr;
    QCheckBox* m_preserveNumbersCb = nullptr;
};
