// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)
//
// Identifiers of game things (items, weapons, characters) as names (section
// "Identifiers"): "<namespace>:<Name>", the base game is namespace "silenthill", a mod's
// namespace is its folder name. Data and scripts may leave out the namespace of the base game
// ("HealthDrink" == "silenthill:HealthDrink"). Numbers are an engine detail handed out at run
// time and stored nowhere; the save format and the data only ever carry names.

#pragma once

#include <string>
#include <string_view>

namespace sh::ids {

inline constexpr std::string_view kBaseNamespace = "silenthill";

// "mymod:Crowbar" -> "mymod"; "" without a namespace.
inline std::string_view namespace_of(std::string_view id) {
    const auto colon = id.find(':');
    return colon == std::string_view::npos ? std::string_view() : id.substr(0, colon);
}

// "mymod:Crowbar" -> "Crowbar"; a short name is returned as it is.
inline std::string_view local_name(std::string_view id) {
    const auto colon = id.find(':');
    return colon == std::string_view::npos ? id : id.substr(colon + 1);
}

// The full id of `name` in `default_namespace` unless it already names one.
inline std::string qualify(std::string_view name, std::string_view default_namespace = kBaseNamespace) {
    if (name.find(':') != std::string_view::npos) {
        return std::string(name);
    }
    std::string out(default_namespace);
    out += ':';
    out += name;
    return out;
}

inline bool is_base(std::string_view id) { return namespace_of(id) == kBaseNamespace; }

// The shortest spelling: base ids without their namespace, others whole.
inline std::string_view display(std::string_view id) { return is_base(id) ? local_name(id) : id; }

// A local name is a PascalCase-friendly identifier: letters, digits and '_', not empty, no
// leading digit. Namespaces (mod folder names) also allow '-' and '.'.
inline bool valid_local_name(std::string_view name) {
    if (name.empty() || (name[0] >= '0' && name[0] <= '9')) {
        return false;
    }
    for (const char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

inline bool valid_namespace(std::string_view ns) {
    if (ns.empty()) {
        return false;
    }
    for (const char c : ns) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '-' || c == '.';
        if (!ok) {
            return false;
        }
    }
    return true;
}

inline bool valid_id(std::string_view id) {
    const auto colon = id.find(':');
    if (colon == std::string_view::npos) {
        return valid_local_name(id);
    }
    return valid_namespace(id.substr(0, colon)) && valid_local_name(id.substr(colon + 1));
}

}  // namespace sh::ids
