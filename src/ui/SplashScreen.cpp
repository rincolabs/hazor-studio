#include "SplashScreen.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QFont>
#include <QApplication>
#include <QScreen>
#include <QTimer>
#include <QPaintEvent>

SplashScreen::SplashScreen(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_OpaquePaintEvent, true);
    setAutoFillBackground(false);
    setFixedSize(900, 600);

    auto* screen = QApplication::primaryScreen();
    if (screen) {
        QRect geo = screen->geometry();
        move(geo.center() - rect().center());
    }

    m_logo = QPixmap(":/icons/splash.jpg");
    setProgress(0, QString());
    m_elapsed.start();
}

void SplashScreen::setMinimumDuration(int ms)
{
    m_minDurationMs = ms;
}

void SplashScreen::setProgress(int percent, const QString& message)
{
    m_progress = qBound(0, percent, 100);
    m_message = message;
    update();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void SplashScreen::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    m_message = QStringLiteral("Ready.");

    int elapsed = static_cast<int>(m_elapsed.elapsed());
    int remaining = m_minDurationMs - elapsed;

    if (remaining > 0 && m_progress < 100) {
        auto* anim = new QVariantAnimation(this);
        anim->setDuration(remaining);
        anim->setStartValue(m_progress);
        anim->setEndValue(100);
        anim->setEasingCurve(QEasingCurve::Linear);
        connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
            m_progress = v.toInt();
            update();
        });
        connect(anim, &QVariantAnimation::finished, this, [this]() {
            emit finished();
            close();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        m_progress = 100;
        update();
        emit finished();
        close();
    }
}

void SplashScreen::setVersion(const QString& version)
{
    m_version = version;
    update();
}

void SplashScreen::paintEvent(QPaintEvent*)
{
    const int w = width();
    const int h = height();

    auto* t = ThemeManager::instance()->current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);

    // Background
    p.fillRect(rect(), t->colorSplashBackground);

    // Border outline
    p.setPen(QPen(t->colorSplashBorder, 1));
    p.drawRect(0, 0, w - 1, h - 1);

    // ── Logo ──
    int logoW = 0, logoH = 0, logoY = 0;
    if (!m_logo.isNull()) {
        int natW = m_logo.width() / m_logo.devicePixelRatio();
        int natH = m_logo.height() / m_logo.devicePixelRatio();
        if (natW > 900) {
            logoW = 900;
            logoH = natH * 900 / natW;
        } else {
            logoW = natW;
            logoH = natH;
        }

        int logoX = (w - logoW) / 2;
        p.drawPixmap(logoX, logoY, logoW, logoH, m_logo);
    }

    // ── Progress bar ──
    const int barX = (w / 2) - 150;
    const int barY = h - 92;
    const int barW = 300;
    const int barH = 3;
    const int radius = 2;

    // Track
    // p.setPen(Qt::NoPen);
    // p.setBrush(t->colorSplashProgressTrack);
    // p.drawRoundedRect(barX, barY, barW, barH, radius, radius);

    // // Fill
    // if (m_progress > 0) {
    //     int fillW = barW * m_progress / 100;
    //     QLinearGradient grad(barX, 0, barX + fillW, 0);
    //     grad.setColorAt(0.0, t->colorSplashProgressStart);
    //     grad.setColorAt(1.0, t->colorSplashProgressEnd);
    //     p.setBrush(grad);
    //     p.drawRoundedRect(barX, barY, fillW, barH, radius, radius);
    // }

    // ── Loading message ──
    QFont msgFont = font();
    msgFont.setPixelSize(t->fontSizeMD);
    p.setFont(msgFont);

    p.setPen(t->colorSplashText);
    p.drawText(QRect(barX, h - 62, barW, 20), Qt::AlignHCenter | Qt::AlignBottom, m_message);

    // ── Version + info footer ──
    QFont infoFont = font();
    infoFont.setPixelSize(t->fontSizeXS);
    p.setFont(infoFont);

    QString footer = m_version.isEmpty() ? QStringLiteral("v") + qApp->applicationVersion() : m_version;
    p.setPen(t->colorSplashVersion);
    p.drawText(QRect(20, h - 22, w - 40, 16), Qt::AlignRight, footer);

    if (!m_firstPainted) {
        m_firstPainted = true;
        QTimer::singleShot(0, this, [this]() {
            emit firstPainted();
        });
    }
}
