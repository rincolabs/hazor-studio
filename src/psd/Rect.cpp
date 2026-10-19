#include "Rect.hpp"

#include "Header.hpp"

namespace psd {

Rect Rect::fromXywh(int32_t left, int32_t top, uint32_t width, uint32_t height) {
    Rect r;
    r.top = top;
    r.left = left;
    int32_t h = height > static_cast<uint32_t>(INT32_MAX) ? INT32_MAX : static_cast<int32_t>(height);
    int32_t w = width > static_cast<uint32_t>(INT32_MAX) ? INT32_MAX : static_cast<int32_t>(width);
    // Saturating add.
    int64_t bottom64 = int64_t(top) + int64_t(h);
    int64_t right64 = int64_t(left) + int64_t(w);
    r.bottom = bottom64 > INT32_MAX ? INT32_MAX : (bottom64 < INT32_MIN ? INT32_MIN : static_cast<int32_t>(bottom64));
    r.right = right64 > INT32_MAX ? INT32_MAX : (right64 < INT32_MIN ? INT32_MIN : static_cast<int32_t>(right64));
    return r;
}

Result<std::pair<size_t, size_t>> Rect::size() const {
    int64_t w = width(), h = height();
    if (w < 0 || h < 0) return PsdError::invalid("rectangle has negative size");
    if (w > int64_t(kMaxDimension) || h > int64_t(kMaxDimension)) return PsdError::limitExceeded("layer dimensions exceed 300000");
    return std::make_pair(static_cast<size_t>(w), static_cast<size_t>(h));
}

Result<Rect> Rect::read(Reader& r) {
    Rect rect;
    PSD_TRY_ASSIGN(rect.top, r.i32());
    PSD_TRY_ASSIGN(rect.left, r.i32());
    PSD_TRY_ASSIGN(rect.bottom, r.i32());
    PSD_TRY_ASSIGN(rect.right, r.i32());
    return rect;
}

void Rect::write(std::vector<uint8_t>& out) const {
    putI32(out, top);
    putI32(out, left);
    putI32(out, bottom);
    putI32(out, right);
}

std::ostream& operator<<(std::ostream& os, const Rect& r) {
    return os << "Rect{top=" << r.top << ", left=" << r.left << ", bottom=" << r.bottom << ", right=" << r.right << "}";
}

} // namespace psd
