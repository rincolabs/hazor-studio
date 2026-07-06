#pragma once

#include <QSize>
#include <QPointF>
#include <QString>
#include <QTransform>
#include <memory>
#include <set>
#include <vector>
#include "LayerTreeNode.hpp"
#include "SelectionMask.hpp"
#include "AlphaChannel.hpp"
#include "RenderScheduler.hpp"
#include "PerformanceConfig.hpp"
#include "GuideManager.hpp"
#include "color/ColorProfile.hpp"
#include "color/SoftProofSettings.hpp"

namespace cv { class Mat; }

class Document {
public:
    QString name = "My new document";
    QSize size;
    double resolutionDpi = 300.0;
    QString colorMode = QStringLiteral("RGB Color");
    int bitDepth = 8;

    std::vector<std::unique_ptr<LayerTreeNode>> roots;
    int activeFlatIndex = 0;
    std::set<int> selectedFlatIndices;

    float zoom = 1.0f;
    QPointF panOffset;

    SelectionMask selection;
    std::vector<AlphaChannel> channels;
    GuideManager guideManager;

    // ── Performance ──────────────────────────────────────────
    PerformanceConfig perfConfig;
    core::RenderScheduler scheduler;
    int tileSize() const { return perfConfig.tileSize; }

    // ── Cache generation (Phase 5) ───────────────────────────
    uint64_t compositionGeneration = 0;
    uint64_t displayGeneration = 0;
    uint64_t colorStateGeneration = 0;

    // ── Mask overlay (rubylith) — editor-only view state ──────
    // On-demand red overlay for inspecting the active layer's mask. Off by
    // default; a pure visualization toggle that never touches mask data and is
    // intentionally NOT persisted by ProjectFileService.
    bool maskOverlayVisible = false;
    float maskOverlayOpacity = 0.5f;  // 0..1, default 50%

    void saveSelectionToChannel(const QString& name);
    void loadChannelToSelection(int index, SelectMode mode);
    void deleteChannel(int index);
    cv::Mat selectionMaskForLayer(int layerWidth, int layerHeight,
                                  const QTransform& layerTransform) const;

    std::vector<LayerTreeNode*> flatten() const
    {
        std::vector<LayerTreeNode*> out;
        LayerTreeNode::flatten(roots, out, 0);
        return out;
    }

    LayerTreeNode* nodeAt(int flatIndex) const
    {
        return LayerTreeNode::findByFlatIndex(roots, flatIndex);
    }

    int flatCount() const
    {
        return static_cast<int>(flatten().size());
    }

    LayerTreeNode* activeNode() const
    {
        return nodeAt(activeFlatIndex);
    }

    Layer* activeLayer() const
    {
        auto* node = activeNode();
        // Adjustment nodes carry a Layer too (mask storage + coordinate
        // space); pixel tools stay blocked by their own node-type gates.
        if (node && (node->type == LayerTreeNode::Type::Layer
                     || node->type == LayerTreeNode::Type::Adjustment))
            return node->layer.get();
        return nullptr;
    }

    int layerCount() const
    {
        int count = 0;
        auto flat = flatten();
        for (auto* node : flat) {
            if (node->type == LayerTreeNode::Type::Layer)
                ++count;
        }
        return count;
    }

    Layer* layerAtFlat(int flatIndex) const
    {
        auto* node = nodeAt(flatIndex);
        if (node && (node->type == LayerTreeNode::Type::Layer
                     || node->type == LayerTreeNode::Type::Adjustment))
            return node->layer.get();
        return nullptr;
    }

    std::unique_ptr<LayerTreeNode> takeNodeAt(int flatIndex)
    {
        auto* node = nodeAt(flatIndex);
        if (!node) return nullptr;

        auto detach = [](std::vector<std::unique_ptr<LayerTreeNode>>& siblings,
                         LayerTreeNode* target) -> std::unique_ptr<LayerTreeNode> {
            for (auto it = siblings.begin(); it != siblings.end(); ++it) {
                if (it->get() == target) {
                    auto ptr = std::move(*it);
                    siblings.erase(it);
                    return ptr;
                }
            }
            return nullptr;
        };

        // Removing a Single-Layer-Mode adjustment changes only its parent layer's
        // adjustment BAKE — the parent's own pixels/mask/layer-style (its styled
        // base) are untouched. Invalidate just the bake, NOT the styled base:
        // invalidateEffects() would mark styledBaseTextureOutdated and force the
        // expensive layer-style blur to re-run synchronously on the UI thread in
        // syncLayersToGpu (froze the app when clipping/removing an adjustment on a
        // large styled layer). Invalidate before detaching (detach clears parent).
        auto invalidateLayerParent = [](LayerTreeNode* taken) {
            if (taken && taken->parent
                && taken->parent->type == LayerTreeNode::Type::Layer) {
                taken->parent->thumbnailDirty = true;
                taken->parent->invalidateEffectedBake();
            }
        };

        if (node->parent) {
            invalidateLayerParent(node);
            if (auto ptr = detach(node->parent->children, node)) {
                ptr->parent = nullptr;
                return ptr;
            }
        }

        if (auto ptr = detach(roots, node)) {
            ptr->parent = nullptr;
            return ptr;
        }

        // Fallback for stale parent pointers after complex reorders.
        std::vector<std::vector<std::unique_ptr<LayerTreeNode>>*> stacks;
        for (auto& root : roots) {
            if (root && !root->children.empty())
                stacks.push_back(&root->children);
        }

        while (!stacks.empty()) {
            auto* siblings = stacks.back();
            stacks.pop_back();
            if (!siblings) continue;
            for (auto it = siblings->begin(); it != siblings->end(); ++it) {
                if (it->get() == node) {
                    auto ptr = std::move(*it);
                    siblings->erase(it);
                    if (ptr) ptr->parent = nullptr;
                    return ptr;
                }
                if ((*it) && !(*it)->children.empty())
                    stacks.push_back(&(*it)->children);
            }
        }

        return nullptr;
    }

