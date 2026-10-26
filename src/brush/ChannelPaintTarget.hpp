#pragma once

#include <QImage>
#include <QRect>
#include <vector>

// Where a stroke lands when the Channels panel has channels armed instead of
// the RGB composite.
//
// The buffers are the document's own channel data (Grayscale8, canvas-sized),
// so a dab goes straight into the channel and nowhere near the active layer —
// that separation is the point: painting in Alpha 1 must not touch the pixel
// layer that happens to be selected in the Layers panel.
//
// Several buffers because the panel allows several channels to be armed at
// once (shift-click), and a stroke writes to every one of them.
struct ChannelPaintTarget {
    std::vector<QImage*> buffers;
    // Union of every rect a dab touched, in document pixels. The canvas turns
    // it into the channel-view damage region so a stroke repaints the dab's
    // area instead of the canvas.
    QRect dirty;
};
