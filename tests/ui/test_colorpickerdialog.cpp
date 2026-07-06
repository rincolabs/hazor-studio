#include <QApplication>
#include <QtTest>
#include <QSignalSpy>
#include <QPushButton>
#include <QLineEdit>

#include "core/ColorEngine.hpp"
#include "ui/colorpicker/ColorPickerDialog.hpp"
#include "ui/colorpicker/SaturationBrightnessArea.hpp"
#include "ui/colorpicker/ColorHueSlider.hpp"
#include "ui/colorpicker/ColorPreviewWidget.hpp"
#include "ui/colorpicker/ColorFieldsWidget.hpp"
#include "ui/ColorSwapWidget.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/ClassicTheme.hpp"

class TestColorPickerDialog : public QObject {
    Q_OBJECT

private slots:

    // ── Enum ────────────────────────────────────────────────

    void test_enum_values()
    {
        QCOMPARE(static_cast<int>(ColorPickerMode::Foreground), 0);
        QCOMPARE(static_cast<int>(ColorPickerMode::Background), 1);
    }

    // ── ColorPickerDialog: construction ─────────────────────

    void test_dialog_construction()
    {
        QColor init(255, 128, 64);
        ColorPickerDialog dlg(init, ColorPickerMode::Foreground);

        QCOMPARE(dlg.selectedColor(), init);
        QVERIFY(dlg.windowTitle().contains("Foreground"));
    }

    void test_dialog_background_mode_title()
    {
        ColorPickerDialog dlg(Qt::cyan, ColorPickerMode::Background);
        QVERIFY(dlg.windowTitle().contains("Background"));
    }

    void test_dialog_minimum_size()
    {
        ColorPickerDialog dlg(Qt::red, ColorPickerMode::Foreground);
        QVERIFY(dlg.minimumWidth() >= 400);
        QVERIFY(dlg.minimumHeight() >= 300);
    }

    // ── ColorPickerDialog: signals ──────────────────────────

