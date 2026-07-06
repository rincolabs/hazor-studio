#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <memory>
#include <vector>

enum class OnnxTensorElementType {
    Float32,
    Int32,
    Int64
};

// A simple tensor used to feed and read ONNX sessions. The public AI layer
// speaks in these terms; raw Ort types never leave the onnx/ .cpp files.
struct OnnxTensorData {
    QString name;
    std::vector<int64_t> shape;
    std::vector<float> data;
    std::vector<int32_t> int32Data;
    std::vector<int64_t> int64Data;
    OnnxTensorElementType elementType = OnnxTensorElementType::Float32;

    int64_t elementCount() const
    {
        int64_t n = 1;
        for (int64_t d : shape) n *= (d > 0 ? d : 1);
        return n;
    }
};

// RAII wrapper around a single loaded ONNX model (one .onnx file). Owns the
// underlying Ort::Session via a pimpl so this header stays free of ONNX
// includes and compiles even in builds without ONNX Runtime. Instances are
// created exclusively by OnnxRuntimeBackend.
class OnnxSessionHandle {
public:
    ~OnnxSessionHandle();

    OnnxSessionHandle(const OnnxSessionHandle&) = delete;
    OnnxSessionHandle& operator=(const OnnxSessionHandle&) = delete;

    QString modelPath() const;
    QString providerUsed() const;       // provider id actually used for this session

    QStringList inputNames() const;
    QStringList outputNames() const;
    std::vector<std::vector<int64_t>> inputShapes() const;
    std::vector<std::vector<int64_t>> outputShapes() const;
    std::vector<OnnxTensorElementType> inputElementTypes() const;

    // Runs the model. Inputs are matched by name. If requestedOutputs is empty
    // every model output is returned. Returns false and fills *error on failure;
    // never throws. Heavy — callers must invoke this off the UI thread.
    bool run(const std::vector<OnnxTensorData>& inputs,
             const QStringList& requestedOutputs,
             std::vector<OnnxTensorData>& outputs,
             QString* error = nullptr);

private:
    friend class OnnxRuntimeBackend;
    friend struct OnnxSessionFactory;   // ONNX-aware factory, defined in onnx/*.cpp
    OnnxSessionHandle();

    struct Impl;
    std::unique_ptr<Impl> d;
};
