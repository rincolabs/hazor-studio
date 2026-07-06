#include "GeneralSettingsPage.hpp"
#include "AppSettingsMetrics.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/DarkerTheme.hpp"
#include "theme/ClassicTheme.hpp"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include "ui/AppCheckBox.hpp"
#include <QGroupBox>
#include <QLabel>
#include <QSettings>

GeneralSettingsPage::GeneralSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* th = ThemeManager::instance()->current();

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(24, 16, 24, 16);
    mainLay->setSpacing(th->spaceMD);

    // ── General ──
    auto* genGroup = new QGroupBox(tr("General"), this);
    auto* genForm = new QFormLayout(genGroup);
    genForm->setSpacing(th->spaceSM);
    genForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_themeCombo = new QComboBox(this);
    m_themeCombo->addItems({tr("Classic"), tr("Darker")});
    m_themeCombo->setToolTip(tr("Restart not required — changes take effect immediately"));
    genForm->addRow(tr("Theme:"), m_themeCombo);

    m_langCombo = new QComboBox(this);
    m_langCombo->addItems({tr("System Default"), QStringLiteral("English"), QStringLiteral("Portugu\u00EAs")});
    m_langCombo->setEnabled(false);
    genForm->addRow(tr("Language:"), m_langCombo);

    mainLay->addWidget(genGroup);


    // ── History ──
    auto* histGroup = new QGroupBox(tr("History"), this);
    auto* histForm = new QFormLayout(histGroup);
    histForm->setSpacing(th->spaceSM);
    histForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_undoLimitSpin = new QSpinBox(this);
    m_undoLimitSpin->setRange(10, 2000);
    m_undoLimitSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    m_undoLimitSpin->setSuffix(tr(" steps"));
    histForm->addRow(tr("Undo Limit:"), m_undoLimitSpin);

    m_autoSaveSpin = new QSpinBox(this);
    m_autoSaveSpin->setRange(0, 60);
    m_autoSaveSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    m_autoSaveSpin->setSuffix(tr(" min"));
    m_autoSaveSpin->setSpecialValueText(tr("Off"));
    histForm->addRow(tr("Auto-save:"), m_autoSaveSpin);

    mainLay->addWidget(histGroup);

    // ── Default Canvas ──
    auto* canvasGroup = new QGroupBox(tr("Default Canvas"), this);
    auto* canvasForm = new QFormLayout(canvasGroup);
    canvasForm->setSpacing(th->spaceSM);
    canvasForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    auto* sizeRow = new QWidget(this);
    auto* sizeLay = new QHBoxLayout(sizeRow);
    sizeLay->setContentsMargins(0, 0, 0, 0);
    sizeLay->setSpacing(th->spaceXS);
    m_canvasWSpin = new QSpinBox(this);
    m_canvasWSpin->setRange(1, 8192);
    m_canvasWSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    m_canvasHSpin = new QSpinBox(this);
    m_canvasHSpin->setRange(1, 8192);
    m_canvasHSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    sizeLay->addWidget(m_canvasWSpin);
    sizeLay->addWidget(new QLabel(QStringLiteral("\u00D7"), this));
    sizeLay->addWidget(m_canvasHSpin);
    sizeLay->addWidget(new QLabel(tr("px"), this));
    sizeLay->addStretch();
    canvasForm->addRow(tr("Size (W \u00D7 H):"), sizeRow);

    mainLay->addWidget(canvasGroup);

    mainLay->addStretch();

    // Load current values
    loadSettings();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &GeneralSettingsPage::applyTheme);
    applyTheme();

    connect(m_themeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &GeneralSettingsPage::onThemeChanged);
}

void GeneralSettingsPage::applyTheme()
{
    auto* th = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("GeneralSettingsPage { background: %1; }")
        .arg(th->colorSurface.name()));
}

void GeneralSettingsPage::loadSettings()
{
    auto* mgr = ThemeManager::instance();
    auto* th = mgr->current();
    QSettings settings;

    // Theme: read saved, apply if different from current
    int savedTheme = settings.value("theme", -1).toInt();
    if (savedTheme >= 0) {
        if (savedTheme >= m_themeCombo->count())
            savedTheme = 0;
        m_themeCombo->setCurrentIndex(savedTheme);
        int currentTheme = (dynamic_cast<DarkerTheme*>(th) ? 1 : 0);
        if (savedTheme != currentTheme) {
            onThemeChanged(savedTheme);
            th = mgr->current();
        }
    } else {
        m_themeCombo->setCurrentIndex(dynamic_cast<DarkerTheme*>(th) ? 1 : 0);
    }

    m_undoLimitSpin->setValue(settings.value("undoLimit", 500).toInt());
    m_autoSaveSpin->setValue(settings.value("autoSaveInterval", 0).toInt());
    m_canvasWSpin->setValue(settings.value("defaultCanvasW", 1920).toInt());
    m_canvasHSpin->setValue(settings.value("defaultCanvasH", 1080).toInt());
    m_langCombo->setCurrentIndex(settings.value("language", 0).toInt());
}

void GeneralSettingsPage::saveSettings()
{
    QSettings settings;
    settings.setValue("theme", m_themeCombo->currentIndex());
    settings.setValue("undoLimit", m_undoLimitSpin->value());
    settings.setValue("autoSaveInterval", m_autoSaveSpin->value());
    settings.setValue("defaultCanvasW", m_canvasWSpin->value());
    settings.setValue("defaultCanvasH", m_canvasHSpin->value());
    settings.setValue("language", m_langCombo->currentIndex());
}

void GeneralSettingsPage::onThemeChanged(int index)
{
    auto* mgr = ThemeManager::instance();
    Theme* newTheme = (index == 0)
        ? static_cast<Theme*>(new ClassicTheme(mgr))
        : static_cast<Theme*>(new DarkerTheme(mgr));
    newTheme->iconOpacity = mgr->current()->iconOpacity;  // preserve current value
    mgr->setTheme(newTheme);
    emit settingsChanged();
}

