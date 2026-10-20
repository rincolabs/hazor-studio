#include "ImageController.hpp"

#include "ChannelCommands.hpp"
#include "Commands.hpp"
#include "core/Clipboard.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "renderer/ChannelView.hpp"
#include "renderer/DocumentCompositor.hpp"
#include "renderer/RenderContext.hpp"

#include "engine/ImageEngine.hpp"

#include <QPainter>

#include <opencv2/core.hpp>

#include <algorithm>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────
// Channels — creation, management, selection round-trips and channel-space
// pixel edits.
//
// Everything destructive goes through a command in ChannelCommands.hpp, so
// the Channels panel inherits the same undo/redo and non-linear history as
// the rest of the editor. The view-state setters at the bottom deliberately
// do NOT: an eye toggle or an edit-target change is not an edit, and turning
// it into a history entry would bury real work under dozens of no-op steps.
// ─────────────────────────────────────────────────────────────────────────

namespace {

QImage toGray(const QImage& src)
{
    if (src.isNull())
        return src;
    return src.format() == QImage::Format_Grayscale8
        ? src : src.convertToFormat(QImage::Format_Grayscale8);
}

// The selection's coverage, resized to the canvas. Null when there is nothing
// selected — callers treat that as "the whole canvas", the way every other
// tool here does.
QImage selectionCoverage(const Document* doc)
{
    if (!doc || !doc->selection.active() || doc->selection.image().isNull())
        return {};
    if (doc->selection.isEmpty())
        return {};
    return toGray(doc->selection.image());
}

} // namespace

// ── Creation ───────────────────────────────────────────────────

int ImageController::newAlphaChannel(const QString& name,
                                     ChannelMaskRepresentation representation,
                                     const QColor& overlayColor,
                                     float overlayOpacity)
{
    if (!m_doc || m_doc->size.isEmpty())
        return -1;
    if (!m_doc->canAddChannel()) {
        emit operationBlocked(tr("This image already has the maximum of %1 channels.")
                                  .arg(Document::kMaxImageChannels));
        return -1;
    }

    DocumentChannel ch;
    ch.kind = ChannelKind::Alpha;
    ch.name = name.isEmpty() ? m_doc->uniqueChannelName(ChannelKind::Alpha) : name;
    ch.representation = representation;
    ch.overlayColor = overlayColor;
    ch.overlayOpacity = std::clamp(overlayOpacity, 0.0f, 1.0f);
    // Black: no selection anywhere, the standard empty alpha channel.
    ch.data = DocumentChannel::makeNeutralBuffer(m_doc->size, ChannelKind::Alpha);
    ch.id = m_doc->allocateChannelId();

    auto cmd = std::make_unique<ChannelAddCommand>(m_doc, ch, -1, tr("New Channel"));
    cmd->execute();
    const int index = cmd->index();
    m_history.push(std::move(cmd));

    if (index >= 0)
        selectChannelAt(index, false);
    emit channelsChanged();
    emit imageChanged();
    return index;
}

int ImageController::newSpotChannel(const QString& name, const QColor& inkColor,
                                    float solidity, const QString& inkBook)
{
    if (!m_doc || m_doc->size.isEmpty())
        return -1;
    if (!m_doc->canAddChannel()) {
        emit operationBlocked(tr("This image already has the maximum of %1 channels.")
                                  .arg(Document::kMaxImageChannels));
        return -1;
    }

    DocumentChannel ch;
    ch.kind = ChannelKind::Spot;
    ch.name = name.isEmpty() ? m_doc->uniqueChannelName(ChannelKind::Spot) : name;
    ch.inkColor = inkColor;
    ch.inkBook = inkBook;
    ch.solidity = std::clamp(solidity, 0.0f, 1.0f);
    // White: no ink. Spot polarity is inverted, so an "empty" spot is white.
    ch.data = DocumentChannel::makeNeutralBuffer(m_doc->size, ChannelKind::Spot);
    ch.id = m_doc->allocateChannelId();

    // With a live selection the new spot starts inked inside it, at the
    // selection's own coverage — the reference behaviour, and the reason the
    // partial values have to survive rather than being thresholded.
    const QImage sel = selectionCoverage(m_doc);
    if (!sel.isNull()) {
        const int w = std::min(sel.width(), ch.data.width());
        const int h = std::min(sel.height(), ch.data.height());
        for (int y = 0; y < h; ++y) {
            const uchar* src = sel.constScanLine(y);
            uchar* dst = ch.data.scanLine(y);
            for (int x = 0; x < w; ++x)
                dst[x] = static_cast<uchar>(255 - src[x]);
        }
    }

    auto cmd = std::make_unique<ChannelAddCommand>(m_doc, ch, -1,
                                                   tr("New Spot Channel"));
    cmd->execute();
    const int index = cmd->index();
    m_history.push(std::move(cmd));

    if (index >= 0)
        selectChannelAt(index, false);
    emit channelsChanged();
    emit imageChanged();
    return index;
}

