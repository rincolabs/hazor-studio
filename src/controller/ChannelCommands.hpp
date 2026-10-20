#pragma once

#include "CommandHistory.hpp"
#include "core/Document.hpp"
#include "core/DocumentChannel.hpp"

#include <QImage>
#include <QString>
#include <algorithm>
#include <utility>
#include <vector>

// Undo/redo for the document's extra channels.
//
// Every command here bumps channelViewGeneration rather than
// compositionGeneration: a channel is not part of the layer composite, so
// re-running the compositor would be wasted work (ProjectionCache re-applies
// only the channel view — see renderer/ChannelView.hpp). The two geometry
// commands are the exception and live with the rest of the document-state
// snapshot in Commands.hpp, because a canvas re-frame moves channels, layers,
// masks, guides and the selection together.

// Stores a channel's settings without its pixels, so renaming or recolouring
// does not snapshot a full buffer.
struct ChannelSettings {
    QString name;
    ChannelKind kind = ChannelKind::Alpha;
    ChannelMaskRepresentation representation = ChannelMaskRepresentation::MaskedAreas;
    QColor overlayColor;
    float overlayOpacity = 0.5f;
    QColor inkColor;
    float solidity = 1.0f;
    QString inkBook;

    static ChannelSettings capture(const DocumentChannel& ch)
    {
        ChannelSettings s;
        s.name = ch.name;
        s.kind = ch.kind;
        s.representation = ch.representation;
        s.overlayColor = ch.overlayColor;
        s.overlayOpacity = ch.overlayOpacity;
        s.inkColor = ch.inkColor;
        s.solidity = ch.solidity;
        s.inkBook = ch.inkBook;
        return s;
    }

    void applyTo(DocumentChannel& ch) const
    {
        ch.name = name;
        ch.kind = kind;
        ch.representation = representation;
        ch.overlayColor = overlayColor;
        ch.overlayOpacity = overlayOpacity;
        ch.inkColor = inkColor;
        ch.solidity = solidity;
        ch.inkBook = inkBook;
    }
};

// Adds a channel (New, New Spot, Duplicate, Save Selection as Channel).
// Undo removes it again; redo puts the SAME id back at the same index, so a
// later command that refers to the channel by id still resolves after a
// round trip through the history.
class ChannelAddCommand : public Command {
public:
    ChannelAddCommand(Document* doc, DocumentChannel channel, int index,
                      QString commandName)
        : m_doc(doc)
        , m_channel(std::move(channel))
        , m_index(index)
        , m_name(std::move(commandName))
    {}

    void execute() override
    {
        if (!m_doc)
            return;
        m_index = m_doc->addChannel(m_channel, m_index);
    }

    void undo() override
    {
        if (!m_doc || m_index < 0)
            return;
        m_doc->deleteChannel(m_index);
    }

    QString name() const override { return m_name; }
    int index() const { return m_index; }
    uint64_t channelId() const { return m_channel.id; }

private:
    Document* m_doc;
    DocumentChannel m_channel;
    int m_index;
    QString m_name;
};

class ChannelRemoveCommand : public Command {
public:
    ChannelRemoveCommand(Document* doc, int index, QString commandName)
        : m_doc(doc), m_index(index), m_name(std::move(commandName))
    {
        if (m_doc && m_index >= 0
            && m_index < static_cast<int>(m_doc->channels.size())) {
            m_channel = m_doc->channels[static_cast<size_t>(m_index)];
            m_channel.data = m_channel.data.copy();
        }
    }

    void execute() override
    {
        if (m_doc)
            m_doc->deleteChannel(m_index);
    }

    void undo() override
    {
        if (m_doc && m_channel.id != 0)
            m_doc->addChannel(m_channel, m_index);
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    int m_index;
    DocumentChannel m_channel;
    QString m_name;
};

// Name / mask representation / overlay colour+opacity / ink colour / solidity,
// and the Alpha↔Spot conversion. Pixels are only snapshotted for the
// conversion, which is the one case that rewrites them (polarity flip).
class ChannelSettingsCommand : public Command {
public:
    ChannelSettingsCommand(Document* doc, uint64_t channelId,
                           ChannelSettings before, ChannelSettings after,
                           QString commandName)
        : m_doc(doc)
        , m_id(channelId)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_before, m_after); }
    void undo() override { apply(m_after, m_before); }
    QString name() const override { return m_name; }

