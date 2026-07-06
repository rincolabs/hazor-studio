#include "ai/AiSubjectSelector.hpp"

#include "ai/sam/Sam1Segmenter.hpp"
#include "ai/sam/SamPostprocessor.hpp"

#include <QPointF>
#include <QVector>
#include <QDebug>

#include <algorithm>

namespace {

// Fraction of border pixels covered by the mask; a high value usually means the
// candidate latched onto the background rather than a subject.
double borderCoverage(const SamMaskCandidate& cand)
{
    const int w = cand.size.width();
    const int h = cand.size.height();
    if (w < 2 || h < 2)
        return 0.0;
    const float* m = cand.logits.data();
    qint64 on = 0;
    qint64 total = 0;
    auto test = [&](int x, int y) {
        ++total;
        if (m[y * w + x] > 0.0f) ++on;
    };
    for (int x = 0; x < w; ++x) { test(x, 0); test(x, h - 1); }
    for (int y = 1; y < h - 1; ++y) { test(0, y); test(w - 1, y); }
    return total > 0 ? double(on) / double(total) : 0.0;
}

} // namespace

SamMaskCandidate AiSubjectSelector::selectMainSubject(Sam1Segmenter& segmenter,
                                                      const SamEmbedding& embedding,
                                                      const QSize& snapshotSize,
                                                      const AiSelectionOptions& options,
                                                      const std::atomic<bool>* cancel)
{
    const double w = snapshotSize.width();
    const double h = snapshotSize.height();

    // A small, deterministic set of interior seed points (snapshot coords). The
    // center is tried first; the quarter points catch off-center subjects.
    const QVector<QPointF> seeds = {
        { w * 0.50, h * 0.50 },
        { w * 0.50, h * 0.40 },
        { w * 0.40, h * 0.55 },
        { w * 0.60, h * 0.55 },
        { w * 0.50, h * 0.65 },
    };

    SamMaskCandidate best;
    double bestScore = -1e9;

    for (const QPointF& seed : seeds) {
        if (cancel && cancel->load())
            return SamMaskCandidate{};

        QVector<AiPromptPoint> pts;
        pts.push_back(AiPromptPoint{ seed, true });
        QString err;
        SamMaskCandidate cand = segmenter.decode(embedding, pts, nullptr, &err);
        if (!cand.isValid())
            continue;

        const SamPostprocessor::MaskStats stats = SamPostprocessor::analyze(cand);
        if (stats.empty)
            continue;
        // Reject specks and near-full-frame masks (likely background).
        if (stats.areaFraction < options.minSubjectAreaFrac
            || stats.areaFraction > options.maxSubjectAreaFrac)
            continue;
        if (borderCoverage(cand) > 0.6)
            continue;

        // Rank: model confidence, a gentle preference for central and
        // moderately-large subjects.
        const double rank = double(cand.score)
                          - 0.35 * stats.centerDistanceFrac
                          + 0.15 * std::min(0.5, stats.areaFraction);
        if (rank > bestScore) {
            bestScore = rank;
            best = cand;
        }
    }

    if (best.isValid()) {
        SamPostprocessor::keepLargestComponent(best);
        qInfo().noquote() << "[AI][SAM] Subject selected rank=" << bestScore
                          << "score=" << best.score;
    } else {
        qWarning().noquote() << "[AI][SAM] No confident subject found";
    }
    return best;
}
