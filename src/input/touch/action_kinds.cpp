// SPDX-License-Identifier: GPL-3.0-or-later
#include "input/touch/action_kinds.h"

#include "formats/common/json.h"

#include <array>

namespace sh::input::touch {
namespace {

constexpr std::array<std::pair<std::string_view, ActionKind>, 6> kNames = {{
    {"act", ActionKind::Act},
    {"take", ActionKind::Take},
    {"read", ActionKind::Read},
    {"open", ActionKind::Open},
    {"use", ActionKind::Use},
    {"check", ActionKind::Check},
}};

}  // namespace

ActionKind action_kind_by_name(std::string_view name, bool* known) {
    for (const auto& [n, k] : kNames) {
        if (n == name) {
            if (known) *known = true;
            return k;
        }
    }
    if (known) *known = false;
    return ActionKind::Act;
}

bool glob_match(std::string_view pattern, std::string_view text) {
    std::size_t p = 0, t = 0, star = std::string_view::npos, resume = 0;
    while (t < text.size()) {
        if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            resume = t;
        } else if (p < pattern.size() && pattern[p] == text[t]) {
            ++p;
            ++t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++resume;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

Result<ActionKinds> ActionKinds::parse(std::string_view json) {
    Result<sh::json::Value> doc = sh::json::parse(json);
    if (!doc) return Result<ActionKinds>::fail(doc.error());
    ActionKinds out;
    const auto kind_of = [](const sh::json::Value& v, std::string& error) {
        bool known = false;
        const ActionKind k = v.is_string() ? action_kind_by_name(v.string(), &known) : ActionKind::Act;
        if (!known) error = "line " + std::to_string(v.line()) + ": not a kind of action";
        return k;
    };
    std::string error;
    if (const sh::json::Value* states = doc.value().find("states"); states != nullptr && states->is_object()) {
        for (const auto& [name, value] : states->members()) out.states.emplace_back(name, kind_of(value, error));
    }
    if (const sh::json::Value* rules = doc.value().find("handlers"); rules != nullptr && rules->is_array()) {
        for (const sh::json::Value& rule : rules->array()) {
            const sh::json::Value* match = rule.find("match");
            const sh::json::Value* kind = rule.find("kind");
            if (match == nullptr || !match->is_string() || kind == nullptr) {
                error = "line " + std::to_string(rule.line()) + ": a rule needs \"match\" and \"kind\"";
                continue;
            }
            out.handlers.push_back({match->string(), kind_of(*kind, error)});
        }
    }
    if (!error.empty()) return Result<ActionKinds>::fail(error);
    return Result<ActionKinds>::ok(std::move(out));
}

ActionKind ActionKinds::classify(std::string_view state, std::string_view handler) const {
    if (!handler.empty()) {
        for (const Rule& rule : handlers) {
            if (glob_match(rule.match, handler)) return rule.kind;
        }
    }
    for (const auto& [name, kind] : states) {
        if (name == state) return kind;
    }
    return ActionKind::Act;
}

}  // namespace sh::input::touch
