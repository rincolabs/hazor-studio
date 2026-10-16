#pragma once
// rtcompat/glib/keyfile.hpp
//
// Stand-in for <glibmm/keyfile.h>: this is the PP3 codec. rtengine's
// procparams.cc and dynamicprofile.cc use Glib::KeyFile as a thin,
// typed wrapper over the GKeyFile/.desktop-file text format (groups,
// `key=value` lines, `;`-separated lists). The highest-risk piece of the
// whole compatibility layer — see docs/RAW-ENGINE.md — because a
// formatting mismatch corrupts PP3 files silently.
//
// Correctness invariants this implementation preserves:
//   - Numbers are formatted/parsed in the "C" locale regardless of the
//     process's global locale (std::to_chars/from_chars, which the
//     standard guarantees are always locale-independent) — a PP3 saved
//     under pt_BR.UTF-8 must use '.' as the decimal separator, exactly
//     like upstream's g_ascii_dtostr.
//   - Doubles round-trip losslessly: 17 significant digits (matches
//     GLib's g_ascii_dtostr(), which is `%.17g` in the C locale).
//   - List values are serialized with a trailing separator after every
//     item, including the last — matches the existing vendored .pp3
//     files under 3rdparty/rawengine/rtdata/profiles/ byte-for-byte.
//   - get_*() on a missing group/key throws Glib::KeyFileError, matching
//     real GKeyFile semantics that rtengine's call sites rely on (either
//     caught, or guarded by a prior has_group()/has_key() check).
#include <string>
#include <vector>

#include "rtcompat/glib/misc.hpp"
#include "rtcompat/glib/ustring.hpp"

namespace Glib {

class KeyFile {
public:
    bool has_group(const ustring& groupName) const;
    bool has_key(const ustring& groupName, const ustring& key) const;

    bool get_boolean(const ustring& groupName, const ustring& key) const;
    int get_integer(const ustring& groupName, const ustring& key) const;
    double get_double(const ustring& groupName, const ustring& key) const;
    ustring get_string(const ustring& groupName, const ustring& key) const;
    std::vector<int> get_integer_list(const ustring& groupName, const ustring& key) const;
    std::vector<double> get_double_list(const ustring& groupName, const ustring& key) const;
    std::vector<ustring> get_string_list(const ustring& groupName, const ustring& key) const;

    std::vector<ustring> get_keys(const ustring& groupName) const;
    std::vector<ustring> get_groups() const;

    void set_boolean(const ustring& groupName, const ustring& key, bool value);
    void set_integer(const ustring& groupName, const ustring& key, int value);
    void set_double(const ustring& groupName, const ustring& key, double value);
    void set_string(const ustring& groupName, const ustring& key, const ustring& value);
    void set_integer_list(const ustring& groupName, const ustring& key, const ArrayHandle<int>& value);
    void set_double_list(const ustring& groupName, const ustring& key, const ArrayHandle<double>& value);
    void set_string_list(const ustring& groupName, const ustring& key, const ArrayHandle<ustring>& value);

    // Returns false (never throws) if `filename` cannot be read or the
    // content is malformed — matches real glibmm's bool-returning
    // load_from_file() overload, which is the one every call site here
    // uses.
    bool load_from_file(const std::string& filename);
    bool save_to_file(const std::string& filename) const;
    std::string to_data() const;

private:
    struct Entry {
        std::string key;
        std::string rawValue;
    };
    struct Group {
        std::string name;
        std::vector<Entry> entries;
    };

    Group* findGroup(const std::string& name);
    const Group* findGroup(const std::string& name) const;
    Group& getOrCreateGroup(const std::string& name);
    const Entry* findEntry(const std::string& groupName, const std::string& key) const;
    const std::string& getRaw(const std::string& groupName, const std::string& key) const;
    void setRaw(const std::string& groupName, const std::string& key, std::string value);

    std::vector<Group> groups_;
};

} // namespace Glib
