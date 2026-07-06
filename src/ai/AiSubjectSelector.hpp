#pragma once

#include "ai/sam/SamTypes.hpp"
#include "ai/AiSelectionTypes.hpp"

#include <QSize>
#include <atomic>
#include <functional>

class Sam1Segmenter;

// Picks the "main subject" of an image when the user asks to Select Subject or
// Remove Background without a specific click/box. SAM has no notion of "the
// subject" on its own, so this drives it with a controlled set of internal point
// prompts and ranks the candidate masks with a transparent heuristic: prefer a
// reasonably-sized, central blob; discard tiny specks and full-frame
// "background" masks; keep only the largest connected component.
//
// Kept isolated (not spread through the tool) so the heuristic can evolve — and
// be replaced by a dedicated background-removal model in Etapa 3 — without
// touching the selection plumbing.
class AiSubjectSelector {
public:
    // Runs several decoder prompts against an already-built embedding and returns
    // the best subject candidate (snapshot-resolution logits). `cancel`, when
    // set true, aborts early and yields an invalid candidate.
    SamMaskCandidate selectMainSubject(Sam1Segmenter& segmenter,
                                       const SamEmbedding& embedding,
                                       const QSize& snapshotSize,
                                       const AiSelectionOptions& options,
                                       const std::atomic<bool>* cancel = nullptr);
};
