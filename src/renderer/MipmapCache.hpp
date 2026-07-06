#pragma once

#include "core/RenderScheduler.hpp"

#include <QImage>
#include <vector>

class Layer;

class MipmapCache {
public:
    MipmapCache() = default;
    ~MipmapCache();

    // Generate & return the QImage for a given LOD level (lazy).
    // Returns nullptr if generation fails.
    const QImage* level(Layer* layer, core::RenderScheduler::LOD lod);

    // Upload the QImage to a GL texture. Texture is created on first
    // call for a given LOD. Returns the GL texture id (0 on failure).
    unsigned int uploadLevel(Layer* layer, core::RenderScheduler::LOD lod);

    // Upload all dirty LOD levels for a layer.
    void uploadDirtyLevels(Layer* layer);

    // Mark all levels dirty. Frees QImages to save memory.
    void invalidate(Layer* layer);

    // Deletes all GL textures for a layer.
    void cleanup(Layer* layer);

    // Clear all data (typically called when changing documents).
    void clearAll();

private:
    void generateLevel(Layer* layer, core::RenderScheduler::LOD lod);
};

