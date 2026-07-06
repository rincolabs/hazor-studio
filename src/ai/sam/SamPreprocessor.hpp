#pragma once

#include "ai/onnx/OnnxSessionHandle.hpp"  // OnnxTensorData
#include "ai/sam/SamCoordinateMapper.hpp"

#include <QImage>
#include <QSize>

// Turns a document snapshot into the SAM encoder input tensor.
//
// This SAM export's encoder takes `input_image` as a dynamic float HWC tensor in
// 0..255 RGB; normalisation (mean/std), CHW transpose and padding to a square
// `input_size` all happen inside the ONNX graph. So preprocessing is just:
// ResizeLongestSide(snapshot, input_size) → float RGB HWC. The matching coord
// transforms live in SamCoordinateMapper.
class SamPreprocessor {
public:
    struct Result {
        OnnxTensorData inputTensor;   // name "input_image", shape [resizedH, resizedW, 3]
        SamCoordinateMapper mapper;   // snapshot↔model mapping for this input
        QSize snapshotSize;           // == orig_im_size fed to the decoder
        bool ok = false;
    };

    // Builds the encoder input from an RGB(A) snapshot image. The snapshot is
    // already in document orientation; it may be any size (working resolution).
    static Result buildEncoderInput(const QImage& snapshot, int inputSize);
};
