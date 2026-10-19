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
#include <QFrame>
#include <QHash>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayoutItem>
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
#include <QTabBar>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <functional>
#include <numeric>
#include <utility>
#include <vector>

namespace {

constexpr int kHeaderAppIconSize = 48;
constexpr int kHeaderHeight = 176;
constexpr int kHeaderHorizontalMargin = 28;
constexpr int kHeaderTopMargin = 24;
constexpr int kNewDocumentButtonWidth = 200;
constexpr int kNewDocumentButtonHeight = 48;
constexpr int kDocumentCardWidth = 296;
constexpr int kRecentCardHeight = 258;
constexpr int kTemplateCardHeight = 238;
constexpr int kCardPreviewWidth = 272;
constexpr int kCardPreviewHeight = 166;

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

class HomeHeader final : public QWidget {
public:
    explicit HomeHeader(QWidget* parent = nullptr)
        : QWidget(parent),
          m_background(QStringLiteral(":/icons/home-workspace-bg.jpg"))
    {
        setFixedHeight(kHeaderHeight);
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_background.isNull())
            return;

        QRectF source(m_background.rect());
        const qreal targetAspect = qreal(qMax(1, width())) / qMax(1, height());
        const qreal sourceAspect = source.width() / source.height();
        if (sourceAspect > targetAspect) {
            const qreal cropWidth = source.height() * targetAspect;
            source.setLeft((source.width() - cropWidth) / 2.0);
            source.setWidth(cropWidth);
        } else {
            const qreal cropHeight = source.width() / targetAspect;
            source.setTop((source.height() - cropHeight) / 2.0);
            source.setHeight(cropHeight);
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        painter.drawPixmap(QRectF(rect()), m_background, source);
    }

private:
    QPixmap m_background;
};

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
        setFixedSize(kDocumentCardWidth, kRecentCardHeight);

        const Theme* theme = ThemeManager::instance()->current();
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(theme->spaceLG, theme->spaceLG,
                                   theme->spaceLG, theme->spaceLG);
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
        setFixedSize(kDocumentCardWidth, kTemplateCardHeight);

        const Theme* theme = ThemeManager::instance()->current();
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(theme->spaceLG, theme->spaceLG,
                                   theme->spaceLG, theme->spaceLG);
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
    auto* layout = new FlowLayout(content, ThemeManager::instance()->current()->spaceXL,
                                  ThemeManager::instance()->current()->spaceXL,
                                  ThemeManager::instance()->current()->spaceXL);
    layout->setRowAlignment(Qt::AlignHCenter);
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
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* header = new HomeHeader(this);
    header->setObjectName(QStringLiteral("HomeHeader"));
    auto* headerLayout = new QVBoxLayout(header);
    headerLayout->setContentsMargins(kHeaderHorizontalMargin, kHeaderTopMargin,
                                     kHeaderHorizontalMargin, 0);
    headerLayout->setSpacing(0);

    auto* actions = new QHBoxLayout();
    actions->setContentsMargins(0, 0, 0, 0);
    actions->setSpacing(theme->spaceMD);

