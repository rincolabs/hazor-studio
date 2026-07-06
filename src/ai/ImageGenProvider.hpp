#pragma once

#include <QObject>
#include <QString>

#include "InpaintTypes.hpp"

// Abstract async image-generation provider. Mirrors the style of LLMClient:
// one in-flight request, results delivered via signals on the main thread.
// Concrete subclasses live in src/ai/ (A1111, OpenAI, Stability, Gemini,
// aggregators...). Created by ImageGenProviderFactory from a Generative preset.
class ImageGenProvider : public QObject {
    Q_OBJECT

public:
    explicit ImageGenProvider(QObject* parent = nullptr) : QObject(parent) {}
    ~ImageGenProvider() override = default;

    // Start an inpaint request. Emits finished() or failed() exactly once.
    virtual void requestInpaint(const InpaintRequest& request) = 0;
    virtual void cancel() = 0;
    virtual QString name() const = 0;

    // Whether the backend accepts an explicit mask. Providers that don't
    // (e.g. Gemini) only receive the cropped region; the caller stitches the
    // result back through the selection mask locally.
    virtual bool supportsNativeMask() const = 0;

    // Whether requests leave the user's machine (cloud). Local SD = false.
    // The caller uses this to require an explicit opt-in for cloud providers.
    virtual bool isCloud() const = 0;

signals:
    void progress(int pct, const QString& stage);  // if the backend reports it
    void finished(const InpaintResult& result);
    void failed(const InpaintError& error);
};
