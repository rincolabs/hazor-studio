#include "rtcompat/glib/keyfile.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>

#include "rtcompat/glib/fileio.hpp"

namespace Glib {

namespace {

std::string trim(const std::string& s)
{
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return s.substr(begin, end - begin);
}

// Escapes backslash/newline/CR/tab (always) and ';' (only for list items,
// where it is otherwise ambiguous with the list separator) — matches
// GLib's g_key_file value escaping.
std::string escapeValue(const std::string& s, bool escapeSemicolon)
{
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case ';':
                if (escapeSemicolon) {
                    out += "\\;";
                } else {
                    out += ';';
                }
                break;
            default: out += c;
        }
    }
    return out;
}

std::string unescapeValue(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (c == '\\' && i + 1 < s.size()) {
            const char next = s[++i];
            switch (next) {
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 's': out += ' '; break;
                case '\\': out += '\\'; break;
                case ';': out += ';'; break;
                default: out += next;
            }
            continue;
        }
        out += c;
    }
    return out;
}

// Splits on unescaped `;`, leaving escape sequences in each part intact
// (callers that want string semantics still need unescapeValue() on each
// part; numeric lists never contain backslashes so this is already their
// final tokenization).
std::vector<std::string> splitList(const std::string& raw)
{
    std::vector<std::string> parts;
    std::string current;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '\\' && i + 1 < raw.size()) {
            current += c;
            current += raw[++i];
            continue;
        }
        if (c == ';') {
            parts.push_back(current);
            current.clear();
            continue;
        }
        current += c;
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

std::string formatDouble(double value)
{
    char buf[64];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value, std::chars_format::general, 17);
    return std::string(buf, result.ptr);
}

double parseDouble(const std::string& text)
{
    const std::string t = trim(text);
    double value = 0.0;
    const auto result = std::from_chars(t.data(), t.data() + t.size(), value);
    if (result.ec != std::errc() || result.ptr != t.data() + t.size()) {
        throw KeyFileError(KeyFileError::INVALID_VALUE, "Glib::KeyFile: invalid double value '" + text + "'");
    }
    return value;
}

int parseInt(const std::string& text)
{
    const std::string t = trim(text);
    int value = 0;
    const auto result = std::from_chars(t.data(), t.data() + t.size(), value);
    if (result.ec != std::errc() || result.ptr != t.data() + t.size()) {
        throw KeyFileError(KeyFileError::INVALID_VALUE, "Glib::KeyFile: invalid integer value '" + text + "'");
    }
    return value;
}

bool parseBool(const std::string& text)
{
    const std::string t = trim(text);
    if (t == "true" || t == "1") {
        return true;
    }
    if (t == "false" || t == "0") {
        return false;
    }
    throw KeyFileError(KeyFileError::INVALID_VALUE, "Glib::KeyFile: invalid boolean value '" + text + "'");
}

} // namespace

