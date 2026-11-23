#include "engine/mods/json_merge.h"

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>

namespace sh::mods {

using json::Value;

const std::string* MergeRules::key_for(std::string_view path) const {
    for (const auto& [p, key] : array_keys) {
        if (p == path) {
            return &key;
        }
    }
    return nullptr;
}

std::string MergeStats::describe() const {
    return "modified " + std::to_string(modified) + ", added " + std::to_string(added) + ", removed " +
           std::to_string(removed);
}

namespace {

bool is_directive(std::string_view key) {
    return !key.empty() && key.front() == '$';
}

bool known_directive(std::string_view key) {
    static constexpr std::string_view kKnown[] = {"$replace", "$remove", "$after",  "$before",
                                                  "$append",  "$prepend", "$keys"};
    return std::find(std::begin(kKnown), std::end(kKnown), key) != std::end(kKnown);
}

bool flag(const Value& object, std::string_view key) {
    const Value* v = object.find(key);
    return v != nullptr && v->is_bool() && v->boolean();
}

std::string label(std::string_view path) {
    return path.empty() ? std::string("(root)") : std::string(path);
}

std::string key_text(const Value& v) {
    return v.is_string() ? json::quote(v.string()) : json::write(v);
}

class Merger {
public:
    Merger(const MergeRules& rules, MergeStats& stats, std::vector<std::string>& warnings)
        : rules_(rules), stats_(stats), warnings_(warnings) {}

    bool value(Value& base, const Value& patch, const std::string& path, bool counting) {
        if (patch.is_object()) {
            if (flag(patch, "$replace")) {
                return replace(base, patch, counting);
            }
            if (base.is_array() &&
                (patch.find("$append") || patch.find("$prepend") || patch.find("$remove"))) {
                return array_ops(base, patch, path, counting);
            }
            if (base.is_object()) {
                return object(base, patch, path, counting);
            }
            return replace(base, patch, counting);
        }
        if (patch.is_array() && base.is_array()) {
            return array(base, patch, path, counting);
        }
        return replace(base, patch, counting);
    }

    bool object(Value& base, const Value& patch, const std::string& path, bool counting) {
        bool changed = false;
        for (const Value::Member& m : patch.members()) {
            if (is_directive(m.first)) {
                if (!known_directive(m.first)) {
                    warnings_.push_back(label(path) + ": unknown directive \"" + m.first + "\" ignored");
                }
                continue;
            }
            const std::string child = path.empty() ? m.first : path + "." + m.first;
            if (rules_.drop_stale_twins && !m.second.is_null()) {
                drop_twin(base, patch, m.first);
            }
            if (m.second.is_null()) {
                if (base.erase(m.first)) {
                    changed = true;
                    stats_.removed += counting ? 1 : 0;
                }
                continue;
            }
            if (Value* existing = base.find_mut(m.first)) {
                changed |= value(*existing, m.second, child, counting);
                continue;
            }
            base.set(m.first, strip_directives(m.second));
            changed = true;
            stats_.added += counting ? 1 : 0;
        }
        return changed;
    }

private:
    // "speed" and "speed_q12" are one value written twice (readers prefer the readable one): a patch that sets
    // one of them must not leave the base's old copy of the other behind.
    static std::string_view twin_stem(std::string_view key) {
        const std::size_t q = key.rfind("_q");
        if (q == std::string_view::npos || q + 2 >= key.size() || q == 0) {
            return {};
        }
        for (std::size_t i = q + 2; i < key.size(); ++i) {
            if (key[i] < '0' || key[i] > '9') {
                return {};
            }
        }
        return key.substr(0, q);
    }

    static void drop_twin(Value& base, const Value& patch, const std::string& key) {
        if (!base.is_object()) {
            return;
        }
        if (const std::string_view stem = twin_stem(key); !stem.empty()) {
            if (patch.find(stem) == nullptr) {
                base.erase(stem);
            }
            return;
        }
        std::vector<std::string> twins;
        for (const Value::Member& m : base.members()) {
            if (twin_stem(m.first) == key && patch.find(m.first) == nullptr) {
                twins.push_back(m.first);
            }
        }
        for (const std::string& t : twins) {
            base.erase(t);
        }
    }

    bool replace(Value& base, const Value& patch, bool counting) {
        Value next = strip_directives(patch);
        if (next == base) {
            return false;
        }
        base = std::move(next);
        stats_.modified += counting ? 1 : 0;
        return true;
    }

