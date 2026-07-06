#pragma once

#include "ai/models/AiModelDescriptor.hpp"

#include <QString>

// Reads, validates and writes the model.json schema (schema_version: 1) that is
// the source of truth for a logical model. The app never infers a model's nature
// from a filename — only from this metadata.
namespace AiModelJson {

constexpr int kSchemaVersion = 1;
constexpr char kMetadataFile[] = "model.json";

// True when a model.json exists directly in `dir`.
bool hasMetadata(const QString& dir);

// Reads & validates the schema-v1 model.json in `dir`. Always returns a
// descriptor (origin pre-set); on any problem status/statusReason explain why and
// the fields are filled as far as possible so diagnostics can still display it.
// Never throws.
AiModelDescriptor read(const QString& dir, AiModelOrigin origin);

// Reads a legacy install (the old "installed" metadata schema) or a SAM sidecar
// config (config.yaml/json) from `dir`, mapping it onto a descriptor for
// back-compat. Returns a descriptor with an empty id when the folder is neither.
AiModelDescriptor readLegacy(const QString& dir, AiModelOrigin origin);

// Dispatches: schema-v1 model.json → read(); legacy model.json / SAM config →
// readLegacy(). Returns an empty-id descriptor when `dir` holds no model.
AiModelDescriptor readAny(const QString& dir, AiModelOrigin origin);

// Writes a schema-v1 model.json describing `model` into `dir`.
bool write(const QString& dir, const AiModelDescriptor& model, QString* error = nullptr);

} // namespace AiModelJson
