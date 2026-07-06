#pragma once

#include <QWidget>

class QPushButton;
class QComboBox;

class AlignBar : public QWidget {
    Q_OBJECT

public:
    explicit AlignBar(const QString& title, QWidget* parent = nullptr);

public slots:
    void updateButtons(bool hasLayer);

signals:
    void alignLeftClicked();
    void alignCenterHClicked();
    void alignRightClicked();
    void alignTopClicked();
    void alignMiddleVClicked();
    void alignBottomClicked();
    void alignCenterClicked();
    void alignTargetChanged(int target);
    void resetTransformClicked();

private:
    QPushButton* m_alignTop        = nullptr;
    QPushButton* m_alignMiddleV    = nullptr;
    QPushButton* m_alignBottom     = nullptr;
    QPushButton* m_alignLeft       = nullptr;
    QPushButton* m_alignCenterH    = nullptr;
    QPushButton* m_alignRight      = nullptr;
    QPushButton* m_alignCenterBoth = nullptr;
    QPushButton* m_resetTransformBtn = nullptr;
    QComboBox*   m_alignTargetCombo  = nullptr;
};
