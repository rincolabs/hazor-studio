#pragma once

#include "CommandHistory.hpp"
#include "core/Document.hpp"
#include "color/ColorProfile.hpp"
#include "core/GuideTypes.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/ShapeTypes.hpp"
#include "text/TextTypes.hpp"
#include "text/TextRenderer.hpp"
#include <QImage>
#include <QPoint>
#include <QTransform>
#include <QSize>
#include <algorithm>
#include <memory>
#include <set>
#include <utility>

struct LayerMaskSnapshot {
    QImage image;
    QImage rawImage;
    QPoint origin = QPoint(0, 0);
    bool visible = true;
    float density = 1.0f;
    float feather = 0.0f;

    static LayerMaskSnapshot capture(const Layer* layer) {
        LayerMaskSnapshot snapshot;
        if (!layer)
            return snapshot;
        snapshot.image = layer->maskImage.copy();
        snapshot.rawImage = layer->maskRawImage.copy();
        snapshot.origin = layer->maskOrigin;
        snapshot.visible = layer->maskVisible;
        snapshot.density = layer->maskDensity;
        snapshot.feather = layer->maskFeather;
        return snapshot;
    }

    static void restore(Layer* layer, const LayerMaskSnapshot& snapshot) {
        if (!layer)
            return;
        layer->maskImage = snapshot.image.copy();
        layer->maskRawImage = snapshot.rawImage.copy();
        layer->maskOrigin = snapshot.origin;
        layer->maskVisible = snapshot.visible;
        layer->maskDensity = snapshot.density;
        layer->maskFeather = snapshot.feather;
        layer->maskTextureOutdated = true;
        layer->maskThumbDirty = true;
        if (layer->owner) {
            layer->owner->thumbnailDirty = true;
            layer->owner->invalidateEffects();
        }
    }

    static void clear(Layer* layer) {
        if (!layer)
            return;
        layer->maskImage = QImage();
        layer->maskRawImage = QImage();
        layer->maskOrigin = QPoint(0, 0);
        layer->maskVisible = false;
        layer->maskDensity = 1.0f;
        layer->maskFeather = 0.0f;
        layer->maskTextureOutdated = true;
        layer->maskThumbDirty = true;
        if (layer->owner) {
            layer->owner->thumbnailDirty = true;
            layer->owner->invalidateEffects();
        }
    }
};

class MaskEditCommand : public Command {
public:
    MaskEditCommand(Document* doc, int flatIndex,
                    QImage before, QImage after, QString commandName,
                    QPoint beforeOrigin = QPoint(0,0), QPoint afterOrigin = QPoint(0,0))
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_name(std::move(commandName))
        , m_beforeOrigin(beforeOrigin)
        , m_afterOrigin(afterOrigin)
    {}

    void execute() override { apply(m_after, m_afterOrigin); }
    void undo() override    { apply(m_before, m_beforeOrigin); }
    QString name() const override { return m_name; }

private:
    void apply(const QImage& img, const QPoint& origin) {
        auto* layer = m_doc ? m_doc->layerAtFlat(m_flatIndex) : nullptr;
        if (layer && !layer->maskImage.isNull()) {
            layer->maskImage = img;
            layer->maskOrigin = origin;
            layer->maskTextureOutdated = true;
            layer->maskThumbDirty = true;
            // Masked shapes draw from cpuImage instead of the sprite cache, so
            // undo/redo that adds/removes the mask must re-upload the texture.
            if (layer->isShapeLayer())
                layer->textureOutdated = true;
            if (layer->owner) {
                layer->owner->thumbnailDirty = true;
                layer->owner->invalidateEffects();
            }
            if (m_doc) ++m_doc->compositionGeneration;
        }
    }

    Document* m_doc;
    int m_flatIndex;
    QImage m_before;
    QImage m_after;
    QString m_name;
    QPoint m_beforeOrigin;
    QPoint m_afterOrigin;
};

class AssignProfileCommand : public Command {
public:
    AssignProfileCommand(Document* doc, ColorProfile afterProfile)
        : m_doc(doc)
        , m_beforeProfile(doc ? doc->colorProfile() : ColorProfile::invalid())
        , m_beforeSource(doc ? doc->profileSource() : ColorProfileSource::Missing)
        , m_afterProfile(std::move(afterProfile))
        , m_afterSource(m_afterProfile.isValid()
              ? ColorProfileSource::AssignedByUser
              : ColorProfileSource::Missing)
    {
        m_afterProfile.setSource(m_afterSource);
    }

    void execute() override { apply(m_afterProfile, m_afterSource); }
    void undo() override { apply(m_beforeProfile, m_beforeSource); }
    QString name() const override { return QStringLiteral("Assign Profile"); }

private:
    void apply(ColorProfile profile, ColorProfileSource source)
    {
        if (!m_doc)
            return;
        profile.setSource(source);
        m_doc->setColorProfile(profile);
        m_doc->setProfileSource(source);
        m_doc->markColorStateDirty();
    }

    Document* m_doc = nullptr;
    ColorProfile m_beforeProfile;
    ColorProfileSource m_beforeSource = ColorProfileSource::Missing;
    ColorProfile m_afterProfile;
    ColorProfileSource m_afterSource = ColorProfileSource::Missing;
};

class ConvertDocumentProfileCommand : public Command {
public:
    ConvertDocumentProfileCommand(Document* doc,
                                  std::vector<std::unique_ptr<LayerTreeNode>> beforeRoots,
                                  int beforeActiveFlatIndex,
                                  std::set<int> beforeSelectedFlatIndices,
                                  ColorProfile beforeProfile,
                                  ColorProfileSource beforeSource,
                                  std::vector<std::unique_ptr<LayerTreeNode>> afterRoots,
                                  int afterActiveFlatIndex,
                                  std::set<int> afterSelectedFlatIndices,
                                  ColorProfile afterProfile,
                                  ColorProfileSource afterSource)
        : m_doc(doc)
        , m_beforeRoots(std::move(beforeRoots))
        , m_beforeActiveFlatIndex(beforeActiveFlatIndex)
        , m_beforeSelectedFlatIndices(std::move(beforeSelectedFlatIndices))
        , m_beforeProfile(std::move(beforeProfile))
        , m_beforeSource(beforeSource)
        , m_afterRoots(std::move(afterRoots))
        , m_afterActiveFlatIndex(afterActiveFlatIndex)
        , m_afterSelectedFlatIndices(std::move(afterSelectedFlatIndices))
        , m_afterProfile(std::move(afterProfile))
        , m_afterSource(afterSource)
    {}

