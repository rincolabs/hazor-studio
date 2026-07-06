#include "OnnxSessionHandle.hpp"

#include <QDebug>

#include <cmath>

#ifdef HAVE_ONNXRUNTIME
#include "OnnxSessionHandle_p.hpp"
#else
// Minimal Impl so the unique_ptr and out-of-line methods compile in builds
// without ONNX Runtime.
struct OnnxSessionHandle::Impl {};
#endif

OnnxSessionHandle::OnnxSessionHandle()
    : d(std::make_unique<Impl>())
{
}

OnnxSessionHandle::~OnnxSessionHandle() = default;

QString OnnxSessionHandle::modelPath() const
{
#ifdef HAVE_ONNXRUNTIME
    return d->modelPath;
#else
    return QString();
#endif
}

QString OnnxSessionHandle::providerUsed() const
{
#ifdef HAVE_ONNXRUNTIME
    return d->providerUsed;
#else
    return QString();
#endif
}

QStringList OnnxSessionHandle::inputNames() const
{
    QStringList out;
#ifdef HAVE_ONNXRUNTIME
    for (const auto& n : d->inputNames)
        out << QString::fromStdString(n);
#endif
    return out;
}

QStringList OnnxSessionHandle::outputNames() const
{
    QStringList out;
#ifdef HAVE_ONNXRUNTIME
    for (const auto& n : d->outputNames)
        out << QString::fromStdString(n);
#endif
    return out;
}

std::vector<std::vector<int64_t>> OnnxSessionHandle::inputShapes() const
{
#ifdef HAVE_ONNXRUNTIME
    return d->inputShapes;
#else
    return {};
#endif
}

std::vector<std::vector<int64_t>> OnnxSessionHandle::outputShapes() const
{
#ifdef HAVE_ONNXRUNTIME
    return d->outputShapes;
#else
    return {};
#endif
}

std::vector<OnnxTensorElementType> OnnxSessionHandle::inputElementTypes() const
{
#ifdef HAVE_ONNXRUNTIME
    return d->inputElementTypes;
#else
    return {};
#endif
}

#ifdef HAVE_ONNXRUNTIME

namespace {

OnnxTensorElementType mapOrtElementType(ONNXTensorElementDataType type)
{
    switch (type) {
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT32:
        return OnnxTensorElementType::Int32;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64:
        return OnnxTensorElementType::Int64;
    case ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT:
    default:
        return OnnxTensorElementType::Float32;
    }
}

OnnxTensorElementType expectedInputType(const std::vector<std::string>& inputNames,
                                        const std::vector<OnnxTensorElementType>& inputElementTypes,
                                        const QString& inputName,
                                        OnnxTensorElementType fallback)
{
    const std::string wanted = inputName.toStdString();
    for (size_t i = 0; i < inputNames.size(); ++i) {
        if (inputNames[i] == wanted && i < inputElementTypes.size())
            return inputElementTypes[i];
    }
    return fallback;
}

std::vector<float> tensorAsFloat(const OnnxTensorData& tensor)
{
    if (!tensor.data.empty())
        return tensor.data;
    if (!tensor.int32Data.empty()) {
        std::vector<float> out;
        out.reserve(tensor.int32Data.size());
        for (int32_t v : tensor.int32Data)
            out.push_back(static_cast<float>(v));
        return out;
    }
    std::vector<float> out;
    out.reserve(tensor.int64Data.size());
    for (int64_t v : tensor.int64Data)
        out.push_back(static_cast<float>(v));
    return out;
}

std::vector<int32_t> tensorAsInt32(const OnnxTensorData& tensor)
{
    if (!tensor.int32Data.empty())
        return tensor.int32Data;
    if (!tensor.int64Data.empty()) {
        std::vector<int32_t> out;
        out.reserve(tensor.int64Data.size());
        for (int64_t v : tensor.int64Data)
            out.push_back(static_cast<int32_t>(v));
        return out;
    }
    std::vector<int32_t> out;
    out.reserve(tensor.data.size());
    for (float v : tensor.data)
        out.push_back(static_cast<int32_t>(std::lround(v)));
    return out;
}

std::vector<int64_t> tensorAsInt64(const OnnxTensorData& tensor)
{
    if (!tensor.int64Data.empty())
        return tensor.int64Data;
    if (!tensor.int32Data.empty()) {
        std::vector<int64_t> out;
        out.reserve(tensor.int32Data.size());
        for (int32_t v : tensor.int32Data)
            out.push_back(static_cast<int64_t>(v));
        return out;
    }
    std::vector<int64_t> out;
    out.reserve(tensor.data.size());
    for (float v : tensor.data)
        out.push_back(static_cast<int64_t>(std::lround(v)));
    return out;
}

} // namespace