    int insertNodeAt(int flatIndex, std::unique_ptr<LayerTreeNode> newNode)
    {
        if (!newNode) return -1;

        if (flatIndex >= flatCount() || flatIndex < 0) {
            newNode->parent = nullptr;
            roots.push_back(std::move(newNode));
            return flatCount() - 1;
        }

        auto* target = nodeAt(flatIndex);
        if (!target) {
            newNode->parent = nullptr;
            roots.push_back(std::move(newNode));
            return flatCount() - 1;
        }

        // Only adjustment layers may nest under a Layer node (Single Layer
        // Mode). Any other node aimed inside a layer's child list is
        // redirected to insert above that layer instead — this keeps every
        // insertion path (reorder, undo/redo, paste, new layer) free of
        // invalid nesting.
        if (newNode->type != LayerTreeNode::Type::Adjustment) {
            while (target->parent
                   && target->parent->type == LayerTreeNode::Type::Layer)
                target = target->parent;
        }

        auto& siblings = target->parent ? target->parent->children : roots;
        for (auto it = siblings.begin(); it != siblings.end(); ++it) {
            if (it->get() == target) {
                LayerTreeNode* inserted = newNode.get();
                newNode->parent = target->parent;
                // Joining a Layer node's child list (Single-Layer-Mode
                // adjustments) changes only that layer's adjustment BAKE, not its
                // styled base (its own pixels/mask/layer-style). Invalidate the
                // bake only — invalidateEffects() would force the layer-style blur
                // to re-run on the UI thread in syncLayersToGpu (froze the app when
                // clipping an adjustment onto a large styled layer).
                if (target->parent
                    && target->parent->type == LayerTreeNode::Type::Layer) {
                    target->parent->thumbnailDirty = true;
                    target->parent->invalidateEffectedBake();
                }
                siblings.insert(it, std::move(newNode));
                // The actual flat position can differ from `flatIndex` when
                // the insert was redirected out of a layer's child list.
                auto flat = flatten();
                for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
                    if (flat[i] == inserted)
                        return i;
                }
                return flatIndex;
            }
        }

        // Defensive fallback: if target's sibling list changed unexpectedly.
        newNode->parent = nullptr;
        roots.push_back(std::move(newNode));
        return flatCount() - 1;
    }

    // ── Multi-selection ──────────────────────────────────────

    bool isSelected(int idx) const
    {
        return selectedFlatIndices.count(idx) > 0;
    }

    void selectNode(int idx, bool addToSelection = false)
    {
        if (addToSelection) {
            if (selectedFlatIndices.count(idx)) {
                selectedFlatIndices.erase(idx);
                // Keep activeFlatIndex on some selected layer if available
                if (selectedFlatIndices.empty())
                    activeFlatIndex = -1;
                else if (selectedFlatIndices.count(activeFlatIndex) == 0)
                    activeFlatIndex = *selectedFlatIndices.rbegin();
            } else {
                selectedFlatIndices.insert(idx);
                activeFlatIndex = idx;
            }
        } else {
            selectedFlatIndices.clear();
            activeFlatIndex = idx;
            if (idx >= 0)
                selectedFlatIndices.insert(idx);
        }
    }

    void deselectAll()
    {
        selectedFlatIndices.clear();
        activeFlatIndex = -1;
    }

    void selectRange(int from, int to)
    {
        if (from > to) std::swap(from, to);
        selectedFlatIndices.clear();
        for (int i = from; i <= to; ++i)
            selectedFlatIndices.insert(i);
        activeFlatIndex = to;
    }

    std::vector<int> selectedList() const
    {
        return {selectedFlatIndices.begin(), selectedFlatIndices.end()};
    }

    ColorProfile colorProfile() const
    {
        return m_colorProfile;
    }

    void setColorProfile(const ColorProfile& profile)
    {
        m_colorProfile = profile.isValid() ? profile : ColorProfile::invalid();
        invalidateDisplayCache();
    }

    ColorProfileSource profileSource() const
    {
        return m_profileSource;
    }

    void setProfileSource(ColorProfileSource source)
    {
        m_profileSource = source;
        if (m_colorProfile.isValid())
            m_colorProfile.setSource(source);
        invalidateDisplayCache();
    }

    bool isColorManaged() const
    {
        return m_colorProfile.isValid();
    }

    void invalidateDisplayCache()
    {
        ++displayGeneration;
    }

    void markColorStateDirty()
    {
        ++colorStateGeneration;
    }

    // ── Soft proof (View > Proof Colors) — display-only preview state ─────
    // Not a destructive op: it never touches pixels or the document profile,
    // only how the composite is transformed for the monitor. Toggling it just
    // invalidates the display cache so the canvas re-derives its display image.
    const SoftProofSettings& softProofSettings() const { return m_softProof; }

    void setSoftProofSettings(const SoftProofSettings& proof)
    {
        m_softProof = proof;
        invalidateDisplayCache();
    }

    bool softProofEnabled() const
    {
        return m_softProof.enabled && m_softProof.proofProfile.isValid();
    }

    void setSoftProofEnabled(bool enabled)
    {
        if (m_softProof.enabled == enabled)
            return;
        m_softProof.enabled = enabled;
        invalidateDisplayCache();
    }

private:
    ColorProfile m_colorProfile = ColorProfile::sRgb();
    ColorProfileSource m_profileSource = ColorProfileSource::GeneratedDefault;
    SoftProofSettings m_softProof;
};