    void execute() override {
        apply(m_afterRoots, m_afterActiveFlatIndex, m_afterSelectedFlatIndices,
              m_afterProfile, m_afterSource);
    }

    void undo() override {
        apply(m_beforeRoots, m_beforeActiveFlatIndex, m_beforeSelectedFlatIndices,
              m_beforeProfile, m_beforeSource);
    }

    QString name() const override { return QStringLiteral("Convert to Profile"); }

private:
    static std::vector<std::unique_ptr<LayerTreeNode>> cloneRoots(
        const std::vector<std::unique_ptr<LayerTreeNode>>& roots)
    {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& root : roots) {
            if (root)
                clones.push_back(root->clone());
        }
        return clones;
    }

    static void markRootsTextureOutdated(std::vector<std::unique_ptr<LayerTreeNode>>& roots)
    {
        std::vector<LayerTreeNode*> flat;
        LayerTreeNode::flatten(roots, flat);
        for (auto* node : flat) {
            if (!node)
                continue;
            node->thumbnailDirty = true;
            node->sourceDirty = true;
            node->invalidateEffects();
            if (node->layer) {
                node->layer->textureOutdated = true;
                node->layer->pendingGpuUpload = true;
                node->layer->shapeCache.dirty = true;
                if (node->layer->rasterStorage.isEnabled())
                    node->layer->rasterStorage.markAllGpuDirty();
            }
        }
    }

    static std::set<int> clampSelected(const std::set<int>& selected, int flatCount)
    {
        std::set<int> out;
        for (int idx : selected) {
            if (idx >= 0 && idx < flatCount)
                out.insert(idx);
        }
        return out;
    }

    void apply(const std::vector<std::unique_ptr<LayerTreeNode>>& roots,
               int activeFlatIndex,
               const std::set<int>& selectedFlatIndices,
               ColorProfile profile,
               ColorProfileSource source)
    {
        if (!m_doc)
            return;

        profile.setSource(source);
        m_doc->roots = cloneRoots(roots);
        markRootsTextureOutdated(m_doc->roots);
        m_doc->setColorProfile(profile);
        m_doc->setProfileSource(source);
        m_doc->markColorStateDirty();

        const int count = m_doc->flatCount();
        if (count <= 0) {
            m_doc->activeFlatIndex = -1;
            m_doc->selectedFlatIndices.clear();
        } else {
            m_doc->activeFlatIndex = std::clamp(activeFlatIndex, 0, count - 1);
            m_doc->selectedFlatIndices = clampSelected(selectedFlatIndices, count);
            if (m_doc->selectedFlatIndices.empty())
                m_doc->selectedFlatIndices.insert(m_doc->activeFlatIndex);
            else if (m_doc->selectedFlatIndices.count(m_doc->activeFlatIndex) == 0)
                m_doc->activeFlatIndex = *m_doc->selectedFlatIndices.rbegin();
        }
        ++m_doc->compositionGeneration;
    }

    Document* m_doc = nullptr;
    std::vector<std::unique_ptr<LayerTreeNode>> m_beforeRoots;
    int m_beforeActiveFlatIndex = -1;
    std::set<int> m_beforeSelectedFlatIndices;
    ColorProfile m_beforeProfile;
    ColorProfileSource m_beforeSource = ColorProfileSource::Missing;
    std::vector<std::unique_ptr<LayerTreeNode>> m_afterRoots;
    int m_afterActiveFlatIndex = -1;
    std::set<int> m_afterSelectedFlatIndices;
    ColorProfile m_afterProfile;
    ColorProfileSource m_afterSource = ColorProfileSource::Missing;
};

class FilterCommand : public Command {
public:
    FilterCommand(Document* doc, int flatIndex,
                  QImage before, QTransform beforeTransform,
                  QImage after, QTransform afterTransform,
                  QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_before(std::move(before))
        , m_beforeTransform(beforeTransform)
        , m_after(std::move(after))
        , m_afterTransform(afterTransform)
        , m_name(std::move(commandName))
    {}

    void execute() override {
        apply(m_after, m_afterTransform);
    }

    void undo() override {
        apply(m_before, m_beforeTransform);
    }

    QString name() const override { return m_name; }

private:
    void apply(const QImage& img, const QTransform& t) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (node && node->type == LayerTreeNode::Type::Layer && node->layer) {
            auto* l = node->layer.get();
            node->layer->cpuImage = img;
            node->transform = t;
            // The before/after snapshots are full flat images (every creator
            // flushes tiles into cpuImage first), so the pixel state is fully
            // defined by `img` in both representations. If the layer currently
            // uses rasterStorage (dab layer), rebuild the tiles from the applied
            // image instead of flattening: keeping the representation preserves
            // the content-bounds transform outline across undo/redo.
            if (l->rasterStorage.isEnabled()) {
                l->rasterStorage.replaceWithImage(
                    img, QPoint(0, 0),
                    l->rasterStorage.tileSize() > 0 ? l->rasterStorage.tileSize()
                                                    : 256);
            } else {
                l->rasterStorage.clear();
            }
            if (l->tiledSystem) {
                l->enableTiling(l->tileManager.tileSize());
                l->tileManager.markAllDirty();
                l->pendingGpuUpload = true;
            }
            l->textureOutdated = true;
            node->invalidateEffects();
            if (m_doc) ++m_doc->compositionGeneration;
        }
    }

    Document* m_doc;
    int m_flatIndex;
    QImage m_before;
    QTransform m_beforeTransform;
    QImage m_after;
    QTransform m_afterTransform;
    QString m_name;
};

class RasterTileCommand : public Command {
public:
    RasterTileCommand(Document* doc, int flatIndex,
                      std::vector<core::RasterTileChange> changes,
                      QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_changes(std::move(changes))
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(true); }
    void undo() override { apply(false); }
    QString name() const override { return m_name; }

private:
    void apply(bool useAfter) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
            return;

