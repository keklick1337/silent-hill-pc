// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/completion.h"

#include "engine/console/tokenizer.h"

#include <algorithm>
#include <set>

namespace sh::console {

namespace {

char lower_char(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = lower_char(c);
    return out;
}

// Filters by prefix/substring (case-insensitive), dedupes by text, ranks prefix matches before substring
// matches, each group alphabetical.
void rank(std::vector<Candidate> raw, std::string_view prefix, Completion& out) {
    std::vector<Candidate> prefixed, inner;
    std::set<std::string> seen;
    for (Candidate& c : raw) {
        const bool starts = starts_with_ci(c.text, prefix);
        if (!starts && !contains_ci(c.text, prefix)) continue;
        if (!seen.insert(lower(c.text)).second) continue;
        (starts ? prefixed : inner).push_back(std::move(c));
    }
    const auto by_text = [](const Candidate& a, const Candidate& b) {
        const std::string la = lower(a.text), lb = lower(b.text);
        return la != lb ? la < lb : a.text < b.text;
    };
    std::sort(prefixed.begin(), prefixed.end(), by_text);
    std::sort(inner.begin(), inner.end(), by_text);
    out.items = std::move(prefixed);
    for (Candidate& c : inner) out.items.push_back(std::move(c));
    out.total = out.items.size();
    if (out.items.size() > kMaxCandidates) out.items.resize(kMaxCandidates);
}

}  // namespace

bool starts_with_ci(std::string_view text, std::string_view prefix) {
    if (prefix.size() > text.size()) return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        if (lower_char(text[i]) != lower_char(prefix[i])) return false;
    }
    return true;
}

bool contains_ci(std::string_view text, std::string_view needle) {
    if (needle.empty()) return true;
    if (needle.size() > text.size()) return false;
    for (std::size_t i = 0; i + needle.size() <= text.size(); ++i) {
        if (starts_with_ci(text.substr(i), needle)) return true;
    }
    return false;
}

Completion complete(const Registry& registry, std::string_view line, std::size_t caret) {
    if (caret > line.size()) caret = line.size();
    Completion out;
    const Cursor cur = cursor_at(line, caret);
    out.replace_begin = cur.replace_begin;
    out.replace_end = cur.replace_end;
    out.prefix = cur.prefix;
    out.word_index = cur.index;

    const Span seg = command_span_at(line, caret);
    const auto tokens = tokenize(line.substr(seg.begin, seg.end - seg.begin));

    std::vector<Candidate> raw;
    const CommandSpec* spec = tokens.empty() ? nullptr : registry.find(tokens[0].text);

    if (spec) {
        out.usage = usage_of(*spec);
        out.command_help = spec->help;
    }

    if (cur.index == 0) {
        for (const CommandSpec* c : registry.commands()) {
            raw.push_back({c->name, c->help});
            for (const std::string& a : c->aliases) raw.push_back({a, "alias of " + c->name});
        }
        for (const std::string& n : registry.names()) {
            if (const std::string* e = registry.user_alias(n)) raw.push_back({n, "alias: " + *e});
        }
        rank(std::move(raw), cur.prefix, out);
        if (!tokens.empty() && !tokens[0].text.empty() && !spec && out.total == 0 &&
            !registry.user_alias(tokens[0].text)) {
            out.problem = "unknown command '" + tokens[0].text + "'";
        }
        return out;
    }

    if (!spec) {
        if (!tokens.empty() && !registry.user_alias(tokens[0].text)) {
            out.problem = "unknown command '" + tokens[0].text + "'";
        }
        return out;
    }

    const std::size_t arg_index = cur.index - 1;
    const ArgSpec* arg = nullptr;
    if (arg_index < spec->args.size()) {
        arg = &spec->args[arg_index];
        out.active_arg = static_cast<int>(arg_index);
    } else if (!spec->args.empty() && spec->args.back().type == ArgType::Rest) {
        arg = &spec->args.back();
        out.active_arg = static_cast<int>(spec->args.size() - 1);
    } else {
        out.problem = "too many arguments";
        return out;
    }
    out.arg_help = arg->help;

    for (const std::string& v : arg->values) raw.push_back({v, {}});
    if (arg->type == ArgType::Bool && arg->values.empty()) {
        for (const char* v : {"on", "off", "0", "1"}) raw.push_back({v, {}});
    }
    if (arg->complete) arg->complete(cur.prefix, raw);
    rank(std::move(raw), cur.prefix, out);
    return out;
}

Applied apply(const Registry& registry, std::string_view line, const Completion& completion,
              const Candidate& candidate) {
    std::size_t rb = std::min(completion.replace_begin, line.size());
    std::size_t re = std::min(std::max(completion.replace_end, rb), line.size());

    std::string text;
    if (rb < line.size() && re > rb && (line[rb] == '"' || line[rb] == '\'')) {
        // The user opened a quote: keep its style and close it.
        const char q = line[rb];
        text += q;
        for (char c : candidate.text) {
            if (c == q || c == '\\') text += '\\';
            text += c;
        }
        text += q;
    } else {
        text = quote_if_needed(candidate.text);
    }

    bool space = true;
    if (completion.word_index > 0) {
        const Span seg = command_span_at(line, rb);
        const auto tokens = tokenize(line.substr(seg.begin, seg.end - seg.begin));
        const CommandSpec* spec = tokens.empty() ? nullptr : registry.find(tokens[0].text);
        if (spec) {
            const std::size_t a = completion.word_index - 1;
            const bool rest = !spec->args.empty() && spec->args.back().type == ArgType::Rest && a + 1 >= spec->args.size();
            space = !rest && a + 1 < spec->args.size();
        } else {
            space = false;
        }
    }

    Applied out;
    out.line.assign(line.substr(0, rb));
    out.line += text;
    out.caret = out.line.size();
    std::string_view tail = line.substr(re);
    if (space) {
        // Reuse an existing separator instead of doubling it.
        if (!tail.empty() && (tail.front() == ' ' || tail.front() == '\t')) {
            out.line += tail.front();
            tail.remove_prefix(1);
        } else {
            out.line += ' ';
        }
        out.caret = out.line.size();
    }
    out.line.append(tail);
    return out;
}

std::string common_prefix(const std::vector<Candidate>& items) {
    if (items.empty()) return {};
    std::string_view first = items[0].text;
    std::size_t n = first.size();
    for (std::size_t i = 1; i < items.size() && n > 0; ++i) {
        std::size_t k = 0;
        const std::string& t = items[i].text;
        while (k < n && k < t.size() && lower_char(first[k]) == lower_char(t[k])) ++k;
        n = k;
    }
    return std::string(first.substr(0, n));
}

}  // namespace sh::console