KeyFile::Group* KeyFile::findGroup(const std::string& name)
{
    for (auto& g : groups_) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

const KeyFile::Group* KeyFile::findGroup(const std::string& name) const
{
    for (const auto& g : groups_) {
        if (g.name == name) {
            return &g;
        }
    }
    return nullptr;
}

KeyFile::Group& KeyFile::getOrCreateGroup(const std::string& name)
{
    if (Group* g = findGroup(name)) {
        return *g;
    }
    groups_.push_back(Group{name, {}});
    return groups_.back();
}

const KeyFile::Entry* KeyFile::findEntry(const std::string& groupName, const std::string& key) const
{
    const Group* g = findGroup(groupName);
    if (!g) {
        return nullptr;
    }
    for (const auto& e : g->entries) {
        if (e.key == key) {
            return &e;
        }
    }
    return nullptr;
}

const std::string& KeyFile::getRaw(const std::string& groupName, const std::string& key) const
{
    const Group* g = findGroup(groupName);
    if (!g) {
        throw KeyFileError(KeyFileError::GROUP_NOT_FOUND, "Glib::KeyFile: group '" + groupName + "' not found");
    }
    for (const auto& e : g->entries) {
        if (e.key == key) {
            return e.rawValue;
        }
    }
    throw KeyFileError(KeyFileError::KEY_NOT_FOUND, "Glib::KeyFile: key '" + key + "' not found in group '" + groupName + "'");
}

void KeyFile::setRaw(const std::string& groupName, const std::string& key, std::string value)
{
    Group& g = getOrCreateGroup(groupName);
    for (auto& e : g.entries) {
        if (e.key == key) {
            e.rawValue = std::move(value);
            return;
        }
    }
    g.entries.push_back(Entry{key, std::move(value)});
}

bool KeyFile::has_group(const ustring& groupName) const
{
    return findGroup(groupName) != nullptr;
}

bool KeyFile::has_key(const ustring& groupName, const ustring& key) const
{
    return findEntry(groupName, key) != nullptr;
}

bool KeyFile::get_boolean(const ustring& groupName, const ustring& key) const
{
    return parseBool(getRaw(groupName, key));
}

int KeyFile::get_integer(const ustring& groupName, const ustring& key) const
{
    return parseInt(getRaw(groupName, key));
}

double KeyFile::get_double(const ustring& groupName, const ustring& key) const
{
    return parseDouble(getRaw(groupName, key));
}

ustring KeyFile::get_string(const ustring& groupName, const ustring& key) const
{
    return ustring(unescapeValue(getRaw(groupName, key)));
}

std::vector<int> KeyFile::get_integer_list(const ustring& groupName, const ustring& key) const
{
    std::vector<int> result;
    for (const auto& part : splitList(getRaw(groupName, key))) {
        result.push_back(parseInt(part));
    }
    return result;
}

std::vector<double> KeyFile::get_double_list(const ustring& groupName, const ustring& key) const
{
    std::vector<double> result;
    for (const auto& part : splitList(getRaw(groupName, key))) {
        result.push_back(parseDouble(part));
    }
    return result;
}

std::vector<ustring> KeyFile::get_string_list(const ustring& groupName, const ustring& key) const
{
    std::vector<ustring> result;
    for (const auto& part : splitList(getRaw(groupName, key))) {
        result.push_back(ustring(unescapeValue(part)));
    }
    return result;
}

std::vector<ustring> KeyFile::get_keys(const ustring& groupName) const
{
    const Group* g = findGroup(groupName);
    if (!g) {
        throw KeyFileError(KeyFileError::GROUP_NOT_FOUND, "Glib::KeyFile: group '" + std::string(groupName) + "' not found");
    }
    std::vector<ustring> result;
    result.reserve(g->entries.size());
    for (const auto& e : g->entries) {
        result.push_back(ustring(e.key));
    }
    return result;
}

std::vector<ustring> KeyFile::get_groups() const
{
    std::vector<ustring> result;
    result.reserve(groups_.size());
    for (const auto& g : groups_) {
        result.push_back(ustring(g.name));
    }
    return result;
}

void KeyFile::set_boolean(const ustring& groupName, const ustring& key, bool value)
{
    setRaw(groupName, key, value ? "true" : "false");
}

void KeyFile::set_integer(const ustring& groupName, const ustring& key, int value)
{
    char buf[32];
    const auto result = std::to_chars(buf, buf + sizeof(buf), value);
    setRaw(groupName, key, std::string(buf, result.ptr));
}

void KeyFile::set_double(const ustring& groupName, const ustring& key, double value)
{
    setRaw(groupName, key, formatDouble(value));
}

void KeyFile::set_string(const ustring& groupName, const ustring& key, const ustring& value)
{
    setRaw(groupName, key, escapeValue(value, false));
}

void KeyFile::set_integer_list(const ustring& groupName, const ustring& key, const ArrayHandle<int>& value)
{
    std::string raw;
    for (const int v : static_cast<std::vector<int>>(value)) {
        char buf[32];
        const auto result = std::to_chars(buf, buf + sizeof(buf), v);
        raw.append(buf, result.ptr);
        raw += ';';
    }
    setRaw(groupName, key, std::move(raw));
}

void KeyFile::set_double_list(const ustring& groupName, const ustring& key, const ArrayHandle<double>& value)
{
    std::string raw;
    for (const double v : static_cast<std::vector<double>>(value)) {
        raw += formatDouble(v);
        raw += ';';
    }
    setRaw(groupName, key, std::move(raw));
}

void KeyFile::set_string_list(const ustring& groupName, const ustring& key, const ArrayHandle<ustring>& value)
{
    std::string raw;
    for (const ustring& v : static_cast<std::vector<ustring>>(value)) {
        raw += escapeValue(v, true);
        raw += ';';
    }
    setRaw(groupName, key, std::move(raw));
}

bool KeyFile::load_from_file(const std::string& filename)
{
    FILE* f = g_fopen(filename.c_str(), "rb");
    if (!f) {
        return false;
    }

    std::string content;
    char buffer[8192];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        content.append(buffer, n);
    }
    std::fclose(f);

    groups_.clear();
    std::string currentGroup;
    bool haveGroup = false;

    std::size_t pos = 0;
    while (pos <= content.size()) {
        std::size_t eol = content.find('\n', pos);
        const std::string lineRaw = (eol == std::string::npos) ? content.substr(pos) : content.substr(pos, eol - pos);
        pos = (eol == std::string::npos) ? content.size() + 1 : eol + 1;

        std::string line = lineRaw;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        // Only the leading indentation is stripped for classification;
        // trailing whitespace in a value is significant and GLib preserves
        // it, so the full line is never trimmed on the right.
        std::size_t firstNonSpace = 0;
        while (firstNonSpace < line.size() && std::isspace(static_cast<unsigned char>(line[firstNonSpace]))) {
            ++firstNonSpace;
        }
        const std::string lstripped = line.substr(firstNonSpace);

        if (lstripped.empty() || lstripped[0] == '#') {
            continue;
        }

        if (lstripped.front() == '[') {
            const std::string header = trim(lstripped);
            if (header.back() != ']') {
                groups_.clear();
                return false;
            }
            currentGroup = header.substr(1, header.size() - 2);
            getOrCreateGroup(currentGroup);
            haveGroup = true;
            continue;
        }

        const std::size_t eq = lstripped.find('=');
        if (eq == std::string::npos || !haveGroup) {
            // Malformed line (no '=' or a key before any [Group]) — matches
            // real GKeyFile's G_KEY_FILE_ERROR_PARSE.
            groups_.clear();
            return false;
        }

        const std::string key = trim(lstripped.substr(0, eq));
        std::string value = lstripped.substr(eq + 1);
        std::size_t valueStart = 0;
        while (valueStart < value.size() && std::isspace(static_cast<unsigned char>(value[valueStart]))) {
            ++valueStart;
        }
        value.erase(0, valueStart);
        setRaw(currentGroup, key, value);
    }

    return true;
}

bool KeyFile::save_to_file(const std::string& filename) const
{
    FILE* f = g_fopen(filename.c_str(), "wb");
    if (!f) {
        return false;
    }
    const std::string data = to_data();
    const std::size_t written = std::fwrite(data.data(), 1, data.size(), f);
    std::fclose(f);
    return written == data.size();
}

std::string KeyFile::to_data() const
{
    std::string out;
    bool first = true;
    for (const auto& group : groups_) {
        if (!first) {
            out += '\n';
        }
        first = false;
        out += '[';
        out += group.name;
        out += "]\n";
        for (const auto& entry : group.entries) {
            out += entry.key;
            out += '=';
            out += entry.rawValue;
            out += '\n';
        }
    }
    return out;
}

} // namespace Glib
