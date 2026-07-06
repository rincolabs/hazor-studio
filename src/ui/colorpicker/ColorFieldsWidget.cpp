#include "ColorFieldsWidget.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QLabel>
#include <QLineEdit>
#include <QGridLayout>
#include <QIntValidator>
#include <QRegularExpressionValidator>
#include <QSignalBlocker>
#include <QtMath>

static const char* fieldStyle()
{
    return "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
           "border-radius: 2px; padding: 2px 4px; font-size: 11px; }";
}

static void styleField(QLineEdit* edit, Theme* t, bool readOnly = false)
{
    QString bg = readOnly ? t->colorBackgroundPrimary.name() : t->colorBackgroundTertiary.name();
    QString fg = readOnly ? t->colorTextDisabled.name() : t->colorTextPrimary.name();
    edit->setStyleSheet(QString(fieldStyle()).arg(bg, fg, t->colorBorder.name()));
    edit->setReadOnly(readOnly);
    edit->setAlignment(Qt::AlignCenter);
    edit->setFixedWidth(52);
    edit->setFixedHeight(22);
}

static QLineEdit* makeField(QWidget* parent)
{
    auto* e = new QLineEdit(parent);
    e->setAlignment(Qt::AlignCenter);
    e->setFixedWidth(52);
    e->setFixedHeight(22);
    return e;
}

static QLabel* makeLabel(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setFixedWidth(20);
    l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    return l;
}

static void setIntValidator(QLineEdit* edit, int min, int max)
{
    auto* v = new QIntValidator(min, max, edit);
    edit->setValidator(v);
}

ColorFieldsWidget::ColorFieldsWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
}

void ColorFieldsWidget::setupUi()
{
    auto* t = ThemeManager::instance()->current();
    auto* grid = new QGridLayout(this);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(4);
    grid->setVerticalSpacing(4);

    // Create all fields + validators first, then arrange them in two columns
    // Two-column layout: left = HSV / RGB / Hex, right = Lab / CMYK.
    m_hueEdit = makeField(this);
    m_satEdit = makeField(this);
    m_briEdit = makeField(this);
    setIntValidator(m_hueEdit, 0, 360);
    setIntValidator(m_satEdit, 0, 100);
    setIntValidator(m_briEdit, 0, 100);

    m_rEdit = makeField(this);
    m_gEdit = makeField(this);
    m_bEdit = makeField(this);
    setIntValidator(m_rEdit, 0, 255);
    setIntValidator(m_gEdit, 0, 255);
    setIntValidator(m_bEdit, 0, 255);

    m_hexEdit = new QLineEdit(this);
    m_hexEdit->setAlignment(Qt::AlignCenter);
    m_hexEdit->setFixedHeight(22);
    m_hexEdit->setMaxLength(7);
    auto* hexRx = new QRegularExpressionValidator(QRegularExpression("#?[0-9A-Fa-f]{0,6}"), m_hexEdit);
    m_hexEdit->setValidator(hexRx);

    m_lEdit = makeField(this);
    m_aEdit = makeField(this);
    m_bLabEdit = makeField(this);

    m_cEdit = makeField(this);
    m_mEdit = makeField(this);
    m_yEdit = makeField(this);
    m_kEdit = makeField(this);
    setIntValidator(m_cEdit, 0, 100);
    setIntValidator(m_mEdit, 0, 100);
    setIntValidator(m_yEdit, 0, 100);
    setIntValidator(m_kEdit, 0, 100);

    auto unit = [&](const QString& text) {
        auto* l = new QLabel(text, this);
        l->setStyleSheet(QString("color: %1; font-size: 11px;").arg(t->colorTextSecondary.name()));
        l->setFixedWidth(12);
        l->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        return l;
    };
    // Left group occupies columns 0-2, right group columns 4-6 (column 3 = gap).
    auto addLeft = [&](int r, const QString& lab, QLineEdit* f, const QString& u = QString()) {
        grid->addWidget(makeLabel(lab, this), r, 0);
        grid->addWidget(f, r, 1);
        if (!u.isEmpty()) grid->addWidget(unit(u), r, 2);
    };
    auto addRight = [&](int r, const QString& lab, QLineEdit* f, const QString& u = QString()) {
        grid->addWidget(makeLabel(lab, this), r, 4);
        grid->addWidget(f, r, 5);
        if (!u.isEmpty()) grid->addWidget(unit(u), r, 6);
    };

    addLeft(0, tr("H"), m_hueEdit, tr("°"));
    addLeft(1, tr("S"), m_satEdit, tr("%"));
    addLeft(2, tr("B"), m_briEdit, tr("%"));
    addLeft(3, tr("R"), m_rEdit);
    addLeft(4, tr("G"), m_gEdit);
    addLeft(5, tr("B"), m_bEdit);

    grid->addWidget(new QLabel("#", this), 6, 0);
    grid->addWidget(m_hexEdit, 6, 1, 1, 2);

    addRight(0, tr("L"), m_lEdit);
    addRight(1, tr("a"), m_aEdit);
    addRight(2, tr("b"), m_bLabEdit);
    addRight(3, tr("C"), m_cEdit, tr("%"));
    addRight(4, tr("M"), m_mEdit, tr("%"));
    addRight(5, tr("Y"), m_yEdit, tr("%"));
    addRight(6, tr("K"), m_kEdit, tr("%"));

    grid->setColumnMinimumWidth(3, 12);   // gap between the two columns
    grid->setColumnStretch(7, 1);         // push everything left

    maybeMarkEditable();

    connect(m_hueEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onHsvChanged);
    connect(m_satEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onHsvChanged);
    connect(m_briEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onHsvChanged);

    connect(m_rEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onRgbChanged);
    connect(m_gEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onRgbChanged);
    connect(m_bEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onRgbChanged);

    connect(m_hexEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onHexChanged);

    connect(m_cEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onCmykChanged);
    connect(m_mEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onCmykChanged);
    connect(m_yEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onCmykChanged);
    connect(m_kEdit, &QLineEdit::editingFinished, this, &ColorFieldsWidget::onCmykChanged);
}