bool OnnxSessionHandle::run(const std::vector<OnnxTensorData>& inputs,
                           const QStringList& requestedOutputs,
                           std::vector<OnnxTensorData>& outputs,
                           QString* error)
{
    outputs.clear();
    try {
        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);

        // Build input tensors (data is copied into Ort::Value-owned buffers via
        // a non-owning view; the source vectors must outlive Run(), which they
        // do for the duration of this call).
        std::vector<Ort::Value> inputValues;
        std::vector<const char*> inputNamePtrs;
        std::vector<std::string> inputNameStore;
        std::vector<std::vector<float>> floatStorage;
        std::vector<std::vector<int32_t>> int32Storage;
        std::vector<std::vector<int64_t>> int64Storage;
        inputValues.reserve(inputs.size());
        inputNamePtrs.reserve(inputs.size());
        inputNameStore.reserve(inputs.size());
        floatStorage.reserve(inputs.size());
        int32Storage.reserve(inputs.size());
        int64Storage.reserve(inputs.size());

        for (const auto& t : inputs) {
            inputNameStore.push_back(t.name.toStdString());
            OnnxTensorElementType expected =
                expectedInputType(d->inputNames, d->inputElementTypes, t.name, t.elementType);
            if (t.name.contains(QStringLiteral("input_ids"), Qt::CaseInsensitive)
                && (t.elementType == OnnxTensorElementType::Int32
                    || t.elementType == OnnxTensorElementType::Int64)) {
                expected = t.elementType;
            }
            if (expected == OnnxTensorElementType::Int32) {
                int32Storage.push_back(tensorAsInt32(t));
                inputValues.push_back(Ort::Value::CreateTensor<int32_t>(
                    memInfo,
                    int32Storage.back().data(), int32Storage.back().size(),
                    t.shape.data(), t.shape.size()));
            } else if (expected == OnnxTensorElementType::Int64) {
                int64Storage.push_back(tensorAsInt64(t));
                inputValues.push_back(Ort::Value::CreateTensor<int64_t>(
                    memInfo,
                    int64Storage.back().data(), int64Storage.back().size(),
                    t.shape.data(), t.shape.size()));
            } else {
                floatStorage.push_back(tensorAsFloat(t));
                inputValues.push_back(Ort::Value::CreateTensor<float>(
                    memInfo,
                    floatStorage.back().data(), floatStorage.back().size(),
                    t.shape.data(), t.shape.size()));
            }
        }
        for (const auto& s : inputNameStore)
            inputNamePtrs.push_back(s.c_str());

        // Resolve requested outputs (default: all model outputs).
        std::vector<std::string> outNameStore;
        if (requestedOutputs.isEmpty()) {
            outNameStore = d->outputNames;
        } else {
            for (const QString& n : requestedOutputs)
                outNameStore.push_back(n.toStdString());
        }
        std::vector<const char*> outNamePtrs;
        outNamePtrs.reserve(outNameStore.size());
        for (const auto& s : outNameStore)
            outNamePtrs.push_back(s.c_str());

        auto results = d->session.Run(Ort::RunOptions{nullptr},
                                      inputNamePtrs.data(), inputValues.data(), inputValues.size(),
                                      outNamePtrs.data(), outNamePtrs.size());

        for (size_t i = 0; i < results.size(); ++i) {
            Ort::Value& v = results[i];
            if (!v.IsTensor())
                continue;
            auto info = v.GetTensorTypeAndShapeInfo();
            OnnxTensorData out;
            out.name = QString::fromStdString(outNameStore[i]);
            out.shape = info.GetShape();
            const size_t count = info.GetElementCount();

            // Etapa 1 foundation: read float32 outputs. Other dtypes are read in
            // later stages as models require them.
            if (info.GetElementType() == ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
                const float* src = v.GetTensorData<float>();
                out.data.assign(src, src + count);
            } else {
                qWarning() << "[AI][ONNX] Output" << out.name
                           << "has non-float dtype" << int(info.GetElementType())
                           << "- not read in this stage";
            }
            outputs.push_back(std::move(out));
        }
        return true;
    } catch (const Ort::Exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        qWarning() << "[AI][ONNX] Run failed:" << e.what();
        return false;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        qWarning() << "[AI][ONNX] Run failed:" << e.what();
        return false;
    }
}

std::shared_ptr<OnnxSessionHandle> OnnxSessionFactory::create(Ort::Env& env,
                                                             Ort::SessionOptions& options,
                                                             const QString& modelPath,
                                                             const QString& providerId,
                                                             QString* error)
{
    try {
        auto handle = std::shared_ptr<OnnxSessionHandle>(new OnnxSessionHandle());
        auto& impl = *handle->d;
        impl.modelPath = modelPath;
        impl.providerUsed = providerId;

#if defined(_WIN32)
        impl.session = Ort::Session(env, modelPath.toStdWString().c_str(), options);
#else
        impl.session = Ort::Session(env, modelPath.toUtf8().constData(), options);
#endif

        Ort::AllocatorWithDefaultOptions allocator;

        const size_t numInputs = impl.session.GetInputCount();
        for (size_t i = 0; i < numInputs; ++i) {
            auto name = impl.session.GetInputNameAllocated(i, allocator);
            impl.inputNames.emplace_back(name.get());
            auto info = impl.session.GetInputTypeInfo(i).GetTensorTypeAndShapeInfo();
            impl.inputShapes.push_back(info.GetShape());
            impl.inputElementTypes.push_back(mapOrtElementType(info.GetElementType()));
        }
        const size_t numOutputs = impl.session.GetOutputCount();
        for (size_t i = 0; i < numOutputs; ++i) {
            auto name = impl.session.GetOutputNameAllocated(i, allocator);
            impl.outputNames.emplace_back(name.get());
            auto info = impl.session.GetOutputTypeInfo(i).GetTensorTypeAndShapeInfo();
            impl.outputShapes.push_back(info.GetShape());
        }
        return handle;
    } catch (const Ort::Exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return nullptr;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        return nullptr;
    }
}

#else // !HAVE_ONNXRUNTIME

bool OnnxSessionHandle::run(const std::vector<OnnxTensorData>&,
                           const QStringList&,
                           std::vector<OnnxTensorData>& outputs,
                           QString* error)
{
    outputs.clear();
    if (error) *error = QStringLiteral("ONNX Runtime is not available in this build.");
    return false;
}

#endif // HAVE_ONNXRUNTIME
