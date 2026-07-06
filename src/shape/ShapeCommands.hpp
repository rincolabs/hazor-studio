#pragma once

#include "controller/CommandHistory.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/ShapeTypes.hpp"
#include "engine/ShapeRenderer.hpp"

class ModifyShapeCommand : public Command {
public:
    ModifyShapeCommand(Document* doc, int flatIndex,
                       ShapeData before, ShapeData after,
                       QImage beforeImage, QImage afterImage,
                       QTransform beforeXf, QTransform afterXf,
                       QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_beforeImage(std::move(beforeImage))
        , m_afterImage(std::move(afterImage))
        , m_beforeXf(beforeXf)
        , m_afterXf(afterXf)
        , m_name(std::move(commandName))
    {}

    void execute() override { apply(m_after, m_afterImage, m_afterXf); }
    void undo() override { apply(m_before, m_beforeImage, m_beforeXf); }
    QString name() const override { return m_name; }

private:
    void apply(const ShapeData& sd, const QImage& img, const QTransform& xf) {
        auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
        if (!node || !node->layer) return;
        node->layer->shapeData = std::make_shared<ShapeData>(sd);
        node->layer->cpuImage = img;
        node->transform = xf;
        node->layer->textureOutdated = true;
        node->layer->shapeCache.dirty = true;
        node->invalidateEffects();
        node->thumbnailDirty = true;
        if (m_doc) ++m_doc->compositionGeneration;
    }

    Document* m_doc;
    int m_flatIndex;
    ShapeData m_before;
    ShapeData m_after;
    QImage m_beforeImage;
    QImage m_afterImage;
    QTransform m_beforeXf;
    QTransform m_afterXf;
    QString m_name;
};
