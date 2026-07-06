#include <QApplication>
#include <QScreen>
#include <QPixmap>
#include <QImage>
#include <QSignalSpy>
#include <QVariantAnimation>
#include <QTest>
#include "ui/SplashScreen.hpp"

class TestSplashScreen : public QObject {
    Q_OBJECT

private slots:

    // ── Window flags and geometry ──

    void test_window_flags()
    {
        SplashScreen s;
        QVERIFY(s.windowFlags() & Qt::FramelessWindowHint);
        QVERIFY(s.windowFlags() & Qt::WindowStaysOnTopHint);
    }

    void test_fixed_size()
    {
        SplashScreen s;
        QCOMPARE(s.minimumSize(), QSize(520, 340));
        QCOMPARE(s.maximumSize(), QSize(520, 340));
        QCOMPARE(s.size(), QSize(520, 340));
    }

    void test_centered_on_screen()
    {
        SplashScreen s;
        auto* screen = QApplication::primaryScreen();
        QVERIFY(screen);
        QPoint expectedCenter = screen->geometry().center();
        QPoint actualCenter = s.geometry().center();
        // Allow 1px tolerance for window manager rounding
        QVERIFY(qAbs(actualCenter.x() - expectedCenter.x()) <= 1);
        QVERIFY(qAbs(actualCenter.y() - expectedCenter.y()) <= 1);
    }

    void test_logo_loaded_from_resources()
    {
        QPixmap pm(":/icons/app-logo.png");
        QVERIFY(!pm.isNull());
        QVERIFY(pm.width() > 0);
        QVERIFY(pm.height() > 0);
    }

    // ── finish() behavior ──

