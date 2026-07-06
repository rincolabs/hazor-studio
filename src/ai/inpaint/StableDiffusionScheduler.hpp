#pragma once

#include "ai/inpaint/StableDiffusionLatentUtils.hpp"

#include <QString>
#include <QVector>

class StableDiffusionScheduler {
public:
    bool loadConfig(const QString& schedulerConfigPath, QString* error = nullptr);
    void setTimesteps(int steps);

    QVector<int> timesteps() const { return m_timesteps; }
    StableDiffusionTensor addNoise(const StableDiffusionTensor& latents,
                                   const StableDiffusionTensor& noise,
                                   int timestep) const;
    StableDiffusionTensor scaleModelInput(const StableDiffusionTensor& latents,
                                          int timestep) const;
    StableDiffusionTensor step(const StableDiffusionTensor& noisePred,
                               int timestep,
                               const StableDiffusionTensor& latents) const;

    int trainTimesteps() const { return m_trainTimesteps; }

private:
    void rebuildAlphas();
    float alphaCumprod(int timestep) const;

    int m_trainTimesteps = 1000;
    float m_betaStart = 0.00085f;
    float m_betaEnd = 0.012f;
    QString m_betaSchedule = QStringLiteral("scaled_linear");
    QVector<float> m_alphasCumprod;
    QVector<int> m_timesteps;
};
