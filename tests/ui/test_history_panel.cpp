#include <QApplication>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QtTest>
#include <QSignalSpy>
#include "ui/HistoryPanel.hpp"

class TestHistoryPanel : public QObject {
    Q_OBJECT

private:
    HistoryPanel* m_panel = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_undoBtn = nullptr;
    QPushButton* m_redoBtn = nullptr;
    QPushButton* m_clearBtn = nullptr;
    QLabel* m_countLabel = nullptr;

    void findWidgets() {
        m_list = m_panel->findChild<QListWidget*>();
        auto btns = m_panel->findChildren<QPushButton*>();
        m_undoBtn = m_redoBtn = m_clearBtn = nullptr;
        for (auto* b : btns) {
            QString txt = b->text().trimmed();
            if (txt.contains("Undo", Qt::CaseInsensitive))
                m_undoBtn = b;
            else if (txt.contains("Redo", Qt::CaseInsensitive))
                m_redoBtn = b;
            else if (txt.contains("Clear", Qt::CaseInsensitive))
                m_clearBtn = b;
        }
        auto labels = m_panel->findChildren<QLabel*>();
        for (auto* lbl : labels) {
            QString t = lbl->text();
            if (t.contains("of", Qt::CaseInsensitive))
                m_countLabel = lbl;
        }
    }

private slots:
    void init()
    {
        m_panel = new HistoryPanel();
        m_panel->show();
        QApplication::processEvents();
        findWidgets();
    }

    void cleanup()
    {
        delete m_panel;
        m_panel = nullptr;
    }

    // ── Initial state ──

    void test_initial_list_is_empty()
    {
        QVERIFY(m_list != nullptr);
        QCOMPARE(m_list->count(), 0);
    }

    void test_initial_buttons_disabled()
    {
        QVERIFY(m_undoBtn != nullptr);
        QVERIFY(m_redoBtn != nullptr);
        QVERIFY(m_clearBtn != nullptr);
        QVERIFY(!m_undoBtn->isEnabled());
        QVERIFY(!m_redoBtn->isEnabled());
        QVERIFY(!m_clearBtn->isEnabled());
    }

    void test_initial_count_label_zero()
    {
        QVERIFY(m_countLabel != nullptr);
        QVERIFY(m_countLabel->text().contains("0"));
    }

    // ── refresh() — content ──

    void test_refresh_empty_list()
    {
        m_panel->refresh({}, -1);
        QCOMPARE(m_list->count(), 0);
        QVERIFY(m_countLabel->text().contains("0"));
    }

    void test_refresh_single_entry()
    {
        m_panel->refresh({"test"}, 0);
        QCOMPARE(m_list->count(), 1);
    }

    void test_refresh_multiple_entries()
    {
        m_panel->refresh({"Open", "Blur", "Brightness"}, 1);
        QCOMPARE(m_list->count(), 3);
    }

    void test_refresh_item_text_format()
    {
        m_panel->refresh({"Gaussian Blur"}, 0);
        QString text = m_list->item(0)->text();
        QVERIFY(text.contains("Gaussian Blur"));
        QVERIFY(text.startsWith("1."));
    }

    void test_refresh_display_name_replaces_underscores()
    {
        m_panel->refresh({"gaussian_blur"}, 0);
        QString text = m_list->item(0)->text();
        QVERIFY(text.contains("Gaussian Blur"));
        QVERIFY(!text.contains("_"));
    }

    void test_refresh_display_name_capitalizes()
    {
        m_panel->refresh({"brightness"}, 0);
        QString text = m_list->item(0)->text();
        QVERIFY(text.contains("Brightness"));
    }

    void test_refresh_display_name_multi_word()
    {
        m_panel->refresh({"adjust_brightness_20"}, 0);
        QString text = m_list->item(0)->text();
        QVERIFY(text.contains("Adjust Brightness 20"));
    }

    // ── refresh() — buttons and label state ──

