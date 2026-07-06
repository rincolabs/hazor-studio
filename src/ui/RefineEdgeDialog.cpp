#include "RefineEdgeDialog.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPixmap>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QPushButton>
#include <QGroupBox>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

static QImage toRgba8888(const QImage& img)
{
    if (img.isNull()) return {};
    return img.convertToFormat(QImage::Format_RGBA8888);
}

RefineEdgeDialog::RefineEdgeDialog(const QImage& sourceMask,
                                   const QImage& sourceImage,
                                   QWidget* parent)
    : QDialog(parent)
    , m_original(toRgba8888(sourceMask))
    , m_sourceImage(toRgba8888(sourceImage))
    , m_result(m_original.copy())
{
    setWindowTitle(tr("Refine Edge"));
    setMinimumSize(600, 450);

    auto* main = new QVBoxLayout(this);

    // Preview
    m_previewLabel = new QLabel(this);
    m_previewLabel->setMinimumSize(400, 280);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    auto* t = ThemeManager::instance()->current();
    m_previewLabel->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2;").arg(t->colorBackgroundPrimary.name()).arg(t->colorBorder.name()));
    main->addWidget(m_previewLabel);

    // Parameters
    auto* form = new QFormLayout();

    auto addSlider = [&](const QString& label, int minV, int maxV, int def, QSlider*& out) {
        auto* row = new QHBoxLayout();
        auto* slider = new QSlider(Qt::Horizontal, this);
        slider->setRange(minV, maxV);
        slider->setValue(def);
        slider->setFixedWidth(200);
        auto* valLabel = new QLabel(QString::number(def), this);
        valLabel->setFixedWidth(30);
        row->addWidget(slider);
        row->addWidget(valLabel);
        connect(slider, &QSlider::valueChanged, this, [valLabel](int v) {
            valLabel->setText(QString::number(v));
        });
        connect(slider, &QSlider::valueChanged, this, &RefineEdgeDialog::onParamChanged);
        form->addRow(label, row);
        out = slider;
    };

    addSlider(tr("Smooth (median blur):"), 0, 15, 0, m_smoothSlider);
    addSlider(tr("Feather (gaussian blur):"), 0, 50, 0, m_featherSlider);
    addSlider(tr("Contrast (threshold):"), 0, 100, 0, m_contrastSlider);
    addSlider(tr("Shift Edge (expand + / shrink -):"), -50, 50, 0, m_shiftSlider);

    main->addLayout(form);

    // Buttons
    auto* btnRow = new QHBoxLayout();
    btnRow->addStretch();
    auto* applyBtn = new QPushButton(tr("Apply"), this);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    btnRow->addWidget(applyBtn);
    btnRow->addWidget(cancelBtn);
    main->addLayout(btnRow);

    connect(applyBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    rebuildPreview();
}

void RefineEdgeDialog::onParamChanged()
{
    rebuildPreview();
}

void RefineEdgeDialog::rebuildPreview()
{
    if (m_original.isNull()) return;

    int w = m_original.width();
    int h = m_original.height();

    cv::Mat mask(h, w, CV_8UC1, const_cast<uchar*>(m_original.bits()));
    cv::Mat result = mask.clone();

    int smooth = m_smoothSlider->value();
    int feather = m_featherSlider->value();
    int contrast = m_contrastSlider->value();
    int shift = m_shiftSlider->value();

    // Smooth (median blur)
    if (smooth > 0) {
        int ksize = smooth * 2 + 1;
        if (ksize > 31) ksize = 31;
        cv::medianBlur(result, result, ksize);
    }

    // Shift edge (expand / shrink)
    if (shift != 0) {
        int absShift = std::abs(shift);
        cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE,
            cv::Size(2 * absShift + 1, 2 * absShift + 1));
        if (shift > 0)
            cv::dilate(result, result, kernel);
        else
            cv::erode(result, result, kernel);
    }

    // Feather
    if (feather > 0) {
        int ksize = 2 * feather + 1;
        cv::GaussianBlur(result, result, cv::Size(ksize, ksize), feather);
    }

    // Contrast
    if (contrast > 0) {
        // Sigmoid-like contrast on edges
        for (int y = 0; y < h; ++y) {
            uchar* row = result.ptr<uchar>(y);
            for (int x = 0; x < w; ++x) {
                float v = row[x] / 255.0f;
                // Center around 0.5, increase contrast
                v = 0.5f + (v - 0.5f) * (1.0f + contrast / 50.0f);
                row[x] = static_cast<uchar>(std::clamp(v * 255.0f, 0.0f, 255.0f));
            }
        }
    }

    m_result = QImage(w, h, QImage::Format_Grayscale8);
    for (int y = 0; y < h; ++y)
        std::copy(result.ptr<uchar>(y), result.ptr<uchar>(y) + w, m_result.scanLine(y));

    // Build preview image: show masked selection on checkerboard + source
    QImage preview(w, h, QImage::Format_RGBA8888);
    preview.fill(Qt::transparent);

    if (!m_sourceImage.isNull()) {
        // Show source image where mask > 0
        for (int y = 0; y < h && y < m_sourceImage.height(); ++y) {
            const uchar* src = m_sourceImage.constScanLine(y);
            const uchar* mRow = m_result.constScanLine(y);
            uchar* pRow = preview.scanLine(y);
            for (int x = 0; x < w && x < m_sourceImage.width(); ++x) {
                float alpha = mRow[x] / 255.0f;
                pRow[x * 4 + 0] = static_cast<uchar>(src[x * 4 + 0] * alpha);
                pRow[x * 4 + 1] = static_cast<uchar>(src[x * 4 + 1] * alpha);
                pRow[x * 4 + 2] = static_cast<uchar>(src[x * 4 + 2] * alpha);
                pRow[x * 4 + 3] = static_cast<uchar>(255 * alpha);
            }
        }
    }

    // Scale preview to fit label
    QPixmap pm = QPixmap::fromImage(preview.scaled(m_previewLabel->size() - QSize(4, 4),
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_previewLabel->setPixmap(pm);
}