        auto* layer = node->layer.get();
        // The change list is a delta against the storage state at stroke time.
        // A later command may have flattened the layer (flush tiles → cpuImage,
        // rasterStorage.clear()) — e.g. a filter; its undo restores the flat
        // representation. Applying the delta to that now-empty storage would
        // re-enable tile rendering with ONLY the stroke's tiles, wiping the rest
        // of the layer. Re-seed the storage from the current (flushed) cpuImage
        // first so the delta lands on the full content again.
        if (!layer->rasterStorage.isEnabled() || !layer->rasterStorage.hasTiles()) {
            if (!layer->cpuImage.isNull()) {
                const int tileSize = layer->rasterStorage.tileSize() > 0
                    ? layer->rasterStorage.tileSize() : 256;
                layer->rasterStorage.replaceWithImage(layer->cpuImage,
                                                      QPoint(0, 0), tileSize);
            }
        }
        layer->rasterStorage.applyChanges(m_changes, useAfter);
        layer->pendingGpuUpload = true;
        layer->textureOutdated = true;
        layer->dirtyRegion.clear();
        if (layer->owner) {
            layer->owner->invalidateEffects();
            layer->owner->thumbnailDirty = true;
        }
        if (m_doc)
            ++m_doc->compositionGeneration;
    }

    Document* m_doc;
    int m_flatIndex;
    std::vector<core::RasterTileChange> m_changes;
    QString m_name;
};

class SelectionCommand : public Command {
public:
    SelectionCommand(Document* doc,
                     QImage beforeMask, QImage afterMask,
                     bool beforeActive, bool afterActive,
                     QString commandName)
        : m_doc(doc)
        , m_before(std::move(beforeMask))
        , m_after(std::move(afterMask))
        , m_beforeActive(beforeActive)
        , m_afterActive(afterActive)
        , m_name(std::move(commandName))
    {}

    void execute() override {
        m_doc->selection.image() = m_after;
        m_doc->selection.setActive(m_afterActive);
    }

    void undo() override {
        m_doc->selection.image() = m_before;
        m_doc->selection.setActive(m_beforeActive);
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    QImage m_before;
    QImage m_after;
    bool m_beforeActive = false;
    bool m_afterActive = false;
    QString m_name;
};

class DocumentSizeCommand : public Command {
public:
    DocumentSizeCommand(Document* doc, QSize before, QSize after, QString name)
        : m_doc(doc), m_before(before), m_after(after), m_name(std::move(name))
    {}

    void execute() override { m_doc->size = m_after; }
    void undo()        override { m_doc->size = m_before; }
    QString name() const override { return m_name; }

private:
    Document* m_doc;
    QSize m_before;
    QSize m_after;
    QString m_name;
};

class NodeTransformCommand : public Command {
public:
    NodeTransformCommand(Document* doc,
                         std::vector<int> flatIndices,
                         std::vector<QTransform> beforeTransforms,
                         std::vector<QTransform> afterTransforms,
                         QString name)
        : m_doc(doc)
        , m_flatIndices(std::move(flatIndices))
        , m_beforeTransforms(std::move(beforeTransforms))
        , m_afterTransforms(std::move(afterTransforms))
        , m_name(std::move(name))
    {}

    void execute() override { apply(m_afterTransforms); }
    void undo() override { apply(m_beforeTransforms); }
    QString name() const override { return m_name; }

private:
    void apply(const std::vector<QTransform>& transforms) {
        if (!m_doc) return;
        const size_t count = std::min(m_flatIndices.size(), transforms.size());
        for (size_t i = 0; i < count; ++i) {
            auto* node = m_doc->nodeAt(m_flatIndices[i]);
            if (node)
                node->transform = transforms[i];
        }
        ++m_doc->compositionGeneration;
    }

    Document* m_doc;
    std::vector<int> m_flatIndices;
    std::vector<QTransform> m_beforeTransforms;
    std::vector<QTransform> m_afterTransforms;
    QString m_name;
};

class CompositeCommand : public Command {
public:
    explicit CompositeCommand(QString name) : m_name(std::move(name)) {}

    void add(std::unique_ptr<Command> cmd) {
        m_commands.push_back(std::move(cmd));
    }

    void execute() override {
        for (auto& c : m_commands) c->execute();
    }

    void undo() override {
        for (auto it = m_commands.rbegin(); it != m_commands.rend(); ++it)
            (*it)->undo();
    }

    QString name() const override { return m_name; }

private:
    std::vector<std::unique_ptr<Command>> m_commands;
    QString m_name;
};

class AddGuideCommand : public Command {
public:
    AddGuideCommand(Document* doc, int index, Guide guide, QString name)
        : m_doc(doc), m_index(index), m_guide(guide), m_name(std::move(name))
    {}

    void execute() override {
        if (!m_doc) return;
        m_doc->guideManager.insertGuide(m_index, m_guide);
    }

    void undo() override {
        if (!m_doc) return;
        m_doc->guideManager.removeGuide(m_index);
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    int m_index = 0;
    Guide m_guide;
    QString m_name;
};

class MoveGuideCommand : public Command {
public:
    MoveGuideCommand(Document* doc, int index, qreal before, qreal after, QString name)
        : m_doc(doc), m_index(index), m_before(before), m_after(after), m_name(std::move(name))
    {}

    void execute() override { apply(m_after); }
    void undo() override { apply(m_before); }
    QString name() const override { return m_name; }

private:
    void apply(qreal position) {
        if (!m_doc) return;
        m_doc->guideManager.moveGuide(m_index, position);
    }

    Document* m_doc;
    int m_index = -1;
    qreal m_before = 0.0;
    qreal m_after = 0.0;
    QString m_name;
};

class RemoveGuideCommand : public Command {
public:
    RemoveGuideCommand(Document* doc, int index, Guide guide, QString name)
        : m_doc(doc), m_index(index), m_guide(guide), m_name(std::move(name))
    {}

    void execute() override {
        if (!m_doc) return;
        m_doc->guideManager.removeGuide(m_index);
    }

    void undo() override {
        if (!m_doc) return;
        m_doc->guideManager.insertGuide(m_index, m_guide);
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    int m_index = -1;
    Guide m_guide;
    QString m_name;
};

class ClearGuidesCommand : public Command {
public:
    ClearGuidesCommand(Document* doc, std::vector<Guide> before, QString name)
        : m_doc(doc), m_before(std::move(before)), m_name(std::move(name))
    {}

    void execute() override {
        if (!m_doc) return;
        m_doc->guideManager.clear();
    }

    void undo() override {
        if (!m_doc) return;
        m_doc->guideManager.setGuides(m_before);
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    std::vector<Guide> m_before;
    QString m_name;
};

// ── Structural Commands ─────────────────────────────────────

class AddLayerCommand : public Command {
public:
    AddLayerCommand(Document* doc, int flatIndex,
                    std::unique_ptr<LayerTreeNode> node, QString name)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_node(std::move(node))
        , m_name(std::move(name))
    {}

