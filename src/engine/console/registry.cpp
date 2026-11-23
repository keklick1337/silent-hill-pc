// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/registry.h"

#include "engine/console/completion.h"
#include "engine/console/tokenizer.h"

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <set>

namespace sh::console {

namespace {

constexpr int kMaxAliasDepth = 8;

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

bool equals_ci(std::string_view a, std::string_view b) {
    return a.size() == b.size() && starts_with_ci(a, b);
}

bool valid_name(std::string_view name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (c == ' ' || c == '\t' || c == ';' || c == '"' || c == '\'' || static_cast<unsigned char>(c) < 0x20) {
            return false;
        }
    }
    return true;
}

bool parse_int(std::string_view text, long long& out) {
    std::string s(text);
    std::size_t i = 0;
    bool neg = false;
    if (i < s.size() && (s[i] == '+' || s[i] == '-')) neg = s[i++] == '-';
    int base = 10;
    if (i + 1 < s.size() && s[i] == '0' && (s[i + 1] == 'x' || s[i + 1] == 'X')) {
        base = 16;
        i += 2;
    }
    if (i >= s.size()) return false;
    for (std::size_t k = i; k < s.size(); ++k) {
        const char c = s[k];
        const bool digit = c >= '0' && c <= '9';
        const bool hex = base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if (!digit && !hex) return false;
    }
    errno = 0;
    const unsigned long long mag = std::strtoull(s.c_str() + i, nullptr, base);
    if (errno == ERANGE) return false;
    constexpr unsigned long long kLimit = 0x7fffffffffffffffULL;
    if (mag > kLimit + (neg ? 1 : 0)) return false;
    out = neg ? static_cast<long long>(0 - mag) : static_cast<long long>(mag);
    return true;
}

bool parse_float(std::string_view text, double& out) {
    if (text.empty()) return false;
    const std::string s(text);
    char* end = nullptr;
    errno = 0;
    const double v = std::strtod(s.c_str(), &end);
    if (end != s.c_str() + s.size()) return false;
    out = v;
    return true;
}

bool parse_bool(std::string_view text, bool& out) {
    const std::string s = lower(text);
    if (s == "1" || s == "on" || s == "true" || s == "yes") {
        out = true;
        return true;
    }
    if (s == "0" || s == "off" || s == "false" || s == "no") {
        out = false;
        return true;
    }
    return false;
}

const std::string kEmpty;

}  // namespace

const std::string& Args::str(std::size_t i) const { return i < words_.size() ? words_[i] : kEmpty; }

long long Args::integer(std::size_t i, long long fallback) const {
    long long v = 0;
    return i < words_.size() && parse_int(words_[i], v) ? v : fallback;
}

double Args::number(std::size_t i, double fallback) const {
    double v = 0;
    return i < words_.size() && parse_float(words_[i], v) ? v : fallback;
}

bool Args::boolean(std::size_t i, bool fallback) const {
    bool v = false;
    return i < words_.size() && parse_bool(words_[i], v) ? v : fallback;
}

std::string Args::joined(std::size_t i) const {
    std::string out;
    bool first = true;
    for (; i < words_.size(); ++i) {
        if (!first) out += ' ';
        first = false;
        out += words_[i];
    }
    return out;
}

std::string usage_of(const CommandSpec& spec) {
    std::string out = spec.name;
    for (const ArgSpec& a : spec.args) {
        out += ' ';
        out += a.optional ? '[' : '<';
        out += a.name;
        if (a.type == ArgType::Rest) out += "...";
        out += a.optional ? ']' : '>';
    }
    return out;
}

