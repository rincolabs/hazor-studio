#include "HomeWorkspace.hpp"

#include "io/RecentFilesManager.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/FlowLayout.hpp"
#include "ui/RecentThumbnailCache.hpp"

#include <QButtonGroup>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPen>
#include <QPushButton>
#include <QScrollArea>
#include <QSet>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <numeric>
#include <utility>
#include <vector>

namespace {

constexpr int kSidebarWidth = 212;
constexpr int kSidebarLogoWidth = 164;
constexpr int kSidebarLogoHeight = 92;
constexpr int kHeaderHeight = 132;
constexpr int kHeaderHorizontalMargin = 28;
constexpr int kHeaderVerticalMargin = 24;
constexpr int kNewDocumentButtonWidth = 172;
constexpr int kOpenButtonWidth = 112;
constexpr int kHeaderButtonHeight = 44;
constexpr int kDocumentCardWidth = 248;
constexpr int kDocumentCardHeight = kDocumentCardWidth;
constexpr int kCardPreviewWidth = 232;
constexpr int kCardPreviewHeight = 180;

struct SocialTemplatePreset {
    QString id;
    QString platform;
    QString name;
    int width = 0;
    int height = 0;
};

struct PlatformFilter {
    QString id;
    QString label;
};

const std::vector<SocialTemplatePreset>& socialTemplates()
{
    static const std::vector<SocialTemplatePreset> templates = {
        {QStringLiteral("instagram-square-post"), QStringLiteral("instagram"), QStringLiteral("Square Post"), 1080, 1080},
        {QStringLiteral("instagram-portrait-post-4-5"), QStringLiteral("instagram"), QStringLiteral("Portrait Post (4:5)"), 1080, 1350},
        {QStringLiteral("instagram-portrait-post-3-4"), QStringLiteral("instagram"), QStringLiteral("Portrait Post (3:4)"), 1080, 1440},
        {QStringLiteral("instagram-story"), QStringLiteral("instagram"), QStringLiteral("Story"), 1080, 1920},
        {QStringLiteral("instagram-reel"), QStringLiteral("instagram"), QStringLiteral("Reel"), 1080, 1920},
        {QStringLiteral("instagram-carousel"), QStringLiteral("instagram"), QStringLiteral("Carousel"), 1080, 1350},

        {QStringLiteral("tiktok-vertical-video-cover"), QStringLiteral("tiktok"), QStringLiteral("Vertical Video / Cover"), 1080, 1920},
        {QStringLiteral("tiktok-square-post"), QStringLiteral("tiktok"), QStringLiteral("Square Post"), 1080, 1080},

        {QStringLiteral("youtube-thumbnail"), QStringLiteral("youtube"), QStringLiteral("Thumbnail"), 1280, 720},
        {QStringLiteral("youtube-thumbnail-hq"), QStringLiteral("youtube"), QStringLiteral("Thumbnail HQ"), 3840, 2160},
        {QStringLiteral("youtube-shorts"), QStringLiteral("youtube"), QStringLiteral("Shorts"), 1080, 1920},
        {QStringLiteral("youtube-channel-banner"), QStringLiteral("youtube"), QStringLiteral("Channel Banner"), 2560, 1440},

        {QStringLiteral("facebook-square-post"), QStringLiteral("facebook"), QStringLiteral("Square Post"), 1080, 1080},
        {QStringLiteral("facebook-story-reel"), QStringLiteral("facebook"), QStringLiteral("Story / Reel"), 1080, 1920},
        {QStringLiteral("facebook-profile-cover"), QStringLiteral("facebook"), QStringLiteral("Profile Cover"), 851, 315},

        {QStringLiteral("linkedin-square-post"), QStringLiteral("linkedin"), QStringLiteral("Square Post"), 1080, 1080},
        {QStringLiteral("linkedin-landscape-post"), QStringLiteral("linkedin"), QStringLiteral("Landscape Post"), 1200, 628},
        {QStringLiteral("linkedin-profile-banner"), QStringLiteral("linkedin"), QStringLiteral("Profile Banner"), 1584, 396},

        {QStringLiteral("pinterest-standard-pin"), QStringLiteral("pinterest"), QStringLiteral("Standard Pin"), 1000, 1500},

        {QStringLiteral("x-landscape-post"), QStringLiteral("x"), QStringLiteral("Landscape Post"), 1200, 675},
        {QStringLiteral("x-profile-banner"), QStringLiteral("x"), QStringLiteral("Profile Banner"), 1500, 500},
    };
    return templates;
}

QString platformDisplayName(const QString& platform)
{
    if (platform == QLatin1String("tiktok"))
        return QStringLiteral("TikTok");
    if (platform == QLatin1String("youtube"))
        return QStringLiteral("YouTube");
    if (platform == QLatin1String("linkedin"))
        return QStringLiteral("LinkedIn");
    if (platform == QLatin1String("x"))
        return QStringLiteral("X");

    QString label = platform;
    if (!label.isEmpty())
        label[0] = label[0].toUpper();
    return label;
}

const std::vector<PlatformFilter>& platformFilters()
{
    static const std::vector<PlatformFilter> filters = [] {
        std::vector<PlatformFilter> result = {
            {QStringLiteral("all"), QStringLiteral("All")},
        };
        QSet<QString> seen;
        for (const SocialTemplatePreset& preset : socialTemplates()) {
            if (seen.contains(preset.platform))
                continue;
            seen.insert(preset.platform);
            result.push_back({preset.platform, platformDisplayName(preset.platform)});
        }
        return result;
    }();
    return filters;
}

QString aspectRatioText(int width, int height)
{
    const int divisor = std::gcd(width, height);
    const int ratioWidth = width / divisor;
    const int ratioHeight = height / divisor;
    if (ratioWidth <= 20 && ratioHeight <= 20)
        return QStringLiteral("%1:%2").arg(ratioWidth).arg(ratioHeight);
    return QStringLiteral("%1:1").arg(qreal(width) / height, 0, 'f', 2);
}

QString dimensionsText(const QSize& size)
{
    if (size.isEmpty())
        return {};
    return QObject::tr("%1 × %2 px").arg(size.width()).arg(size.height());
}

QPixmap makeNavIcon(bool grid, const QColor& color)
{
    // Keep the 20 px artwork at the left and reserve an 8 px gutter before
    // the button label, matching the header action buttons.
    QPixmap pixmap(28, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (grid) {
        constexpr qreal cell = 5.0;
        painter.drawRoundedRect(QRectF(3.0, 3.0, cell, cell), 0.8, 0.8);
        painter.drawRoundedRect(QRectF(12.0, 3.0, cell, cell), 0.8, 0.8);
        painter.drawRoundedRect(QRectF(3.0, 12.0, cell, cell), 0.8, 0.8);
        painter.drawRoundedRect(QRectF(12.0, 12.0, cell, cell), 0.8, 0.8);
    } else {
        painter.drawEllipse(QRectF(2.5, 2.5, 15.0, 15.0));
        painter.drawLine(QPointF(10.0, 5.5), QPointF(10.0, 10.0));
        painter.drawLine(QPointF(10.0, 10.0), QPointF(13.5, 12.0));
    }
    return pixmap;
}

QIcon makeStatefulNavIcon(bool grid, const Theme* theme)
{
    QIcon icon;
    icon.addPixmap(makeNavIcon(grid, theme->colorTextSecondary), QIcon::Normal, QIcon::Off);
    icon.addPixmap(makeNavIcon(grid, theme->colorTextBright), QIcon::Normal, QIcon::On);
    return icon;
}

QIcon makeHeaderActionIcon(bool folder, const QColor& color)
{
    // The extra transparent width is intentional: QPushButton offers no
    // theme-independent icon/text gap, so keep an 8 px trailing gutter in the
    // icon itself while preserving the 20 px artwork size.
    QPixmap pixmap(28, 20);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    if (folder) {
        QPainterPath path;
        path.moveTo(2.5, 6.0);
        path.lineTo(8.0, 6.0);
        path.lineTo(10.0, 8.0);
        path.lineTo(17.5, 8.0);
        path.lineTo(17.5, 16.5);
        path.lineTo(2.5, 16.5);
        path.closeSubpath();
        painter.drawPath(path);
    } else {
        painter.drawLine(QPointF(10.0, 3.0), QPointF(10.0, 17.0));
        painter.drawLine(QPointF(3.0, 10.0), QPointF(17.0, 10.0));
    }
    return QIcon(pixmap);
}

class RecentPreview final : public QWidget {
public:
    explicit RecentPreview(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(kCardPreviewWidth, kCardPreviewHeight);
        connect(ThemeManager::instance(), &ThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

    void setThumbnail(const QImage& thumbnail)
    {
        m_thumbnail = thumbnail;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Theme* theme = ThemeManager::instance()->current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);

        const QRectF previewRect = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPainterPath clip;
        clip.addRoundedRect(previewRect, theme->radiusLG, theme->radiusLG);
        painter.fillPath(clip, theme->colorSurfaceDark);

        painter.save();
        painter.setClipPath(clip);
        if (!m_thumbnail.isNull()) {
            QSize scaledSize = m_thumbnail.size();
            scaledSize.scale(size(), Qt::KeepAspectRatio);
            const QRect target((width() - scaledSize.width()) / 2,
                               (height() - scaledSize.height()) / 2,
                               scaledSize.width(), scaledSize.height());
            painter.drawImage(target, m_thumbnail);
        } else {
            const QRectF iconRect(width() / 2.0 - 18.0, 48.0, 36.0, 32.0);
            QPen iconPen(theme->colorTextDisabled, 2.0);
            iconPen.setJoinStyle(Qt::RoundJoin);
            painter.setPen(iconPen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(iconRect, theme->radiusMD, theme->radiusMD);
            painter.drawEllipse(QPointF(iconRect.right() - 9.0, iconRect.top() + 9.0),
                                3.0, 3.0);

            QPainterPath mountains;
            mountains.moveTo(iconRect.left() + 5.0, iconRect.bottom() - 5.0);
            mountains.lineTo(iconRect.left() + 14.0, iconRect.top() + 16.0);
            mountains.lineTo(iconRect.left() + 20.0, iconRect.top() + 22.0);
            mountains.lineTo(iconRect.left() + 25.0, iconRect.top() + 17.0);
            mountains.lineTo(iconRect.right() - 4.0, iconRect.bottom() - 5.0);
            painter.drawPath(mountains);

            painter.setPen(theme->colorTextSecondary);
            painter.drawText(QRectF(0.0, 91.0, width(), 24.0),
                             Qt::AlignHCenter | Qt::AlignVCenter,
                             tr("No preview available"));
        }
        painter.restore();

        painter.setPen(QPen(theme->colorBorder, theme->borderThinWidth));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(clip);
    }

private:
    QImage m_thumbnail;
};

class FormatPreview final : public QWidget {
public:
    explicit FormatPreview(const QSizeF& ratio, QWidget* parent = nullptr)
        : QWidget(parent), m_ratio(ratio)
    {
        setFixedHeight(kCardPreviewHeight);
        connect(ThemeManager::instance(), &ThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const Theme* theme = ThemeManager::instance()->current();
        painter.fillRect(rect(), theme->colorSurfaceDark);

        const QSizeF available(qMax(1, width() - 48), qMax(1, height() - 36));
        QSizeF document = m_ratio;
        if (document.isEmpty())
            document = QSizeF(1.0, 1.0);
        document.scale(available, Qt::KeepAspectRatio);
        const QRectF target((width() - document.width()) / 2.0,
                            (height() - document.height()) / 2.0,
                            document.width(), document.height());
        painter.fillRect(target, theme->colorTextBright);
        painter.setPen(QPen(theme->colorBorderLight, theme->borderThinWidth));
        painter.drawRect(target.adjusted(0.5, 0.5, -0.5, -0.5));
    }

private:
    QSizeF m_ratio;
};

class RecentDocumentCard final : public QFrame {
public:
    RecentDocumentCard(const RecentFileEntry& entry,
                       std::function<void()> open,
                       std::function<void()> showInFolder,
                       std::function<void()> remove,
                       QWidget* parent = nullptr)
        : QFrame(parent), m_open(std::move(open)), m_showInFolder(std::move(showInFolder)),
          m_remove(std::move(remove))
    {
        setObjectName(QStringLiteral("HomeRecentCard"));
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(kDocumentCardWidth, kDocumentCardHeight);

        const Theme* theme = ThemeManager::instance()->current();
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(theme->spaceMD, theme->spaceMD,
                                   theme->spaceMD, theme->spaceMD);
        layout->setSpacing(theme->spaceSM);

        m_preview = new RecentPreview(this);
        layout->addWidget(m_preview);

        auto* titleRow = new QHBoxLayout();
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(theme->spaceSM);
        m_name = new QLabel(this);
        m_name->setObjectName(QStringLiteral("HomeCardTitle"));
        m_name->setWordWrap(false);
        m_name->setMinimumWidth(0);
        m_name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_name->setTextInteractionFlags(Qt::NoTextInteraction);
        titleRow->addWidget(m_name, 1);

        auto* options = new QLabel(QStringLiteral("\u2026"), this);
        options->setObjectName(QStringLiteral("HomeCardMenu"));
        options->setFixedWidth(18);
        options->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        options->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        titleRow->addWidget(options);
        layout->addLayout(titleRow);

        m_details = new QLabel(this);
        m_details->setObjectName(QStringLiteral("HomeCardDetail"));
        m_details->setWordWrap(false);
        layout->addWidget(m_details);
        layout->addStretch(1);

        updateEntry(entry);
    }

    void updateEntry(const RecentFileEntry& entry)
    {
        m_name->setText(QFileInfo(entry.path).fileName());
        m_name->setToolTip(entry.path);
        m_openedText = entry.lastOpened.isValid()
            ? tr("Opened %1").arg(
                  QLocale().toString(entry.lastOpened, QLocale::ShortFormat))
            : QString();
        refreshDetails();
    }

    void setThumbnail(const QImage& thumbnail, const QSize& originalSize)
    {
        if (!thumbnail.isNull())
            m_preview->setThumbnail(thumbnail);
        if (!originalSize.isEmpty())
            m_dimensionsText = dimensionsText(originalSize);
        refreshDetails();
    }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QFrame::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()))
            activate();
    }

    void contextMenuEvent(QContextMenuEvent* event) override
    {
        QMenu menu(this);
        QAction* open = menu.addAction(tr("Open"));
        QAction* show = menu.addAction(tr("Show in Folder"));
        menu.addSeparator();
        QAction* remove = menu.addAction(tr("Remove from Recent"));
        QAction* chosen = menu.exec(event->globalPos());
        if (chosen == open)
            activate();
        else if (chosen == show && m_showInFolder) {
            const auto showInFolder = m_showInFolder;
            showInFolder();
        } else if (chosen == remove && m_remove) {
            const auto remove = m_remove;
            remove();
        }
    }

private:
    void refreshDetails()
    {
        if (m_dimensionsText.isEmpty())
            m_details->setText(m_openedText);
        else if (m_openedText.isEmpty())
            m_details->setText(m_dimensionsText);
        else
            m_details->setText(tr("%1  ·  %2").arg(m_dimensionsText, m_openedText));
    }

    void activate()
    {
        // A double click produces two release events. Opening image documents
        // is asynchronous, so suppress the second one before a new tab exists
        // for MainWindow's normal already-open detection to find.
        if (m_openPending || !m_open)
            return;
        const auto open = m_open;
        m_openPending = true;
        QTimer::singleShot(350, this, [this]() { m_openPending = false; });
        open();
    }

    RecentPreview* m_preview = nullptr;
    QLabel* m_name = nullptr;
    QLabel* m_details = nullptr;
    QString m_dimensionsText;
    QString m_openedText;
    bool m_openPending = false;
    std::function<void()> m_open;
    std::function<void()> m_showInFolder;
    std::function<void()> m_remove;
};

class TemplateFilterButton final : public QPushButton {
public:
    explicit TemplateFilterButton(const QString& text, QWidget* parent = nullptr)
        : QPushButton(text, parent)
    {
        setObjectName(QStringLiteral("HomeTemplateFilter"));
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setAttribute(Qt::WA_Hover, true);
        connect(ThemeManager::instance(), &ThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Theme* theme = ThemeManager::instance()->current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);

        // A cosmetic one-pixel pen centred on half-pixel coordinates avoids
        // the jagged rounded borders produced by the Qt stylesheet rasterizer.
        const QRectF outline = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
        QPen border(theme->colorSurfaceDark, theme->borderThinWidth);
        border.setCosmetic(true);
        painter.setPen(border);

        if (isChecked()) {
            const QColor fill = isDown() ? theme->colorAccentPressed
                : underMouse() ? theme->colorAccentHover
                               : theme->colorAccent;
            painter.setBrush(fill);
        } else {
            painter.setBrush(Qt::NoBrush);
        }
        painter.drawRoundedRect(outline, theme->radiusLG, theme->radiusLG);

        QFont labelFont = font();
        labelFont.setWeight(isChecked() ? QFont::DemiBold : QFont::Medium);
        painter.setFont(labelFont);
        painter.setPen(isChecked() || underMouse()
            ? theme->colorTextBright
            : theme->colorTextSecondary);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }
};

class RecentEmptyIcon final : public QWidget {
public:
    explicit RecentEmptyIcon(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setFixedSize(44, 44);
        connect(ThemeManager::instance(), &ThemeManager::themeChanged,
                this, qOverload<>(&QWidget::update));
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Theme* theme = ThemeManager::instance()->current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPen pen(theme->colorTextDisabled, 2.0, Qt::SolidLine,
                 Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);

        painter.drawRoundedRect(QRectF(15.0, 7.0, 22.0, 25.0),
                                theme->radiusMD, theme->radiusMD);
        const QRectF imageRect(7.0, 14.0, 25.0, 23.0);
        painter.drawRoundedRect(imageRect, theme->radiusMD, theme->radiusMD);
        painter.drawEllipse(QPointF(25.0, 21.0), 2.5, 2.5);

        QPainterPath mountains;
        mountains.moveTo(11.0, 32.0);
        mountains.lineTo(17.0, 25.0);
        mountains.lineTo(21.0, 29.0);
        mountains.lineTo(25.0, 26.0);
        mountains.lineTo(29.0, 32.0);
        painter.drawPath(mountains);
    }
};

class TemplateCard final : public QFrame {
public:
    TemplateCard(const SocialTemplatePreset& preset, std::function<void()> select,
                 QWidget* parent = nullptr)
        : QFrame(parent), m_platform(preset.platform), m_select(std::move(select))
    {
        setObjectName(QStringLiteral("HomeTemplateCard"));
        setProperty("templateId", preset.id);
        setFrameShape(QFrame::NoFrame);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(kDocumentCardWidth, kDocumentCardHeight);

        const Theme* theme = ThemeManager::instance()->current();
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(theme->spaceMD, theme->spaceMD,
                                   theme->spaceMD, theme->spaceMD);
        layout->setSpacing(theme->spaceSM);
        layout->addWidget(new FormatPreview(QSizeF(preset.width, preset.height), this));

        auto* name = new QLabel(preset.name, this);
        name->setObjectName(QStringLiteral("HomeCardTitle"));
        layout->addWidget(name);

        auto* detail = new QLabel(
            tr("%1 × %2 px  ·  %3")
                .arg(preset.width)
                .arg(preset.height)
                .arg(aspectRatioText(preset.width, preset.height)),
            this);
        detail->setObjectName(QStringLiteral("HomeCardDetail"));
        layout->addWidget(detail);
    }

    const QString& platform() const { return m_platform; }

protected:
    void mouseReleaseEvent(QMouseEvent* event) override
    {
        QFrame::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint())
            && m_select) {
            m_select();
        }
    }

private:
    QString m_platform;
    std::function<void()> m_select;
};

