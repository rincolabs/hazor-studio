#include "ai/inpaint/StableDiffusionScheduler.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

bool StableDiffusionScheduler::loadConfig(const QString& schedulerConfigPath, QString* error)
{
    if (schedulerConfigPath.isEmpty()) {
        rebuildAlphas();
        return true;
    }

    QFile file(schedulerConfigPath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Unable to read scheduler config.");
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid scheduler config: %1").arg(parseError.errorString());
        return false;
    }

    const QJsonObject root = doc.object();
    m_trainTimesteps = std::max(1, root.value(QStringLiteral("num_train_timesteps")).toInt(m_trainTimesteps));
    m_betaStart = static_cast<float>(root.value(QStringLiteral("beta_start")).toDouble(m_betaStart));
    m_betaEnd = static_cast<float>(root.value(QStringLiteral("beta_end")).toDouble(m_betaEnd));
    m_betaSchedule = root.value(QStringLiteral("beta_schedule")).toString(m_betaSchedule);
    rebuildAlphas();
    return true;
}

void StableDiffusionScheduler::setTimesteps(int steps)
{
    steps = std::clamp(steps, 1, m_trainTimesteps);
    m_timesteps.clear();
    m_timesteps.reserve(steps);
    const double stride = static_cast<double>(m_trainTimesteps) / steps;
    for (int i = steps - 1; i >= 0; --i) {
        const int t = std::clamp(static_cast<int>(std::floor(i * stride)), 0, m_trainTimesteps - 1);
        m_timesteps.append(t);
    }
}

StableDiffusionTensor StableDiffusionScheduler::addNoise(const StableDiffusionTensor& latents,
                                                         const StableDiffusionTensor& noise,
                                                         int timestep) const
{
    if (latents.shape != noise.shape || !latents.isValid() || !noise.isValid())
        return {};
    const float alpha = alphaCumprod(timestep);
    const float sqrtAlpha = std::sqrt(alpha);
    const float sqrtOneMinusAlpha = std::sqrt(std::max(0.0f, 1.0f - alpha));
    StableDiffusionTensor out;
    out.shape = latents.shape;
    out.data.resize(latents.data.size());
    for (size_t i = 0; i < latents.data.size(); ++i)
        out.data[i] = latents.data[i] * sqrtAlpha + noise.data[i] * sqrtOneMinusAlpha;
    return out;
}

StableDiffusionTensor StableDiffusionScheduler::scaleModelInput(const StableDiffusionTensor& latents,
                                                                int) const
{
    return latents;
}

StableDiffusionTensor StableDiffusionScheduler::step(const StableDiffusionTensor& noisePred,
                                                     int timestep,
                                                     const StableDiffusionTensor& latents) const
{
    if (noisePred.shape != latents.shape || !noisePred.isValid() || !latents.isValid())
        return {};

    const int stepIndex = m_timesteps.indexOf(timestep);
    const int prevTimestep = (stepIndex >= 0 && stepIndex + 1 < m_timesteps.size())
        ? m_timesteps.at(stepIndex + 1)
        : 0;
    const float alpha = alphaCumprod(timestep);
    const float prevAlpha = alphaCumprod(prevTimestep);
    const float beta = std::max(0.0f, 1.0f - alpha);
    const float sqrtAlpha = std::sqrt(alpha);
    const float sqrtBeta = std::sqrt(beta);

    StableDiffusionTensor predOriginal;
    predOriginal.shape = latents.shape;
    predOriginal.data.resize(latents.data.size());
    for (size_t i = 0; i < latents.data.size(); ++i)
        predOriginal.data[i] = (latents.data[i] - sqrtBeta * noisePred.data[i]) / std::max(1e-6f, sqrtAlpha);

    const float prevBeta = std::max(0.0f, 1.0f - prevAlpha);
    StableDiffusionTensor out;
    out.shape = latents.shape;
    out.data.resize(latents.data.size());
    for (size_t i = 0; i < latents.data.size(); ++i) {
        out.data[i] = std::sqrt(prevAlpha) * predOriginal.data[i]
            + std::sqrt(prevBeta) * noisePred.data[i];
    }
    return out;
}

void StableDiffusionScheduler::rebuildAlphas()
{
    m_alphasCumprod.clear();
    m_alphasCumprod.reserve(m_trainTimesteps);
    float cumulative = 1.0f;
    const bool scaledLinear = m_betaSchedule == QLatin1String("scaled_linear");
    for (int i = 0; i < m_trainTimesteps; ++i) {
        const float t = m_trainTimesteps <= 1 ? 0.0f : static_cast<float>(i) / (m_trainTimesteps - 1);
        float beta = 0.0f;
        if (scaledLinear) {
            const float start = std::sqrt(m_betaStart);
            const float end = std::sqrt(m_betaEnd);
            const float value = start + (end - start) * t;
            beta = value * value;
        } else {
            beta = m_betaStart + (m_betaEnd - m_betaStart) * t;
        }
        cumulative *= (1.0f - std::clamp(beta, 0.0f, 0.999f));
        m_alphasCumprod.append(cumulative);
    }
}

float StableDiffusionScheduler::alphaCumprod(int timestep) const
{
    if (m_alphasCumprod.isEmpty())
        return 1.0f;
    const int last = static_cast<int>(m_alphasCumprod.size()) - 1;
    return m_alphasCumprod.at(std::clamp(timestep, 0, last));
}
