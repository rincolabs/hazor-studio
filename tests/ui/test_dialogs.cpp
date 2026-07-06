#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QtTest>

#include "ui/dialogs/AdjustColorDialog.hpp"
#include "ui/dialogs/GaussianBlurDialog.hpp"
#include "ui/dialogs/SharpenDialog.hpp"
#include "ui/dialogs/MedianBlurDialog.hpp"
#include "ui/dialogs/EdgeDetectDialog.hpp"
#include "ui/dialogs/PosterizeDialog.hpp"
#include "ui/dialogs/ThresholdDialog.hpp"
#include "ui/dialogs/NoiseReduceDialog.hpp"
#include "ui/dialogs/RemoveBgDialog.hpp"
#include "ui/dialogs/FillLayerDialog.hpp"
#include "ui/dialogs/ToolDialog.hpp"

static QCheckBox* findPreview(const ToolDialog& dlg)
{
    for (auto* cb : dlg.findChildren<QCheckBox*>())
        if (cb->text() == "Preview") return cb;
    return nullptr;
}

// ── AdjustColorDialog ─────────────────────────────────────────

class TestAdjustColorDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(AdjustColorDialog(ToolDialog::Mode::Direct).windowTitle(), "Color Adjustments"); }
    void previewDefault()
    { auto* cb = findPreview(AdjustColorDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKeys()
    { auto p = AdjustColorDialog(ToolDialog::Mode::Direct).collectParams();
      QVERIFY(p.contains("brightness")); QVERIFY(p.contains("contrast"));
      QVERIFY(p.contains("saturation")); QVERIFY(p.contains("hue"));
      QVERIFY(p.contains("auto_contrast")); }
    void paramDefaults()
    { auto p = AdjustColorDialog(ToolDialog::Mode::Direct).collectParams();
      QCOMPARE(p["brightness"].toDouble(), 0.0); QCOMPARE(p["contrast"].toDouble(), 0.0);
      QCOMPARE(p["saturation"].toDouble(), 0.0); QCOMPARE(p["hue"].toDouble(), 0.0);
      QCOMPARE(p["auto_contrast"].toDouble(), 0.0); }
};

// ── GaussianBlurDialog ────────────────────────────────────────

class TestGaussianBlurDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(GaussianBlurDialog(ToolDialog::Mode::Direct).windowTitle(), "Gaussian Blur"); }
    void previewDefault()
    { auto* cb = findPreview(GaussianBlurDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKey() { QVERIFY(GaussianBlurDialog(ToolDialog::Mode::Direct).collectParams().contains("radius")); }
    void paramDefault() { QCOMPARE(GaussianBlurDialog(ToolDialog::Mode::Direct).collectParams()["radius"].toDouble(), 3.0); }
};

// ── SharpenDialog ─────────────────────────────────────────────

class TestSharpenDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(SharpenDialog(ToolDialog::Mode::Direct).windowTitle(), "Sharpen"); }
    void previewDefault()
    { auto* cb = findPreview(SharpenDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKey() { QVERIFY(SharpenDialog(ToolDialog::Mode::Direct).collectParams().contains("strength")); }
    void paramDefault() { QCOMPARE(SharpenDialog(ToolDialog::Mode::Direct).collectParams()["strength"].toDouble(), 1.0); }
};

// ── MedianBlurDialog ──────────────────────────────────────────

class TestMedianBlurDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(MedianBlurDialog(ToolDialog::Mode::Direct).windowTitle(), "Median Blur"); }
    void previewDefault()
    { auto* cb = findPreview(MedianBlurDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKey() { QVERIFY(MedianBlurDialog(ToolDialog::Mode::Direct).collectParams().contains("kernel_size")); }
    void paramDefault() { QCOMPARE(MedianBlurDialog(ToolDialog::Mode::Direct).collectParams()["kernel_size"].toDouble(), 5.0); }
};

// ── EdgeDetectDialog ──────────────────────────────────────────

class TestEdgeDetectDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(EdgeDetectDialog(ToolDialog::Mode::Direct).windowTitle(), "Edge Detection (Canny)"); }
    void previewDefault()
    { auto* cb = findPreview(EdgeDetectDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKeys()
    { auto p = EdgeDetectDialog(ToolDialog::Mode::Direct).collectParams();
      QVERIFY(p.contains("threshold1")); QVERIFY(p.contains("threshold2")); }
    void paramDefaults()
    { auto p = EdgeDetectDialog(ToolDialog::Mode::Direct).collectParams();
      QCOMPARE(p["threshold1"].toDouble(), 50.0); QCOMPARE(p["threshold2"].toDouble(), 150.0); }
};

// ── PosterizeDialog ───────────────────────────────────────────

class TestPosterizeDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(PosterizeDialog(ToolDialog::Mode::Direct).windowTitle(), "Posterize"); }
    void previewDefault()
    { auto* cb = findPreview(PosterizeDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKey() { QVERIFY(PosterizeDialog(ToolDialog::Mode::Direct).collectParams().contains("levels")); }
    void paramDefault() { QCOMPARE(PosterizeDialog(ToolDialog::Mode::Direct).collectParams()["levels"].toDouble(), 8.0); }
};

// ── ThresholdDialog ───────────────────────────────────────────

class TestThresholdDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(ThresholdDialog(ToolDialog::Mode::Direct).windowTitle(), "Threshold"); }
    void previewDefault()
    { auto* cb = findPreview(ThresholdDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKeys()
    { auto p = ThresholdDialog(ToolDialog::Mode::Direct).collectParams();
      QVERIFY(p.contains("value")); QVERIFY(p.contains("adaptive")); }
    void paramDefaults()
    { auto p = ThresholdDialog(ToolDialog::Mode::Direct).collectParams();
      QCOMPARE(p["value"].toDouble(), 128.0); QCOMPARE(p["adaptive"].toDouble(), 0.0); }
};

// ── NoiseReduceDialog ─────────────────────────────────────────

class TestNoiseReduceDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(NoiseReduceDialog(ToolDialog::Mode::Direct).windowTitle(), "Noise Reduce"); }
    void previewDefault()
    { auto* cb = findPreview(NoiseReduceDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKeys()
    { auto p = NoiseReduceDialog(ToolDialog::Mode::Direct).collectParams();
      QVERIFY(p.contains("strength")); QVERIFY(p.contains("preserve_edges")); }
    void paramDefaults()
    { auto p = NoiseReduceDialog(ToolDialog::Mode::Direct).collectParams();
      QCOMPARE(p["strength"].toDouble(), 2.0); QCOMPARE(p["preserve_edges"].toDouble(), 0.5); }
};

// ── RemoveBgDialog ────────────────────────────────────────────

class TestRemoveBgDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(RemoveBgDialog(ToolDialog::Mode::Direct).windowTitle(), "Remove Background"); }
    void previewDefault()
    { auto* cb = findPreview(RemoveBgDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKeys()
    { auto p = RemoveBgDialog(ToolDialog::Mode::Direct).collectParams();
      QVERIFY(p.contains("mode")); QVERIFY(p.contains("threshold")); QVERIFY(p.contains("feather")); }
    void paramDefaults()
    { auto p = RemoveBgDialog(ToolDialog::Mode::Direct).collectParams();
      QCOMPARE(p["mode"].toDouble(), 0.0); QCOMPARE(p["threshold"].toDouble(), 20.0);
      QCOMPARE(p["feather"].toDouble(), 5.0); }
};

// ── FillLayerDialog ───────────────────────────────────────────

class TestFillLayerDialog : public QObject {
    Q_OBJECT
private slots:
    void title() { QCOMPARE(FillLayerDialog(ToolDialog::Mode::Direct).windowTitle(), "Fill Layer"); }
    void previewDefault()
    { auto* cb = findPreview(FillLayerDialog(ToolDialog::Mode::Direct)); QVERIFY(cb); QVERIFY(cb->isChecked()); }
    void paramKeys()
    { auto p = FillLayerDialog(ToolDialog::Mode::Direct).collectParams();
      QVERIFY(p.contains("red")); QVERIFY(p.contains("green"));
      QVERIFY(p.contains("blue")); QVERIFY(p.contains("alpha")); }
    void paramDefaults()
    { auto p = FillLayerDialog(ToolDialog::Mode::Direct).collectParams();
      QCOMPARE(p["red"].toDouble(), 255.0); QCOMPARE(p["green"].toDouble(), 255.0);
      QCOMPARE(p["blue"].toDouble(), 255.0); QCOMPARE(p["alpha"].toDouble(), 255.0); }
};

// ── ToolDialog mode ───────────────────────────────────────────

class TestToolDialogMode : public QObject {
    Q_OBJECT
private slots:
    void directApplyText()
    {
        SharpenDialog dlg(ToolDialog::Mode::Direct);
        bool found = false;
        for (auto* btn : dlg.findChildren<QPushButton*>())
            if (btn->text() == "Apply") found = true;
        QVERIFY(found);
    }
    void adjustmentApplyText()
    {
        SharpenDialog dlg(ToolDialog::Mode::Adjustment);
        bool found = false;
        for (auto* btn : dlg.findChildren<QPushButton*>())
            if (btn->text() == "Add Adjustment") found = true;
        QVERIFY(found);
    }
};

// ── Custom main: run all test classes ──────────────────────────

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    int status = 0;

    auto run = [&](QObject* obj) {
        status |= QTest::qExec(obj, argc, argv);
        delete obj;
    };

    run(new TestAdjustColorDialog);
    run(new TestGaussianBlurDialog);
    run(new TestSharpenDialog);
    run(new TestMedianBlurDialog);
    run(new TestEdgeDetectDialog);
    run(new TestPosterizeDialog);
    run(new TestThresholdDialog);
    run(new TestNoiseReduceDialog);
    run(new TestRemoveBgDialog);
    run(new TestFillLayerDialog);
    run(new TestToolDialogMode);

    return status;
}

#include "test_dialogs.moc"