    void execute() override {
        if (!m_node) return;
        m_flatIndex = m_doc->insertNodeAt(m_flatIndex, std::move(m_node));
        m_doc->activeFlatIndex = m_flatIndex;
        m_doc->selectedFlatIndices = {m_flatIndex};
        auto* n = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (n && n->layer) { n->layer->textureOutdated = true; n->invalidateEffects(); }
        if (m_doc) ++m_doc->compositionGeneration;
    }

    void undo() override {
        auto removed = m_doc->takeNodeAt(m_flatIndex);
        if (removed) m_node = std::move(removed);
        if (m_doc->flatCount() == 0) {
            m_doc->activeFlatIndex = -1;
            m_doc->selectedFlatIndices.clear();
        } else {
            if (m_doc->activeFlatIndex >= m_doc->flatCount())
                m_doc->activeFlatIndex = m_doc->flatCount() - 1;
            m_doc->selectedFlatIndices = {m_doc->activeFlatIndex};
        }
        if (m_doc) ++m_doc->compositionGeneration;
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    int m_flatIndex = 0;
    std::unique_ptr<LayerTreeNode> m_node;
    QString m_name;
};

class RemoveLayerCommand : public Command {
public:
    RemoveLayerCommand(Document* doc, int flatIndex,
                       std::unique_ptr<LayerTreeNode> node, QString name)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_node(std::move(node))
        , m_name(std::move(name))
    {}

    void execute() override {
        auto removed = m_doc->takeNodeAt(m_flatIndex);
        if (removed) m_node = std::move(removed);
        if (m_doc->flatCount() == 0)
            m_doc->activeFlatIndex = -1;
        else if (m_doc->activeFlatIndex >= m_doc->flatCount())
            m_doc->activeFlatIndex = m_doc->flatCount() - 1;
    }

    void undo() override {
        if (!m_node) return;
        m_flatIndex = m_doc->insertNodeAt(m_flatIndex, std::move(m_node));
        m_doc->activeFlatIndex = m_flatIndex;
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    int m_flatIndex = 0;
    std::unique_ptr<LayerTreeNode> m_node;
    QString m_name;
};

class DuplicateLayerCommand : public Command {
public:
    DuplicateLayerCommand(Document* doc, int flatIndex,
                          std::unique_ptr<LayerTreeNode> node, QString name)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_node(std::move(node))
        , m_name(std::move(name))
    {}

    void execute() override {
        if (!m_node) return;
        m_flatIndex = m_doc->insertNodeAt(m_flatIndex, std::move(m_node));
        m_doc->activeFlatIndex = m_flatIndex;
        m_doc->selectedFlatIndices = {m_flatIndex};
        auto* n = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (n && n->layer) { n->layer->textureOutdated = true; n->invalidateEffects(); }
    }

    void undo() override {
        auto removed = m_doc->takeNodeAt(m_flatIndex);
        if (removed) m_node = std::move(removed);
        if (m_doc->flatCount() == 0) {
            m_doc->activeFlatIndex = -1;
            m_doc->selectedFlatIndices.clear();
        } else {
            if (m_doc->activeFlatIndex >= m_doc->flatCount())
                m_doc->activeFlatIndex = m_doc->flatCount() - 1;
            m_doc->selectedFlatIndices = {m_doc->activeFlatIndex};
        }
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    int m_flatIndex = 0;
    std::unique_ptr<LayerTreeNode> m_node;
    QString m_name;
};

// ── Text Commands ────────────────────────────────────────────

class TextEditCommand : public Command {
public:
    TextEditCommand(Document* doc, int flatIndex,
                    TextLayerData before, TextLayerData after,
                    QTransform beforeTransform, QTransform afterTransform,
                    QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_beforeTransform(beforeTransform)
        , m_afterTransform(afterTransform)
        , m_name(std::move(commandName))
    {}

    void execute() override {
        apply(m_after, m_afterTransform);
    }

    void undo() override {
        apply(m_before, m_beforeTransform);
    }

    QString name() const override { return m_name; }

private:
    void apply(const TextLayerData& data, const QTransform& t) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
            return;

        auto textData = std::make_shared<TextLayerData>(data);
        node->layer->textData = textData;
        node->transform = t;

        TextRenderer renderer;
        renderer.render(*textData, node->layer->cpuImage);
        node->layer->textureOutdated = true;
        node->invalidateEffects();
        node->thumbnailDirty = true;
        if (m_doc)
            ++m_doc->compositionGeneration;
    }

    Document* m_doc;
    int m_flatIndex;
    TextLayerData m_before;
    TextLayerData m_after;
    QTransform m_beforeTransform;
    QTransform m_afterTransform;
    QString m_name;
};

class RasterizeCommand : public Command {
public:
    RasterizeCommand(Document* doc, int flatIndex,
                     QImage beforeImage, QTransform beforeTransform,
                     std::shared_ptr<TextLayerData> beforeTextData,
                     std::shared_ptr<ShapeData> beforeShapeData,
                     QImage afterImage, QTransform afterTransform,
                     QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_beforeImage(std::move(beforeImage))
        , m_beforeTransform(beforeTransform)
        , m_beforeTextData(std::move(beforeTextData))
        , m_beforeShapeData(std::move(beforeShapeData))
        , m_afterImage(std::move(afterImage))
        , m_afterTransform(afterTransform)
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_afterImage, m_afterTransform, nullptr, nullptr); }
    void undo() override { apply(m_beforeImage, m_beforeTransform, m_beforeTextData, m_beforeShapeData); }
    QString name() const override { return m_name; }

private:
    void apply(const QImage& img, const QTransform& t,
               std::shared_ptr<TextLayerData> textData,
               std::shared_ptr<ShapeData> shapeData) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
            return;
        node->layer->cpuImage = img;
        node->transform = t;
        node->layer->textData = textData;
        node->layer->shapeData = shapeData;
        node->layer->textureOutdated = true;
        node->invalidateEffects();
    }

