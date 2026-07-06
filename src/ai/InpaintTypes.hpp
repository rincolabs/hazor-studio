#pragma once

#include <QImage>
#include <QSize>
#include <QString>

// Shared value types for the generative-fill / inpaint pipeline (src/ai/).
// Providers consume an InpaintRequest and emit an InpaintResult or InpaintError.

struct InpaintRequest {
    QImage  image;            // RGBA region (bbox of the selection + context margin)
    QImage  mask;             // Grayscale8: white = repaint, black = keep
    QString prompt;           // optional (a pure erase ignores it)
    QString negativePrompt;
    int     seed = -1;        // -1 = random
    double  strength = 0.75;  // denoising strength (img2img)
    int     steps = 20;
    QSize   outSize;          // target size (= aligned selection bbox)
};

struct InpaintResult {
    QImage  image;            // RGBA result, sized to InpaintRequest::outSize
    int     seed = -1;        // seed actually used (if reported by the backend)
};

struct InpaintError {
    enum Code {
        Network,              // connection refused / timeout (e.g. SD server down)
        Auth,                 // missing / invalid API key
        BadResponse,          // unparseable or empty response
        Canceled,
        Unsupported,          // request not supported by this provider
        Unknown
    };

    Code    code = Unknown;
    QString message;
};