int ImageController::duplicateChannel(int index, const QString& newName,
                                      bool invert, Document* targetDoc)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return -1;

    Document* dest = targetDoc ? targetDoc : m_doc;
    if (dest->size != m_doc->size) {
        emit operationBlocked(tr("The destination document has different pixel "
                                 "dimensions."));
        return -1;
    }
    if (!dest->canAddChannel()) {
        emit operationBlocked(tr("The destination document already has the "
                                 "maximum of %1 channels.")
                                  .arg(Document::kMaxImageChannels));
        return -1;
    }

    DocumentChannel copy = m_doc->channels[static_cast<size_t>(index)];
    copy.id = 0;  // a duplicate is a new channel, not an alias of the source
    copy.externalId = 0;
    copy.visible = false;
    copy.data = toGray(copy.data).copy();
    copy.name = newName.isEmpty()
        ? dest->uniqueChannelName(copy.kind) : newName;
    if (invert) {
        for (int y = 0; y < copy.data.height(); ++y) {
            uchar* row = copy.data.scanLine(y);
            for (int x = 0; x < copy.data.width(); ++x)
                row[x] = static_cast<uchar>(255 - row[x]);
        }
    }
    copy.id = dest->allocateChannelId();

    // Duplicating into ANOTHER document cannot ride this document's history:
    // undoing here would have to reach into a stack that knows nothing about
    // it. The destination owns the change, so it is applied directly and its
    // own dirty state picks it up.
    if (dest != m_doc) {
        const int at = dest->addChannel(std::move(copy), -1);
        // The caller owns the destination tab and marks it modified; this
        // document did not change, so it must not be flagged dirty here.
        emit channelsChanged();
        return at;
    }

    auto cmd = std::make_unique<ChannelAddCommand>(m_doc, std::move(copy), -1,
                                                   tr("Duplicate Channel"));
    cmd->execute();
    const int at = cmd->index();
    m_history.push(std::move(cmd));
    if (at >= 0)
        selectChannelAt(at, false);
    emit channelsChanged();
    emit imageChanged();
    return at;
}

void ImageController::deleteChannelAt(int index)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;

    auto cmd = std::make_unique<ChannelRemoveCommand>(m_doc, index,
                                                       tr("Delete Channel"));
    cmd->execute();
    m_history.push(std::move(cmd));
    emit channelsChanged();
    emit imageChanged();
}

void ImageController::renameChannel(int index, const QString& name)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;
    DocumentChannel& ch = m_doc->channels[static_cast<size_t>(index)];
    if (name.isEmpty() || name == ch.name)
        return;

    ChannelSettings before = ChannelSettings::capture(ch);
    ChannelSettings after = before;
    after.name = name;
    auto cmd = std::make_unique<ChannelSettingsCommand>(
        m_doc, ch.id, before, after, tr("Rename Channel"));
    cmd->execute();
    m_history.push(std::move(cmd));
    emit channelsChanged();
}

void ImageController::setChannelSettings(int index, const ChannelSettings& settings)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;
    DocumentChannel& ch = m_doc->channels[static_cast<size_t>(index)];
    ChannelSettings before = ChannelSettings::capture(ch);

    auto cmd = std::make_unique<ChannelSettingsCommand>(
        m_doc, ch.id, before, settings, tr("Channel Options"));
    cmd->execute();
    m_history.push(std::move(cmd));
    emit channelsChanged();
    emit imageChanged();
}

void ImageController::reorderChannel(int fromIndex, int toIndex)
{
    if (!m_doc)
        return;
    const int count = static_cast<int>(m_doc->channels.size());
    if (fromIndex < 0 || fromIndex >= count || toIndex < 0 || toIndex >= count
        || fromIndex == toIndex)
        return;

    auto cmd = std::make_unique<ChannelReorderCommand>(m_doc, fromIndex, toIndex,
                                                        tr("Reorder Channel"));
    cmd->execute();
    m_history.push(std::move(cmd));
    emit channelsChanged();
    emit imageChanged();
}