    Document* m_doc;
    int m_flatIndex;
    QImage m_beforeImage;
    QTransform m_beforeTransform;
    std::shared_ptr<TextLayerData> m_beforeTextData;
    std::shared_ptr<ShapeData> m_beforeShapeData;
    QImage m_afterImage;
    QTransform m_afterTransform;
    QString m_name;
};

// Non-destructive Distort/Perspective edit: captures the layer's rendered
// pixels, transform, and DistortData before/after an editing session. The
// layer remains a re-editable distort layer (DistortData preserved).
class DistortCommand : public Command {
public:
    DistortCommand(Document* doc, int flatIndex,
                   QImage beforeImage, QTransform beforeTransform,
                   std::shared_ptr<DistortData> beforeData,
                   QImage afterImage, QTransform afterTransform,
                   std::shared_ptr<DistortData> afterData,
                   QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_beforeImage(std::move(beforeImage))
        , m_beforeTransform(beforeTransform)
        , m_beforeData(std::move(beforeData))
        , m_afterImage(std::move(afterImage))
        , m_afterTransform(afterTransform)
        , m_afterData(std::move(afterData))
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_afterImage, m_afterTransform, m_afterData); }
    void undo() override { apply(m_beforeImage, m_beforeTransform, m_beforeData); }
    QString name() const override { return m_name; }

private:
    void apply(const QImage& img, const QTransform& t,
               const std::shared_ptr<DistortData>& data) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
            return;
        Layer* layer = node->layer.get();
        if (layer->rasterStorage.isEnabled())
            layer->rasterStorage.clear();
        layer->cpuImage = img;
        layer->tiledSystem = false;
        layer->tileManager.clear();
        layer->dirtyRegion.clear();
        layer->textureOutdated = true;
        layer->pendingGpuUpload = true;
        node->transform = t;
        // Deep-copy so later edits don't mutate the stored undo state.
        layer->distortData = data
            ? std::make_shared<DistortData>(*data) : nullptr;
        node->invalidateEffects();
        ++m_doc->compositionGeneration;
    }

    Document* m_doc;
    int m_flatIndex;
    QImage m_beforeImage;
    QTransform m_beforeTransform;
    std::shared_ptr<DistortData> m_beforeData;
    QImage m_afterImage;
    QTransform m_afterTransform;
    std::shared_ptr<DistortData> m_afterData;
    QString m_name;
};

// ── Structural Commands ───────────────────────────────────────

class MergeLayersCommand : public Command {
public:
    MergeLayersCommand(Document* doc, int srcFlat, int dstFlat, int postMergeDstFlat,
                       QImage srcBefore, QTransform srcBeforeXf,
                       QImage dstBefore, QTransform dstBeforeXf,
                       LayerMaskSnapshot dstMaskBefore,
                       std::shared_ptr<ShapeData> dstShapeData,
                       std::shared_ptr<TextLayerData> dstTextData,
                       QImage mergedImage,
                       std::unique_ptr<LayerTreeNode> srcNode,
                       QString commandName)
        : m_doc(doc)
        , m_srcFlat(srcFlat)
        , m_dstFlat(dstFlat)
        , m_postMergeDstFlat(postMergeDstFlat)
        , m_srcBefore(std::move(srcBefore))
        , m_srcBeforeXf(srcBeforeXf)
        , m_dstBefore(std::move(dstBefore))
        , m_dstBeforeXf(dstBeforeXf)
        , m_dstMaskBefore(std::move(dstMaskBefore))
        , m_dstShapeData(std::move(dstShapeData))
        , m_dstTextData(std::move(dstTextData))
        , m_mergedImage(std::move(mergedImage))
        , m_srcNode(std::move(srcNode))
        , m_name(std::move(commandName))
    {}

    void execute() override {
        // Redo: src is back in tree (after undo re-inserted it), dst is at original m_dstFlat.
        auto* dstNode = m_doc ? m_doc->nodeAt(m_dstFlat) : nullptr;
        if (!dstNode || !dstNode->layer) {
            return;
        }
        auto removed = m_doc->takeNodeAt(m_srcFlat);
        if (removed && !m_srcNode) m_srcNode = std::move(removed);
        dstNode->layer->rasterStorage.clear();
        dstNode->layer->cpuImage = m_mergedImage;
        dstNode->layer->shapeData.reset();
        dstNode->layer->textData.reset();
        dstNode->layer->shapeCache = Layer::ShapeRenderCache{};
        LayerMaskSnapshot::clear(dstNode->layer.get());
        dstNode->layer->textureOutdated = true;
        dstNode->thumbnailDirty = true;
        dstNode->sourceDirty    = true;
        dstNode->invalidateEffects();
        if (m_doc) m_doc->activeFlatIndex = m_postMergeDstFlat;
    }

    void undo() override {
        // Undo: src is NOT in tree, dst is at m_postMergeDstFlat.
        auto* dstNode = m_doc ? m_doc->nodeAt(m_postMergeDstFlat) : nullptr;
        if (!dstNode || !dstNode->layer) {
            return;
        }
        dstNode->layer->rasterStorage.clear();
        dstNode->layer->cpuImage = m_dstBefore;
        dstNode->layer->shapeData = m_dstShapeData;
        dstNode->layer->textData  = m_dstTextData;
        dstNode->layer->shapeCache = Layer::ShapeRenderCache{};
        LayerMaskSnapshot::restore(dstNode->layer.get(), m_dstMaskBefore);
        dstNode->transform = m_dstBeforeXf;
        dstNode->layer->textureOutdated = true;
        dstNode->thumbnailDirty = true;
        dstNode->sourceDirty    = true;
        dstNode->invalidateEffects();
        if (m_srcNode) {
            auto srcCopy = m_srcNode->clone();
            srcCopy->thumbnailDirty = true;
            srcCopy->sourceDirty    = true;
            m_doc->insertNodeAt(m_srcFlat, std::move(srcCopy));
        }
        if (m_doc) m_doc->activeFlatIndex = m_srcFlat;
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    int m_srcFlat;
    int m_dstFlat;
    int m_postMergeDstFlat;
    QImage m_srcBefore;
    QTransform m_srcBeforeXf;
    QImage m_dstBefore;
    QTransform m_dstBeforeXf;
    LayerMaskSnapshot m_dstMaskBefore;
    std::shared_ptr<ShapeData> m_dstShapeData;
    std::shared_ptr<TextLayerData> m_dstTextData;
    QImage m_mergedImage;
    std::unique_ptr<LayerTreeNode> m_srcNode;
    QString m_name;
};

class ReorderCommand : public Command {
public:
    ReorderCommand(Document* doc, int fromFlat, int toFlat, QString commandName)
        : m_doc(doc)
        , m_fromFlat(fromFlat)
        , m_toFlat(toFlat)
        , m_name(std::move(commandName))
    {}

    void execute() override {
        std::swap(m_fromFlat, m_toFlat);
        doReorder();
        m_doc->activeFlatIndex = m_toFlat;
    }

    void undo() override {
        std::swap(m_fromFlat, m_toFlat);
        doReorder();
        m_doc->activeFlatIndex = m_toFlat;
    }

    QString name() const override { return m_name; }

private:
    void doReorder() {
        if (!m_doc) return;
        auto owned = m_doc->takeNodeAt(m_fromFlat);
        if (!owned) return;
        m_doc->insertNodeAt(m_toFlat, std::move(owned));
    }

