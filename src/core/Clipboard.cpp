#include "Clipboard.hpp"

ClipboardManager& ClipboardManager::instance()
{
    static ClipboardManager inst;
    return inst;
}

void ClipboardManager::setData(ClipboardData data, QString token)
{
    m_data = std::move(data);
    m_token = std::move(token);
}

const ClipboardData& ClipboardManager::data() const
{
    return m_data;
}

bool ClipboardManager::hasData() const
{
    return m_data.type != ClipboardType::None;
}

void ClipboardManager::clear()
{
    m_data = ClipboardData{};
    m_token.clear();
}

const QString& ClipboardManager::token() const
{
    return m_token;
}
