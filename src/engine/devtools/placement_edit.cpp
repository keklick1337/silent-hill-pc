// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright 2026 Vladislav Tislenko (keklick1337)

#include "engine/devtools/placement_edit.h"

#include "engine/devtools/position_snippets.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace sh::devtools {

namespace {

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_'; }
bool space_char(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

std::string generic(std::string_view path) {
    std::string s(path);
    std::replace(s.begin(), s.end(), '\\', '/');
    while (s.size() > 1 && s.back() == '/') s.pop_back();
    return s;
}

// The text below `root` when `file` lies in it.
std::optional<std::string> below(const std::string& file, const std::string& root) {
    if (root.empty() || file.size() <= root.size() + 1 || file.compare(0, root.size(), root) != 0 ||
        file[root.size()] != '/') {
        return std::nullopt;
    }
    return file.substr(root.size() + 1);
}

// The end of the string literal that opens at text[at] (one past its closing quote, or the end of the text).
std::size_t string_end(std::string_view text, std::size_t at) {
    const char quote = text[at];
    std::size_t i = at + 1;
    while (i < text.size() && text[i] != quote) {
        if (text[i] == '\\' && i + 1 < text.size()) ++i;
        ++i;
    }
    return std::min(i + 1, text.size());
}

std::size_t skip_spaces(std::string_view text, std::size_t i) {
    while (i < text.size() && space_char(text[i])) ++i;
    return i;
}

bool matches(const NumberToken& t, const Wanted& w, bool* as_q12) {
    if (std::fabs(t.value - w.value) <= w.tolerance) {
        *as_q12 = false;
        return true;
    }
    if (w.q12 && t.integer && std::fabs(t.value - w.value * 4096.0) <= 1.5) {
        *as_q12 = true;
        return true;
    }
    return false;
}

// The literal is written as `key = <n>` or `"key": <n>`.
bool keyed(std::string_view line, const NumberToken& t, std::string_view key) {
    if (key.empty()) return false;
    std::size_t i = t.begin;
    while (i > 0 && space_char(line[i - 1])) --i;
    if (i == 0 || (line[i - 1] != '=' && line[i - 1] != ':')) return false;
    --i;
    while (i > 0 && space_char(line[i - 1])) --i;
    if (i > 0 && (line[i - 1] == '"' || line[i - 1] == '\'')) --i;
    if (i < key.size() || line.substr(i - key.size(), key.size()) != key) return false;
    return i == key.size() || !ident_char(line[i - key.size() - 1]);
}

std::vector<std::string_view> split_lines(std::string_view source) {
    std::vector<std::string_view> out;
    std::size_t at = 0;
    while (at <= source.size()) {
        std::size_t end = source.find('\n', at);
        if (end == std::string_view::npos) end = source.size();
        std::string_view line = source.substr(at, end - at);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        out.push_back(line);
        at = end + 1;
    }
    return out;
}

std::string one_line(std::string_view text) {
    std::string out;
    bool gap = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '"') {
            const std::size_t end = string_end(text, i);
            if (gap && !out.empty()) out += ' ';
            gap = false;
            out.append(text.substr(i, end - i));
            i = end - 1;
        } else if (space_char(text[i])) {
            gap = true;
        } else {
            // A break after an opening bracket or before a closing one leaves no space.
            const bool tight =
                !out.empty() && (out.back() == '[' || out.back() == '{' || text[i] == ']' || text[i] == '}');
            if (gap && !out.empty() && !tight) out += ' ';
            gap = false;
            out += text[i];
        }
    }
    return out;
}

std::string number_list(const MemberEdit& e) {
    std::string out = "[";
    for (std::size_t i = 0; i < e.values.size(); ++i) {
        if (i) out += ", ";
        out += tidy_number(e.values[i], e.decimals);
    }
    return out + "]";
}

}  // namespace