Result<void> Registry::add(CommandSpec spec) {
    spec.name = lower(spec.name);
    if (!valid_name(spec.name)) return Result<void>::fail("invalid command name '" + spec.name + "'");
    if (!spec.run) return Result<void>::fail("command '" + spec.name + "' has no run function");
    for (std::string& a : spec.aliases) {
        a = lower(a);
        if (!valid_name(a)) return Result<void>::fail("invalid alias '" + a + "' for command '" + spec.name + "'");
    }
    bool seen_optional = false;
    for (std::size_t i = 0; i < spec.args.size(); ++i) {
        const ArgSpec& a = spec.args[i];
        if (a.type == ArgType::Rest && i + 1 != spec.args.size()) {
            return Result<void>::fail("command '" + spec.name + "': a rest argument must be last");
        }
        if (a.optional) seen_optional = true;
        else if (seen_optional) {
            return Result<void>::fail("command '" + spec.name + "': a required argument follows an optional one");
        }
    }

    auto existing = commands_.find(spec.name);
    if (existing != commands_.end() && existing->second.owner != spec.owner) {
        return Result<void>::fail("command '" + spec.name + "' is already registered by another owner");
    }
    // A name clashes unless it belongs to the command being replaced.
    std::set<std::string> names{spec.name};
    for (const std::string& a : spec.aliases) names.insert(a);
    for (const std::string& n : names) {
        auto c = commands_.find(n);
        if (c != commands_.end() && c->first != spec.name) {
            return Result<void>::fail("name '" + n + "' clashes with command '" + c->first + "'");
        }
        auto al = alias_to_name_.find(n);
        if (al != alias_to_name_.end() && al->second != spec.name) {
            return Result<void>::fail("name '" + n + "' clashes with an alias of command '" + al->second + "'");
        }
    }

    if (existing != commands_.end()) {
        for (const std::string& a : existing->second.aliases) alias_to_name_.erase(a);
        commands_.erase(existing);
    }
    for (const std::string& a : spec.aliases) {
        if (a != spec.name) alias_to_name_[a] = spec.name;
    }
    commands_.emplace(spec.name, std::move(spec));
    return Result<void>::ok();
}

Result<void> Registry::add_alias(std::string name, std::string expansion) {
    name = lower(name);
    if (!valid_name(name)) return Result<void>::fail("invalid alias name '" + name + "'");
    if (expansion.find_first_not_of(" \t") == std::string::npos) {
        return Result<void>::fail("alias '" + name + "' has an empty expansion");
    }
    if (find(name)) return Result<void>::fail("'" + name + "' is a command and cannot be an alias");
    user_aliases_[name] = std::move(expansion);
    return Result<void>::ok();
}

void Registry::remove(std::string_view name) {
    const std::string key = lower(name);
    auto it = commands_.find(key);
    if (it != commands_.end()) {
        for (const std::string& a : it->second.aliases) alias_to_name_.erase(a);
        commands_.erase(it);
        return;
    }
    auto u = user_aliases_.find(key);
    if (u != user_aliases_.end()) user_aliases_.erase(u);
}