    auto* appIcon = new QLabel(header);
    appIcon->setPixmap(QPixmap(QStringLiteral(":/icons/app-icon.png")).scaled(
        kHeaderAppIconSize, kHeaderAppIconSize,
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
    appIcon->setFixedSize(kHeaderAppIconSize, kHeaderAppIconSize);
    appIcon->setAlignment(Qt::AlignCenter);
    actions->addWidget(appIcon);
    actions->addStretch(1);

    auto* newDocument = new QPushButton(tr("New Document"), header);
    newDocument->setObjectName(QStringLiteral("HomeNewDocumentButton"));
    newDocument->setFixedSize(kNewDocumentButtonWidth, kNewDocumentButtonHeight);
    QPixmap addIcon(20, 20);
    addIcon.fill(Qt::transparent);
    {
        QPainter painter(&addIcon);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(Qt::white, 2.0, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(10.0, 3.0), QPointF(10.0, 17.0));
        painter.drawLine(QPointF(3.0, 10.0), QPointF(17.0, 10.0));
    }
    newDocument->setIcon(QIcon(addIcon));
    newDocument->setIconSize(addIcon.size());
    actions->addWidget(newDocument);
    headerLayout->addLayout(actions);
    headerLayout->addStretch(1);
    connect(newDocument, &QPushButton::clicked, this, &HomeWorkspace::newDocumentRequested);

    m_sectionTabs = new QTabBar(header);
    m_sectionTabs->setObjectName(QStringLiteral("HomeSectionTabs"));
    m_sectionTabs->addTab(tr("Recent"));
    m_sectionTabs->addTab(tr("Templates"));
    m_sectionTabs->setExpanding(false);
    m_sectionTabs->setFixedHeight(43);
    m_sectionTabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    headerLayout->addWidget(m_sectionTabs);
    root->addWidget(header);

    m_sections = new QStackedWidget(this);
    m_sections->addWidget(makeGridScrollArea(&m_recentLayout, m_sections));

    auto* templatePage = new QWidget(m_sections);
    templatePage->setObjectName(QStringLiteral("HomeTemplatePage"));
    auto* templatePageLayout = new QVBoxLayout(templatePage);
    templatePageLayout->setContentsMargins(0, 0, 0, 0);
    templatePageLayout->setSpacing(0);

    auto* filterScroll = new QScrollArea(templatePage);
    filterScroll->setObjectName(QStringLiteral("HomeTemplateFiltersScroll"));
    filterScroll->setFrameShape(QFrame::NoFrame);
    filterScroll->setWidgetResizable(true);
    filterScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    filterScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    filterScroll->setFixedHeight(80);

    auto* filterContent = new QWidget(filterScroll);
    filterContent->setObjectName(QStringLiteral("HomeTemplateFiltersContent"));
    auto* filterLayout = new QHBoxLayout(filterContent);
    filterLayout->setContentsMargins(theme->spaceXXL, theme->spaceLG,
                                     theme->spaceXXL, theme->spaceLG);
    filterLayout->setSpacing(theme->spaceLG);
    filterLayout->setSizeConstraint(QLayout::SetMinimumSize);

    auto* filterGroup = new QButtonGroup(this);
    filterGroup->setExclusive(true);
    const auto& filters = platformFilters();
    for (int index = 0; index < static_cast<int>(filters.size()); ++index) {
        const PlatformFilter& filter = filters[index];
        auto* chip = new QPushButton(filter.label, filterContent);
        chip->setObjectName(QStringLiteral("HomeTemplateFilter"));
        chip->setCheckable(true);
        chip->setChecked(index == 0);
        chip->setMinimumSize(72, 42);
        chip->setCursor(Qt::PointingHandCursor);
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

    root->addWidget(m_sections, 1);
    connect(m_sectionTabs, &QTabBar::currentChanged, m_sections,
            &QStackedWidget::setCurrentIndex);

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
        orderedCards.append(card);
    }

    // Reorder the layout without destroying cards that are still represented
    // by the shared recent-files model. This keeps click handlers and loaded
    // thumbnails stable when an open moves an entry to the front.
    while (QLayoutItem* item = m_recentLayout->takeAt(0))
        delete item;

    for (auto it = m_recentCards.begin(); it != m_recentCards.end();) {
        if (!currentPaths.contains(it.key())) {
            it.value()->hide();
            it.value()->deleteLater();
            it = m_recentCards.erase(it);
        } else {
            ++it;
        }
    }

    if (orderedCards.isEmpty()) {
        if (!m_recentEmptyState) {
            auto* empty = new QLabel(tr("No recent documents yet."), grid);
            empty->setObjectName(QStringLiteral("HomeEmptyState"));
            empty->setFixedSize(300, 80);
            empty->setAlignment(Qt::AlignCenter);
            m_recentEmptyState = empty;
        }
        m_recentLayout->addWidget(m_recentEmptyState);
        m_recentEmptyState->show();
    } else {
        if (m_recentEmptyState) {
            m_recentEmptyState->hide();
            m_recentEmptyState->deleteLater();
            m_recentEmptyState = nullptr;
        }
        for (QWidget* card : std::as_const(orderedCards)) {
            m_recentLayout->addWidget(card);
            card->show();
        }
    }

    m_recentLayout->invalidate();
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
    setStyleSheet(QStringLiteral(
        "HomeWorkspace { background: %1; }"
        "#HomeHeader { background: transparent; }"
        "#HomeNewDocumentButton {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #176fd1, stop:1 #2f91f5);"
        "  color: #ffffff; border: 1px solid #3b91eb; border-radius: 8px;"
        "  padding: 0 20px; font-size: 14px; font-weight: 600;"
        "}"
        "#HomeNewDocumentButton:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #237ddd, stop:1 #3b9cff);"
        "}"
        "#HomeNewDocumentButton:pressed { background: #1767bd; }"
        "#HomeSectionTabs { background: transparent; border: none; border-bottom: %2px solid %3; }"
        "#HomeSectionTabs::tab { background: transparent; color: %4; border: none;"
        "  border-bottom: 2px solid transparent; min-width: 62px; height: 42px;"
        "  padding: 0 %5px; margin-right: %6px; font-size: %7px; font-weight: 500;"
        "}"
        "#HomeSectionTabs::tab:selected { color: %8; border-bottom: 2px solid %9; }"
        "#HomeSectionTabs::tab:hover:!selected { color: %8; }"
        "#HomeTemplatePage, #HomeTemplateFiltersScroll,"
        "#HomeTemplateFiltersScroll > QWidget, #HomeTemplateFiltersContent {"
        "  background: %16; border: none;"
        "}"
        "#HomeTemplateFilter { background: %10; color: %4;"
        "  border: %2px solid %3; border-radius: 20px; padding: 0 %6px;"
        "  font-size: %7px; font-weight: 500;"
        "}"
        "#HomeTemplateFilter:hover:!checked { background: %12; border-color: %13; color: %8; }"
        "#HomeTemplateFilter:checked { background: %9; border-color: %9; color: %8; font-weight: 600; }"
        "#HomeTemplateFilter:checked:hover { background: %17; border-color: %17; }"
        "#HomeGridScroll, #HomeGridScroll > QWidget, #HomeGridContent { background: %16; }"
        "#HomeRecentCard, #HomeTemplateCard { background: %10; border: %2px solid %3; border-radius: %11px; }"
        "#HomeRecentCard:hover, #HomeTemplateCard:hover { background: %12; border-color: %13; }"
        "#HomeCardTitle { color: %8; font-size: %14px; font-weight: 600; }"
        "#HomeCardDetail, #HomeCardMenu, #HomeEmptyState { color: %4; font-size: %15px; }"
    ).arg(theme->colorBackgroundPrimary.name())
     .arg(theme->borderThinWidth)
     .arg(theme->colorBorder.name())
     .arg(theme->colorTextSecondary.name())
     .arg(theme->spaceMD)
     .arg(theme->spaceXL)
     .arg(theme->fontSizeMD)
     .arg(theme->colorTextBright.name())
     .arg(theme->colorAccent.name())
     .arg(theme->colorSurface.name())
     .arg(theme->radiusLG)
     .arg(theme->colorSurfaceHover.name())
     .arg(theme->colorBorderFocus.name())
     .arg(theme->fontSizeMD)
     .arg(theme->fontSizeSM)
     .arg(theme->colorPanelBackground.name())
     .arg(theme->colorAccentHover.name()));
}