    Document* m_doc;
    int m_fromFlat;
    int m_toFlat;
    QString m_name;
};

// ── Merge / Flatten Commands ──────────────────────────────────

class MergeVisibleCommand : public Command {
public:
    struct LayerSnap {
        int flatIndex;
        QImage beforeImg;
        QTransform beforeXf;
        std::unique_ptr<LayerTreeNode> node;
    };

    MergeVisibleCommand(Document* doc, int resultFlatIndex, int originalActiveFlatIndex,
                        QImage activeBefore, QTransform activeBeforeXf,
                        LayerMaskSnapshot activeMaskBefore,
                        QImage mergedImage,
                        std::vector<LayerSnap> removed,
                        QString commandName)
        : m_doc(doc)
        , m_resultFlatIndex(resultFlatIndex)
        , m_originalActiveFlatIndex(originalActiveFlatIndex)
        , m_activeBefore(std::move(activeBefore))
        , m_activeBeforeXf(activeBeforeXf)
        , m_activeMaskBefore(std::move(activeMaskBefore))
        , m_mergedImage(std::move(mergedImage))
        , m_removed(std::move(removed))
        , m_name(std::move(commandName))
    {}

    void execute() override {
        // Redo: remove non-active visible layers (highest fi first, same order as initial merge).
        for (int i = 0; i < static_cast<int>(m_removed.size()); ++i) {
            auto& r = m_removed[i];
            r.node = m_doc->takeNodeAt(r.flatIndex);
        }
        // Set active layer to composite result.
        auto* node = m_doc->nodeAt(m_resultFlatIndex);
        if (node && node->layer) {
            node->layer->rasterStorage.clear();
            node->layer->cpuImage = m_mergedImage;
            node->transform = QTransform();
            LayerMaskSnapshot::clear(node->layer.get());
            node->layer->textureOutdated = true;
            node->thumbnailDirty = true;
            node->sourceDirty    = true;
            node->invalidateEffects();
        }
        if (m_doc) m_doc->activeFlatIndex = m_resultFlatIndex;
    }

    void undo() override {
        // Restore active layer's before state.
        auto* node = m_doc->nodeAt(m_resultFlatIndex);
        if (node && node->layer) {
            node->layer->rasterStorage.clear();
            node->layer->cpuImage = m_activeBefore;
            node->transform = m_activeBeforeXf;
            LayerMaskSnapshot::restore(node->layer.get(), m_activeMaskBefore);
            node->layer->textureOutdated = true;
            node->thumbnailDirty = true;
            node->sourceDirty    = true;
            node->invalidateEffects();
        }
        // Re-insert removed layers in REVERSE order (lowest fi first) so that
        // each insertion does not shift the subsequent insertion positions.
        for (int i = static_cast<int>(m_removed.size()) - 1; i >= 0; --i) {
            auto& r = m_removed[i];
            if (r.node) {
                auto copy = r.node->clone();
                if (copy) {
                    copy->thumbnailDirty = true;
                    copy->sourceDirty    = true;
                }
                m_doc->insertNodeAt(r.flatIndex, std::move(copy));
            }
        }
        if (m_doc) m_doc->activeFlatIndex = m_originalActiveFlatIndex;
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    int m_resultFlatIndex;
    int m_originalActiveFlatIndex;
    QImage m_activeBefore;
    QTransform m_activeBeforeXf;
    LayerMaskSnapshot m_activeMaskBefore;
    QImage m_mergedImage;
    std::vector<LayerSnap> m_removed;
    QString m_name;
};

class FlattenImageCommand : public Command {
public:
    FlattenImageCommand(Document* doc,
                        std::vector<std::unique_ptr<LayerTreeNode>> beforeRoots,
                        int beforeActiveFlatIndex,
                        std::vector<std::unique_ptr<LayerTreeNode>> afterRoots,
                        int afterActiveFlatIndex,
                        QString commandName)
        : m_doc(doc)
        , m_beforeRoots(std::move(beforeRoots))
        , m_beforeActiveFlatIndex(beforeActiveFlatIndex)
        , m_afterRoots(std::move(afterRoots))
        , m_afterActiveFlatIndex(afterActiveFlatIndex)
        , m_name(std::move(commandName))
    {}

    void execute() override {
        apply(m_afterRoots, m_afterActiveFlatIndex);
    }
    void undo() override {
        apply(m_beforeRoots, m_beforeActiveFlatIndex);
    }
    QString name() const override { return m_name; }

private:
    static std::vector<std::unique_ptr<LayerTreeNode>> cloneRoots(
        const std::vector<std::unique_ptr<LayerTreeNode>>& roots)
    {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& root : roots) {
            if (root)
                clones.push_back(root->clone());
        }
        return clones;
    }

    static void markRootsTextureOutdated(
        std::vector<std::unique_ptr<LayerTreeNode>>& roots)
    {
        std::vector<LayerTreeNode*> flat;
        LayerTreeNode::flatten(roots, flat);
        for (auto* node : flat) {
            if (node && node->layer) {
                node->layer->textureOutdated = true;
                node->invalidateEffects();
            }
        }
    }

    void apply(const std::vector<std::unique_ptr<LayerTreeNode>>& snapshotRoots,
               int activeFlatIndex)
    {
        if (!m_doc) return;
        m_doc->roots = cloneRoots(snapshotRoots);
        markRootsTextureOutdated(m_doc->roots);
        const int count = m_doc->flatCount();
        if (count <= 0) {
            m_doc->activeFlatIndex = -1;
        } else {
            m_doc->activeFlatIndex = std::clamp(activeFlatIndex, 0, count - 1);
        }
        ++m_doc->compositionGeneration;
    }