QScrollArea* makeGridScrollArea(FlowLayout** gridLayout, QWidget* parent)
{
    auto* scroll = new QScrollArea(parent);
    scroll->setObjectName(QStringLiteral("HomeGridScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* content = new QWidget(scroll);
    content->setObjectName(QStringLiteral("HomeGridContent"));
    auto* layout = new FlowLayout(content, ThemeManager::instance()->current()->spaceXXL,
                                  ThemeManager::instance()->current()->spaceLG,
                                  ThemeManager::instance()->current()->spaceLG);
    layout->setRowAlignment(Qt::AlignLeft);
    content->setLayout(layout);
    scroll->setWidget(content);
    *gridLayout = layout;
    return scroll;
}

} // namespace

HomeWorkspace::HomeWorkspace(QWidget* parent)
    : QWidget(parent), m_thumbnails(new RecentThumbnailCache(this))
{
    setObjectName(QStringLiteral("HomeWorkspace"));
    setAttribute(Qt::WA_StyledBackground, true);

    const Theme* theme = ThemeManager::instance()->current();
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("HomeSidebar"));
    sidebar->setFixedWidth(kSidebarWidth);
    sidebar->setAttribute(Qt::WA_StyledBackground, true);
    auto* sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setContentsMargins(0, theme->spaceXL, 0, theme->spaceXL);
    sidebarLayout->setSpacing(theme->spaceMD);

    auto* logo = new QLabel(sidebar);
    logo->setObjectName(QStringLiteral("HomeLogo"));
    logo->setPixmap(QPixmap(QStringLiteral(":/icons/home-logo.png")).scaled(
        kSidebarLogoWidth, kSidebarLogoHeight,
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedSize(kSidebarWidth, 108);
    logo->setAlignment(Qt::AlignCenter);
    sidebarLayout->addWidget(logo);
    sidebarLayout->addSpacing(theme->spaceLG);

    auto* sectionGroup = new QButtonGroup(this);
    sectionGroup->setExclusive(true);

    m_recentButton = new QPushButton(tr("Recent"), sidebar);
    m_recentButton->setObjectName(QStringLiteral("HomeNavButton"));
    m_recentButton->setCheckable(true);
    m_recentButton->setChecked(true);
    m_recentButton->setIconSize(QSize(28, 20));
    m_recentButton->setFixedHeight(42);
    m_recentButton->setCursor(Qt::PointingHandCursor);
    sectionGroup->addButton(m_recentButton, 0);
    sidebarLayout->addWidget(m_recentButton);

    m_templatesButton = new QPushButton(tr("Templates"), sidebar);
    m_templatesButton->setObjectName(QStringLiteral("HomeNavButton"));
    m_templatesButton->setCheckable(true);
    m_templatesButton->setIconSize(QSize(28, 20));
    m_templatesButton->setFixedHeight(42);
    m_templatesButton->setCursor(Qt::PointingHandCursor);
    sectionGroup->addButton(m_templatesButton, 1);
    sidebarLayout->addWidget(m_templatesButton);
    sidebarLayout->addStretch(1);
    root->addWidget(sidebar);

    auto* mainArea = new QWidget(this);
    mainArea->setObjectName(QStringLiteral("HomeMainArea"));
    mainArea->setAttribute(Qt::WA_StyledBackground, true);
    auto* mainLayout = new QVBoxLayout(mainArea);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto* header = new QWidget(mainArea);
    header->setObjectName(QStringLiteral("HomeHeader"));
    header->setFixedHeight(kHeaderHeight);
    header->setAttribute(Qt::WA_StyledBackground, true);
    auto* headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(kHeaderHorizontalMargin, kHeaderVerticalMargin,
                                     kHeaderHorizontalMargin, kHeaderVerticalMargin);
    headerLayout->setSpacing(theme->spaceXL);

    auto* welcomeLayout = new QVBoxLayout();
    welcomeLayout->setContentsMargins(0, 0, 0, 0);
    welcomeLayout->setSpacing(theme->spaceSM);
    auto* welcome = new QLabel(tr("WELCOME"), header);
    welcome->setObjectName(QStringLiteral("HomeWelcomeTitle"));
    welcomeLayout->addWidget(welcome);
    auto* tagline = new QLabel(tr("Create. Design. Visualize."), header);
    tagline->setObjectName(QStringLiteral("HomeWelcomeTagline"));
    welcomeLayout->addWidget(tagline);
    headerLayout->addLayout(welcomeLayout);
    headerLayout->addStretch(1);

    m_newDocumentButton = new QPushButton(tr("New Document"), header);
    m_newDocumentButton->setObjectName(QStringLiteral("HomePrimaryAction"));
    m_newDocumentButton->setIconSize(QSize(28, 20));
    m_newDocumentButton->setFixedSize(kNewDocumentButtonWidth, kHeaderButtonHeight);
    m_newDocumentButton->setCursor(Qt::PointingHandCursor);
    headerLayout->addWidget(m_newDocumentButton);

    m_openDocumentButton = new QPushButton(tr("Open"), header);
    m_openDocumentButton->setObjectName(QStringLiteral("HomeSecondaryAction"));
    m_openDocumentButton->setIconSize(QSize(28, 20));
    m_openDocumentButton->setFixedSize(kOpenButtonWidth, kHeaderButtonHeight);
    m_openDocumentButton->setCursor(Qt::PointingHandCursor);
    headerLayout->addWidget(m_openDocumentButton);
    mainLayout->addWidget(header);

    connect(m_newDocumentButton, &QPushButton::clicked,
            this, &HomeWorkspace::newDocumentRequested);
    connect(m_openDocumentButton, &QPushButton::clicked,
            this, &HomeWorkspace::openDocumentRequested);

    m_sections = new QStackedWidget(mainArea);

    auto* recentPage = new QWidget(m_sections);
    recentPage->setObjectName(QStringLiteral("HomeRecentPage"));
    auto* recentPageLayout = new QVBoxLayout(recentPage);
    recentPageLayout->setContentsMargins(0, 0, 0, 0);
    recentPageLayout->setSpacing(0);

    auto* recentHeader = new QWidget(recentPage);
    recentHeader->setObjectName(QStringLiteral("HomeSectionHeader"));
    auto* recentHeaderLayout = new QHBoxLayout(recentHeader);
    recentHeaderLayout->setContentsMargins(theme->spaceXXL, theme->spaceLG,
                                           theme->spaceXXL, theme->spaceLG);
    recentHeaderLayout->setSpacing(theme->spaceLG);
    auto* recentTitle = new QLabel(tr("Recent"), recentHeader);
    recentTitle->setObjectName(QStringLiteral("HomeSectionTitle"));
    recentHeaderLayout->addWidget(recentTitle);
    recentHeaderLayout->addStretch(1);
    auto* recentFilter = new QLineEdit(recentHeader);
    recentFilter->setObjectName(QStringLiteral("HomeRecentFilter"));
    recentFilter->setPlaceholderText(tr("Filter recent files..."));
    recentFilter->setClearButtonEnabled(true);
    recentFilter->setFixedSize(220, 34);
    recentHeaderLayout->addWidget(recentFilter);
    recentPageLayout->addWidget(recentHeader);

    m_recentContent = new QStackedWidget(recentPage);
    m_recentContent->setObjectName(QStringLiteral("HomeRecentContent"));
    m_recentContent->addWidget(makeGridScrollArea(&m_recentLayout, m_recentContent));

    m_recentEmptyState = new QWidget(m_recentContent);
    m_recentEmptyState->setObjectName(QStringLiteral("HomeRecentEmptyState"));
    auto* emptyPageLayout = new QVBoxLayout(m_recentEmptyState);
    emptyPageLayout->setContentsMargins(theme->spaceXXL, theme->spaceXXL,
                                        theme->spaceXXL, theme->spaceXXL);
    emptyPageLayout->setSpacing(0);
    emptyPageLayout->addStretch(1);

    auto* emptyIcon = new RecentEmptyIcon(m_recentEmptyState);
    emptyPageLayout->addWidget(emptyIcon, 0, Qt::AlignHCenter);
    emptyPageLayout->addSpacing(theme->spaceLG);

    auto* emptyTitle = new QLabel(tr("No recent files yet"), m_recentEmptyState);
    emptyTitle->setObjectName(QStringLiteral("HomeEmptyTitle"));
    emptyTitle->setAlignment(Qt::AlignCenter);
    emptyPageLayout->addWidget(emptyTitle, 0, Qt::AlignHCenter);
    emptyPageLayout->addSpacing(theme->spaceLG);

    auto* emptyDescription = new QLabel(
        tr("Create a new document or open an existing file to get started."),
        m_recentEmptyState);
    emptyDescription->setObjectName(QStringLiteral("HomeEmptyDescription"));
    emptyDescription->setAlignment(Qt::AlignCenter);
    emptyDescription->setWordWrap(true);
    emptyDescription->setMaximumWidth(520);
    emptyDescription->setMinimumHeight(44);
    emptyPageLayout->addWidget(emptyDescription, 0, Qt::AlignHCenter);
    emptyPageLayout->addSpacing(theme->spaceXL);

    auto* emptyActions = new QHBoxLayout();
    emptyActions->setContentsMargins(0, 0, 0, 0);
    emptyActions->setSpacing(theme->spaceMD);
    emptyActions->addStretch(1);
    m_emptyNewDocumentButton = new QPushButton(tr("New Document"), m_recentEmptyState);
    m_emptyNewDocumentButton->setObjectName(QStringLiteral("HomePrimaryAction"));
    m_emptyNewDocumentButton->setIconSize(QSize(28, 20));
    m_emptyNewDocumentButton->setFixedSize(kNewDocumentButtonWidth, kHeaderButtonHeight);
    m_emptyNewDocumentButton->setCursor(Qt::PointingHandCursor);
    emptyActions->addWidget(m_emptyNewDocumentButton);
    m_emptyOpenFileButton = new QPushButton(tr("Open File"), m_recentEmptyState);
    m_emptyOpenFileButton->setObjectName(QStringLiteral("HomeSecondaryAction"));
    m_emptyOpenFileButton->setIconSize(QSize(28, 20));
    m_emptyOpenFileButton->setFixedSize(132, kHeaderButtonHeight);
    m_emptyOpenFileButton->setCursor(Qt::PointingHandCursor);
    emptyActions->addWidget(m_emptyOpenFileButton);
    emptyActions->addStretch(1);
    emptyPageLayout->addLayout(emptyActions);
    emptyPageLayout->addStretch(1);
    m_recentContent->addWidget(m_recentEmptyState);
    recentPageLayout->addWidget(m_recentContent, 1);

    connect(m_emptyNewDocumentButton, &QPushButton::clicked,
            this, &HomeWorkspace::newDocumentRequested);
    connect(m_emptyOpenFileButton, &QPushButton::clicked,
            this, &HomeWorkspace::openDocumentRequested);
    m_sections->addWidget(recentPage);

    auto* templatePage = new QWidget(m_sections);
    templatePage->setObjectName(QStringLiteral("HomeTemplatePage"));
    auto* templatePageLayout = new QVBoxLayout(templatePage);
    templatePageLayout->setContentsMargins(0, 0, 0, 0);
    templatePageLayout->setSpacing(0);

    auto* templateHeader = new QWidget(templatePage);
    templateHeader->setObjectName(QStringLiteral("HomeSectionHeader"));
    auto* templateHeaderLayout = new QHBoxLayout(templateHeader);
    templateHeaderLayout->setContentsMargins(theme->spaceXXL, theme->spaceLG,
                                             theme->spaceXXL, theme->spaceLG);
    auto* templateTitle = new QLabel(tr("Templates"), templateHeader);
    templateTitle->setObjectName(QStringLiteral("HomeSectionTitle"));
    templateHeaderLayout->addWidget(templateTitle);
    templateHeaderLayout->addStretch(1);
    templatePageLayout->addWidget(templateHeader);

    auto* filterScroll = new QScrollArea(templatePage);
    filterScroll->setObjectName(QStringLiteral("HomeTemplateFiltersScroll"));
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setWidgetResizable(true);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    filterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setFixedHeight(66);

    auto* filterContent = new QWidget(filterScroll);
    filterContent->setObjectName(QStringLiteral("HomeTemplateFiltersContent"));
    auto* filterLayout = new QHBoxLayout(filterContent);
    filterLayout->setContentsMargins(theme->spaceXXL, theme->spaceMD,
                                     theme->spaceXXL, theme->spaceXL);
    filterLayout->setSpacing(theme->spaceLG);
    filterLayout->setSizeConstraint(QLayout::SetMinimumSize);

    auto* filterGroup = new QButtonGroup(this);
    filterGroup->setExclusive(true);
    const auto& filters = platformFilters();
    for (int index = 0; index < static_cast<int>(filters.size()); ++index) {
        const PlatformFilter& filter = filters[index];
        auto* chip = new TemplateFilterButton(filter.label, filterContent);
        chip->setChecked(index == 0);
        chip->setMinimumSize(72, 42);
        filterGroup->addButton(chip, index);
        filterLayout->addWidget(chip);
    }
    filterLayout->addStretch(1);
    filterScroll->setWidget(filterContent);
    templatePageLayout->addWidget(filterScroll);
    templatePageLayout->addWidget(
        makeGridScrollArea(&m_templateLayout, templatePage), 1);
    m_sections->addWidget(templatePage);

    connect(filterGroup, &QButtonGroup::idClicked, this, [this](int index) {
        const auto& filters = platformFilters();
        if (index >= 0 && index < static_cast<int>(filters.size()))
            applyTemplateFilter(filters[index].id);
    });

    mainLayout->addWidget(m_sections, 1);
    root->addWidget(mainArea, 1);
    connect(sectionGroup, &QButtonGroup::idClicked,
            m_sections, &QStackedWidget::setCurrentIndex);
    connect(recentFilter, &QLineEdit::textChanged,
            this, &HomeWorkspace::setRecentFilter);

    connect(RecentFilesManager::instance(), &RecentFilesManager::changed,
            this, &HomeWorkspace::rebuildRecent);
    connect(m_thumbnails, &RecentThumbnailCache::thumbnailAvailable,
            this, &HomeWorkspace::applyThumbnail);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &HomeWorkspace::applyTheme);

    applyTheme();
    rebuildTemplates();
    rebuildRecent();
}

void HomeWorkspace::storeDocumentThumbnail(const QString& path, const Document* document)
{
    m_thumbnails->storeDocument(path, document);
}

void HomeWorkspace::rebuildRecent()
{
    QWidget* grid = m_recentLayout->parentWidget();
    const QList<RecentFileEntry> entries = RecentFilesManager::instance()->entries();
    QList<QWidget*> orderedCards;
    orderedCards.reserve(entries.size());
    QSet<QString> currentPaths;

    for (const RecentFileEntry& entry : entries) {
        const QString path = QFileInfo(entry.path).absoluteFilePath();
        currentPaths.insert(path);

        auto* card = static_cast<RecentDocumentCard*>(m_recentCards.value(path));
        if (!card) {
            card = new RecentDocumentCard(
                entry,
                [this, path]() { emit recentDocumentRequested(path); },
                [path]() {
                    QDesktopServices::openUrl(
                        QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
                },
                [this, path]() {
                    m_thumbnails->remove(path);
                    RecentFilesManager::instance()->removeFile(path);
                }, grid);
            card->hide();
            m_recentCards.insert(path, card);
            m_thumbnails->request(path);
        } else {
            card->updateEntry(entry);
        }
        if (m_recentFilter.isEmpty()
            || QFileInfo(path).fileName().contains(m_recentFilter, Qt::CaseInsensitive)) {
            orderedCards.append(card);
        }
    }

    // Reorder the layout without destroying cards that are still represented
    // by the shared recent-files model. This keeps click handlers and loaded
    // thumbnails stable when an open moves an entry to the front.
    while (QLayoutItem* item = m_recentLayout->takeAt(0))
        delete item;

    for (QWidget* card : std::as_const(m_recentCards))
        card->hide();

    for (auto it = m_recentCards.begin(); it != m_recentCards.end();) {
        if (!currentPaths.contains(it.key())) {
            it.value()->hide();
            it.value()->deleteLater();
            it = m_recentCards.erase(it);
        } else {
            ++it;
        }
    }

    if (entries.isEmpty()) {
        if (m_recentNoMatchState) {
            m_recentNoMatchState->hide();
            m_recentNoMatchState->deleteLater();
            m_recentNoMatchState = nullptr;
        }
        m_recentContent->setCurrentWidget(m_recentEmptyState);
    } else if (orderedCards.isEmpty()) {
        m_recentContent->setCurrentIndex(0);
        if (!m_recentNoMatchState) {
            auto* empty = new QLabel(grid);
            empty->setObjectName(QStringLiteral("HomeEmptyState"));
            empty->setFixedSize(300, 80);
            empty->setAlignment(Qt::AlignCenter);
            empty->setText(tr("No recent documents match this filter."));
            m_recentNoMatchState = empty;
        }
        m_recentLayout->addWidget(m_recentNoMatchState);
        m_recentNoMatchState->show();
    } else {
        m_recentContent->setCurrentIndex(0);
        if (m_recentNoMatchState) {
            m_recentNoMatchState->hide();
            m_recentNoMatchState->deleteLater();
            m_recentNoMatchState = nullptr;
        }
        for (QWidget* card : std::as_const(orderedCards)) {
            m_recentLayout->addWidget(card);
            card->show();
        }
    }

    m_recentLayout->invalidate();
}

void HomeWorkspace::setRecentFilter(const QString& text)
{
    m_recentFilter = text.trimmed();
    rebuildRecent();
}

void HomeWorkspace::rebuildTemplates()
{
    while (QLayoutItem* item = m_templateLayout->takeAt(0))
        delete item;
    for (QWidget* card : std::as_const(m_templateCards))
        delete card;
    m_templateCards.clear();
    QWidget* grid = m_templateLayout->parentWidget();
    for (const SocialTemplatePreset& preset : socialTemplates()) {
        auto* card = new TemplateCard(
            preset,
            [this, name = preset.name, width = preset.width, height = preset.height]() {
                emit templateRequested(name, width, height);
            },
            grid);
        m_templateCards.append(card);
    }
    applyTemplateFilter(m_templatePlatform);
}

void HomeWorkspace::applyTemplateFilter(const QString& platform)
{
    m_templatePlatform = platform;

    while (QLayoutItem* item = m_templateLayout->takeAt(0))
        delete item;

    for (QWidget* widget : std::as_const(m_templateCards)) {
        auto* card = static_cast<TemplateCard*>(widget);
        const bool visible = platform == QLatin1String("all")
            || card->platform() == platform;
        card->setVisible(visible);
        if (visible)
            m_templateLayout->addWidget(card);
    }
    m_templateLayout->invalidate();
}

void HomeWorkspace::applyThumbnail(const QString& path, const QImage& thumbnail,
                                   const QSize& originalSize)
{
    if (auto* card = static_cast<RecentDocumentCard*>(
            m_recentCards.value(QFileInfo(path).absoluteFilePath()))) {
        card->setThumbnail(thumbnail, originalSize);
    }
}

void HomeWorkspace::applyTheme()
{
    const Theme* theme = ThemeManager::instance()->current();
    const int homeButtonFontSize = theme->fontSizeMD + 2;
    m_recentButton->setIcon(makeStatefulNavIcon(false, theme));
    m_templatesButton->setIcon(makeStatefulNavIcon(true, theme));
    m_newDocumentButton->setIcon(makeHeaderActionIcon(false, theme->colorTextBright));
    m_openDocumentButton->setIcon(makeHeaderActionIcon(true, theme->colorTextBright));
    m_emptyNewDocumentButton->setIcon(
        makeHeaderActionIcon(false, theme->colorTextBright));
    m_emptyOpenFileButton->setIcon(
        makeHeaderActionIcon(true, theme->colorTextBright));

    setStyleSheet(QStringLiteral(
        "HomeWorkspace, #HomeMainArea { background: %1; }"
        "#HomeSidebar { background: %2; border-right: %3px solid %4; }"
        "#HomeLogo { background: transparent; }"
        "#HomeNavButton { background: transparent; color: %5; border: none;"
        "  border-left: 3px solid transparent; border-radius: %18px;"
        "  margin: 0 8px; padding: 0 14px; text-align: left;"
        "  font-size: %19px; font-weight: 500; }"
        "#HomeNavButton:hover:!checked { background: %10; color: %6; }"
        "#HomeNavButton:checked { background: %10; color: %6;"
        "  border-left-color: %7; font-weight: 600; }"
        "#HomeHeader { background: %2; }"
        "#HomeWelcomeTitle { color: %6; background: transparent;"
        "  font-size: 22px; font-weight: 700; letter-spacing: 3px; }"
        "#HomeWelcomeTagline { color: %5; background: transparent;"
        "  font-size: %15px; letter-spacing: 1px; }"
        "#HomePrimaryAction, #HomeSecondaryAction { border-radius: %13px;"
        "  padding: 0 16px; font-size: %19px; font-weight: 600; }"
        "#HomePrimaryAction { background: %7; color: %6; border: %3px solid %7; }"
        "#HomePrimaryAction:hover { background: %8; border-color: %8; }"
        "#HomePrimaryAction:pressed { background: %9; border-color: %9; }"
        "#HomeSecondaryAction { background: %16; color: %6; border: %3px solid %4; }"
        "#HomeSecondaryAction:hover { background: %11; border-color: %12; }"
        "#HomeSecondaryAction:pressed { background: %10; }"
        "#HomeRecentPage, #HomeTemplatePage, #HomeSectionHeader,"
        "#HomeTemplateFiltersScroll,"
        "#HomeTemplateFiltersScroll > QWidget, #HomeTemplateFiltersContent {"
        "  background: %2; border: none;"
        "}"
        "#HomeRecentContent, #HomeRecentEmptyState { background: %1; border: none; }"
        "#HomeSectionHeader { border-bottom: %3px solid %4; }"
        "#HomeSectionTitle { color: %6; background: transparent;"
        "  font-size: 18px; font-weight: 700; }"
        "#HomeEmptyTitle { background: transparent; color: %6;"
        "  font-size: 18px; font-weight: 700; }"
        "#HomeEmptyDescription { background: transparent; color: %5;"
        "  font-size: 13px; }"
        "#HomeRecentFilter { background: %2; color: %6;"
        "  border: %3px solid %4; border-radius: %13px; padding: 0 10px;"
        "  font-size: %15px; placeholder-text-color: %17; }"
        "#HomeRecentFilter:focus { border-color: %12; }"
        "#HomeTemplateFilter { background: transparent; border: none;"
        "  padding: 0 16px; font-size: %19px; }"
        "#HomeGridScroll, #HomeGridScroll > QWidget, #HomeGridContent { background: %1; }"
        "#HomeRecentCard, #HomeTemplateCard { background: %16; border: %3px solid %4; border-radius: %13px; }"
        "#HomeRecentCard:hover, #HomeTemplateCard:hover { background: %11; border-color: %12; }"
        "#HomeCardTitle { background: transparent; color: %6; font-size: %14px; font-weight: 600; }"
        "#HomeCardDetail, #HomeCardMenu, #HomeEmptyState { background: transparent; color: %5; font-size: %15px; }"
    ).arg(theme->colorBackgroundPrimary.name())
     .arg(theme->colorBackgroundPrimary.name())
     .arg(theme->borderThinWidth)
     .arg(theme->colorBorder.name())
     .arg(theme->colorTextSecondary.name())
     .arg(theme->colorTextBright.name())
     .arg(theme->colorAccent.name())
     .arg(theme->colorAccentHover.name())
     .arg(theme->colorAccentPressed.name())
     .arg(theme->colorSurface.name())
     .arg(theme->colorSurfaceHover.name())
     .arg(theme->colorBorderFocus.name())
     .arg(theme->radiusLG)
     .arg(theme->fontSizeMD)
     .arg(theme->fontSizeSM)
     .arg(theme->colorSurfaceDark.name())
     .arg(theme->colorTextDisabled.name())
     .arg(theme->radiusMD)
     .arg(homeButtonFontSize));
}