std::string tidy_number(double value, int decimals, bool keep_point) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.*f", std::clamp(decimals, 0, 9), value);
    std::string s = buf;
    if (s.find('.') != std::string::npos) {
        while (s.back() == '0') s.pop_back();
        if (s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    if (keep_point && s.find('.') == std::string::npos) s += ".0";
    return s;
}

std::string raw_point_row(double x, double y, double z) {
    const auto raw = [](double m) { return std::to_string(q12_of_metres(static_cast<float>(m))); };
    return "{ x = " + raw(x) + ", y = " + raw(y) + ", z = " + raw(z) + " } -- " + tidy_number(x) + ", " +
           tidy_number(y) + ", " + tidy_number(z) + " m";
}

FileOwner file_owner(std::string_view file, std::string_view gamedata_dir,
                     const std::vector<std::pair<std::string, std::string>>& mods) {
    FileOwner owner;
    const std::string f = generic(file);
    owner.relative = f;
    std::size_t best = 0;
    for (const auto& [id, root] : mods) {
        const std::string r = generic(root);
        if (const auto rel = below(f, r); rel && r.size() >= best) {
            best = r.size();
            owner.mod = id;
            owner.relative = *rel;
            owner.known = true;
        }
    }
    if (owner.known) return owner;
    if (const auto rel = below(f, generic(gamedata_dir))) {
        owner.relative = *rel;
        owner.known = true;
    }
    return owner;
}

std::string file_owner_text(const FileOwner& owner) {
    if (!owner.known) return owner.relative;
    return owner.mod.empty() ? "base gamedata/" + owner.relative : "mod " + owner.mod + ": " + owner.relative;
}

std::vector<NumberToken> number_tokens(std::string_view text, bool lua) {
    std::vector<NumberToken> out;
    std::size_t i = 0;
    const std::size_t n = text.size();
    while (i < n) {
        const char c = text[i];
        if (c == '"' || (lua && c == '\'')) {
            i = string_end(text, i);
        } else if (lua && c == '-' && i + 1 < n && text[i + 1] == '-') {
            if (text.compare(i, 4, "--[[") == 0) {
                const std::size_t end = text.find("]]", i + 4);
                i = end == std::string_view::npos ? n : end + 2;
            } else {
                const std::size_t end = text.find('\n', i);
                i = end == std::string_view::npos ? n : end;
            }
        } else if (std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_') {
            while (i < n && ident_char(text[i])) ++i;
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0 ||
                   (c == '.' && i + 1 < n && std::isdigit(static_cast<unsigned char>(text[i + 1])) != 0)) {
            NumberToken t;
            t.begin = i;
            const std::string rest(text.substr(i, 64));
            char* end = nullptr;
            t.value = std::strtod(rest.c_str(), &end);
            const std::size_t len = std::max<std::size_t>(1, static_cast<std::size_t>(end - rest.c_str()));
            const std::string_view literal = text.substr(i, len);
            const bool hex = literal.size() > 1 && literal[0] == '0' && (literal[1] == 'x' || literal[1] == 'X');
            t.integer = hex || literal.find_first_of(".eE") == std::string_view::npos;
            t.end = i + len;
            // A minus with no left operand is the literal's sign.
            std::size_t s = i;
            while (s > 0 && (text[s - 1] == ' ' || text[s - 1] == '\t')) --s;
            if (s > 0 && text[s - 1] == '-') {
                std::size_t b = s - 1;
                while (b > 0 && (text[b - 1] == ' ' || text[b - 1] == '\t')) --b;
                const char before = b > 0 ? text[b - 1] : '\0';
                if (before == '\0' || before == '\n' ||
                    std::string_view("=,({[:<>~*/+-%^").find(before) != std::string_view::npos) {
                    t.begin = s - 1;
                    t.value = -t.value;
                }
            }
            out.push_back(t);
            i = t.end;
        } else {
            ++i;
        }
    }
    return out;
}

int line_of(std::string_view text, std::size_t offset) {
    offset = std::min(offset, text.size());
    return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
}

std::vector<SourceLine> lines_with_values(std::string_view source, const std::vector<Wanted>& wanted) {
    std::vector<SourceLine> out;
    if (wanted.empty()) return out;
    const std::vector<std::string_view> lines = split_lines(source);
    for (std::size_t l = 0; l < lines.size(); ++l) {
        const std::vector<NumberToken> tokens = number_tokens(lines[l]);
        if (tokens.size() < wanted.size()) continue;
        std::vector<bool> used(tokens.size(), false);
        bool all = true;
        for (const Wanted& w : wanted) {
            bool found = false;
            for (std::size_t t = 0; t < tokens.size() && !found; ++t) {
                bool q = false;
                if (!used[t] && matches(tokens[t], w, &q)) used[t] = found = true;
            }
            all = all && found;
        }
        if (all) out.push_back({static_cast<int>(l) + 1, std::string(lines[l])});
    }
    return out;
}

std::vector<SourceLine> lines_with_string(std::string_view source, std::string_view text) {
    std::vector<SourceLine> out;
    if (text.empty()) return out;
    const std::string a = "\"" + std::string(text) + "\"", b = "'" + std::string(text) + "'";
    const std::vector<std::string_view> lines = split_lines(source);
    for (std::size_t l = 0; l < lines.size(); ++l) {
        if (lines[l].find(a) != std::string_view::npos || lines[l].find(b) != std::string_view::npos)
            out.push_back({static_cast<int>(l) + 1, std::string(lines[l])});
    }
    return out;
}

std::string patch_line(std::string_view line, const std::vector<Change>& changes, int* replaced) {
    const std::vector<NumberToken> tokens = number_tokens(line);
    std::vector<bool> used(tokens.size(), false);
    struct Splice {
        std::size_t begin, end;
        std::string text;
    };
    std::vector<Splice> splices;
    for (const Change& c : changes) {
        int pick = -1;
        bool pick_q12 = false;
        for (std::size_t t = 0; t < tokens.size(); ++t) {
            bool q = false;
            if (used[t] || !matches(tokens[t], c.from, &q)) continue;
            const bool by_key = keyed(line, tokens[t], c.key);
            if (pick < 0 || by_key) {
                pick = static_cast<int>(t);
                pick_q12 = q;
            }
            if (by_key) break;
        }
        if (pick < 0) continue;
        const NumberToken& t = tokens[static_cast<std::size_t>(pick)];
        used[static_cast<std::size_t>(pick)] = true;
        splices.push_back(
            {t.begin, t.end,
             pick_q12 ? std::to_string(q12_of_metres(static_cast<float>(c.to))) : tidy_number(c.to, 4, !t.integer)});
    }
    if (replaced != nullptr) *replaced = static_cast<int>(splices.size());
    std::sort(splices.begin(), splices.end(), [](const Splice& a, const Splice& b) { return a.begin > b.begin; });
    std::string out(line);
    for (const Splice& s : splices) out.replace(s.begin, s.end - s.begin, s.text);
    return out;
}

std::optional<TextSpan> json_record(std::string_view text, std::string_view id_key, std::string_view id) {
    struct Frame {
        char type;
        std::size_t begin;
        bool match;
    };
    std::vector<Frame> stack;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '"') {
            const std::size_t end = string_end(text, i);
            const std::size_t colon = skip_spaces(text, end);
            if (!stack.empty() && stack.back().type == '{' && colon < text.size() && text[colon] == ':' &&
                text.substr(i + 1, end - i - 2) == id_key) {
                const std::size_t v = skip_spaces(text, colon + 1);
                if (v < text.size() && text[v] == '"') {
                    const std::size_t v_end = string_end(text, v);
                    if (text.substr(v + 1, v_end - v - 2) == id) stack.back().match = true;
                } else {
                    std::size_t v_end = v;
                    while (v_end < text.size() && !space_char(text[v_end]) && text[v_end] != ',' && text[v_end] != '}')
                        ++v_end;
                    if (text.substr(v, v_end - v) == id) stack.back().match = true;
                }
            }
            i = end;
        } else if (c == '{' || c == '[') {
            stack.push_back({c, i, false});
            ++i;
        } else if (c == '}' || c == ']') {
            if (!stack.empty()) {
                const Frame f = stack.back();
                stack.pop_back();
                if (f.match) return TextSpan{f.begin, i + 1};
            }
            ++i;
        } else {
            ++i;
        }
    }
    return std::nullopt;
}