void ColorFieldsWidget::maybeMarkEditable()
{
    auto* t = ThemeManager::instance()->current();

    styleField(m_hueEdit, t);
    styleField(m_satEdit, t);
    styleField(m_briEdit, t);
    styleField(m_rEdit, t);
    styleField(m_gEdit, t);
    styleField(m_bEdit, t);
    styleField(m_cEdit, t);
    styleField(m_mEdit, t);
    styleField(m_yEdit, t);
    styleField(m_kEdit, t);

    styleField(m_lEdit, t, true);
    styleField(m_aEdit, t, true);
    styleField(m_bLabEdit, t, true);

    QString hexStyle = QString(
        "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 2px; padding: 2px 4px; font-size: 11px; }")
        .arg(t->colorBackgroundTertiary.name(), t->colorTextPrimary.name(), t->colorBorder.name());
    m_hexEdit->setStyleSheet(hexStyle);
}

void ColorFieldsWidget::setColor(const QColor& color)
{
    m_updatingFields = true;
    updateFieldsFromColor(color);
    m_updatingFields = false;
}

void ColorFieldsWidget::updateFieldsFromColor(const QColor& color)
{
    QSignalBlocker hb(m_hueEdit);
    QSignalBlocker sb(m_satEdit);
    QSignalBlocker bb(m_briEdit);
    QSignalBlocker rb(m_rEdit);
    QSignalBlocker gb(m_gEdit);
    QSignalBlocker blb(m_bEdit);
    QSignalBlocker hexb(m_hexEdit);
    QSignalBlocker lb(m_lEdit);
    QSignalBlocker ab(m_aEdit);
    QSignalBlocker blab(m_bLabEdit);
    QSignalBlocker cb(m_cEdit);
    QSignalBlocker mb(m_mEdit);
    QSignalBlocker yb(m_yEdit);
    QSignalBlocker kb(m_kEdit);

    m_hueEdit->setText(QString::number(static_cast<int>(color.hueF() * 360.0)));
    m_satEdit->setText(QString::number(static_cast<int>(color.saturationF() * 100.0)));
    m_briEdit->setText(QString::number(static_cast<int>(color.valueF() * 100.0)));

    m_rEdit->setText(QString::number(color.red()));
    m_gEdit->setText(QString::number(color.green()));
    m_bEdit->setText(QString::number(color.blue()));

    m_hexEdit->setText(color.name(QColor::HexRgb).mid(1).toUpper());

    double L, a, bLab;
    computeLab(color, L, a, bLab);
    m_lEdit->setText(QString::number(static_cast<int>(L)));
    m_aEdit->setText(QString::number(static_cast<int>(a)));
    m_bLabEdit->setText(QString::number(static_cast<int>(bLab)));

    auto cmyk = color.toCmyk();
    m_cEdit->setText(QString::number(static_cast<int>(cmyk.cyanF() * 100.0)));
    m_mEdit->setText(QString::number(static_cast<int>(cmyk.magentaF() * 100.0)));
    m_yEdit->setText(QString::number(static_cast<int>(cmyk.yellowF() * 100.0)));
    m_kEdit->setText(QString::number(static_cast<int>(cmyk.blackF() * 100.0)));
}