bool ImageController::mergeSpotChannel(int index)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return false;
    const DocumentChannel& spot = m_doc->channels[static_cast<size_t>(index)];
    if (!spot.isSpot())
        return false;
    if (anyFullyLockedLayer()) {
        emit operationBlocked(tr("A layer is fully locked; unable to merge the "
                                 "spot channel."));
        return false;
    }

    // The ink has to end up in real colour pixels, and it belongs to the
    // document rather than to any one layer — so the layer stack is flattened
    // first, exactly as the reference command does. The caller is responsible
    // for confirming that with the user before getting here.
    const DocumentChannel spotCopy = spot;
    m_history.beginMacro(tr("Merge Spot Channel"));
    flattenImage();

    auto* node = m_doc->nodeAt(0);
    if (!node || !node->layer) {
        m_history.endMacro();
        return false;
    }

    QImage before = node->layer->cpuImage.copy();
    QImage after = before.convertToFormat(QImage::Format_RGBA8888);

    // One visible spot, composited by the same rule the preview uses, so the
    // merged pixels match what the user was looking at.
    channelview::View view;
    channelview::Extra extra;
    extra.mode = channelview::Extra::Mode::SpotInk;
    extra.stored = &spotCopy.data;
    extra.color = spotCopy.inkColor;
    extra.solidity = std::clamp(spotCopy.solidity, 0.0f, 1.0f);
    view.extras.push_back(extra);
    channelview::apply(after, view);

    auto pixels = std::make_unique<FilterCommand>(
        m_doc, 0, std::move(before), node->transform,
        after, node->transform, tr("Merge Spot Channel"));
    pixels->execute();
    m_history.push(std::move(pixels));

    const int spotIndex = m_doc->channelIndexById(spotCopy.id);
    if (spotIndex >= 0) {
        auto remove = std::make_unique<ChannelRemoveCommand>(
            m_doc, spotIndex, tr("Merge Spot Channel"));
        remove->execute();
        m_history.push(std::move(remove));
    }

    m_history.endMacro();
    emit channelsChanged();
    emit imageChanged();
    return true;
}

// ── Selections ─────────────────────────────────────────────────

int ImageController::saveSelectionAsChannel(const QString& name)
{
    if (!m_doc || m_doc->selection.image().isNull())
        return -1;
    if (!m_doc->selection.active() || m_doc->selection.isEmpty()) {
        emit operationBlocked(tr("There is no active selection to save."));
        return -1;
    }
    if (!m_doc->canAddChannel()) {
        emit operationBlocked(tr("This image already has the maximum of %1 channels.")
                                  .arg(Document::kMaxImageChannels));
        return -1;
    }

    // Document::saveSelectionToChannel defines what a saved selection channel
    // IS (an Alpha holding the mask verbatim, partial coverage included); this
    // only adds the history entry, so there is one implementation of the rule.
    m_doc->saveSelectionToChannel(
        name.isEmpty() ? m_doc->uniqueChannelName(ChannelKind::Alpha) : name);
    if (m_doc->channels.empty())
        return -1;

    const int index = static_cast<int>(m_doc->channels.size()) - 1;
    // Pushed without executing: the channel is already there, and the command
    // only drives undo/redo from here on — the same contract the layer and
    // mask snapshots use.
    m_history.push(std::make_unique<ChannelAddCommand>(
        m_doc, m_doc->channels[static_cast<size_t>(index)], index,
        tr("Save Selection")));
    emit channelsChanged();
    emit imageChanged();
    return index;
}

