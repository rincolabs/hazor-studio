#pragma once

#include <QSize>
#include <QString>
#include <QVariantMap>
#include <QVector>

namespace canvaspresets {

// Which kind of document a preset targets. For now this only feeds the New
// Document dialog's "Document Type" selector; later it will choose a workspace.
enum class DocumentKind {
    Photo,
    Animation,
};

// A named, categorised canvas size shared by the New Document dialog (which
// shows the whole catalogue) and the Export Animation dialog (which reuses the
// Animation entries as its size presets, avoiding a duplicated list). A preset
// only *fills* a dialog — every field stays editable afterwards.
struct CanvasPreset {
    QString group;                        // category node in the preset tree
    QString name;                         // display label
    DocumentKind kind = DocumentKind::Photo;
    double width = 0.0;                   // measured in `unit`
    double height = 0.0;                  // measured in `unit`
    QString unit = QStringLiteral("px");  // "in", "cm", "mm" or "px"
    int resolution = 300;                 // px/in, used to resolve physical units
    bool builtin = true;                  // false for user-saved presets
};

// Label shown in the document-type selector ("Photo" / "Animation"), and the
// inverse lookup used when persisting a custom preset's kind.
QString documentKindLabel(DocumentKind kind);
DocumentKind documentKindFromLabel(const QString& label);

// The built-in catalogue, shared by both dialogs. Photo presets come first,
// then the animation/video presets grouped by platform.
const QVector<CanvasPreset>& builtinPresets();

// Resolve a preset to concrete pixels (physical units are scaled by resolution).
QSize presetPixelSize(const CanvasPreset& preset);

// Round-trips a preset through QSettings for user-saved custom presets.
QVariantMap toVariantMap(const CanvasPreset& preset);
CanvasPreset fromVariantMap(const QVariantMap& map);

} // namespace canvaspresets
