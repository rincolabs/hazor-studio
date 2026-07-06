#include "HistogramStatsWidget.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QGridLayout>
#include <QLabel>
#include <QList>
#include <QLocale>

namespace {
QString fmtFloat(float v) { return QString::number(v, 'f', 2); }
QString fmtInt(int v)
{
    // Group thousands for readability (e.g. 1,234,567).
    return QLocale().toString(static_cast<qlonglong>(v));
}
} // namespace

HistogramStatsWidget::HistogramStatsWidget(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("histogramStats"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* t = ThemeManager::instance()->current();
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(t->spaceSM, t->spaceXS, t->spaceSM, t->spaceXS);
    grid->setHorizontalSpacing(t->spaceMD);
    grid->setVerticalSpacing(1);

    auto nameLabel = [this](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setProperty("histName", true);
        return l;
    };

    // Left column.
    grid->addWidget(nameLabel(tr("Mean:")),    0, 0);
    grid->addWidget(makeValue(&m_meanVal),     0, 1);
    grid->addWidget(nameLabel(tr("Std Dev:")), 1, 0);
    grid->addWidget(makeValue(&m_stdDevVal),   1, 1);
    grid->addWidget(nameLabel(tr("Median:")),  2, 0);
    grid->addWidget(makeValue(&m_medianVal),   2, 1);
    grid->addWidget(nameLabel(tr("Pixels:")),  3, 0);
    grid->addWidget(makeValue(&m_pixelsVal),   3, 1);

    // Right column.
    grid->addWidget(nameLabel(tr("Level:")),       0, 2);
    grid->addWidget(makeValue(&m_levelVal),        0, 3);
    grid->addWidget(nameLabel(tr("Count:")),       1, 2);
    grid->addWidget(makeValue(&m_countVal),        1, 3);
    grid->addWidget(nameLabel(tr("Percentile:")),  2, 2);
    grid->addWidget(makeValue(&m_percentileVal),   2, 3);
    grid->addWidget(nameLabel(tr("Cache Level:")), 3, 2);
    grid->addWidget(makeValue(&m_cacheLevelVal),   3, 3);

    grid->setColumnStretch(1, 1);
    grid->setColumnStretch(3, 1);

    applyLabelStyles();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, [this]() { applyLabelStyles(); });

    clear();
}

QLabel* HistogramStatsWidget::makeValue(QLabel** out)
{
    auto* l = new QLabel("-", this);
    l->setProperty("histValue", true);
    *out = l;
    return l;
}

void HistogramStatsWidget::applyLabelStyles()
{
    auto* t = ThemeManager::instance()->current();
    const QString nameStyle = QStringLiteral("color:%1; font-size:%2px;")
        .arg(t->colorTextSecondary.name()).arg(t->fontSizeXS);
    const QString valueStyle = QStringLiteral("color:%1; font-size:%2px; font-weight:600;")
        .arg(t->colorTextBright.name()).arg(t->fontSizeXS);

    const auto labels = findChildren<QLabel*>();
    for (auto* l : labels) {
        if (l->property("histName").toBool())
            l->setStyleSheet(nameStyle);
        else if (l->property("histValue").toBool())
            l->setStyleSheet(valueStyle);
    }
}

void HistogramStatsWidget::setStats(float mean, float stdDev, float median, int pixels)
{
    m_meanVal->setText(fmtFloat(mean));
    m_stdDevVal->setText(fmtFloat(stdDev));
    m_medianVal->setText(QString::number(static_cast<int>(median + 0.5f)));
    m_pixelsVal->setText(fmtInt(pixels));
}

void HistogramStatsWidget::setProbe(int level, int count, double percentile)
{
    if (level < 0) {
        m_levelVal->setText("-");
        m_countVal->setText("-");
        m_percentileVal->setText("-");
        return;
    }
    m_levelVal->setText(QString::number(level));
    m_countVal->setText(fmtInt(count));
    m_percentileVal->setText(QString::number(percentile, 'f', 2));
}

void HistogramStatsWidget::setCacheLevel(const QString& text)
{
    m_cacheLevelVal->setText(text);
}

void HistogramStatsWidget::clear()
{
    m_meanVal->setText("-");
    m_stdDevVal->setText("-");
    m_medianVal->setText("-");
    m_pixelsVal->setText("-");
    setProbe(-1, 0, 0.0);
    m_cacheLevelVal->setText("-");
}
