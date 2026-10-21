#include "ChannelDialogs.hpp"

#include "core/SwatchManager.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppCheckBox.hpp"
#include "ui/AppComboBox.hpp"
#include "ui/colorpicker/ColorPickerDialog.hpp"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

// Every dialog here is a short form: one column of labelled rows plus OK /
// Cancel. Built by hand rather than from a shared base because the existing
// ToolDialog is a filter-preview host (Apply/Reset/preview toggle), which none
// of these need.
QDialogButtonBox* makeButtons(QDialog* dialog)
{
    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    QObject::connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    return buttons;
}

QSpinBox* makePercentSpin(QWidget* parent, int value)
{
    auto* spin = new QSpinBox(parent);
    spin->setRange(0, 100);
    spin->setSuffix(QStringLiteral(" %"));
    spin->setValue(value);
    return spin;
}

void applyPanelTheme(QWidget* widget)
{
    auto* th = ThemeManager::instance()->current();
    widget->setStyleSheet(th->globalStyleSheet());
}

// Flattens the swatch tree into (collection, colour) pairs so a spot ink can be
// picked from any of the project's colour libraries and keep the library's own
// name for the ink.
struct LibraryRef {
    QString id;
    QString label;
};

std::vector<LibraryRef> swatchLibraries()
{
    std::vector<LibraryRef> out;
    for (const auto& ref : SwatchManager::instance()->allCollections())
        out.push_back({ref.id, ref.path.isEmpty() ? ref.name : ref.path});
    return out;
}

} // namespace

// ── ChannelColorButton ─────────────────────────────────────────

ChannelColorButton::ChannelColorButton(const QColor& color, QWidget* parent)
    : QPushButton(parent)
    , m_color(color)
{
    setFixedSize(48, 22);
    setCursor(Qt::PointingHandCursor);
    updateSwatch();
    connect(this, &QPushButton::clicked, this, [this]() {
        if (m_picker) {
            m_picker->raise();
            m_picker->activateWindow();
            return;
        }
        // Modeless so the picker's eyedropper can sample the image: an ink or
        // overlay colour is usually matched to something in the document.
        auto* picker = new ColorPickerDialog(m_color, ColorPickerMode::Foreground, this);
        m_picker = picker;
        connect(picker, &ColorPickerDialog::colorAccepted, this,
                [this](const QColor& c) { setColor(c); });
        picker->setAttribute(Qt::WA_DeleteOnClose);
        picker->showModeless();
    });
}

void ChannelColorButton::setColor(const QColor& color)
{
    if (!color.isValid() || color == m_color)
        return;
    m_color = color;
    updateSwatch();
    emit colorChanged(m_color);
}

void ChannelColorButton::updateSwatch()
{
    auto* th = ThemeManager::instance()->current();
    QPixmap pix(width() - 6, height() - 6);
    pix.fill(m_color);
    QPainter p(&pix);
    p.setPen(th->colorBorder);
    p.drawRect(0, 0, pix.width() - 1, pix.height() - 1);
    p.end();
    setIcon(QIcon(pix));
    setIconSize(pix.size());
}

// ── AlphaChannelDialog ─────────────────────────────────────────