QColor ColorFieldsWidget::currentColor() const
{
    bool ok;
    int h = m_hueEdit->text().toInt(&ok);
    int s = m_satEdit->text().toInt(&ok);
    int v = m_briEdit->text().toInt(&ok);

    int r = m_rEdit->text().toInt(&ok);
    int g = m_gEdit->text().toInt(&ok);
    int b = m_bEdit->text().toInt(&ok);

    QColor result;
    if (m_hueEdit->hasFocus() || m_satEdit->hasFocus() || m_briEdit->hasFocus())
        result = QColor::fromHsvF(qBound(0, h, 360) / 360.0,
                                   qBound(0, s, 100) / 100.0,
                                   qBound(0, v, 100) / 100.0);
    else if (m_rEdit->hasFocus() || m_gEdit->hasFocus() || m_bEdit->hasFocus())
        result = QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
    else
        result = QColor::fromHsvF(qBound(0, h, 360) / 360.0,
                                   qBound(0, s, 100) / 100.0,
                                   qBound(0, v, 100) / 100.0);

    return result.isValid() ? result : QColor(Qt::black);
}

void ColorFieldsWidget::onHsvChanged()
{
    if (m_updatingFields) return;

    bool ok;
    int h = m_hueEdit->text().toInt(&ok);
    int s = m_satEdit->text().toInt(&ok);
    int v = m_briEdit->text().toInt(&ok);

    QColor c = QColor::fromHsvF(qBound(0, h, 360) / 360.0,
                                 qBound(0, s, 100) / 100.0,
                                 qBound(0, v, 100) / 100.0);
    emit colorChanged(c);
}

void ColorFieldsWidget::onRgbChanged()
{
    if (m_updatingFields) return;

    bool ok;
    int r = m_rEdit->text().toInt(&ok);
    int g = m_gEdit->text().toInt(&ok);
    int b = m_bEdit->text().toInt(&ok);

    QColor c(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
    emit colorChanged(c);
}

void ColorFieldsWidget::onHexChanged()
{
    if (m_updatingFields) return;

    QString hex = m_hexEdit->text().trimmed();
    if (hex.startsWith('#'))
        hex = hex.mid(1);

    if (hex.length() == 6) {
        QColor c("#" + hex);
        if (c.isValid()) {
            emit colorChanged(c);
            return;
        }
    }

    updateFieldsFromColor(currentColor());
}

void ColorFieldsWidget::onCmykChanged()
{
    if (m_updatingFields) return;

    bool ok;
    int c = m_cEdit->text().toInt(&ok);
    int m = m_mEdit->text().toInt(&ok);
    int y = m_yEdit->text().toInt(&ok);
    int k = m_kEdit->text().toInt(&ok);

    QColor color = QColor::fromCmykF(qBound(0, c, 100) / 100.0,
                                      qBound(0, m, 100) / 100.0,
                                      qBound(0, y, 100) / 100.0,
                                      qBound(0, k, 100) / 100.0);
    emit colorChanged(color);
}

void ColorFieldsWidget::computeLab(const QColor& color, double& L, double& ai, double& bi) const
{
    double r = color.redF();
    double g = color.greenF();
    double b = color.blueF();

    auto gamma = [](double c) -> double {
        return (c <= 0.04045) ? (c / 12.92) : std::pow((c + 0.055) / 1.055, 2.4);
    };

    double rLin = gamma(r);
    double gLin = gamma(g);
    double bLin = gamma(b);

    double x = rLin * 0.4124564 + gLin * 0.3575761 + bLin * 0.1804375;
    double y = rLin * 0.2126729 + gLin * 0.7151522 + bLin * 0.0721750;
    double z = rLin * 0.0193339 + gLin * 0.1191920 + bLin * 0.9503041;

    auto f = [](double t) -> double {
        const double d = 6.0 / 29.0;
        return (t > d * d * d) ? std::cbrt(t) : (t / (3.0 * d * d) + 4.0 / 29.0);
    };

    const double xn = 0.95047;
    const double yn = 1.00000;
    const double zn = 1.08883;

    double fy = f(y / yn);
    L = 116.0 * fy - 16.0;
    ai = 500.0 * (f(x / xn) - fy);
    bi = 200.0 * (fy - f(z / zn));

    L = qBound(0.0, L, 100.0);
    ai = qBound(-128.0, ai, 127.0);
    bi = qBound(-128.0, bi, 127.0);
}
