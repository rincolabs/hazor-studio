#pragma once

#include <QMap>
#include "ComponentStyle.hpp"

struct QtTheme
{
    // ── Base widget styles ──
    QtComponentStyle QWidget;
    QtComponentStyle QMainWindow;
    QtComponentStyle QFrame;
    QtComponentStyle QLabel;
    QtComponentStyle QPushButton;
    QtComponentStyle QToolButton;
    QtComponentStyle QLineEdit;
    QtComponentStyle QTextEdit;
    QtComponentStyle QPlainTextEdit;
    QtComponentStyle QComboBox;
    QtComponentStyle QCheckBox;
    QtComponentStyle QRadioButton;
    QtComponentStyle QSlider;
    QtComponentStyle QSpinBox;
    QtComponentStyle QDoubleSpinBox;
    QtComponentStyle QTabWidget;
    QtComponentStyle QTabBar;
    QtComponentStyle QScrollBar;
    QtComponentStyle QMenuBar;
    QtComponentStyle QMenu;
    QtComponentStyle QStatusBar;
    QtComponentStyle QDialog;
    QtComponentStyle QListWidget;
    QtComponentStyle QTreeWidget;
    QtComponentStyle QTableWidget;
    QtComponentStyle QDockWidget;
    QtComponentStyle QGroupBox;
    QtComponentStyle QSplitter;
    QtComponentStyle QToolBar;

    // ── Named widget overrides (key = full selector, e.g. "QPushButton#okBtn") ──
    QMap<QString, QtComponentStyle> namedStyles;
};