void ImageController::saveSelectionIntoChannel(int index, SelectMode mode)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;
    const QImage sel = selectionCoverage(m_doc);
    if (sel.isNull()) {
        emit operationBlocked(tr("There is no active selection to save."));
        return;
    }

    DocumentChannel& ch = m_doc->channels[static_cast<size_t>(index)];
    const QImage before = toGray(ch.data).copy();
    QImage coverage = m_doc->channelCoverageMask(index);
    if (coverage.isNull())
        return;

    const int w = std::min(sel.width(), coverage.width());
    const int h = std::min(sel.height(), coverage.height());
    for (int y = 0; y < h; ++y) {
        const uchar* srcRow = sel.constScanLine(y);
        uchar* dstRow = coverage.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int s = srcRow[x];
            const int d = dstRow[x];
            switch (mode) {
            case SelectMode::Replace:   dstRow[x] = static_cast<uchar>(s); break;
            case SelectMode::Add:       dstRow[x] = static_cast<uchar>(std::max(d, s)); break;
            case SelectMode::Subtract:  dstRow[x] = static_cast<uchar>(std::max(0, d - s)); break;
            case SelectMode::Intersect: dstRow[x] = static_cast<uchar>(std::min(d, s)); break;
            }
        }
    }
    if (mode == SelectMode::Replace) {
        // Replace covers the whole canvas, including the part outside the
        // selection rectangle, which the loop above never visited.
        for (int y = h; y < coverage.height(); ++y)
            std::fill(coverage.scanLine(y), coverage.scanLine(y) + coverage.width(), 0);
        for (int y = 0; y < h; ++y)
            std::fill(coverage.scanLine(y) + w,
                      coverage.scanLine(y) + coverage.width(), 0);
    }

    // Back to stored polarity.
    QImage after(coverage.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < coverage.height(); ++y) {
        const uchar* src = coverage.constScanLine(y);
        uchar* dst = after.scanLine(y);
        for (int x = 0; x < coverage.width(); ++x)
            dst[x] = ch.fromCoverage8(src[x]);
    }

    auto cmd = std::make_unique<ChannelPixelsCommand>(
        m_doc, ch.id, before, after, tr("Save Selection"));
    cmd->execute();
    m_history.push(std::move(cmd));
    emit channelsChanged();
    emit imageChanged();
}

void ImageController::loadChannelAsSelection(int index, SelectMode mode, bool invert)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;

    const QImage beforeMask = m_doc->selection.image().copy();
    const bool beforeActive = m_doc->selection.active();

    if (!invert) {
        m_doc->loadChannelToSelection(index, mode);
    } else {
        // Inverting the SOURCE, not the result: a subtract of the inverted
        // channel is not the same as the inverse of a subtract.
        QImage coverage = m_doc->channelCoverageMask(index);
        if (coverage.isNull())
            return;
        for (int y = 0; y < coverage.height(); ++y) {
            uchar* row = coverage.scanLine(y);
            for (int x = 0; x < coverage.width(); ++x)
                row[x] = static_cast<uchar>(255 - row[x]);
        }
        if (m_doc->selection.image().isNull()
            || m_doc->selection.image().size() != m_doc->size)
            m_doc->selection.resize(m_doc->size.width(), m_doc->size.height());
        m_doc->selection.combineGrayscaleMask(coverage, mode);
        m_doc->selection.setActive(!m_doc->selection.isEmpty());
    }

    m_history.push(std::make_unique<SelectionCommand>(
        m_doc, beforeMask, m_doc->selection.image().copy(),
        beforeActive, m_doc->selection.active(), tr("Load Selection")));
    emit selectionChanged();
    emit imageChanged();
}

void ImageController::loadColorComponentAsSelection(int component, SelectMode mode,
                                                     bool invert)
{
    if (!m_doc || component < 0 || component > 2 || m_doc->size.isEmpty())
        return;

    RenderContext ctx;
    ctx.document = m_doc;
    ctx.targetType = RenderTargetType::Flatten;
    ctx.outputSize = m_doc->size;
    const QImage flat = DocumentCompositor::composite(m_doc, ctx)
        .convertToFormat(QImage::Format_RGBA8888);
    if (flat.isNull())
        return;

    QImage coverage(flat.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < flat.height(); ++y) {
        const uchar* src = flat.constScanLine(y);
        uchar* dst = coverage.scanLine(y);
        for (int x = 0; x < flat.width(); ++x) {
            // The component's intensity IS the coverage; the composite's own
            // alpha scales it so a transparent region contributes nothing.
            const int v = src[x * 4 + component];
            const int a = src[x * 4 + 3];
            int value = v * a / 255;
            if (invert)
                value = 255 - value;
            dst[x] = static_cast<uchar>(value);
        }
    }

    const QImage beforeMask = m_doc->selection.image().copy();
    const bool beforeActive = m_doc->selection.active();
    if (m_doc->selection.image().isNull()
        || m_doc->selection.image().size() != m_doc->size)
        m_doc->selection.resize(m_doc->size.width(), m_doc->size.height());
    m_doc->selection.combineGrayscaleMask(coverage, mode);
    m_doc->selection.setActive(!m_doc->selection.isEmpty());

    m_history.push(std::make_unique<SelectionCommand>(
        m_doc, beforeMask, m_doc->selection.image().copy(),
        beforeActive, m_doc->selection.active(), tr("Load Selection")));
    emit selectionChanged();
    emit imageChanged();
}

// ── View and edit state (no history by design) ─────────────────

