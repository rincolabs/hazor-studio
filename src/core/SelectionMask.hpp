#pragma once

#include <QImage>
#include <QRectF>
#include <QPointF>
#include <cstdlib>
#include <vector>

enum class SelectMode { Replace = 0, Add = 1, Subtract = 2, Intersect = 3 };
enum class SelectType { Rectangular = 0, Elliptical = 1, Lasso = 2, MagicWand = 3, QuickSelect = 4, MagneticLasso = 5, PolygonalLasso = 6 };

class SelectionMask {
public:
    SelectionMask() = default;

    void create(int w, int h);
    void clear();
    void setRect(const QRectF& rectDoc, SelectMode mode, bool antiAlias = false);
    void setEllipse(const QRectF& rectDoc, SelectMode mode, bool antiAlias = true);
    void setPolygon(const std::vector<QPointF>& points, SelectMode mode, bool antiAlias = true);
    void setMagicWand(int seedX, int seedY, float tolerance, bool contiguous,
                      SelectMode mode, const uchar* srcBits, int srcW, int srcH,
                      const double* invAffine = nullptr);
    // Combines an externally-produced 8-bit grayscale mask (e.g. an AI Object
    // Selection result) into the selection using the shared combine rule, so the
    // result lands in the selection exactly like every other selection tool.
    // The incoming mask is expected at document resolution (255 = selected).
    void combineGrayscaleMask(const QImage& mask8, SelectMode mode);
    void invert();
    void resize(int w, int h);

    void feather(float radius);
    void grow(int pixels);
    void shrink(int pixels);
    void border(int pixels);
    void smooth(float radius);
    void translate(int dx, int dy);
    void applyTransform(float scaleX, float scaleY, float angleDeg,
                        int centerX, int centerY);

    QRectF bounds() const;
    bool isEmpty() const;
    bool isSelected(int x, int y) const;

    // Shared color-match rule for Magic Wand / Quick Select so a given
    // tolerance selects the same pixels in every tool: RGB Manhattan distance
    // within tol·3 AND alpha within tol (a fully transparent pixel never
    // matches an opaque seed even when its RGB is identical).
    static bool colorMatches(const uchar* px, int sR, int sG, int sB, int sA,
                             float tolerance)
    {
        const int tol = static_cast<int>(tolerance);
        const int rgb = std::abs(static_cast<int>(px[0]) - sR)
                      + std::abs(static_cast<int>(px[1]) - sG)
                      + std::abs(static_cast<int>(px[2]) - sB);
        return rgb <= tol * 3 && std::abs(static_cast<int>(px[3]) - sA) <= tol;
    }

    int width() const { return m_mask.width(); }
    int height() const { return m_mask.height(); }
    bool active() const { return m_active; }
    void setActive(bool a) { m_active = a; }

    // Non-const access may mutate the mask (SelectionCommand undo/redo, quick
    // select dabs, refine previews), so it conservatively drops the cached
    // isEmpty()/bounds() result.
    uchar* bits() { m_cacheValid = false; return m_mask.bits(); }
    const uchar* constBits() const { return m_mask.constBits(); }
    const QImage& image() const { return m_mask; }
    QImage& image() { m_cacheValid = false; return m_mask; }

    QImage m_mask;
    bool m_active = false;

private:
    void updateCache() const;

    // isEmpty()/bounds() are full-mask scans called several times per frame by
    // the marching-ants pipeline; cache them until the mask mutates.
    mutable bool m_cacheValid = false;
    mutable bool m_cachedEmpty = true;
    mutable QRectF m_cachedBounds;
};