    Document* m_doc = nullptr;
    std::vector<std::unique_ptr<LayerTreeNode>> m_beforeRoots;
    int m_beforeActiveFlatIndex = -1;
    std::vector<std::unique_ptr<LayerTreeNode>> m_afterRoots;
    int m_afterActiveFlatIndex = -1;
    QString m_name;
};

// Undo/redo record for PURELY STRUCTURAL tree edits (reorder, group/ungroup,
// move-to-group, clip/unclip adjustment). These change the arrangement of nodes,
// never a pixel layer's pixels, so both the captured snapshots and the apply()
// installs use shallowClone() — a copy-on-write clone that shares pixel buffers
// (no memcpy) and only detaches if something later writes them. This is what
// keeps a reorder on a huge/merged document from freezing the UI.
class LayerTreeStateCommand : public Command {
public:
    LayerTreeStateCommand(Document* doc,
                          std::vector<std::unique_ptr<LayerTreeNode>> beforeRoots,
                          int beforeActiveFlatIndex,
                          std::set<int> beforeSelectedFlatIndices,
                          std::vector<std::unique_ptr<LayerTreeNode>> afterRoots,
                          int afterActiveFlatIndex,
                          std::set<int> afterSelectedFlatIndices,
                          QString commandName)
        : m_doc(doc)
        , m_beforeRoots(std::move(beforeRoots))
        , m_beforeActiveFlatIndex(beforeActiveFlatIndex)
        , m_beforeSelectedFlatIndices(std::move(beforeSelectedFlatIndices))
        , m_afterRoots(std::move(afterRoots))
        , m_afterActiveFlatIndex(afterActiveFlatIndex)
        , m_afterSelectedFlatIndices(std::move(afterSelectedFlatIndices))
        , m_name(std::move(commandName))
    {}

    void execute() override {
        apply(m_afterRoots, m_afterActiveFlatIndex, m_afterSelectedFlatIndices);
    }

    void undo() override {
        apply(m_beforeRoots, m_beforeActiveFlatIndex, m_beforeSelectedFlatIndices);
    }

    QString name() const override { return m_name; }

private:
    static std::vector<std::unique_ptr<LayerTreeNode>> cloneRoots(
        const std::vector<std::unique_ptr<LayerTreeNode>>& roots)
    {
        // COW clone — structural edits never touch pixels, so sharing the buffers
        // (and letting any later write detach them) is both cheap and correct.
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& root : roots) {
            if (root)
                clones.push_back(root->shallowClone());
        }
        return clones;
    }

    static void markRootsTextureOutdated(
        std::vector<std::unique_ptr<LayerTreeNode>>& roots)
    {
        std::vector<LayerTreeNode*> flat;
        LayerTreeNode::flatten(roots, flat);
        for (auto* node : flat) {
            if (node && node->layer) {
                node->layer->textureOutdated = true;
                node->invalidateEffects();
            }
        }
    }

    static std::set<int> clampSelected(const std::set<int>& selected, int flatCount)
    {
        std::set<int> out;
        for (int idx : selected) {
            if (idx >= 0 && idx < flatCount)
                out.insert(idx);
        }
        return out;
    }

    void apply(const std::vector<std::unique_ptr<LayerTreeNode>>& roots,
               int activeFlatIndex,
               const std::set<int>& selectedFlatIndices)
    {
        if (!m_doc) return;
        m_doc->roots = cloneRoots(roots);
        markRootsTextureOutdated(m_doc->roots);

        const int count = m_doc->flatCount();
        if (count <= 0) {
            m_doc->activeFlatIndex = -1;
            m_doc->selectedFlatIndices.clear();
        } else {
            m_doc->activeFlatIndex = std::clamp(activeFlatIndex, 0, count - 1);
            m_doc->selectedFlatIndices = clampSelected(selectedFlatIndices, count);
            if (m_doc->selectedFlatIndices.empty())
                m_doc->selectedFlatIndices.insert(m_doc->activeFlatIndex);
            else if (m_doc->selectedFlatIndices.count(m_doc->activeFlatIndex) == 0)
                m_doc->activeFlatIndex = *m_doc->selectedFlatIndices.rbegin();
        }
        ++m_doc->compositionGeneration;
    }

    Document* m_doc = nullptr;
    std::vector<std::unique_ptr<LayerTreeNode>> m_beforeRoots;
    int m_beforeActiveFlatIndex = -1;
    std::set<int> m_beforeSelectedFlatIndices;
    std::vector<std::unique_ptr<LayerTreeNode>> m_afterRoots;
    int m_afterActiveFlatIndex = -1;
    std::set<int> m_afterSelectedFlatIndices;
    QString m_name;
};

class MoveToGroupCommand : public Command {
public:
    MoveToGroupCommand(Document* doc, int nodeFlatIndex, int groupFlatIndex,
                       QString commandName)
        : m_doc(doc)
        , m_nodeFlat(nodeFlatIndex)
        , m_groupFlat(groupFlatIndex)
        , m_name(std::move(commandName))
    {}

    void execute() override {
        std::swap(m_nodeFlat, m_groupFlat);
        doMove();
    }

    void undo() override {
        std::swap(m_nodeFlat, m_groupFlat);
        doMove();
    }

    QString name() const override { return m_name; }

private:
    void doMove() {
        if (!m_doc) return;
        auto node = m_doc->takeNodeAt(m_nodeFlat);
        if (!node) return;
        auto* groupNode = m_doc->nodeAt(m_groupFlat);
        if (!groupNode || groupNode->type != LayerTreeNode::Type::Group) {
            m_doc->insertNodeAt(m_nodeFlat, std::move(node));
            return;
        }
        node->parent = groupNode;
        groupNode->children.push_back(std::move(node));
    }

    Document* m_doc;
    int m_nodeFlat;
    int m_groupFlat;
    QString m_name;
};

class NodePropertyCommand : public Command {
public:
    NodePropertyCommand(Document* doc, int flatIndex,
                        float beforeOp, float afterOp,
                        bool beforeVis, bool afterVis,
                        BlendMode beforeBlend, BlendMode afterBlend,
                        QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_beforeOp(beforeOp)
        , m_afterOp(afterOp)
        , m_beforeVis(beforeVis)
        , m_afterVis(afterVis)
        , m_beforeBlend(beforeBlend)
        , m_afterBlend(afterBlend)
        , m_name(std::move(commandName))
    {}

    void execute() override {
        apply(m_afterOp, m_afterVis, m_afterBlend);
    }

    void undo() override {
        apply(m_beforeOp, m_beforeVis, m_beforeBlend);
    }

    QString name() const override { return m_name; }

private:
    void apply(float op, bool vis, BlendMode bm) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (node) {
            node->opacity = op;
            node->visible = vis;
            node->blendMode = bm;
            // A nested adjustment (Single Layer Mode) bakes opacity/visibility
            // into its parent layer's effected image — invalidate on undo/redo.
            if (node->type == LayerTreeNode::Type::Adjustment)
                node->invalidateEffects();
            if (m_doc) ++m_doc->compositionGeneration;
        }
    }