    bool array_ops(Value& base, const Value& patch, const std::string& path, bool counting) {
        std::vector<Value>& items = base.array_mut();
        bool changed = false;
        if (const Value* remove = patch.find("$remove")) {
            if (!remove->is_array()) {
                warnings_.push_back(label(path) + ": \"$remove\" must be an array of values");
            } else {
                for (const Value& gone : remove->array()) {
                    const std::size_t before = items.size();
                    std::erase(items, gone);
                    const int n = static_cast<int>(before - items.size());
                    if (n == 0) {
                        warnings_.push_back(label(path) + ": \"$remove\": no " + json::write(gone) + " to remove");
                    }
                    stats_.removed += counting ? n : 0;
                    changed |= n > 0;
                }
            }
        }
        auto add = [&](const char* directive, bool front) {
            const Value* extra = patch.find(directive);
            if (extra == nullptr) {
                return;
            }
            if (!extra->is_array()) {
                warnings_.push_back(label(path) + ": \"" + directive + "\" must be an array");
                return;
            }
            std::vector<Value> stripped;
            for (const Value& e : extra->array()) {
                stripped.push_back(strip_directives(e));
            }
            items.insert(front ? items.begin() : items.end(), stripped.begin(), stripped.end());
            stats_.added += counting ? static_cast<int>(stripped.size()) : 0;
            changed |= !stripped.empty();
        };
        add("$prepend", true);
        add("$append", false);
        for (const Value::Member& m : patch.members()) {
            if (m.first != "$remove" && m.first != "$append" && m.first != "$prepend") {
                warnings_.push_back(label(path) + ": \"" + m.first + "\" ignored next to \"$append\"/\"$remove\"");
            }
        }
        return changed;
    }

    using Fields = std::vector<std::string>;

    static Fields split_key(const std::string& key) {
        Fields fields;
        std::size_t start = 0;
        while (start <= key.size()) {
            const std::size_t comma = std::min(key.find(',', start), key.size());
            if (comma > start) {
                fields.push_back(key.substr(start, comma - start));
            }
            start = comma + 1;
        }
        return fields;
    }

    // The fields the entries of the array at `path` are matched by, or nothing (the array is replaced).
    std::optional<Fields> array_key(const Value& patch, const std::string& path) const {
        if (const std::string* key = rules_.key_for(path)) {
            return split_key(*key);
        }
        if (patch.array().empty()) {
            return std::nullopt;
        }
        for (const std::string& candidate : rules_.key_candidates) {
            const bool everywhere = std::all_of(patch.array().begin(), patch.array().end(), [&](const Value& e) {
                const Value* k = e.find(candidate);
                return k != nullptr && (k->is_string() || k->is_integer());
            });
            if (everywhere) {
                return Fields{candidate};
            }
        }
        return std::nullopt;
    }

    // The key of `entry` (for a single field, its value; for a compound key, an object of the fields), or
    // nothing when a field is missing.
    static std::optional<Value> key_of(const Value& entry, const Fields& fields) {
        if (fields.size() == 1) {
            const Value* v = entry.find(fields.front());
            return v != nullptr ? std::optional<Value>(*v) : std::nullopt;
        }
        Value out = Value::empty_object();
        for (const std::string& f : fields) {
            const Value* v = entry.find(f);
            if (v == nullptr) {
                return std::nullopt;
            }
            out.set(f, *v);
        }
        return out;
    }

    static std::ptrdiff_t index_of(const std::vector<Value>& items, const Fields& fields, const Value& wanted) {
        for (std::size_t i = 0; i < items.size(); ++i) {
            const std::optional<Value> k = key_of(items[i], fields);
            if (k && *k == wanted) {
                return static_cast<std::ptrdiff_t>(i);
            }
        }
        return -1;
    }

    static std::string fields_text(const Fields& fields) {
        std::string out;
        for (const std::string& f : fields) {
            out += (out.empty() ? "" : ",") + f;
        }
        return out;
    }

