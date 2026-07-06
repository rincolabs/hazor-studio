#pragma once

#include <QSize>
#include <QPointF>
#include <QRectF>
#include <cmath>

// Maps coordinates between the three spaces SAM 1 inference uses, following the
// official Segment Anything "ResizeLongestSide" convention:
//
//   document space  ──(snapshot scale)──>  snapshot space
//   snapshot space  ──(resize scale)────>  model/decoder space (point_coords)
//
// The decoder is fed `orig_im_size = snapshotSize`, so its output mask comes back
// at snapshot resolution; mapping the mask to the document is just the inverse of
// the snapshot scale (handled by AiImageSnapshot). This class owns the
// snapshot↔model part, which is the SAM-specific piece.
class SamCoordinateMapper {
public:
    SamCoordinateMapper() = default;
    SamCoordinateMapper(const QSize& snapshotSize, int inputSize)
        : m_snapshot(snapshotSize), m_inputSize(inputSize)
    {
        const int longest = std::max(snapshotSize.width(), snapshotSize.height());
        m_scale = longest > 0 ? double(inputSize) / double(longest) : 1.0;
    }

    QSize snapshotSize() const { return m_snapshot; }
    int   inputSize() const { return m_inputSize; }
    double resizeScale() const { return m_scale; }

    // ResizeLongestSide target the encoder is actually fed.
    QSize resizedSize() const
    {
        return QSize(int(std::round(m_snapshot.width()  * m_scale)),
                     int(std::round(m_snapshot.height() * m_scale)));
    }

    // Snapshot pixel → decoder point_coords (model space).
    QPointF snapshotToModel(const QPointF& p) const
    {
        return QPointF(p.x() * m_scale, p.y() * m_scale);
    }

    // Snapshot rect → decoder box corners (model space).
    QRectF snapshotBoxToModel(const QRectF& r) const
    {
        return QRectF(snapshotToModel(r.topLeft()), snapshotToModel(r.bottomRight()));
    }

private:
    QSize  m_snapshot;
    int    m_inputSize = 1024;
    double m_scale = 1.0;
};
