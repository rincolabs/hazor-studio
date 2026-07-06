#pragma once

#include "ai/matting/AiMattingModel.hpp"

#include <QString>
#include <QStringList>

#include <memory>

// Resolves installed model ids into concrete matting / background-removal model
// instances by inspecting their family. Keeps the pipeline free of any knowledge
// of which class implements which model — it just asks for "a matting model" or
// "a background-removal model" for a (possibly Auto) id.
namespace AiRefineModelFactory {

// Installed model ids by capability (role "matting" / "background-removal").
QStringList installedMattingModels();
QStringList installedBackgroundRemovalModels();

// Resolve an Auto/empty request to a concrete installed id (best available), or
// validate an explicit id. Returns "" when none is installed.
QString resolveMattingModelId(const QString& requested);
QString resolveBackgroundRemovalModelId(const QString& requested);

// Build a model for an explicit id (must already be resolved/installed). Returns
// nullptr with *error set on failure. Must be called from the AI worker thread.
std::shared_ptr<AiMattingModel> createMatting(const QString& modelId, QString* error);
std::shared_ptr<AiBackgroundRemovalModel> createBackgroundRemoval(const QString& modelId, QString* error);

} // namespace AiRefineModelFactory