void ImageController::setChannelVisible(int index, bool visible)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;
    DocumentChannel& ch = m_doc->channels[static_cast<size_t>(index)];
    if (ch.visible == visible)
        return;
    ch.visible = visible;
    m_doc->invalidateChannelView();
    emit imageChanged();
}

void ImageController::setColorChannelVisible(int component, bool visible)
{
    if (!m_doc || component < 0 || component > 2)
        return;
    if (m_doc->channelState.colorVisible[component] == visible)
        return;
    m_doc->channelState.colorVisible[component] = visible;
    m_doc->invalidateChannelView();
    emit imageChanged();
}

void ImageController::setShowChannelsInColor(bool enabled)
{
    if (!m_doc || m_doc->channelState.showChannelsInColor == enabled)
        return;
    m_doc->channelState.showChannelsInColor = enabled;
    m_doc->invalidateChannelView();
    emit imageChanged();
}

void ImageController::selectCompositeChannel()
{
    if (!m_doc)
        return;
    // The RGB row IS the layer-pixel target, so it ends a layer-mask edit the
    // same way picking a channel does.
    claimEditTarget(EditTarget::LayerPixels);
    ChannelState& st = m_doc->channelState;
    // selectComposite() restores the three components (arming + visibility);
    // the extras are the panel's own rows, so hiding them belongs here.
    st.selectComposite();
    for (auto& ch : m_doc->channels)
        ch.visible = false;
    m_doc->invalidateChannelView();
    emit channelsChanged();
    emit imageChanged();
}

void ImageController::selectColorComponent(int component, bool additive)
{
    if (!m_doc || component < 0 || component > 2)
        return;
    claimEditTarget(EditTarget::Channel);
    ChannelState& st = m_doc->channelState;

    if (!additive) {
        st.selectedChannels.clear();
        for (int c = 0; c < 3; ++c) {
            st.colorSelected[c] = (c == component);
            st.colorVisible[c] = (c == component);
        }
        for (auto& ch : m_doc->channels)
            ch.visible = false;
    } else {
        // Shift+click extends the armed set. Un-arming the last target would
        // leave the tools with nowhere to write, so the composite takes over.
        st.colorSelected[component] = !st.colorSelected[component];
        st.colorVisible[component] = st.colorSelected[component];
        if (!st.anyColorSelected() && st.selectedChannels.empty())
            st.selectComposite();
    }
    m_doc->invalidateChannelView();
    emit channelsChanged();
    emit imageChanged();
}

void ImageController::selectChannelAt(int index, bool additive)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;
    claimEditTarget(EditTarget::Channel);
    ChannelState& st = m_doc->channelState;
    const uint64_t id = m_doc->channels[static_cast<size_t>(index)].id;

    if (!additive) {
        st.selectOnlyChannel(id);
        for (int c = 0; c < 3; ++c)
            st.colorVisible[c] = false;
        for (auto& ch : m_doc->channels)
            ch.visible = (ch.id == id);
    } else {
        if (st.isChannelSelected(id)) {
            auto& armed = st.selectedChannels;
            armed.erase(std::remove(armed.begin(), armed.end(), id), armed.end());
            if (auto* ch = m_doc->channelById(id))
                ch->visible = false;
            if (armed.empty() && !st.anyColorSelected())
                st.selectComposite();
        } else {
            st.selectedChannels.push_back(id);
            if (auto* ch = m_doc->channelById(id))
                ch->visible = true;
        }
    }
    m_doc->invalidateChannelView();
    emit channelsChanged();
    emit imageChanged();
}

// ── Channel pixels ─────────────────────────────────────────────

std::vector<QImage*> ImageController::armedChannelBuffers()
{
    std::vector<QImage*> out;
    if (!m_doc)
        return out;
    for (uint64_t id : m_doc->channelState.selectedChannels) {
        if (auto* ch = m_doc->channelById(id)) {
            if (ch->data.isNull() || ch->data.size() != m_doc->size)
                m_doc->ensureChannelBuffers();
            if (ch->data.format() != QImage::Format_Grayscale8)
                ch->data = ch->data.convertToFormat(QImage::Format_Grayscale8);
            out.push_back(&ch->data);
        }
    }
    return out;
}

bool ImageController::hasArmedChannels() const
{
    return m_doc && !m_doc->channelState.selectedChannels.empty();
}

bool ImageController::hasArmedColorSubset() const
{
    if (!m_doc)
        return false;
    const ChannelState& st = m_doc->channelState;
    return st.anyColorSelected() && !st.allColorSelected();
}