private:
    void apply(const ChannelSettings& from, const ChannelSettings& to)
    {
        if (!m_doc)
            return;
        auto* ch = m_doc->channelById(m_id);
        if (!ch)
            return;
        // A kind change is a polarity change: convert through coverage so the
        // selected areas of an Alpha become the inked areas of a Spot instead
        // of its negative. applyTo() then sets the settled kind.
        if (from.kind != to.kind)
            ch->convertKindPreservingCoverage(to.kind);
        to.applyTo(*ch);
        ch->thumbDirty = true;
        m_doc->invalidateChannelView();
    }

    Document* m_doc;
    uint64_t m_id;
    ChannelSettings m_before;
    ChannelSettings m_after;
    QString m_name;
};

// Drag-and-drop reordering within the extra block. Spot overprint order is
// panel order, so this is a visual change with real consequences for the
// composite — hence a history entry.
class ChannelReorderCommand : public Command {
public:
    ChannelReorderCommand(Document* doc, int from, int to, QString commandName)
        : m_doc(doc), m_from(from), m_to(to), m_name(std::move(commandName))
    {}

    void execute() override { move(m_from, m_to); }
    void undo() override { move(m_to, m_from); }
    QString name() const override { return m_name; }

private:
    void move(int from, int to)
    {
        if (!m_doc)
            return;
        const int count = static_cast<int>(m_doc->channels.size());
        if (from < 0 || from >= count || to < 0 || to >= count || from == to)
            return;
        DocumentChannel ch = std::move(m_doc->channels[static_cast<size_t>(from)]);
        m_doc->channels.erase(m_doc->channels.begin() + from);
        m_doc->channels.insert(m_doc->channels.begin() + to, std::move(ch));
        m_doc->invalidateChannelView();
    }

    Document* m_doc;
    int m_from;
    int m_to;
    QString m_name;
};

// Pixels of one channel: a brush stroke (one entry per stroke, not per dab),
// a gradient, a fill, Invert, a paste, a filter. The whole buffer is kept
// because a channel is one 8-bit plane — a document-sized grayscale snapshot
// is a quarter of what a layer snapshot already costs elsewhere in this file.
class ChannelPixelsCommand : public Command {
public:
    ChannelPixelsCommand(Document* doc, uint64_t channelId,
                         QImage before, QImage after, QString commandName)
        : m_doc(doc)
        , m_id(channelId)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_after); }
    void undo() override { apply(m_before); }
    QString name() const override { return m_name; }

private:
    void apply(const QImage& img)
    {
        if (!m_doc || img.isNull())
            return;
        auto* ch = m_doc->channelById(m_id);
        if (!ch)
            return;
        ch->data = img;
        ch->thumbDirty = true;
        m_doc->invalidateChannelView();
    }

    Document* m_doc;
    uint64_t m_id;
    QImage m_before;
    QImage m_after;
    QString m_name;
};

// Replaces the whole channel list. Used by the operations that restructure it
// as a unit — Merge Spot Channel (which also flattens the layers, so it rides
// along with a document-state command) and a channel import.
class ChannelListCommand : public Command {
public:
    ChannelListCommand(Document* doc,
                       std::vector<DocumentChannel> before,
                       std::vector<DocumentChannel> after,
                       QString commandName)
        : m_doc(doc)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_after); }
    void undo() override { apply(m_before); }
    QString name() const override { return m_name; }

private:
    void apply(const std::vector<DocumentChannel>& list)
    {
        if (!m_doc)
            return;
        m_doc->channels = list;
        for (auto& ch : m_doc->channels)
            ch.thumbDirty = true;
        // An armed channel may be gone from the restored list; fall back to the
        // composite so the tools never write into a dangling id.
        auto& armed = m_doc->channelState.selectedChannels;
        armed.erase(std::remove_if(armed.begin(), armed.end(),
                                   [this](uint64_t id) {
                                       return m_doc->channelById(id) == nullptr;
                                   }),
                    armed.end());
        if (armed.empty() && !m_doc->channelState.anyColorSelected())
            m_doc->channelState.selectComposite();
        m_doc->invalidateChannelView();
    }

    Document* m_doc;
    std::vector<DocumentChannel> m_before;
    std::vector<DocumentChannel> m_after;
    QString m_name;
};
