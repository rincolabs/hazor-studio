#pragma once

#include "ai/AiSelectionTypes.hpp"

#include <QString>
#include <QMetaType>
#include <cstdint>

// Result of one async AI job, delivered to the UI thread by AiJobRunner. It
// echoes the request's document/layer/revision identity so the controller can
// discard results that are no longer valid (document/layer changed, pixels
// edited, tool switched) before touching the editor.
struct AiJobResult {
    quint64 jobId = 0;
    AiToolMode mode = AiToolMode::SelectObject;

    AiMaskResult mask;            // document-space mask (when ok)
    AiSelectionOperation operation = AiSelectionOperation::Replace;
    QString error;
    QString providerUsed;
    bool cancelled = false;
    bool fromCache = false;

    // Identity echo for stale-result rejection.
    quint64 documentId = 0;
    int     sourceLayerIndex = -1;
    quint64 sourceRevision = 0;
    int     targetLayerIndex = -1; // layer the action targets (mask creation)

    bool ok() const { return !cancelled && mask.isValid(); }
};

Q_DECLARE_METATYPE(AiJobResult)
