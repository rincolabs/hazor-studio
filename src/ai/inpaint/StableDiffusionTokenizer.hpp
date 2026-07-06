#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class StableDiffusionTokenizer {
public:
    bool load(const QString& vocabJsonPath,
              const QString& mergesTxtPath,
              QString* error = nullptr);

    QVector<int64_t> encode(const QString& text, int maxLength = 77) const;
    bool isLoaded() const { return !m_vocab.isEmpty() && !m_merges.isEmpty(); }

private:
    QStringList tokenizeWords(const QString& text) const;
    QStringList bpe(const QString& token) const;
    QString byteEncode(const QByteArray& bytes) const;
    QString pairKey(const QString& a, const QString& b) const;

    QHash<QString, int> m_vocab;
    QHash<QString, int> m_merges;
    int m_bosId = 49406;
    int m_eosId = 49407;
    int m_padId = 49407;
};