void ImageController::pushChannelPixelSnapshot(int index, const QImage& before,
                                                const QString& commandName)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;
    DocumentChannel& ch = m_doc->channels[static_cast<size_t>(index)];
    if (before.isNull() || ch.data.isNull())
        return;
    // The edit is already in the buffer: record it without re-executing, the
    // same contract the brush/mask snapshots use.
    m_history.push(std::make_unique<ChannelPixelsCommand>(
        m_doc, ch.id, before, ch.data.copy(), commandName));
    ch.thumbDirty = true;
    m_doc->invalidateChannelView();
}

void ImageController::invertChannelAt(int index)
{
    if (!m_doc || index < 0 || index >= static_cast<int>(m_doc->channels.size()))
        return;
    DocumentChannel& ch = m_doc->channels[static_cast<size_t>(index)];
    if (ch.data.isNull())
        return;

    const QImage before = toGray(ch.data).copy();
    QImage after = before;
    const QImage sel = selectionCoverage(m_doc);
    for (int y = 0; y < after.height(); ++y) {
        uchar* row = after.scanLine(y);
        const uchar* selRow = (!sel.isNull() && y < sel.height())
            ? sel.constScanLine(y) : nullptr;
        for (int x = 0; x < after.width(); ++x) {
            const int inverted = 255 - row[x];
            if (!selRow) {
                row[x] = static_cast<uchar>(inverted);
                continue;
            }
            // Partial selection coverage blends partially — a hard switch here
            // would turn a feathered selection into a hard edge.
            const int cov = (x < sel.width()) ? selRow[x] : 0;
            row[x] = static_cast<uchar>(row[x] + (inverted - row[x]) * cov / 255);
        }
    }

    auto cmd = std::make_unique<ChannelPixelsCommand>(
        m_doc, ch.id, before, after, tr("Invert"));
    cmd->execute();
    m_history.push(std::move(cmd));
    emit channelsChanged();
    emit imageChanged();
}

void ImageController::fillArmedChannels(float value, const QString& commandName)
{
    const int target = static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    applyToArmedChannels([this, target](QImage& buffer) {
        const QImage sel = selectionCoverage(m_doc);
        for (int y = 0; y < buffer.height(); ++y) {
            uchar* row = buffer.scanLine(y);
            const uchar* selRow = (!sel.isNull() && y < sel.height())
                ? sel.constScanLine(y) : nullptr;
            for (int x = 0; x < buffer.width(); ++x) {
                if (!selRow) {
                    row[x] = static_cast<uchar>(target);
                    continue;
                }
                const int cov = (x < sel.width()) ? selRow[x] : 0;
                row[x] = static_cast<uchar>(row[x] + (target - row[x]) * cov / 255);
            }
        }
    }, commandName);
}

void ImageController::applyGradientToArmedChannels(const QImage& gradientCoverage,
                                                    const QString& commandName)
{
    if (gradientCoverage.isNull())
        return;
    const QImage ramp = toGray(gradientCoverage);
    applyToArmedChannels([this, &ramp](QImage& buffer) {
        const QImage sel = selectionCoverage(m_doc);
        const int h = std::min(buffer.height(), ramp.height());
        const int w = std::min(buffer.width(), ramp.width());
        for (int y = 0; y < h; ++y) {
            uchar* row = buffer.scanLine(y);
            const uchar* src = ramp.constScanLine(y);
            const uchar* selRow = (!sel.isNull() && y < sel.height())
                ? sel.constScanLine(y) : nullptr;
            for (int x = 0; x < w; ++x) {
                const int target = src[x];
                if (!selRow) {
                    row[x] = static_cast<uchar>(target);
                    continue;
                }
                const int cov = (x < sel.width()) ? selRow[x] : 0;
                row[x] = static_cast<uchar>(row[x] + (target - row[x]) * cov / 255);
            }
        }
    }, commandName);
}

void ImageController::applyToArmedChannels(const std::function<void(QImage&)>& op,
                                            const QString& commandName)
{
    if (!m_doc || !op)
        return;
    const auto armed = m_doc->channelState.selectedChannels;
    if (armed.empty())
        return;

    // One history entry for the whole operation, however many channels are
    // armed: the user performed one action.
    const bool group = armed.size() > 1;
    if (group)
        m_history.beginMacro(commandName);

    for (uint64_t id : armed) {
        auto* ch = m_doc->channelById(id);
        if (!ch || ch->data.isNull())
            continue;
        if (ch->data.format() != QImage::Format_Grayscale8)
            ch->data = ch->data.convertToFormat(QImage::Format_Grayscale8);
        const QImage before = ch->data.copy();
        QImage after = before.copy();
        op(after);
        auto cmd = std::make_unique<ChannelPixelsCommand>(
            m_doc, id, before, after, commandName);
        cmd->execute();
        m_history.push(std::move(cmd));
    }

    if (group)
        m_history.endMacro();
    emit channelsChanged();
    emit imageChanged();
}