std::optional<TextSpan> json_member(std::string_view text, TextSpan record, std::string_view key) {
    int depth = 0;
    std::size_t i = record.begin;
    const std::size_t stop = std::min(record.end, text.size());
    while (i < stop) {
        const char c = text[i];
        if (c == '"') {
            const std::size_t end = string_end(text, i);
            const std::size_t colon = skip_spaces(text, end);
            if (depth == 1 && colon < stop && text[colon] == ':' && text.substr(i + 1, end - i - 2) == key) {
                const std::size_t v = skip_spaces(text, colon + 1);
                std::size_t e = v;
                if (v < stop && (text[v] == '[' || text[v] == '{')) {
                    int d = 0;
                    while (e < stop) {
                        if (text[e] == '"') {
                            e = string_end(text, e);
                            continue;
                        }
                        if (text[e] == '[' || text[e] == '{') ++d;
                        if (text[e] == ']' || text[e] == '}') {
                            if (--d == 0) {
                                ++e;
                                break;
                            }
                        }
                        ++e;
                    }
                } else if (v < stop && text[v] == '"') {
                    e = string_end(text, v);
                } else {
                    while (e < stop && !space_char(text[e]) && text[e] != ',' && text[e] != '}') ++e;
                }
                return TextSpan{v, e};
            }
            i = end;
        } else {
            if (c == '{' || c == '[') ++depth;
            if (c == '}' || c == ']') --depth;
            ++i;
        }
    }
    return std::nullopt;
}

