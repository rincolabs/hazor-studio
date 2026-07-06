#include "ai/inpaint/StableDiffusionTokenizer.hpp"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <limits>

namespace {

uint byteToCodepoint(uchar b)
{
    if ((b >= 33 && b <= 126) || (b >= 161 && b <= 172) || (b >= 174 && b <= 255))
        return b;

    static QHash<uchar, uint> table;
    if (table.isEmpty()) {
        uint next = 256;
        for (int i = 0; i < 256; ++i) {
            const uchar value = static_cast<uchar>(i);
            if ((value >= 33 && value <= 126)
                || (value >= 161 && value <= 172)
                || (value >= 174 && value <= 255)) {
                table.insert(value, value);
            } else {
                table.insert(value, next++);
            }
        }
    }
    return table.value(b);
}

} // namespace

bool StableDiffusionTokenizer::load(const QString& vocabJsonPath,
                                    const QString& mergesTxtPath,
                                    QString* error)
{
    QFile vocabFile(vocabJsonPath);
    if (!vocabFile.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Unable to read tokenizer vocab.");
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(vocabFile.readAll(), &parseError);
    if (!doc.isObject()) {
        if (error) *error = QStringLiteral("Invalid tokenizer vocab: %1").arg(parseError.errorString());
        return false;
    }

    m_vocab.clear();
    const QJsonObject vocab = doc.object();
    for (auto it = vocab.constBegin(); it != vocab.constEnd(); ++it)
        m_vocab.insert(it.key(), it.value().toInt());

    m_bosId = m_vocab.value(QStringLiteral("<|startoftext|>"), m_bosId);
    m_eosId = m_vocab.value(QStringLiteral("<|endoftext|>"), m_eosId);
    m_padId = m_eosId;

    QFile mergesFile(mergesTxtPath);
    if (!mergesFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error) *error = QStringLiteral("Unable to read tokenizer merges.");
        return false;
    }

    m_merges.clear();
    QTextStream stream(&mergesFile);
    int rank = 0;
    while (!stream.atEnd()) {
        const QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const QStringList parts = line.split(QRegularExpression(QStringLiteral("\\s+")),
                                             Qt::SkipEmptyParts);
        if (parts.size() != 2)
            continue;
        m_merges.insert(pairKey(parts.at(0), parts.at(1)), rank++);
    }

    if (m_vocab.isEmpty() || m_merges.isEmpty()) {
        if (error) *error = QStringLiteral("Tokenizer files are incomplete.");
        return false;
    }
    return true;
}

QVector<int64_t> StableDiffusionTokenizer::encode(const QString& text, int maxLength) const
{
    QVector<int64_t> ids;
    maxLength = std::max(2, maxLength);
    ids.reserve(maxLength);
    ids.append(m_bosId);

    for (const QString& token : tokenizeWords(text)) {
        for (const QString& piece : bpe(byteEncode(token.toUtf8()))) {
            const int id = m_vocab.value(piece, -1);
            if (id >= 0)
                ids.append(id);
            if (ids.size() >= maxLength - 1)
                break;
        }
        if (ids.size() >= maxLength - 1)
            break;
    }

    ids.append(m_eosId);
    while (ids.size() < maxLength)
        ids.append(m_padId);
    if (ids.size() > maxLength)
        ids.resize(maxLength);
    return ids;
}

QStringList StableDiffusionTokenizer::tokenizeWords(const QString& text) const
{
    QStringList out;
    static const QRegularExpression re(QStringLiteral("[\\p{L}\\p{N}]+|[^\\s\\p{L}\\p{N}]"));
    QRegularExpressionMatchIterator it = re.globalMatch(text.toLower());
    while (it.hasNext())
        out << it.next().captured(0);
    return out;
}

QStringList StableDiffusionTokenizer::bpe(const QString& token) const
{
    if (token.isEmpty())
        return {};

    QStringList word;
    word.reserve(token.size());
    for (int i = 0; i < token.size(); ++i)
        word << token.mid(i, 1);
    word.last().append(QStringLiteral("</w>"));

    while (word.size() > 1) {
        int bestRank = std::numeric_limits<int>::max();
        int bestIndex = -1;
        for (int i = 0; i + 1 < word.size(); ++i) {
            const int rank = m_merges.value(pairKey(word.at(i), word.at(i + 1)),
                                            std::numeric_limits<int>::max());
            if (rank < bestRank) {
                bestRank = rank;
                bestIndex = i;
            }
        }
        if (bestIndex < 0)
            break;

        QStringList merged;
        merged.reserve(word.size() - 1);
        for (int i = 0; i < word.size(); ++i) {
            if (i == bestIndex) {
                merged << (word.at(i) + word.at(i + 1));
                ++i;
            } else {
                merged << word.at(i);
            }
        }
        word = merged;
    }
    return word;
}

QString StableDiffusionTokenizer::byteEncode(const QByteArray& bytes) const
{
    QString out;
    for (char ch : bytes) {
        const uchar b = static_cast<uchar>(ch);
        char32_t cp = static_cast<char32_t>(byteToCodepoint(b));
        out.append(QString::fromUcs4(&cp, 1));
    }
    return out;
}

QString StableDiffusionTokenizer::pairKey(const QString& a, const QString& b) const
{
    return a + QChar(0x0001) + b;
}
