// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/console/tokenizer.h"

#include <utility>

namespace sh::console {

namespace {

bool is_space(char c) { return c == ' ' || c == '\t'; }
bool is_quote(char c) { return c == '"' || c == '\''; }

// Raw ';'-separated segments. A quote opens only at the start of a word, so an apostrophe inside a word
// ("don't") stays literal, which matters for `echo` and for typing prose.
std::vector<Span> scan_segments(std::string_view line) {
    std::vector<Span> out;
    std::size_t begin = 0;
    char quote = 0;
    bool word_start = true;
    for (std::size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (quote) {
            if (c == '\\' && i + 1 < line.size()) {
                ++i;
            } else if (c == quote) {
                quote = 0;
                word_start = true;
            }
            continue;
        }
        if (c == ';') {
            out.push_back({begin, i});
            begin = i + 1;
            word_start = true;
        } else if (is_space(c)) {
            word_start = true;
        } else if (word_start && is_quote(c)) {
            quote = c;
            word_start = false;
        } else {
            word_start = false;
        }
    }
    out.push_back({begin, line.size()});
    return out;
}

}  // namespace

std::vector<Token> tokenize(std::string_view line) {
    std::vector<Token> out;
    std::size_t i = 0;
    const std::size_t n = line.size();
    while (true) {
        while (i < n && is_space(line[i])) ++i;
        if (i >= n) break;
        Token t;
        t.begin = i;
        if (is_quote(line[i])) {
            const char q = line[i++];
            t.quoted = true;
            t.closed = false;
            while (i < n) {
                char c = line[i];
                if (c == '\\' && i + 1 < n) {
                    t.text += line[i + 1];
                    i += 2;
                } else if (c == q) {
                    t.closed = true;
                    ++i;
                    break;
                } else {
                    t.text += c;
                    ++i;
                }
            }
        } else {
            while (i < n && !is_space(line[i])) t.text += line[i++];
        }
        t.end = i;
        out.push_back(std::move(t));
    }
    return out;
}

std::vector<std::string_view> split_commands(std::string_view line) {
    std::vector<std::string_view> out;
    for (const Span& s : scan_segments(line)) {
        std::size_t b = s.begin, e = s.end;
        while (b < e && is_space(line[b])) ++b;
        while (e > b && is_space(line[e - 1])) --e;
        if (b < e) out.push_back(line.substr(b, e - b));
    }
    return out;
}

Span command_span_at(std::string_view line, std::size_t caret) {
    if (caret > line.size()) caret = line.size();
    const auto segments = scan_segments(line);
    for (const Span& s : segments) {
        if (caret >= s.begin && caret <= s.end) return s;
    }
    return segments.back();
}

Cursor cursor_at(std::string_view line, std::size_t caret) {
    if (caret > line.size()) caret = line.size();
    const Span seg = command_span_at(line, caret);
    const std::string_view cmd = line.substr(seg.begin, seg.end - seg.begin);
    const std::size_t rel = caret - seg.begin;
    const auto tokens = tokenize(cmd);

    Cursor cur;
    std::size_t before = 0;
    for (std::size_t i = 0; i < tokens.size(); ++i) {
        const Token& t = tokens[i];
        if (t.begin < rel && rel <= t.end) {
            // The prefix is the unquoted text up to the caret, so re-tokenize the truncated command.
            const auto head = tokenize(cmd.substr(0, rel));
            cur.index = i;
            cur.prefix = i < head.size() ? head[i].text : t.text;
            cur.replace_begin = seg.begin + t.begin;
            cur.replace_end = seg.begin + t.end;
            cur.quoted = t.quoted;
            return cur;
        }
        if (t.end <= rel) ++before;
    }
    cur.index = before;
    cur.replace_begin = cur.replace_end = caret;
    return cur;
}

std::string quote_if_needed(std::string_view text) {
    bool need = text.empty() || is_quote(text.front());
    for (char c : text) {
        if (is_space(c) || c == ';' || c == '"' || c == '\'') need = true;
    }
    if (!need) return std::string(text);
    std::string out = "\"";
    for (char c : text) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    out += '"';
    return out;
}

}  // namespace sh::console