    // Where "$after" / "$before" put an entry; the end when neither is given or the anchor is missing.
    std::size_t position(const std::vector<Value>& items, const Fields& fields, const Value& entry,
                         const std::string& path) {
        for (const char* directive : {"$after", "$before"}) {
            const Value* anchor = entry.find(directive);
            if (anchor == nullptr) {
                continue;
            }
            const std::optional<Value> wanted =
                fields.size() == 1 ? std::optional<Value>(*anchor) : key_of(*anchor, fields);
            const std::ptrdiff_t at = wanted ? index_of(items, fields, *wanted) : -1;
            if (at < 0) {
                warnings_.push_back(label(path) + ": \"" + directive + "\": no entry " + fields_text(fields) +
                                    " = " + key_text(*anchor) + "; put at the end");
                return items.size();
            }
            return static_cast<std::size_t>(at) + (directive[1] == 'a' ? 1 : 0);
        }
        return items.size();
    }

    bool array(Value& base, const Value& patch, const std::string& path, bool counting) {
        const std::optional<Fields> key = array_key(patch, path);
        const bool all_objects = std::all_of(patch.array().begin(), patch.array().end(),
                                             [](const Value& e) { return e.is_object(); });
        if (!key || key->empty() || !all_objects) {
            return replace(base, patch, counting);
        }
        std::vector<Value>& items = base.array_mut();
        const std::string entry_path = path + "[]";
        bool changed = false;
        for (const Value& entry : patch.array()) {
            const std::optional<Value> id = key_of(entry, *key);
            const std::ptrdiff_t at = id ? index_of(items, *key, *id) : -1;
            if (flag(entry, "$remove")) {
                if (at < 0) {
                    warnings_.push_back(label(path) + ": \"$remove\": no entry " + fields_text(*key) + " = " +
                                        (id ? key_text(*id) : std::string("(none)")));
                    continue;
                }
                items.erase(items.begin() + at);
                stats_.removed += counting ? 1 : 0;
                changed = true;
                continue;
            }
            if (at < 0) {
                const std::size_t where = position(items, *key, entry, path);
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(where), strip_directives(entry));
                stats_.added += counting ? 1 : 0;
                changed = true;
                continue;
            }
            bool entry_changed = value(items[static_cast<std::size_t>(at)], entry, entry_path, false);
            if (entry.find("$after") || entry.find("$before")) {
                Value moved = std::move(items[static_cast<std::size_t>(at)]);
                items.erase(items.begin() + at);
                const std::size_t where = position(items, *key, entry, path);
                items.insert(items.begin() + static_cast<std::ptrdiff_t>(where), std::move(moved));
                entry_changed |= where != static_cast<std::size_t>(at);
            }
            stats_.modified += counting && entry_changed ? 1 : 0;
            changed |= entry_changed;
        }
        return changed;
    }

    const MergeRules& rules_;
    MergeStats& stats_;
    std::vector<std::string>& warnings_;
};

}  // namespace

void merge_patch(Value& base, const Value& patch, const MergeRules& rules, MergeStats& stats,
                 std::vector<std::string>& warnings) {
    MergeRules local;
    const MergeRules* use = &rules;
    if (const Value* keys = patch.find("$keys")) {
        if (!keys->is_object()) {
            warnings.push_back("\"$keys\" must be an object {\"path\": \"field\"}");
        } else {
            local = rules;
            for (const Value::Member& m : keys->members()) {
                if (m.second.is_string()) {
                    local.array_keys.insert(local.array_keys.begin(), {m.first, m.second.string()});
                } else {
                    warnings.push_back("\"$keys\": \"" + m.first + "\" must name a field");
                }
            }
            use = &local;
        }
    }
    Merger merger(*use, stats, warnings);
    if (patch.is_object() && base.is_object()) {
        // The file-level "$replace" is the caller's decision; here it is only a directive to skip.
        merger.object(base, patch, "", true);
    } else {
        merger.value(base, patch, "", true);
    }
}

bool wants_replace(const Value& patch) {
    return flag(patch, "$replace");
}

Value strip_directives(const Value& value) {
    if (value.is_object()) {
        Value out = Value::empty_object();
        for (const Value::Member& m : value.members()) {
            if (!is_directive(m.first)) {
                out.members_mut().emplace_back(m.first, strip_directives(m.second));
            }
        }
        return out;
    }
    if (value.is_array()) {
        Value out = Value::empty_array();
        for (const Value& e : value.array()) {
            out.array_mut().push_back(strip_directives(e));
        }
        return out;
    }
    return value;
}

}  // namespace sh::mods