    void test_refresh_undo_enabled_with_history()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        QVERIFY(m_undoBtn->isEnabled());
    }

    void test_refresh_undo_enabled_single_entry()
    {
        m_panel->refresh({"a"}, 0);
        QVERIFY(m_undoBtn->isEnabled());
    }

    void test_refresh_redo_enabled_when_future_exists()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        QVERIFY(m_redoBtn->isEnabled());
    }

    void test_refresh_redo_disabled_at_latest_state()
    {
        m_panel->refresh({"a", "b", "c"}, 2);
        QVERIFY(!m_redoBtn->isEnabled());
    }

    void test_refresh_clear_enabled_with_history()
    {
        m_panel->refresh({"a", "b"}, 0);
        QVERIFY(m_clearBtn->isEnabled());
    }

    void test_refresh_clear_disabled_empty()
    {
        m_panel->refresh({}, -1);
        QVERIFY(!m_clearBtn->isEnabled());
    }

    void test_refresh_count_label_format()
    {
        m_panel->refresh({"a", "b", "c", "d", "e"}, 1);
        QVERIFY(m_countLabel->text().contains("2"));
        QVERIFY(m_countLabel->text().contains("5"));
    }

    void test_refresh_count_label_first_of_one()
    {
        m_panel->refresh({"only"}, 0);
        QVERIFY(m_countLabel->text().contains("1"));
        QVERIFY(m_countLabel->text().contains("1"));
    }

    void test_refresh_overwrites_previous()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        m_panel->refresh({"x", "y"}, 0);
        QCOMPARE(m_list->count(), 2);
        QVERIFY(m_countLabel->text().contains("1 of 2"));
    }

    // ── Item styling ──

    void test_past_item_font_normal()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        auto* item = m_list->item(0);
        QVERIFY(!item->font().bold());
        QVERIFY(!item->font().italic());
    }

    void test_current_item_font_bold()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        auto* item = m_list->item(1);
        QVERIFY(item->font().bold());
        QVERIFY(!item->font().italic());
    }

    void test_future_item_font_italic()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        auto* item = m_list->item(2);
        QVERIFY(item->font().italic());
        QVERIFY(!item->font().bold());
    }

    void test_current_item_foreground_valid()
    {
        m_panel->refresh({"a", "b"}, 0);
        auto* item = m_list->item(0);
        QVERIFY(item->foreground().color().isValid());
        QVERIFY(item->font().bold());
    }

    void test_past_item_not_bold()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        auto* item = m_list->item(0);
        QVERIFY(!item->font().bold());
        QVERIFY(!item->font().italic());
    }

    void test_future_item_italic()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        auto* item = m_list->item(2);
        QVERIFY(item->font().italic());
        QVERIFY(!item->font().bold());
    }

    void test_current_item_has_background()
    {
        m_panel->refresh({"a", "b"}, 0);
        auto* item = m_list->item(0);
        QVERIFY(item->background().style() != Qt::NoBrush);
        QVERIFY(item->foreground().color().isValid());
    }

    void test_past_item_no_background()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        auto* item = m_list->item(0);
        QCOMPARE(item->background().style(), Qt::NoBrush);
    }

    void test_future_item_no_background()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        auto* item = m_list->item(2);
        QCOMPARE(item->background().style(), Qt::NoBrush);
    }

    void test_states_foregrounds_differ()
    {
        // Past, current, and future items should each have different foreground colors
        m_panel->refresh({"first", "second", "third", "fourth"}, 2);
        QColor pastFg    = m_list->item(0)->foreground().color();
        QColor currentFg = m_list->item(2)->foreground().color();
        QColor futureFg  = m_list->item(3)->foreground().color();

        QVERIFY(pastFg != currentFg);
        QVERIFY(currentFg != futureFg);
        QVERIFY(pastFg != futureFg);

        QVERIFY(m_list->item(0)->background().style() == Qt::NoBrush);
        QVERIFY(m_list->item(2)->background().style() != Qt::NoBrush);
        QVERIFY(m_list->item(3)->background().style() == Qt::NoBrush);

        QVERIFY(!m_list->item(0)->font().bold());
        QVERIFY(m_list->item(2)->font().bold());
        QVERIFY(!m_list->item(3)->font().bold());
        QVERIFY(!m_list->item(0)->font().italic());
        QVERIFY(m_list->item(3)->font().italic());
    }

    // ── clear() ──

    void test_clear_empties_list()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        m_panel->clear();
        QCOMPARE(m_list->count(), 0);
    }

    void test_clear_disables_all_buttons()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        m_panel->clear();
        QVERIFY(!m_undoBtn->isEnabled());
        QVERIFY(!m_redoBtn->isEnabled());
        QVERIFY(!m_clearBtn->isEnabled());
    }

    void test_clear_resets_count_label()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        m_panel->clear();
        QVERIFY(m_countLabel->text().contains("0 of 0"));
    }

    void test_clear_on_already_empty()
    {
        m_panel->clear();
        QCOMPARE(m_list->count(), 0);
        QVERIFY(!m_undoBtn->isEnabled());
        QVERIFY(!m_redoBtn->isEnabled());
        QVERIFY(!m_clearBtn->isEnabled());
    }

    // ── Signals ──

    void test_click_item_emits_jump_requested()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        QSignalSpy spy(m_panel, &HistoryPanel::jumpRequested);

        auto* item = m_list->item(2);
        QRect r = m_list->visualItemRect(item);
        QTest::mouseClick(m_list->viewport(), Qt::LeftButton, {}, r.center());

        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 2);
    }

    void test_click_current_item_does_not_emit_jump()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        QSignalSpy spy(m_panel, &HistoryPanel::jumpRequested);

        auto* item = m_list->item(1);
        QRect r = m_list->visualItemRect(item);
        QTest::mouseClick(m_list->viewport(), Qt::LeftButton, {}, r.center());

        QCOMPARE(spy.count(), 0);
    }

    void test_click_undo_button_emits_signal()
    {
        m_panel->refresh({"a", "b"}, 0);
        QSignalSpy spy(m_panel, &HistoryPanel::undoRequested);

        QTest::mouseClick(m_undoBtn, Qt::LeftButton);

        QCOMPARE(spy.count(), 1);
    }

    void test_click_redo_button_emits_signal()
    {
        m_panel->refresh({"a", "b", "c"}, 1);
        QSignalSpy spy(m_panel, &HistoryPanel::redoRequested);

        QTest::mouseClick(m_redoBtn, Qt::LeftButton);

        QCOMPARE(spy.count(), 1);
    }

    void test_click_clear_button_emits_signal()
    {
        m_panel->refresh({"a", "b"}, 0);
        QSignalSpy spy(m_panel, &HistoryPanel::clearRequested);

        QTest::mouseClick(m_clearBtn, Qt::LeftButton);

        QCOMPARE(spy.count(), 1);
    }

    void test_jump_requested_index_matches_clicked_item()
    {
        m_panel->refresh({"first", "second", "third", "fourth", "fifth"}, 2);
        QSignalSpy spy(m_panel, &HistoryPanel::jumpRequested);

        // Click item 4 (future)
        QTest::mouseClick(m_list->viewport(), Qt::LeftButton, {},
                          m_list->visualItemRect(m_list->item(4)).center());
        QCOMPARE(spy.count(), 1);
        QCOMPARE(spy.at(0).at(0).toInt(), 4);

        // Click item 0 (past)
        QTest::mouseClick(m_list->viewport(), Qt::LeftButton, {},
                          m_list->visualItemRect(m_list->item(0)).center());
        QCOMPARE(spy.count(), 2);
        QCOMPARE(spy.at(1).at(0).toInt(), 0);
    }

    void test_undo_redo_clear_disable_when_disabled()
    {
        m_panel->refresh({}, -1);
        QVERIFY(!m_undoBtn->isEnabled());
        QVERIFY(!m_redoBtn->isEnabled());
        QVERIFY(!m_clearBtn->isEnabled());
    }

    void test_refresh_redo_enabled_with_future()
    {
        QVERIFY(m_redoBtn != nullptr);
        m_panel->refresh({"a", "b"}, 0);
        QVERIFY(m_redoBtn->isEnabled());
    }

    void test_refresh_undo_redo_clear_cycle()
    {
        QCOMPARE(m_list->count(), 0);

        // currentIndex=0, total=1
        m_panel->refresh({"only"}, 0);
        QCOMPARE(m_list->count(), 1);
        QVERIFY(m_undoBtn->isEnabled());
        QVERIFY(!m_redoBtn->isEnabled());

        // currentIndex=0, total=2
        m_panel->refresh({"first", "second"}, 0);
        QCOMPARE(m_list->count(), 2);
        QVERIFY(m_undoBtn->isEnabled());
        QVERIFY(m_redoBtn->isEnabled());
        QVERIFY(m_clearBtn->isEnabled());

        // currentIndex=1, total=2 (at end, no redo)
        m_panel->refresh({"first", "second"}, 1);
        QVERIFY(m_undoBtn->isEnabled());
        QVERIFY(!m_redoBtn->isEnabled());

        // currentIndex=-1 (at position before any state - all are future)
        m_panel->refresh({"first", "second"}, -1);
        QVERIFY(!m_undoBtn->isEnabled());
        QVERIFY(m_redoBtn->isEnabled());  // can redo into state 0
        QVERIFY(m_clearBtn->isEnabled());
    }
};

QTEST_MAIN(TestHistoryPanel)
#include "test_history_panel.moc"
