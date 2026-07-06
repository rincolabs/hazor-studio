#pragma once

#include <QDialog>

class QSpinBox;
class QCheckBox;
class QLabel;

class ResizeLayerDialog : public QDialog {
    Q_OBJECT
public:
    explicit ResizeLayerDialog(int origWidth, int origHeight,
                               QWidget* parent = nullptr);

    int width() const;
    int height() const;
    bool keepAspectRatio() const;

private slots:
    void onWidthChanged(int value);
    void onHeightChanged(int value);

private:
    void setupUi(int origWidth, int origHeight);

    QSpinBox* m_widthSpin = nullptr;
    QSpinBox* m_heightSpin = nullptr;
    QCheckBox* m_keepRatio = nullptr;
    QLabel* m_currentSizeLabel = nullptr;
    QLabel* m_newSizeLabel = nullptr;

    int m_origWidth = 0;
    int m_origHeight = 0;
};