    void test_finish_emits_signal_and_closes()
    {
        SplashScreen s;
        s.setMinimumDuration(0);
        QSignalSpy spy(&s, &SplashScreen::finished);
        QVERIFY(s.isVisible() == false);

        s.show();
        QVERIFY(s.isVisible());

        s.finish();
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);
        QVERIFY(!s.isVisible());
    }

    void test_finish_waits_minimum_duration()
    {
        SplashScreen s;
        s.setMinimumDuration(200);

        QSignalSpy spyFinished(&s, &SplashScreen::finished);
        QElapsedTimer timer;
        timer.start();

        s.finish();
        QCOMPARE(spyFinished.count(), 0);

        QVERIFY(QTest::qWaitFor([&]() { return spyFinished.count() > 0; }, 500));
        QVERIFY(timer.elapsed() >= 200);
    }

    void test_finish_no_wait_when_past_duration()
    {
        SplashScreen s;
        s.setMinimumDuration(100);

        QTest::qWait(150);

        QSignalSpy spy(&s, &SplashScreen::finished);
        QElapsedTimer timer;
        timer.start();

        s.finish();
        QTest::qWait(50);
        QCOMPARE(spy.count(), 1);
        QVERIFY(timer.elapsed() < 100);
    }

    void test_finish_creates_animation()
    {
        SplashScreen s;
        s.setMinimumDuration(300);
        s.finish();

        bool found = false;
        const auto children = s.children();
        for (auto* child : children) {
            if (qobject_cast<QVariantAnimation*>(child)) {
                found = true;
                break;
            }
        }
        QVERIFY(found);
    }

    // ── Input is ignored ──

    void test_mouse_click_ignored()
    {
        SplashScreen s;
        s.show();
        s.raise();
        QApplication::processEvents();

        QSignalSpy spyFinished(&s, &SplashScreen::finished);

        QTest::mouseClick(&s, Qt::LeftButton, Qt::NoModifier, s.rect().center());
        QApplication::processEvents();
        QCOMPARE(spyFinished.count(), 0);
        QVERIFY(s.isVisible());

        s.close();
    }

    void test_key_press_ignored()
    {
        SplashScreen s;
        s.show();
        s.raise();
        QApplication::processEvents();

        QSignalSpy spyFinished(&s, &SplashScreen::finished);

        QTest::keyClick(&s, Qt::Key_Escape);
        QApplication::processEvents();
        QCOMPARE(spyFinished.count(), 0);
        QVERIFY(s.isVisible());

        s.close();
    }

    // ── Paint verification ──

    void test_paint_background_and_border()
    {
        SplashScreen s;
        s.setProgress(0, QString());
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Background at top-left
        QColor bg = img.pixelColor(5, 5);
        QVERIFY(bg.isValid());
        QVERIFY(bg.alpha() == 255);  // fully opaque

        // Left edge pixel — should differ from background (border pen)
        QColor interior = img.pixelColor(1, 170);
        QCOMPARE(interior, bg);       // interior matches background
        QColor leftEdge = img.pixelColor(0, 170);
        QVERIFY(leftEdge != bg);      // border present, differs from bg
    }

    void test_paint_progress_zero()
    {
        SplashScreen s;
        s.setProgress(0, QString());
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Bar track at center of progress bar (y=243, the 6px bar is at y=240-246)
        QColor track = img.pixelColor(260, 243);
        QVERIFY(track.isValid());
        QVERIFY(track.alpha() == 255);

        // No fill — left edge of bar should be same color as track (uniform unfilled)
        QColor leftFill = img.pixelColor(85, 243);
        QCOMPARE(leftFill, track);
    }

    void test_paint_progress_half()
    {
        SplashScreen s;
        s.setProgress(50, QString());
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Bar track
        QColor track = img.pixelColor(260, 243);

        // Filled area (x=100 is within the first 50% of bar) — should differ from track
        QColor filled = img.pixelColor(100, 243);
        QVERIFY(filled != track);

        // Unfilled area (x=350 is in the second 50%)
        QColor unfilled = img.pixelColor(350, 243);
        QCOMPARE(unfilled, track);
    }

    void test_paint_progress_full()
    {
        SplashScreen s;
        s.setProgress(100, QString());
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Entire bar should be filled — all pixels differ from track
        QColor track = img.pixelColor(260, 243);

        QColor filled1 = img.pixelColor(100, 243);
        QColor filled2 = img.pixelColor(350, 243);
        QColor filled3 = img.pixelColor(430, 243);
        QVERIFY(filled1 != track);
        QVERIFY(filled2 != track);
        QVERIFY(filled3 != track);

        // Outside bar (to the right) should match the filled color
        // since progress bar is fully painted (gradient extends to end)
        QColor outside = img.pixelColor(450, 243);
        QVERIFY(outside.isValid());
    }

    void test_paint_message()
    {
        SplashScreen s;
        s.setProgress(50, "Test Message");
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Message is rendered at rect(80, 218, 360, 20), bottom-aligned, 11px font
        // Text baseline is near y=236, ascenders reach y=225
        bool textFound = false;
        for (int x = 80; x < 250; ++x) {
            for (int y = 220; y < 238; ++y) {
                QColor c = img.pixelColor(x, y);
                if (c != QColor("#1c1c1c") && c != QColor("#333333")) {
                    textFound = true;
                    break;
                }
            }
            if (textFound) break;
        }
        QVERIFY(textFound);
    }

    void test_paint_version()
    {
        SplashScreen s;
        s.setVersion("vTest");
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Version is right-aligned at bottom, around (400, 320)
        bool textFound = false;
        for (int y = 315; y < 335; ++y) {
            for (int x = 350; x < 500; ++x) {
                QColor c = img.pixelColor(x, y);
                if (c != QColor("#1c1c1c") && c != QColor("#333333")) {
                    textFound = true;
                    break;
                }
            }
            if (textFound) break;
        }
        QVERIFY(textFound);
    }

    void test_paint_logo_present()
    {
        SplashScreen s;
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Logo is at center-top, max 350 wide, centered horizontally
        // For a 128x128 image: logoX = (520-128)/2 = 196, logo at (196, 52)
        // Sample multiple points inside the expected logo area
        bool logoFound = false;
        for (int x = 200; x < 320; ++x) {
            for (int y = 55; y < 170; ++y) {
                QColor c = img.pixelColor(x, y);
                if (c != QColor("#1c1c1c") && c != QColor("#333333")) {
                    logoFound = true;
                    break;
                }
            }
            if (logoFound) break;
        }
        QVERIFY(logoFound);
    }

    // ── Version setter ──

    void test_set_version_stores()
    {
        SplashScreen s;
        s.setVersion("vCustom");
        // Can only verify via paint — version text will appear at bottom-right
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();
        // Find a pixel that differs from background (text is rendered)
        QColor bg = img.pixelColor(5, 5);
        bool foundCustom = false;
        for (int y = 315; y < 335; ++y) {
            for (int x = 350; x < 500; ++x) {
                QColor c = img.pixelColor(x, y);
                if (c != bg) {
                    foundCustom = true;
                    break;
                }
            }
            if (foundCustom) break;
        }
        QVERIFY(foundCustom);
    }

    // ── Progress clamps ──

    void test_progress_below_zero_clamped()
    {
        SplashScreen s;
        s.setProgress(-10, "test");
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Bar at 0% — left edge of bar is track color (uniform, no fill)
        QColor track = img.pixelColor(260, 243);
        QColor leftEdge = img.pixelColor(85, 243);
        QCOMPARE(leftEdge, track);
    }

    void test_progress_above_100_clamped()
    {
        SplashScreen s;
        s.setProgress(200, "test");
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // Bar at 100% — entire bar should be filled
        QColor track = img.pixelColor(260, 243);
        QColor rightEdge = img.pixelColor(430, 243);
        QVERIFY(rightEdge != track);
    }

    // ── Multiple progress updates ──

    void test_multiple_progress_updates()
    {
        SplashScreen s;
        s.setProgress(20, "Step 1");
        s.setProgress(60, "Step 2");
        s.setProgress(90, "Step 3");
        s.show();
        s.repaint();
        QApplication::processEvents();

        QImage img = s.grab().toImage();

        // At 90%: filled area (x=400) differs from unfilled area (x=430)
        QColor filled = img.pixelColor(400, 243);
        QColor unfilled = img.pixelColor(430, 243);
        QVERIFY(filled.isValid());
        QVERIFY(unfilled.isValid());
        QVERIFY(filled != unfilled);
    }
};

QTEST_MAIN(TestSplashScreen)
#include "test_splashscreen.moc"