    Document* m_doc;
    int m_flatIndex;
    float m_beforeOp;
    float m_afterOp;
    bool m_beforeVis;
    bool m_afterVis;
    BlendMode m_beforeBlend;
    BlendMode m_afterBlend;
    QString m_name;
};

// Undoable change of an adjustment node's payload (type + params). Used by the
// Curves editor: every committed gesture — point add/move/remove, Input/Output
// edit, preset change, channel reset, etc. — stores the before/after
// AdjustmentData so it is one precise history step. The live edit applies the
// "after" state first; this command only records it for undo/redo.
class AdjustmentParamsCommand : public Command {
public:
    AdjustmentParamsCommand(Document* doc, int flatIndex,
                            AdjustmentData before, AdjustmentData after,
                            QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_after); }
    void undo() override { apply(m_before); }
    QString name() const override { return m_name; }

private:
    void apply(const AdjustmentData& d) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (node && node->adjustment) {
            *node->adjustment = d;
            // A nested adjustment (Single Layer Mode) is baked into its parent
            // layer's effected image — invalidate so undo/redo re-renders it.
            if (node->type == LayerTreeNode::Type::Adjustment)
                node->invalidateEffects();
            if (m_doc) ++m_doc->compositionGeneration;
        }
    }

    Document* m_doc;
    int m_flatIndex;
    AdjustmentData m_before;
    AdjustmentData m_after;
    QString m_name;
};

// Undoable change of editing locks across one or more layers. Stores the exact
// before/after lockFlags per node so a single toggle (possibly over a
// multi-selection) is one history step that restores prior state precisely.
class LockFlagsCommand : public Command {
public:
    struct Entry { int flatIndex; int before; int after; };

    LockFlagsCommand(Document* doc, std::vector<Entry> entries, QString commandName)
        : m_doc(doc), m_entries(std::move(entries)), m_name(std::move(commandName)) {}

    void execute() override { for (const auto& e : m_entries) apply(e.flatIndex, e.after); }
    void undo() override { for (const auto& e : m_entries) apply(e.flatIndex, e.before); }
    QString name() const override { return m_name; }

private:
    void apply(int flatIndex, int flags) {
        if (auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr)
            node->lockFlags = flags;
    }

    Document* m_doc;
    std::vector<Entry> m_entries;
    QString m_name;
};

class LayerEffectsCommand : public Command {
public:
    LayerEffectsCommand(Document* doc, int flatIndex,
                        std::vector<LayerEffect> beforeEffects,
                        std::vector<LayerEffect> afterEffects,
                        QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_beforeEffects(std::move(beforeEffects))
        , m_afterEffects(std::move(afterEffects))
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_afterEffects); }
    void undo() override { apply(m_beforeEffects); }
    QString name() const override { return m_name; }

private:
    void apply(const std::vector<LayerEffect>& effects) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (!node) return;
        node->effects = effects;
        node->invalidateEffects();
        if (node->layer) {
            node->layer->textureOutdated = true;
            node->thumbnailDirty = true;
        }
        if (m_doc) ++m_doc->compositionGeneration;
    }

    Document* m_doc = nullptr;
    int m_flatIndex = -1;
    std::vector<LayerEffect> m_beforeEffects;
    std::vector<LayerEffect> m_afterEffects;
    QString m_name;
};

struct ResizeDocumentState {
    QSize size;
    double resolutionDpi = 300.0;
    QString colorMode = QStringLiteral("RGB Color");
    int bitDepth = 8;
    std::vector<std::unique_ptr<LayerTreeNode>> roots;
    int activeFlatIndex = -1;
    std::set<int> selectedFlatIndices;
    QImage selectionMask;
    bool selectionActive = false;
    std::vector<Guide> guides;
};

class ResizeDocumentCommand : public Command {
public:
    ResizeDocumentCommand(Document* doc,
                          ResizeDocumentState beforeState,
                          ResizeDocumentState afterState,
                          QString commandName)
        : m_doc(doc)
        , m_before(std::move(beforeState))
        , m_after(std::move(afterState))
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_after); }
    void undo() override { apply(m_before); }
    QString name() const override { return m_name; }

private:
    static std::vector<std::unique_ptr<LayerTreeNode>> cloneRoots(
        const std::vector<std::unique_ptr<LayerTreeNode>>& roots)
    {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& root : roots) {
            if (root)
                clones.push_back(root->clone());
        }
        return clones;
    }

    static void markRootsTextureOutdated(
        std::vector<std::unique_ptr<LayerTreeNode>>& roots)
    {
        std::vector<LayerTreeNode*> flat;
        LayerTreeNode::flatten(roots, flat);
        for (auto* node : flat) {
            if (!node || !node->layer)
                continue;
            node->layer->textureOutdated = true;
            node->layer->pendingGpuUpload = true;
            node->layer->shapeCache.dirty = true;
            node->invalidateEffects();
            node->thumbnailDirty = true;
        }
    }

    static std::set<int> clampSelected(const std::set<int>& selected, int flatCount)
    {
        std::set<int> out;
        for (int idx : selected) {
            if (idx >= 0 && idx < flatCount)
                out.insert(idx);
        }
        return out;
    }

    void apply(const ResizeDocumentState& state)
    {
        if (!m_doc)
            return;

        m_doc->size = state.size;
        m_doc->resolutionDpi = state.resolutionDpi;
        m_doc->colorMode = state.colorMode;
        m_doc->bitDepth = state.bitDepth;
        m_doc->roots = cloneRoots(state.roots);
        markRootsTextureOutdated(m_doc->roots);

        const int count = m_doc->flatCount();
        if (count <= 0 || (state.activeFlatIndex < 0 && state.selectedFlatIndices.empty())) {
            m_doc->activeFlatIndex = -1;
            m_doc->selectedFlatIndices.clear();
        } else {
            m_doc->activeFlatIndex = std::clamp(state.activeFlatIndex, 0, count - 1);
            m_doc->selectedFlatIndices = clampSelected(state.selectedFlatIndices, count);
            if (m_doc->selectedFlatIndices.empty())
                m_doc->selectedFlatIndices.insert(m_doc->activeFlatIndex);
            else if (m_doc->selectedFlatIndices.count(m_doc->activeFlatIndex) == 0)
                m_doc->activeFlatIndex = *m_doc->selectedFlatIndices.rbegin();
        }

        m_doc->selection.resize(state.size.width(), state.size.height());
        m_doc->selection.image() = state.selectionMask.copy();
        m_doc->selection.setActive(state.selectionActive);
        m_doc->guideManager.setGuides(state.guides);
        ++m_doc->compositionGeneration;
    }

    Document* m_doc = nullptr;
    ResizeDocumentState m_before;
    ResizeDocumentState m_after;
    QString m_name;
};