PatchedRecord patch_json_record(std::string_view text, TextSpan record, const std::vector<MemberEdit>& edits) {
    PatchedRecord out;
    out.line = line_of(text, record.begin);
    struct Splice {
        std::size_t begin, end;
        std::string text;
    };
    std::vector<Splice> splices;
    for (const MemberEdit& e : edits) {
        const std::optional<TextSpan> value = json_member(text, record, e.key);
        if (!value) {
            out.added.push_back("\"" + e.key + "\": " + number_list(e));
            continue;
        }
        const std::string_view old = text.substr(value->begin, value->end - value->begin);
        out.before.push_back({line_of(text, value->begin), "\"" + e.key + "\": " + one_line(old)});
        const std::vector<NumberToken> tokens = number_tokens(old, false);
        std::string now(old);
        if (tokens.size() == e.values.size()) {
            // Each number where it stands: an array written over several lines keeps its lines.
            for (std::size_t t = tokens.size(); t-- > 0;) {
                if (t < e.keep.size() && e.keep[t]) continue;
                now.replace(tokens[t].begin, tokens[t].end - tokens[t].begin, tidy_number(e.values[t], e.decimals));
            }
        } else {
            now = number_list(e);
        }
        out.after.push_back("\"" + e.key + "\": " + one_line(now));
        splices.push_back({value->begin, value->end, std::move(now)});
    }
    std::sort(splices.begin(), splices.end(), [](const Splice& a, const Splice& b) { return a.begin > b.begin; });
    std::string whole(text.substr(record.begin, record.end - record.begin));
    for (const Splice& s : splices) whole.replace(s.begin - record.begin, s.end - s.begin, s.text);
    if (!out.added.empty()) {
        // New members go before the closing brace, after the last one the record has.
        std::size_t close = whole.rfind('}');
        std::size_t last = close;
        while (last > 0 && space_char(whole[last - 1])) --last;
        std::string add;
        for (const std::string& a : out.added) add += (last > 1 ? ", " : "") + a;
        whole.insert(last, add);
    }
    out.record = std::move(whole);
    return out;
}

std::vector<double> path_segments(const std::vector<PathPoint>& points) {
    std::vector<double> out;
    for (std::size_t i = 1; i < points.size(); ++i) {
        out.push_back(
            std::hypot(points[i].x - points[i - 1].x, points[i].y - points[i - 1].y, points[i].z - points[i - 1].z));
    }
    return out;
}

double path_length(const std::vector<PathPoint>& points) {
    double total = 0.0;
    for (const double s : path_segments(points)) total += s;
    return total;
}

double path_time(double length, double speed) { return speed > 0.0 ? length / speed : 0.0; }
double path_speed(double length, double seconds) { return seconds > 0.0 ? length / seconds : 0.0; }

std::string path_lua_raw(const std::vector<PathPoint>& points) {
    std::string out = "{\n";
    for (const PathPoint& p : points) {
        const std::string row = raw_point_row(p.x, p.y, p.z);
        // The comma goes before the row's comment.
        const std::size_t comment = row.find(" -- ");
        out += "    " + row.substr(0, comment) + "," + row.substr(comment) + "\n";
    }
    return out + "}";
}

std::string path_lua_metres(const std::vector<PathPoint>& points) {
    std::string out = "{\n";
    for (const PathPoint& p : points) {
        out += "    { x = " + tidy_number(p.x) + ", y = " + tidy_number(p.y) + ", z = " + tidy_number(p.z) + " },\n";
    }
    return out + "}";
}

std::string path_json(const std::vector<PathPoint>& points) {
    std::string out = "[";
    for (std::size_t i = 0; i < points.size(); ++i) {
        out += std::string(i ? ", " : "") + "[" + tidy_number(points[i].x) + ", " + tidy_number(points[i].y) + ", " +
               tidy_number(points[i].z) + "]";
    }
    return out + "]";
}

void EditHistory::record(const std::string& key, const EditValue& before, const EditValue& after, int gesture) {
    if (before == after) return;
    originals_.try_emplace(key, before);
    steps_.resize(at_);  // a new edit drops what was taken back
    if (gesture != 0 && gesture == gesture_ && !steps_.empty() && steps_.back().key == key) {
        steps_.back().after = after;
        return;
    }
    gesture_ = gesture;
    steps_.push_back({key, before, after});
    at_ = steps_.size();
}

std::optional<EditStep> EditHistory::undo() {
    if (at_ == 0) return std::nullopt;
    gesture_ = 0;
    return steps_[--at_];
}

std::optional<EditStep> EditHistory::redo() {
    if (at_ >= steps_.size()) return std::nullopt;
    gesture_ = 0;
    return steps_[at_++];
}

std::optional<EditValue> EditHistory::original(const std::string& key) const {
    const auto it = originals_.find(key);
    return it != originals_.end() ? std::optional<EditValue>(it->second) : std::nullopt;
}

void EditHistory::clear() {
    steps_.clear();
    originals_.clear();
    at_ = 0;
    gesture_ = 0;
}

}  // namespace sh::devtools
