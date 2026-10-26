/*
 *  This file is part of RawTherapee.
 *
 *  Copyright (c) 2004-2010 Gabor Horvath <hgabor@rawtherapee.com>
 *
 *  RawTherapee is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  RawTherapee is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with RawTherapee.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "alpha.h"

namespace
{

// Matches cairo_format_stride_for_width(CAIRO_FORMAT_A8, width): one byte
// per pixel, row stride padded up to a multiple of 4 bytes.
std::size_t strideForWidth(int width)
{
    return static_cast<std::size_t>((width + 3) & ~3);
}

}

namespace rtengine
{

Alpha::Alpha() : width(-1), height(-1), stride(0) {}

Alpha::Alpha(int w, int h) : width(-1), height(-1), stride(0)
{
    setSize(w, h);
}

void Alpha::setSize(int w, int h)
{
    if (w > 0 && h > 0) {
        width = w;
        height = h;
        stride = strideForWidth(w);
        data.assign(stride * static_cast<std::size_t>(h), 0);
    } else {
        width = -1;
        height = -1;
        stride = 0;
        data.clear();
    }
}

int Alpha::getWidth() const
{
    return width;
}

int Alpha::getHeight() const
{
    return height;
}

unsigned char Alpha::operator () (unsigned row, unsigned col) const
{
    return data[row * stride + col];
}

unsigned char& Alpha::operator () (unsigned row, unsigned col)
{
    return data[row * stride + col];
}

unsigned char* Alpha::operator () (unsigned row) const
{
    return const_cast<unsigned char*>(data.data()) + row * stride;
}

}