// ── Split / Merge Channels ─────────────────────────────────────

std::vector<QImage> ImageController::splitColorChannels() const
{
    std::vector<QImage> planes;
    if (!m_doc || m_doc->size.isEmpty())
        return planes;

    RenderContext ctx;
    ctx.document = const_cast<Document*>(m_doc);
    ctx.targetType = RenderTargetType::Flatten;
    ctx.outputSize = m_doc->size;
    const QImage flat = DocumentCompositor::composite(m_doc, ctx)
        .convertToFormat(QImage::Format_RGBA8888);
    if (flat.isNull())
        return planes;

    const int components = std::min(3, m_doc->colorChannelCount());
    for (int c = 0; c < components; ++c) {
        QImage plane(flat.size(), QImage::Format_Grayscale8);
        for (int y = 0; y < flat.height(); ++y) {
            const uchar* src = flat.constScanLine(y);
            uchar* dst = plane.scanLine(y);
            for (int x = 0; x < flat.width(); ++x) {
                // Split works on a flattened image, so transparency has to
                // resolve to something: white, the same paper the composite is
                // matted against everywhere else in the export path.
                const int a = src[x * 4 + 3];
                const int v = src[x * 4 + c];
                dst[x] = static_cast<uchar>(v * a / 255 + (255 - a));
            }
        }
        planes.push_back(std::move(plane));
    }
    return planes;
}

QImage ImageController::mergeColorChannels(const std::vector<QImage>& planes)
{
    if (planes.size() < 3)
        return {};
    const QSize size = planes[0].size();
    for (const QImage& plane : planes) {
        if (plane.isNull() || plane.size() != size)
            return {};
    }

    QImage out(size, QImage::Format_RGBA8888);
    std::vector<QImage> gray;
    gray.reserve(3);
    for (int c = 0; c < 3; ++c)
        gray.push_back(toGray(planes[static_cast<size_t>(c)]));

    for (int y = 0; y < size.height(); ++y) {
        const uchar* r = gray[0].constScanLine(y);
        const uchar* g = gray[1].constScanLine(y);
        const uchar* b = gray[2].constScanLine(y);
        uchar* dst = out.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            dst[x * 4 + 0] = r[x];
            dst[x * 4 + 1] = g[x];
            dst[x * 4 + 2] = b[x];
            dst[x * 4 + 3] = 255;
        }
    }
    return out;
}

// ── Quick Mask ─────────────────────────────────────────────────

void ImageController::setQuickMaskActive(bool active)
{
    if (!m_doc || m_doc->quickMask.active == active)
        return;
    m_doc->quickMask.active = active;
    m_doc->invalidateChannelView();
    emit channelsChanged();
    emit imageChanged();
}

bool ImageController::isQuickMaskActive() const
{
    return m_doc && m_doc->quickMask.active;
}

void ImageController::setQuickMaskSettings(ChannelMaskRepresentation representation,
                                           const QColor& color, float opacity)
{
    if (!m_doc)
        return;
    m_doc->quickMask.representation = representation;
    m_doc->quickMask.overlayColor = color;
    m_doc->quickMask.overlayOpacity = std::clamp(opacity, 0.0f, 1.0f);
    m_doc->invalidateChannelView();
    emit imageChanged();
}

// ── Fill Bucket into a channel ─────────────────────────────────

