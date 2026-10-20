#pragma once

#include <QColor>
#include <QDialog>
#include <QPushButton>
#include <QString>
#include <vector>

#include "core/DocumentChannel.hpp"
#include "core/SelectionMask.hpp"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QRadioButton;
class QSpinBox;
class Document;

// Dialogs for the Channels panel. Each one only COLLECTS values — the
// operations themselves live on ImageController, so every path (panel button,
// context menu, menu action) runs the same code and records the same history
// entry.

// A colour square that opens the project's colour picker. Used for the alpha
// overlay colour and the spot ink.
class ChannelColorButton : public QPushButton {
    Q_OBJECT
public:
    explicit ChannelColorButton(const QColor& color, QWidget* parent = nullptr);
    QColor color() const { return m_color; }
    void setColor(const QColor& color);

signals:
    void colorChanged(const QColor& color);

private:
    void updateSwatch();
    QColor m_color;
};

// New Channel / Channel Options for an Alpha channel.
class AlphaChannelDialog : public QDialog {
    Q_OBJECT
public:
    // `allowKindChange` offers the "make this a spot channel" conversion, which
    // Channel Options has and New Channel does not.
    AlphaChannelDialog(const QString& title, const QString& name,
                       ChannelMaskRepresentation representation,
                       const QColor& overlayColor, float overlayOpacity,
                       bool allowKindChange, QWidget* parent = nullptr);

    QString channelName() const;
    ChannelMaskRepresentation representation() const;
    QColor overlayColor() const;
    float overlayOpacity() const;
    bool convertToSpot() const;

private:
    QLineEdit* m_name = nullptr;
    QRadioButton* m_masked = nullptr;
    QRadioButton* m_selected = nullptr;
    ChannelColorButton* m_color = nullptr;
    QSpinBox* m_opacity = nullptr;
    QCheckBox* m_toSpot = nullptr;
};

// New Spot Channel / Channel Options for a Spot channel.
class SpotChannelDialog : public QDialog {
    Q_OBJECT
public:
    SpotChannelDialog(const QString& title, const QString& name,
                      const QColor& inkColor, float solidity,
                      const QString& inkBook, bool allowKindChange,
                      QWidget* parent = nullptr);

    QString channelName() const;
    QColor inkColor() const;
    float solidity() const;
    // Colour-book name, when the ink was picked from one of the project's
    // colour libraries. Empty for a hand-picked colour.
    QString inkBook() const;
    bool convertToAlpha() const;

private slots:
    void onLibraryChanged(int index);
    void onInkChosen(int index);

private:
    void reloadLibraries();

    QLineEdit* m_name = nullptr;
    ChannelColorButton* m_color = nullptr;
    QSpinBox* m_solidity = nullptr;
    QComboBox* m_library = nullptr;
    QComboBox* m_ink = nullptr;
    QCheckBox* m_toAlpha = nullptr;
    QString m_inkBook;
};

// Duplicate Channel.
class DuplicateChannelDialog : public QDialog {
    Q_OBJECT
public:
    // `documents` are the open documents the duplicate may go to, in the
    // order the caller wants them listed; `labels` are their display names.
    // The first entry must be the source document itself.
    DuplicateChannelDialog(const QString& suggestedName,
                           const std::vector<QString>& labels,
                           QWidget* parent = nullptr);

    QString channelName() const;
    // Index into the `labels` passed in, or -1 for "New" (a new document).
    int destinationIndex() const;
    bool invertContents() const;

private:
    QLineEdit* m_name = nullptr;
    QComboBox* m_destination = nullptr;
    QCheckBox* m_invert = nullptr;
};

// Select ▸ Save Selection.
class SaveSelectionDialog : public QDialog {
    Q_OBJECT
public:
    struct ChannelEntry { QString label; };

    SaveSelectionDialog(const std::vector<QString>& documentLabels,
                        const std::vector<QString>& channelLabels,
                        const QString& suggestedName, QWidget* parent = nullptr);

    int destinationDocumentIndex() const;
    // -1 for "New Channel", otherwise the index into `channelLabels`.
    int destinationChannelIndex() const;
    QString channelName() const;
    SelectMode combineMode() const;

    // Replaces the channel list when the destination document changes.
    void setChannelLabels(const std::vector<QString>& channelLabels);

signals:
    void destinationDocumentChanged(int index);

private slots:
    void onChannelChanged(int index);

private:
    void updateOperationState();

    QComboBox* m_document = nullptr;
    QComboBox* m_channel = nullptr;
    QLineEdit* m_name = nullptr;
    QRadioButton* m_replace = nullptr;
    QRadioButton* m_add = nullptr;
    QRadioButton* m_subtract = nullptr;
    QRadioButton* m_intersect = nullptr;
};

// Select ▸ Load Selection.
class LoadSelectionDialog : public QDialog {
    Q_OBJECT
public:
    LoadSelectionDialog(const std::vector<QString>& documentLabels,
                        const std::vector<QString>& channelLabels,
                        bool hasActiveSelection, QWidget* parent = nullptr);

    int sourceDocumentIndex() const;
    int sourceChannelIndex() const;
    bool invert() const;
    SelectMode combineMode() const;

    void setChannelLabels(const std::vector<QString>& channelLabels);

signals:
    void sourceDocumentChanged(int index);

private:
    QComboBox* m_document = nullptr;
    QComboBox* m_channel = nullptr;
    QCheckBox* m_invert = nullptr;
    QRadioButton* m_replace = nullptr;
    QRadioButton* m_add = nullptr;
    QRadioButton* m_subtract = nullptr;
    QRadioButton* m_intersect = nullptr;
};

// Merge Channels: picks the source document for each colour component.
class MergeChannelsDialog : public QDialog {
    Q_OBJECT
public:
    MergeChannelsDialog(const std::vector<QString>& documentLabels,
                        QWidget* parent = nullptr);

    // One document index per component, in R, G, B order.
    std::vector<int> componentSources() const;

private:
    QComboBox* m_red = nullptr;
    QComboBox* m_green = nullptr;
    QComboBox* m_blue = nullptr;
};