std::size_t Registry::remove_owner(std::string_view owner) {
    std::size_t removed = 0;
    for (auto it = commands_.begin(); it != commands_.end();) {
        if (it->second.owner == owner) {
            for (const std::string& a : it->second.aliases) alias_to_name_.erase(a);
            it = commands_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

const CommandSpec* Registry::find(std::string_view name) const {
    const std::string key = lower(name);
    auto it = commands_.find(key);
    if (it != commands_.end()) return &it->second;
    auto al = alias_to_name_.find(key);
    if (al != alias_to_name_.end()) {
        auto target = commands_.find(al->second);
        if (target != commands_.end()) return &target->second;
    }
    return nullptr;
}

std::vector<const CommandSpec*> Registry::commands() const {
    std::vector<const CommandSpec*> out;
    out.reserve(commands_.size());
    for (const auto& [name, spec] : commands_) out.push_back(&spec);
    return out;
}

std::vector<std::string> Registry::names() const {
    std::set<std::string> all;
    for (const auto& [name, spec] : commands_) all.insert(name);
    for (const auto& [name, target] : alias_to_name_) all.insert(name);
    for (const auto& [name, expansion] : user_aliases_) all.insert(name);
    return {all.begin(), all.end()};
}

const std::string* Registry::user_alias(std::string_view name) const {
    auto it = user_aliases_.find(lower(name));
    return it == user_aliases_.end() ? nullptr : &it->second;
}

Result<Args> Registry::parse_args(const CommandSpec& spec, const std::vector<std::string>& words) {
    const std::string usage = usage_of(spec);
    std::vector<std::string> out;
    std::size_t w = 0;
    for (const ArgSpec& a : spec.args) {
        if (w >= words.size()) {
            if (a.optional) break;
            return Result<Args>::fail("missing argument '" + a.name + "'; usage: " + usage);
        }
        if (a.type == ArgType::Rest) {
            std::string joined;
            for (; w < words.size(); ++w) {
                if (!joined.empty()) joined += ' ';
                joined += words[w];
            }
            out.push_back(std::move(joined));
            break;
        }
        const std::string& word = words[w++];
        const auto bad = [&](const char* expected) {
            return Result<Args>::fail("argument '" + a.name + "': expected " + expected + ", got '" + word + "'");
        };
        switch (a.type) {
            case ArgType::Int: {
                long long v;
                if (!parse_int(word, v)) return bad("integer");
                out.push_back(word);
                break;
            }
            case ArgType::Float: {
                double v;
                if (!parse_float(word, v)) return bad("number");
                out.push_back(word);
                break;
            }
            case ArgType::Bool: {
                bool v;
                if (!parse_bool(word, v)) return bad("0/1/on/off/true/false/yes/no");
                out.push_back(word);
                break;
            }
            case ArgType::Enum: {
                if (a.values.empty()) {
                    out.push_back(word);
                    break;
                }
                const std::string* hit = nullptr;
                for (const std::string& v : a.values) {
                    if (equals_ci(v, word)) {
                        hit = &v;
                        break;
                    }
                }
                if (!hit) {
                    std::string list;
                    for (const std::string& v : a.values) list += (list.empty() ? "" : ", ") + v;
                    return Result<Args>::fail("argument '" + a.name + "': expected one of " + list + ", got '" +
                                              word + "'");
                }
                out.push_back(*hit);
                break;
            }
            default:
                out.push_back(word);
                break;
        }
    }
    if (w < words.size()) {
        return Result<Args>::fail("too many arguments; usage: " + usage);
    }
    return Result<Args>::ok(Args(std::move(out)));
}

bool Registry::execute(std::string_view line, Output& out) {
    for (std::string_view command : split_commands(line)) {
        const auto tokens = tokenize(command);
        if (tokens.empty()) continue;
        const std::string& name = tokens[0].text;

        if (const CommandSpec* spec = find(name)) {
            std::vector<std::string> words;
            for (std::size_t i = 1; i < tokens.size(); ++i) words.push_back(tokens[i].text);
            auto args = parse_args(*spec, words);
            if (!args) {
                out.error(args.error());
                return false;
            }
            // Copy: the command may add or remove commands (scripts reloading) while it runs.
            const RunFn run = spec->run;
            Context ctx{out, *this};
            auto result = run(ctx, args.value());
            if (!result) {
                out.error(result.error());
                return false;
            }
            continue;
        }
        if (const std::string* expansion = user_alias(name)) {
            if (depth_ >= kMaxAliasDepth) {
                out.error("alias '" + lower(name) + "': expansion too deep (alias loop?)");
                return false;
            }
            std::string expanded = *expansion;
            const std::string_view rest = command.substr(tokens[0].end);
            expanded.append(rest);
            ++depth_;
            const bool ok = execute(expanded, out);
            --depth_;
            if (!ok) return false;
            continue;
        }
        out.error("unknown command '" + name + "'");
        return false;
    }
    return true;
}

void register_builtin_commands(Registry& registry) {
    CommandSpec help;
    help.name = "help";
    help.aliases = {"?"};
    help.help = "list commands or describe one";
    help.group = "console";
    help.args = {{"command", ArgType::String, true, "command to describe", {},
                  [&registry](std::string_view, std::vector<Candidate>& out) {
                      for (const CommandSpec* c : registry.commands()) out.push_back({c->name, c->help});
                  }}};
    help.run = [](Context& ctx, const Args& args) -> Result<void> {
        if (args.has(0)) {
            const CommandSpec* spec = ctx.registry.find(args.str(0));
            if (!spec) return Result<void>::fail("unknown command '" + args.str(0) + "'");
            ctx.out.print(Level::Help, "usage: " + usage_of(*spec));
            if (!spec->help.empty()) ctx.out.print(Level::Plain, spec->help);
            std::string_view details = spec->details;
            while (!details.empty()) {
                const std::size_t nl = details.find('\n');
                ctx.out.print(Level::Plain, details.substr(0, nl));
                details = nl == std::string_view::npos ? std::string_view() : details.substr(nl + 1);
            }
            if (!spec->aliases.empty()) {
                std::string list;
                for (const std::string& a : spec->aliases) list += (list.empty() ? "" : ", ") + a;
                ctx.out.print(Level::Plain, "aliases: " + list);
            }
            return Result<void>::ok();
        }
        std::map<std::string, std::vector<const CommandSpec*>> groups;
        for (const CommandSpec* c : ctx.registry.commands()) {
            groups[c->group.empty() ? "other" : c->group].push_back(c);
        }
        // "other" goes last so named groups read first.
        auto print_group = [&](const std::string& name, const std::vector<const CommandSpec*>& list) {
            ctx.out.print(Level::Help, name + ":");
            for (const CommandSpec* c : list) ctx.out.print(Level::Plain, "  " + c->name + " - " + c->help);
        };
        for (const auto& [name, list] : groups) {
            if (name != "other") print_group(name, list);
        }
        auto other = groups.find("other");
        if (other != groups.end()) print_group(other->first, other->second);
        return Result<void>::ok();
    };
    registry.add(std::move(help));

    CommandSpec alias;
    alias.name = "alias";
    alias.help = "show or define a command alias";
    alias.details = "alias            list aliases\nalias <name>     show one\nalias <name> <command...>  define";
    alias.group = "console";
    alias.args = {{"name", ArgType::String, true, "alias name", {}, {}},
                  {"expansion", ArgType::Rest, true, "command line to run", {}, {}}};
    alias.run = [](Context& ctx, const Args& args) -> Result<void> {
        if (!args.has(0)) {
            bool any = false;
            for (const std::string& n : ctx.registry.names()) {
                if (const std::string* e = ctx.registry.user_alias(n)) {
                    ctx.out.print(Level::Plain, n + " = " + *e);
                    any = true;
                }
            }
            if (!any) ctx.out.print(Level::Plain, "no aliases defined");
            return Result<void>::ok();
        }
        if (!args.has(1)) {
            const std::string* e = ctx.registry.user_alias(args.str(0));
            if (!e) return Result<void>::fail("no alias '" + args.str(0) + "'");
            ctx.out.print(Level::Plain, args.str(0) + " = " + *e);
            return Result<void>::ok();
        }
        return ctx.registry.add_alias(args.str(0), args.str(1));
    };
    registry.add(std::move(alias));

    CommandSpec unalias;
    unalias.name = "unalias";
    unalias.help = "remove a command alias";
    unalias.group = "console";
    unalias.args = {{"name", ArgType::String, false, "alias to remove", {},
                    [&registry](std::string_view, std::vector<Candidate>& out) {
                        for (const std::string& n : registry.names()) {
                            if (const std::string* e = registry.user_alias(n)) out.push_back({n, *e});
                        }
                    }}};
    unalias.run = [](Context& ctx, const Args& args) -> Result<void> {
        if (!ctx.registry.user_alias(args.str(0))) return Result<void>::fail("no alias '" + args.str(0) + "'");
        ctx.registry.remove(args.str(0));
        return Result<void>::ok();
    };
    registry.add(std::move(unalias));

    CommandSpec echo;
    echo.name = "echo";
    echo.help = "print text";
    echo.group = "console";
    echo.args = {{"text", ArgType::Rest, true, "text to print", {}, {}}};
    echo.run = [](Context& ctx, const Args& args) -> Result<void> {
        ctx.out.print(Level::Plain, args.str(0));
        return Result<void>::ok();
    };
    registry.add(std::move(echo));
}

}  // namespace sh::console
