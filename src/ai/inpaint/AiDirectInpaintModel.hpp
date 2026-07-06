#pragma once

#include "ai/AiRemoveTypes.hpp"
#include "ai/models/AiModelDescriptor.hpp"
#include "ai/runtime/AiInferenceSession.hpp"

#include <QImage>
#include <QString>

#include <memory>
#include <atomic>

class AiDirectInpaintModel {
public:
    AiDirectInpaintModel(std::shared_ptr<AiInferenceSession> session,
                         AiModelDescriptor descriptor);

    QImage run(const QImage& image, const QImage& mask,
               const AiRemoveOptions& options,
               QString* error = nullptr,
               std::atomic_bool* cancel = nullptr,
               const QString& debugDir = QString());

    QString providerUsed() const;

private:
    QString inputName(const QString& key, const QString& fallback) const;
    QString outputName(const QString& key, const QString& fallback) const;

    std::shared_ptr<AiInferenceSession> m_session;
    AiModelDescriptor m_descriptor;
};
