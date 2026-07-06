#pragma once

// Private, ONNX-aware header. Included ONLY by onnx/*.cpp (never by the public
// AI layer, tools or UI), and only when HAVE_ONNXRUNTIME is defined. It exposes
// the full OnnxSessionHandle::Impl plus a friend factory so OnnxRuntimeBackend
// can build a session with its own SessionOptions while OnnxSessionHandle keeps
// ownership of the resulting Ort::Session.

#include "OnnxSessionHandle.hpp"

#include <onnxruntime_cxx_api.h>

#include <string>
#include <vector>

struct OnnxSessionHandle::Impl {
    Ort::Session session{nullptr};
    QString modelPath;
    QString providerUsed;

    std::vector<std::string> inputNames;
    std::vector<std::string> outputNames;
    std::vector<std::vector<int64_t>> inputShapes;
    std::vector<std::vector<int64_t>> outputShapes;
    std::vector<OnnxTensorElementType> inputElementTypes;
};

// Friend of OnnxSessionHandle (declared in the public header). The backend owns
// provider/SessionOptions decisions and calls create() to obtain a populated
// handle.
struct OnnxSessionFactory {
    static std::shared_ptr<OnnxSessionHandle> create(Ort::Env& env,
                                                     Ort::SessionOptions& options,
                                                     const QString& modelPath,
                                                     const QString& providerId,
                                                     QString* error);
};