bool ImageController::applyFillBucketToChannels(const QPoint& docPos,
                                                const QColor& color,
                                                float tolerance)
{
    if (!m_doc || m_doc->channelState.selectedChannels.empty())
        return false;

    // The same rule the mask fill bucket uses: the colour's luminance is the
    // value written, and an active selection replaces the flood region with a
    // coverage-weighted blend so a feathered selection stays feathered.
    const int gray = qGray(color.rgb());
    const bool hasSelection = m_doc->selection.active()
                           && !m_doc->selection.isEmpty();
    bool changed = false;

    applyToArmedChannels([&](QImage& buffer) {
        cv::Mat mat(buffer.height(), buffer.width(), CV_8UC1, buffer.bits(),
                    static_cast<size_t>(buffer.bytesPerLine()));
        if (hasSelection) {
            const QImage sel = selectionCoverage(m_doc);
            if (sel.isNull())
                return;
            const int h = std::min(mat.rows, sel.height());
            const int w = std::min(mat.cols, sel.width());
            for (int y = 0; y < h; ++y) {
                uchar* d = mat.ptr<uchar>(y);
                const uchar* m = sel.constScanLine(y);
                for (int x = 0; x < w; ++x) {
                    const int mv = m[x];
                    if (mv == 0) continue;
                    if (mv == 255) { d[x] = static_cast<uchar>(gray); continue; }
                    const int dv = d[x];
                    d[x] = static_cast<uchar>(dv + ((gray - dv) * mv + 127) / 255);
                }
            }
            changed = true;
            return;
        }
        if (docPos.x() < 0 || docPos.y() < 0
            || docPos.x() >= mat.cols || docPos.y() >= mat.rows)
            return;
        cv::Mat result = ImageEngine::fillRegion(mat, docPos.x(), docPos.y(),
                                                 cv::Scalar(gray), tolerance);
        if (result.empty())
            return;
        result.copyTo(mat);
        changed = true;
    }, tr("Fill Channel"));

    return changed;
}

// ── Copy / Paste with a channel as the target ──────────────────

bool ImageController::copyArmedChannel()
{
    if (!m_doc || m_doc->channelState.selectedChannels.empty())
        return false;
    const int index = m_doc->channelIndexById(m_doc->channelState.selectedChannels.front());
    if (index < 0)
        return false;
    const DocumentChannel& ch = m_doc->channels[static_cast<size_t>(index)];
    if (ch.data.isNull())
        return false;

    // The selection bounds, or the whole channel without one — the same rule
    // the layer copy follows.
    QRect rect = ch.data.rect();
    if (m_doc->selection.active() && !m_doc->selection.isEmpty())
        rect = m_doc->selection.bounds().toAlignedRect().intersected(rect);
    if (rect.isEmpty())
        return false;

    const QImage region = ch.data.copy(rect);
    // Published as RGBA grey so it pastes into a layer, another channel or
    // another application alike. The channel's own polarity is NOT applied:
    // what is copied is what the channel stores.
    QImage pixels(region.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < region.height(); ++y) {
        const uchar* src = region.constScanLine(y);
        uchar* dst = pixels.scanLine(y);
        for (int x = 0; x < region.width(); ++x) {
            dst[x * 4 + 0] = src[x];
            dst[x * 4 + 1] = src[x];
            dst[x * 4 + 2] = src[x];
            dst[x * 4 + 3] = 255;
        }
    }

    ClipboardData data;
    data.type = ClipboardType::Pixels;
    data.pixels = std::move(pixels);
    data.docPosition = rect.topLeft();
    data.sourceDocSize = m_doc->size;
    data.name = ch.name;
    ClipboardManager::instance().setData(std::move(data));
    emit clipboardChanged();
    return true;
}

bool ImageController::pasteIntoArmedChannels()
{
    if (!m_doc || m_doc->channelState.selectedChannels.empty())
        return false;
    const ClipboardData& data = ClipboardManager::instance().data();
    if (data.type != ClipboardType::Pixels || data.pixels.isNull())
        return false;

    const QImage source = data.pixels.convertToFormat(QImage::Format_RGBA8888);
    // Anchored at the selection's top-left when there is one, otherwise where
    // it was copied from — same placement rule as a pixel paste.
    QPoint origin = data.docPosition.toPoint();
    if (m_doc->selection.active() && !m_doc->selection.isEmpty())
        origin = m_doc->selection.bounds().toAlignedRect().topLeft();

    applyToArmedChannels([&source, origin](QImage& buffer) {
        for (int y = 0; y < source.height(); ++y) {
            const int dy = origin.y() + y;
            if (dy < 0 || dy >= buffer.height())
                continue;
            const uchar* src = source.constScanLine(y);
            uchar* dst = buffer.scanLine(dy);
            for (int x = 0; x < source.width(); ++x) {
                const int dx = origin.x() + x;
                if (dx < 0 || dx >= buffer.width())
                    continue;
                const int lum = (src[x * 4] * 299 + src[x * 4 + 1] * 587
                                 + src[x * 4 + 2] * 114) / 1000;
                const int a = src[x * 4 + 3];
                // The pasted image's own alpha blends it in, so a cut-out
                // paste does not stamp a black rectangle around itself.
                dst[dx] = static_cast<uchar>(dst[dx] + (lum - dst[dx]) * a / 255);
            }
        }
    }, tr("Paste"));
    return true;
}