AlphaChannelDialog::AlphaChannelDialog(const QString& title, const QString& name,
                                       ChannelMaskRepresentation representation,
                                       const QColor& overlayColor,
                                       float overlayOpacity,
                                       bool allowKindChange, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(title);
    applyPanelTheme(this);

    m_name = new QLineEdit(name, this);
    m_masked = new QRadioButton(tr("&Masked Areas"), this);
    m_selected = new QRadioButton(tr("&Selected Areas"), this);
    (representation == ChannelMaskRepresentation::SelectedAreas ? m_selected : m_masked)
        ->setChecked(true);
    m_color = new ChannelColorButton(overlayColor, this);
    m_opacity = makePercentSpin(this, static_cast<int>(std::lround(overlayOpacity * 100.0f)));

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"), m_name);

    auto* indicates = new QVBoxLayout;
    indicates->setContentsMargins(0, 0, 0, 0);
    indicates->addWidget(m_masked);
    indicates->addWidget(m_selected);
    auto* indicatesHost = new QWidget(this);
    indicatesHost->setLayout(indicates);
    form->addRow(tr("Color Indicates:"), indicatesHost);

    auto* colorRow = new QHBoxLayout;
    colorRow->setContentsMargins(0, 0, 0, 0);
    colorRow->addWidget(m_color);
    colorRow->addWidget(new QLabel(tr("Opacity:"), this));
    colorRow->addWidget(m_opacity);
    colorRow->addStretch();
    auto* colorHost = new QWidget(this);
    colorHost->setLayout(colorRow);
    form->addRow(tr("Color:"), colorHost);

    if (allowKindChange) {
        m_toSpot = new AppCheckBox(tr("Convert to s&pot channel"));
        form->addRow(QString(), m_toSpot);
    }

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    // These settings are display conventions: they never rewrite the channel's
    // values, which is what keeps switching them reversible.
    auto* note = new QLabel(tr("Display settings only — the channel's stored "
                               "values are not changed."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;")
        .arg(ThemeManager::instance()->current()->colorTextSecondary.name()));
    layout->addWidget(note);
    layout->addWidget(makeButtons(this));

    m_name->setFocus();
    m_name->selectAll();
}

QString AlphaChannelDialog::channelName() const { return m_name->text().trimmed(); }

ChannelMaskRepresentation AlphaChannelDialog::representation() const
{
    return m_selected->isChecked() ? ChannelMaskRepresentation::SelectedAreas
                                   : ChannelMaskRepresentation::MaskedAreas;
}

QColor AlphaChannelDialog::overlayColor() const { return m_color->color(); }

float AlphaChannelDialog::overlayOpacity() const
{
    return static_cast<float>(m_opacity->value()) / 100.0f;
}

bool AlphaChannelDialog::convertToSpot() const
{
    return m_toSpot && m_toSpot->isChecked();
}

// ── SpotChannelDialog ──────────────────────────────────────────

SpotChannelDialog::SpotChannelDialog(const QString& title, const QString& name,
                                     const QColor& inkColor, float solidity,
                                     const QString& inkBook, bool allowKindChange,
                                     QWidget* parent)
    : QDialog(parent)
    , m_inkBook(inkBook)
{
    setWindowTitle(title);
    applyPanelTheme(this);

    m_name = new QLineEdit(name, this);
    m_color = new ChannelColorButton(inkColor, this);
    m_solidity = makePercentSpin(this, static_cast<int>(std::lround(solidity * 100.0f)));
    m_library = new AppComboBox(this);
    m_ink = new AppComboBox(this);

    auto* form = new QFormLayout;
    form->addRow(tr("Name:"), m_name);

    auto* colorRow = new QHBoxLayout;
    colorRow->setContentsMargins(0, 0, 0, 0);
    colorRow->addWidget(m_color);
    colorRow->addWidget(new QLabel(tr("Solidity:"), this));
    colorRow->addWidget(m_solidity);
    colorRow->addStretch();
    auto* colorHost = new QWidget(this);
    colorHost->setLayout(colorRow);
    form->addRow(tr("Color:"), colorHost);

    form->addRow(tr("Library:"), m_library);
    form->addRow(tr("Ink:"), m_ink);

    if (allowKindChange) {
        m_toAlpha = new AppCheckBox(tr("Convert to &alpha channel"));
        form->addRow(QString(), m_toAlpha);
    }

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    auto* note = new QLabel(tr("Solidity affects the on-screen ink simulation "
                               "only; the channel's ink coverage is unchanged."), this);
    note->setWordWrap(true);
    note->setStyleSheet(QStringLiteral("color: %1;")
        .arg(ThemeManager::instance()->current()->colorTextSecondary.name()));
    layout->addWidget(note);
    layout->addWidget(makeButtons(this));

    reloadLibraries();
    connect(m_library, &QComboBox::currentIndexChanged,
            this, &SpotChannelDialog::onLibraryChanged);
    connect(m_ink, &QComboBox::activated, this, &SpotChannelDialog::onInkChosen);

    m_name->setFocus();
    m_name->selectAll();
}

void SpotChannelDialog::reloadLibraries()
{
    m_library->clear();
    m_library->addItem(tr("(none)"), QString());
    for (const auto& lib : swatchLibraries())
        m_library->addItem(lib.label, lib.id);
    // Re-select the library the ink came from so re-opening Channel Options
    // does not silently drop its provenance.
    if (!m_inkBook.isEmpty()) {
        const int found = m_library->findText(m_inkBook);
        if (found >= 0)
            m_library->setCurrentIndex(found);
    }
    onLibraryChanged(m_library->currentIndex());
}

void SpotChannelDialog::onLibraryChanged(int index)
{
    m_ink->clear();
    const QString id = m_library->itemData(index).toString();
    if (id.isEmpty()) {
        m_ink->setEnabled(false);
        return;
    }
    auto* collection = SwatchManager::instance()->findCollection(id);
    if (!collection) {
        m_ink->setEnabled(false);
        return;
    }
    m_ink->setEnabled(true);
    m_ink->addItem(tr("(custom)"), QString());
    for (const SwatchColor& swatch : collection->colors) {
        const QString label = swatch.name.isEmpty()
            ? swatch.color.name(QColor::HexRgb).toUpper() : swatch.name;
        m_ink->addItem(label, swatch.color);
    }
}

void SpotChannelDialog::onInkChosen(int index)
{
    const QVariant data = m_ink->itemData(index);
    if (!data.canConvert<QColor>())
        return;
    const QColor color = data.value<QColor>();
    if (!color.isValid())
        return;
    m_color->setColor(color);
    // An ink from a library keeps the library's name and the swatch's name —
    // that provenance is what distinguishes a named special ink from an
    // anonymous RGB triple.
    m_inkBook = m_library->currentText();
    if (!m_ink->itemText(index).isEmpty())
        m_name->setText(m_ink->itemText(index));
}

QString SpotChannelDialog::channelName() const { return m_name->text().trimmed(); }
QColor SpotChannelDialog::inkColor() const { return m_color->color(); }

float SpotChannelDialog::solidity() const
{
    return static_cast<float>(m_solidity->value()) / 100.0f;
}

QString SpotChannelDialog::inkBook() const { return m_inkBook; }

bool SpotChannelDialog::convertToAlpha() const
{
    return m_toAlpha && m_toAlpha->isChecked();
}

// ── DuplicateChannelDialog ─────────────────────────────────────

DuplicateChannelDialog::DuplicateChannelDialog(const QString& suggestedName,
                                               const std::vector<QString>& labels,
                                               QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Duplicate Channel"));
    applyPanelTheme(this);

    m_name = new QLineEdit(suggestedName, this);
    m_destination = new AppComboBox(this);
    for (int i = 0; i < static_cast<int>(labels.size()); ++i)
        m_destination->addItem(labels[static_cast<size_t>(i)], i);
    m_destination->addItem(tr("New"), -1);
    m_invert = new AppCheckBox(tr("&Invert"));

    auto* form = new QFormLayout;
    form->addRow(tr("As:"), m_name);
    form->addRow(tr("Document:"), m_destination);
    form->addRow(QString(), m_invert);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(makeButtons(this));

    m_name->setFocus();
    m_name->selectAll();
}

QString DuplicateChannelDialog::channelName() const { return m_name->text().trimmed(); }

int DuplicateChannelDialog::destinationIndex() const
{
    return m_destination->currentData().toInt();
}

bool DuplicateChannelDialog::invertContents() const { return m_invert->isChecked(); }

// ── SaveSelectionDialog ────────────────────────────────────────

SaveSelectionDialog::SaveSelectionDialog(const std::vector<QString>& documentLabels,
                                         const std::vector<QString>& channelLabels,
                                         const QString& suggestedName,
                                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Save Selection"));
    applyPanelTheme(this);

    m_document = new AppComboBox(this);
    for (int i = 0; i < static_cast<int>(documentLabels.size()); ++i)
        m_document->addItem(documentLabels[static_cast<size_t>(i)], i);
    m_channel = new AppComboBox(this);
    m_name = new QLineEdit(suggestedName, this);

    m_replace = new QRadioButton(tr("&Replace Channel"), this);
    m_add = new QRadioButton(tr("&Add to Channel"), this);
    m_subtract = new QRadioButton(tr("&Subtract from Channel"), this);
    m_intersect = new QRadioButton(tr("&Intersect with Channel"), this);
    m_replace->setChecked(true);

    auto* form = new QFormLayout;
    form->addRow(tr("Document:"), m_document);
    form->addRow(tr("Channel:"), m_channel);
    form->addRow(tr("Name:"), m_name);

    auto* ops = new QVBoxLayout;
    ops->setContentsMargins(0, 0, 0, 0);
    ops->addWidget(m_replace);
    ops->addWidget(m_add);
    ops->addWidget(m_subtract);
    ops->addWidget(m_intersect);
    auto* opsHost = new QWidget(this);
    opsHost->setLayout(ops);
    form->addRow(tr("Operation:"), opsHost);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(makeButtons(this));

    setChannelLabels(channelLabels);
    connect(m_document, &QComboBox::currentIndexChanged, this, [this](int index) {
        emit destinationDocumentChanged(m_document->itemData(index).toInt());
    });
    connect(m_channel, &QComboBox::currentIndexChanged,
            this, &SaveSelectionDialog::onChannelChanged);
}

void SaveSelectionDialog::setChannelLabels(const std::vector<QString>& channelLabels)
{
    m_channel->blockSignals(true);
    m_channel->clear();
    m_channel->addItem(tr("New"), -1);
    for (int i = 0; i < static_cast<int>(channelLabels.size()); ++i)
        m_channel->addItem(channelLabels[static_cast<size_t>(i)], i);
    m_channel->setCurrentIndex(0);
    m_channel->blockSignals(false);
    updateOperationState();
}

void SaveSelectionDialog::onChannelChanged(int)
{
    updateOperationState();
}

void SaveSelectionDialog::updateOperationState()
{
    // A new channel has nothing to combine with, so only Replace applies — and
    // the name field is only meaningful then.
    const bool isNew = destinationChannelIndex() < 0;
    m_name->setEnabled(isNew);
    m_add->setEnabled(!isNew);
    m_subtract->setEnabled(!isNew);
    m_intersect->setEnabled(!isNew);
    if (isNew)
        m_replace->setChecked(true);
}

int SaveSelectionDialog::destinationDocumentIndex() const
{
    return m_document->currentData().toInt();
}

int SaveSelectionDialog::destinationChannelIndex() const
{
    return m_channel->currentData().toInt();
}

QString SaveSelectionDialog::channelName() const { return m_name->text().trimmed(); }

SelectMode SaveSelectionDialog::combineMode() const
{
    if (m_add->isChecked()) return SelectMode::Add;
    if (m_subtract->isChecked()) return SelectMode::Subtract;
    if (m_intersect->isChecked()) return SelectMode::Intersect;
    return SelectMode::Replace;
}

// ── LoadSelectionDialog ────────────────────────────────────────

LoadSelectionDialog::LoadSelectionDialog(const std::vector<QString>& documentLabels,
                                         const std::vector<QString>& channelLabels,
                                         bool hasActiveSelection, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Load Selection"));
    applyPanelTheme(this);

    m_document = new AppComboBox(this);
    for (int i = 0; i < static_cast<int>(documentLabels.size()); ++i)
        m_document->addItem(documentLabels[static_cast<size_t>(i)], i);
    m_channel = new AppComboBox(this);
    m_invert = new AppCheckBox(tr("&Invert"));

    m_replace = new QRadioButton(tr("&New Selection"), this);
    m_add = new QRadioButton(tr("&Add to Selection"), this);
    m_subtract = new QRadioButton(tr("&Subtract from Selection"), this);
    m_intersect = new QRadioButton(tr("&Intersect with Selection"), this);
    m_replace->setChecked(true);
    // The three boolean operations need something to combine WITH.
    m_add->setEnabled(hasActiveSelection);
    m_subtract->setEnabled(hasActiveSelection);
    m_intersect->setEnabled(hasActiveSelection);

    auto* form = new QFormLayout;
    form->addRow(tr("Document:"), m_document);
    form->addRow(tr("Channel:"), m_channel);
    form->addRow(QString(), m_invert);

    auto* ops = new QVBoxLayout;
    ops->setContentsMargins(0, 0, 0, 0);
    ops->addWidget(m_replace);
    ops->addWidget(m_add);
    ops->addWidget(m_subtract);
    ops->addWidget(m_intersect);
    auto* opsHost = new QWidget(this);
    opsHost->setLayout(ops);
    form->addRow(tr("Operation:"), opsHost);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(makeButtons(this));

    setChannelLabels(channelLabels);
    connect(m_document, &QComboBox::currentIndexChanged, this, [this](int index) {
        emit sourceDocumentChanged(m_document->itemData(index).toInt());
    });
}

void LoadSelectionDialog::setChannelLabels(const std::vector<QString>& channelLabels)
{
    m_channel->blockSignals(true);
    m_channel->clear();
    for (int i = 0; i < static_cast<int>(channelLabels.size()); ++i)
        m_channel->addItem(channelLabels[static_cast<size_t>(i)], i);
    m_channel->blockSignals(false);
}

int LoadSelectionDialog::sourceDocumentIndex() const
{
    return m_document->currentData().toInt();
}

int LoadSelectionDialog::sourceChannelIndex() const
{
    return m_channel->currentData().isValid() ? m_channel->currentData().toInt() : -1;
}

bool LoadSelectionDialog::invert() const { return m_invert->isChecked(); }

SelectMode LoadSelectionDialog::combineMode() const
{
    if (m_add->isChecked()) return SelectMode::Add;
    if (m_subtract->isChecked()) return SelectMode::Subtract;
    if (m_intersect->isChecked()) return SelectMode::Intersect;
    return SelectMode::Replace;
}

// ── MergeChannelsDialog ────────────────────────────────────────

MergeChannelsDialog::MergeChannelsDialog(const std::vector<QString>& documentLabels,
                                         QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Merge Channels"));
    applyPanelTheme(this);

    auto makeCombo = [&](int preselect) {
        auto* combo = new AppComboBox(this);
        for (int i = 0; i < static_cast<int>(documentLabels.size()); ++i)
            combo->addItem(documentLabels[static_cast<size_t>(i)], i);
        if (preselect < combo->count())
            combo->setCurrentIndex(preselect);
        return combo;
    };
    // Pre-pick the first three sources in order: Split Channels produces them
    // in R, G, B order, so the common case needs no fiddling.
    m_red = makeCombo(0);
    m_green = makeCombo(1);
    m_blue = makeCombo(2);

    auto* form = new QFormLayout;
    form->addRow(tr("Red:"), m_red);
    form->addRow(tr("Green:"), m_green);
    form->addRow(tr("Blue:"), m_blue);

    auto* layout = new QVBoxLayout(this);
    auto* note = new QLabel(tr("Pick one grayscale document per component. "
                               "All of them must have the same pixel "
                               "dimensions."), this);
    note->setWordWrap(true);
    layout->addWidget(note);
    layout->addLayout(form);
    layout->addWidget(makeButtons(this));
}

std::vector<int> MergeChannelsDialog::componentSources() const
{
    return {m_red->currentData().toInt(),
            m_green->currentData().toInt(),
            m_blue->currentData().toInt()};
}