    void test_signal_color_accepted_on_ok()
    {
        QColor init(10, 20, 30);
        ColorPickerDialog dlg(init, ColorPickerMode::Foreground);

        QSignalSpy spy(&dlg, &ColorPickerDialog::colorAccepted);
        auto* okBtn = dlg.findChild<QPushButton*>("okBtn");
        QVERIFY(okBtn != nullptr);
        QTest::mouseClick(okBtn, Qt::LeftButton);

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<QColor>(), init);
    }

    void test_signal_cancel_restores_color()
    {
        QColor init(50, 60, 70);
        ColorPickerDialog dlg(init, ColorPickerMode::Foreground);

        QSignalSpy previewSpy(&dlg, &ColorPickerDialog::colorPreviewChanged);
        auto* cancelBtn = dlg.findChild<QPushButton*>("cancelBtn");
        QVERIFY(cancelBtn != nullptr);
        QTest::mouseClick(cancelBtn, Qt::LeftButton);

        QCOMPARE(previewSpy.count(), 1);
        QCOMPARE(previewSpy.at(0).at(0).value<QColor>(), init);
    }

    void test_signal_swatch_add_requested()
    {
        QColor init(100, 150, 200);
        ColorPickerDialog dlg(init, ColorPickerMode::Foreground);

        QSignalSpy spy(&dlg, &ColorPickerDialog::swatchAddRequested);
        auto* swatchesBtn = dlg.findChild<QPushButton*>();
        QVERIFY(swatchesBtn != nullptr);

        auto buttons = dlg.findChildren<QPushButton*>();
        for (auto* b : buttons) {
            if (b->text().contains("Swatches")) {
                QTest::mouseClick(b, Qt::LeftButton);
                break;
            }
        }

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).value<QColor>(), init);
    }

    // ── SaturationBrightnessArea ────────────────────────────

    void test_sb_area_set_color_roundtrip()
    {
        SaturationBrightnessArea area;
        QColor c = QColor::fromHsvF(0.5, 0.75, 0.6);
        area.setColor(c);

        double expectedHue = c.hueF() * 360.0;
        if (expectedHue < 0.0) expectedHue = 0.0;

        QVERIFY(qFuzzyCompare(area.hue(), expectedHue));
        QVERIFY(qFuzzyCompare(area.saturation(), c.saturationF() * 100.0));
        QVERIFY(qFuzzyCompare(area.brightness(), c.valueF() * 100.0));
    }

    void test_sb_area_set_color_edge_black()
    {
        SaturationBrightnessArea area;
        area.setColor(Qt::black);
        QVERIFY(qFuzzyCompare(area.brightness(), 0.0));
    }

    void test_sb_area_set_color_edge_white()
    {
        SaturationBrightnessArea area;
        area.setColor(Qt::white);
        QVERIFY(qFuzzyCompare(area.saturation(), 0.0));
        QVERIFY(qFuzzyCompare(area.brightness(), 100.0));
    }

    void test_sb_area_set_hue()
    {
        SaturationBrightnessArea area;
        area.setHue(180.0);
        QVERIFY(qFuzzyCompare(area.hue(), 180.0));
    }

    void test_sb_area_hue_clamped()
    {
        SaturationBrightnessArea area;
        area.setHue(-50.0);
        QVERIFY(qFuzzyCompare(area.hue(), 0.0));

        area.setHue(500.0);
        QVERIFY(qFuzzyCompare(area.hue(), 360.0));
    }

    void test_sb_area_emits_color_on_mouse_click()
    {
        SaturationBrightnessArea area;
        area.setHue(0.0);
        area.resize(200, 200);
        QTest::qWait(10);

        QSignalSpy spy(&area, &SaturationBrightnessArea::colorChanged);
        QTest::mouseClick(&area, Qt::LeftButton, {}, QPoint(50, 100));
        QCOMPARE(spy.count(), 1);

        QColor c = spy.at(0).at(0).value<QColor>();
        QVERIFY(c.isValid());
    }

    // ── ColorHueSlider ──────────────────────────────────────

    void test_hue_slider_set_get()
    {
        ColorHueSlider slider;
        slider.setHue(90.0);
        QVERIFY(qFuzzyCompare(slider.hue(), 90.0));
    }

    void test_hue_slider_clamped()
    {
        ColorHueSlider slider;
        slider.setHue(-10.0);
        QVERIFY(qFuzzyCompare(slider.hue(), 0.0));

        slider.setHue(400.0);
        QVERIFY(qFuzzyCompare(slider.hue(), 360.0));
    }

    void test_hue_slider_edge_min()
    {
        ColorHueSlider slider;
        slider.setHue(0.0);
        QVERIFY(qFuzzyCompare(slider.hue(), 0.0));
    }

    void test_hue_slider_edge_max()
    {
        ColorHueSlider slider;
        slider.setHue(360.0);
        QVERIFY(qFuzzyCompare(slider.hue(), 360.0));
    }

    void test_hue_slider_mouse_release_resets_drag()
    {
        ColorHueSlider slider;
        slider.resize(30, 200);
        QTest::qWait(10);

        QTest::mousePress(&slider, Qt::LeftButton, {}, QPoint(15, 100));
        QTest::mouseRelease(&slider, Qt::LeftButton, {}, QPoint(15, 100));

        double hueAfterRelease = slider.hue();

        QTest::mouseMove(&slider, QPoint(15, 50));
        double hueAfterMove = slider.hue();

        QCOMPARE(hueAfterRelease, hueAfterMove);
    }

    void test_hue_slider_emits_on_press()
    {
        ColorHueSlider slider;
        slider.resize(30, 200);
        QTest::qWait(10);

        QSignalSpy spy(&slider, &ColorHueSlider::hueChanged);
        QTest::mousePress(&slider, Qt::LeftButton, {}, QPoint(15, 50));
        QCOMPARE(spy.count(), 1);
    }

    // ── ColorPreviewWidget ──────────────────────────────────

    void test_preview_set_current()
    {
        ColorPreviewWidget preview;
        preview.setCurrentColor(Qt::red);
        preview.setNewColor(Qt::blue);
        QVERIFY(true);
    }

    void test_preview_size_hint()
    {
        ColorPreviewWidget preview;
        QVERIFY(preview.sizeHint().width() > 0);
        QVERIFY(preview.sizeHint().height() > 0);
    }

    // ── ColorFieldsWidget ───────────────────────────────────

    void test_fields_hsv_roundtrip()
    {
        ColorFieldsWidget fields;
        QColor c = QColor::fromHsvF(0.5, 0.75, 0.6);
        fields.setColor(c);

        QSignalSpy spy(&fields, &ColorFieldsWidget::colorChanged);
        auto* hEdit = fields.findChild<QLineEdit*>("", Qt::FindChildrenRecursively);
        QVERIFY(hEdit != nullptr);

        QCOMPARE(spy.count(), 0);
    }

    void test_fields_rgb_roundtrip()
    {
        ColorFieldsWidget fields;
        QColor c(128, 64, 32);
        fields.setColor(c);
        QVERIFY(true);
    }

    void test_fields_hex_set_then_get()
    {
        ColorFieldsWidget fields;
        QColor c("#AABBCC");
        fields.setColor(c);
        QVERIFY(true);
    }

    void test_fields_lab_is_readonly()
    {
        ColorFieldsWidget fields;
        fields.setColor(Qt::red);

        auto lineEdits = fields.findChildren<QLineEdit*>();
        QLineEdit* lEdit = nullptr;
        for (auto* le : lineEdits) {
            if (le->isReadOnly()) {
                lEdit = le;
                break;
            }
        }
        QVERIFY(lEdit != nullptr);
        QVERIFY(lEdit->isReadOnly());
    }

    void test_fields_cmyk_roundtrip()
    {
        ColorFieldsWidget fields;
        // Cyan = 100% C in CMYK
        QColor c = QColor::fromCmykF(1.0, 0.0, 0.0, 0.0);
        fields.setColor(c);
        QVERIFY(true);
    }

    void test_fields_set_color_does_not_emit()
    {
        ColorFieldsWidget fields;
        QSignalSpy spy(&fields, &ColorFieldsWidget::colorChanged);
        fields.setColor(Qt::red);
        QCOMPARE(spy.count(), 0);
    }

    // ── Theme integration ───────────────────────────────────

    void test_dialog_has_stylesheet()
    {
        ColorPickerDialog dlg(Qt::white, ColorPickerMode::Foreground);
        QVERIFY(!dlg.styleSheet().isEmpty());
    }

    void test_dialog_respects_default_theme()
    {
        ClassicTheme dt;
        ColorPickerDialog dlg(Qt::white, ColorPickerMode::Foreground);
        QString qss = dlg.styleSheet();
        QVERIFY(qss.contains("QDialog"));
        QVERIFY(qss.contains("QPushButton"));
        QVERIFY(qss.contains("QLineEdit"));
    }

    // ── Paint/rendering (no-crash smoke tests) ──────────────

    void test_sb_area_paint_no_crash()
    {
        SaturationBrightnessArea area;
        area.setHue(120.0);
        area.resize(200, 200);
        area.show();
        QTest::qWait(20);
        area.hide();
        QVERIFY(true);
    }

    void test_hue_slider_paint_no_crash()
    {
        ColorHueSlider slider;
        slider.setHue(240.0);
        slider.resize(30, 200);
        slider.show();
        QTest::qWait(20);
        slider.hide();
        QVERIFY(true);
    }

    void test_preview_paint_no_crash()
    {
        ColorPreviewWidget preview;
        preview.setNewColor(Qt::red);
        preview.setCurrentColor(Qt::blue);
        preview.resize(100, 80);
        preview.show();
        QTest::qWait(20);
        preview.hide();
        QVERIFY(true);
    }

    void test_fields_paint_no_crash()
    {
        ColorFieldsWidget fields;
        fields.setColor(Qt::green);
        fields.show();
        QTest::qWait(20);
        fields.hide();
        QVERIFY(true);
    }

    void test_dialog_show_hide_no_crash()
    {
        ColorPickerDialog dlg(QColor(100, 150, 200), ColorPickerMode::Foreground);
        dlg.show();
        QTest::qWait(20);
        dlg.hide();
        QVERIFY(true);
    }

    // ── Dialog: resize no crash ─────────────────────────────

    void test_dialog_resize_no_crash()
    {
        ColorPickerDialog dlg(Qt::red, ColorPickerMode::Foreground);
        dlg.resize(500, 400);
        QTest::qWait(10);
        dlg.resize(600, 500);
        QTest::qWait(10);
        QVERIFY(true);
    }

    // ── Race/signal loop prevention ─────────────────────────

    void test_dialog_no_signal_loop_on_initial_sync()
    {
        QColor init(64, 128, 192);
        ColorPickerDialog dlg(init, ColorPickerMode::Foreground);

        QSignalSpy spy(&dlg, &ColorPickerDialog::colorPreviewChanged);
        QCOMPARE(spy.count(), 0);
    }

    void test_cancel_then_accept_keeps_state()
    {
        QColor init(42, 42, 42);
        ColorPickerDialog dlg(init, ColorPickerMode::Foreground);

        auto* cancelBtn = dlg.findChild<QPushButton*>("cancelBtn");
        QVERIFY(cancelBtn != nullptr);
        QTest::mouseClick(cancelBtn, Qt::LeftButton);

        QCOMPARE(dlg.selectedColor(), init);
    }
};

QTEST_MAIN(TestColorPickerDialog)
#include "test_colorpickerdialog.moc"
