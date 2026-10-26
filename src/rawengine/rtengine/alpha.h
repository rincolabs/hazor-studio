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
// rtcompat note (tmp/feat-raw-therapee-plan.md Fase 2 item 3): reimplemented
// over std::vector<unsigned char> instead of Cairo::ImageSurface, since
// cairomm is being eliminated outright rather than shimmed. Preserves
// Cairo's own FORMAT_A8 layout (one byte per pixel, row stride padded to a
// multiple of 4 bytes) so behaviour is unchanged for any caller indexing by
// (row, col) — rtengine itself doesn't currently instantiate this class
// (improcfun.h's locImage field is unused), but the class is kept faithful
// in case that changes.
#ifndef _ALPHA_H_
#define _ALPHA_H_

#include <cstddef>
#include <vector>

namespace rtengine
{

/// Alpha channel class (8 bits)
class Alpha
{
protected:
    int width;
    int height;
    std::size_t stride; // bytes per row; Cairo FORMAT_A8 pads this to a multiple of 4.
    std::vector<unsigned char> data;

public:
    Alpha();
    Alpha(int width, int height);

    void setSize(int width, int height);
    int getWidth() const;
    int getHeight() const;

    // Will send back the start of a row
    unsigned char* operator () (unsigned row) const;
    // Will send back a value at a given row, col position
    unsigned char& operator () (unsigned row, unsigned col);
    unsigned char operator () (unsigned row, unsigned col) const;
};

}

#endif
