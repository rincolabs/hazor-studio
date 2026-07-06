#pragma once

#include <QDialog>
#include <QImage>
#include <QLabel>
#include <QSlider>

class RefineEdgeDialog : public QDialog {
    Q_OBJECT
public:
    explicit RefineEdgeDialog(const QImage& sourceMask, const QImage& sourceImage,
                               QWidget* parent = nullptr);

    QImage resultMask() const { return m_result; }

private slots:
    void onParamChanged();

private:
    void rebuildPreview();
    QImage m_original;
    QImage m_sourceImage;
    QImage m_result;
    QLabel* m_previewLabel = nullptr;
    QSlider* m_smoothSlider = nullptr;
    QSlider* m_featherSlider = nullptr;
    QSlider* m_contrastSlider = nullptr;
    QSlider* m_shiftSlider = nullptr;
};
