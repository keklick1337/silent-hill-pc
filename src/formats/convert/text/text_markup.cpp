#include "formats/convert/text/text_markup.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <optional>
#include <vector>

namespace sh::convert::text::markup {

namespace {

// One readable element. kind: 'g' glyph (one UTF-8 character), 's' visible space, 'n' Ui line
// break, 'b' control byte, 'c' ~code, 'j' ~J timed line, 'r' raw unmodelled code.
struct Unit {
    char kind = 'g';
    char code = 0;     // code letter for 'c', mode digit for 'j'
    std::string arg;   // digit for C/S/L, seconds for 'j'
    std::string text;  // glyph / byte / raw bytes
};

constexpr std::string_view kNoArg = "NEPDHMT";  // codes whose argument byte is swallowed
constexpr std::string_view kDigitArg = "CSL";

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_layout(char c, Style style) {
    return c == ' ' || c == '\t' || (style == Style::Message && c == '\n');
}
bool has(std::string_view set, char c) { return c != 0 && set.find(c) != std::string_view::npos; }

std::size_t utf8_len(std::string_view s, std::size_t i) {
    const auto b = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    if ((b & 0xE0) == 0xC0) {
        len = 2;
    } else if ((b & 0xF0) == 0xE0) {
        len = 3;
    } else if ((b & 0xF8) == 0xF0) {
        len = 4;
    }
    return std::min(len, s.size() - i);
}

std::string_view noarg_name(char code) {
    switch (code) {
        case 'E': return "end";
        case 'P': return "page";
        case 'D': return "instant";
        case 'H': return "hires";
        case 'M': return "center";
        case 'T': return "inset";
        default: return "";
    }
}

std::string_view digit_name(char code) {
    switch (code) {
        case 'C': return "color";
        case 'S': return "choice";
        case 'L': return "pos";
        default: return "";
    }
}

char code_for(std::string_view name, std::string_view (*names)(char), std::string_view set) {
    for (char c : set) {
        if (names(c) == name && !name.empty()) {
            return c;
        }
    }
    return 0;
}

bool all_number(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (!is_digit(c) && c != '.') {
            return false;
        }
    }
    return true;
}

void engine_units(std::string_view s, Style style, std::vector<Unit>& units, std::vector<std::string>& runs) {
    runs.assign(1, std::string());
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const char c = s[i];
        if (is_layout(c, style)) {
            runs.back() += c;
            ++i;
            continue;
        }
        std::optional<Unit> u;
        if (style == Style::Ui) {
            Unit v;
            if (c == '\n') {
                v.kind = 'n';
                ++i;
            } else if (c == '_') {
                v.kind = 's';
                ++i;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                v.kind = 'b';
                v.text = std::string(1, c);
                ++i;
            } else {
                const std::size_t len = utf8_len(s, i);
                v.text = std::string(s.substr(i, len));
                i += len;
            }
            units.push_back(std::move(v));
            runs.emplace_back();
            continue;
        }
        if (c == '~' && i + 1 < n) {
            const char t = s[i + 1];
            if (t == 'J' && i + 2 < n && is_digit(s[i + 2]) && i + 3 < n && s[i + 3] == '(') {
                const std::size_t close = s.find(')', i + 4);
                if (close != std::string_view::npos && all_number(s.substr(i + 4, close - (i + 4)))) {
                    Unit v;
                    v.kind = 'j';
                    v.code = s[i + 2];
                    v.arg = std::string(s.substr(i + 4, close - (i + 4)));
                    u = std::move(v);
                    i = close + 1;
                }
            } else if (has(kDigitArg, t) && i + 2 < n && is_digit(s[i + 2])) {
                Unit v;
                v.kind = 'c';
                v.code = t;
                v.arg = std::string(1, s[i + 2]);
                u = std::move(v);
                i += 3;
            } else if (has(kNoArg, t) && (i + 2 >= n || is_layout(s[i + 2], style))) {
                Unit v;
                v.kind = 'c';
                v.code = t;
                u = std::move(v);
                i += 2;
            }
        }
        if (!u) {
            Unit v;
            if (c == '~') {
                // A code this format does not model: '~' and the two characters after it, raw.
                std::size_t end = i + 1;
                for (int k = 0; k < 2 && end < n; ++k) {
                    end += utf8_len(s, end);
                }
                v.kind = 'r';
                v.text = std::string(s.substr(i, end - i));
                i = end;
            } else if (c == '_') {
                v.kind = 's';
                ++i;
            } else if (static_cast<unsigned char>(c) < 0x20) {
                v.kind = 'b';
                v.text = std::string(1, c);
                ++i;
            } else {
                const std::size_t len = utf8_len(s, i);
                v.text = std::string(s.substr(i, len));
                i += len;
            }
            u = std::move(v);
        }
        units.push_back(std::move(*u));
        runs.emplace_back();
    }
}

bool has_wait(const std::vector<Unit>& units) {
    for (const Unit& u : units) {
        if (u.kind == 'j') {
            return true;
        }
    }
    return false;
}

// Must match sh_text.canonical_run. Keeps every code's argument byte and the ~J terminator.
std::string canonical_run(const std::vector<Unit>& units, std::size_t k, Style style, bool wait) {
    if (style == Style::Ui) {
        return {};
    }
    const Unit* prev = k > 0 ? &units[k - 1] : nullptr;
    const Unit* next = k < units.size() ? &units[k] : nullptr;
    if (prev == nullptr) {
        if (next == nullptr || next->kind == 'j' || next->kind == 'c' || next->kind == 'r') {
            return {};
        }
        return "\t";
    }
    if (prev->kind == 'j') {
        return "\t";
    }
    if (prev->kind == 'c' && prev->code == 'N') {
        return wait ? "\n\t\t\t" : "\n\t";
    }
    if (prev->kind == 'c') {
        return " ";
    }
    if (next == nullptr || next->kind == 'c') {
        return " ";
    }
    return {};
}

bool run_ok(const std::vector<Unit>& units, std::size_t k, const std::string& run) {
    const Unit* prev = k > 0 ? &units[k - 1] : nullptr;
    if (prev == nullptr) {
        return true;
    }
    if (prev->kind == 'c' && has(kNoArg, prev->code) && k < units.size() && run.empty()) {
        return false;
    }
    if (prev->kind == 'j' && run.find_first_of(" \t") == std::string::npos) {
        return false;
    }
    return true;
}

std::string readable(const std::vector<Unit>& units) {
    std::string out;
    bool color_open = false;
    for (const Unit& u : units) {
        switch (u.kind) {
            case 'g':
                out += u.text == "{" ? "{{" : u.text == "}" ? "}}" : u.text;
                break;
            case 's':
                out += ' ';
                break;
            case 'n':
                out += '\n';
                break;
            case 'b':
                if (u.text == "\v") {
                    out += "{wide}";
                } else if (u.text == "\x01") {
                    out += "{kern}";
                } else {
                    char buf[16];
                    std::snprintf(buf, sizeof buf, "{byte %02x}", static_cast<unsigned char>(u.text[0]));
                    out += buf;
                }
                break;
            case 'r':
                out += "{raw " + u.text + "}";
                break;
            case 'j':
                out += u.code == '0' ? "{wait " + u.arg + "}" : "{wait" + std::string(1, u.code) + " " + u.arg + "}";
                break;
            default:  // 'c'
                if (u.code == 'N') {
                    out += '\n';
                } else if (has(kNoArg, u.code)) {
                    out += "{" + std::string(noarg_name(u.code)) + "}";
                } else if (u.code == 'C' && u.arg == "7" && color_open) {
                    out += "{/color}";
                    color_open = false;
                } else {
                    out += "{" + std::string(digit_name(u.code)) + " " + u.arg + "}";
                    if (u.code == 'C') {
                        color_open = u.arg != "7";
                    }
                }
                break;
        }
    }
    return out;
}

char run_letter(char c) { return c == ' ' ? 's' : c == '\t' ? 't' : 'n'; }

Result<std::vector<Unit>> parse_readable(std::string_view text, Style style) {
    std::vector<Unit> units;
    std::size_t i = 0;
    const std::size_t n = text.size();
    auto unit = [](char kind, char code = 0, std::string arg = {}, std::string t = {}) {
        Unit u;
        u.kind = kind;
        u.code = code;
        u.arg = std::move(arg);
        u.text = std::move(t);
        return u;
    };
    while (i < n) {
        const char c = text[i];
        if (c == '{') {
            if (text.substr(i, 2) == "{{") {
                units.push_back(unit('g', 0, {}, "{"));
                i += 2;
                continue;
            }
            const std::size_t close = text.find('}', i);
            if (close == std::string_view::npos) {
                return Result<std::vector<Unit>>::fail("unclosed { at " + std::to_string(i));
            }
            const std::string_view tag = text.substr(i + 1, close - i - 1);
            i = close + 1;
            if (tag.starts_with("raw ")) {
                units.push_back(unit('r', 0, {}, std::string(tag.substr(4))));
                continue;
            }
            if (tag == "wide" || tag == "kern") {
                units.push_back(unit('b', 0, {}, tag == "wide" ? "\v" : "\x01"));
                continue;
            }
            if (tag.starts_with("byte ") && tag.size() == 7) {
                auto hex = [](char h) {
                    return h >= '0' && h <= '9' ? h - '0' : h >= 'a' && h <= 'f' ? h - 'a' + 10 : -1;
                };
                const int hi = hex(tag[5]);
                const int lo = hex(tag[6]);
                if (hi >= 0 && lo >= 0) {
                    units.push_back(unit('b', 0, {}, std::string(1, static_cast<char>(hi * 16 + lo))));
                    continue;
                }
            }
            std::vector<std::string_view> parts;
            for (std::size_t p = 0;;) {
                const std::size_t sp = tag.find(' ', p);
                parts.push_back(tag.substr(p, sp == std::string_view::npos ? std::string_view::npos : sp - p));
                if (sp == std::string_view::npos) {
                    break;
                }
                p = sp + 1;
            }
            const std::string_view name = parts[0];
            const char noarg = code_for(name, noarg_name, kNoArg);
            const char digit = code_for(name, digit_name, kDigitArg);
            if (name == "/color" && parts.size() == 1) {
                units.push_back(unit('c', 'C', "7"));
            } else if (noarg != 0 && parts.size() == 1) {
                units.push_back(unit('c', noarg));
            } else if (digit != 0 && parts.size() == 2 && parts[1].size() == 1 && is_digit(parts[1][0])) {
                units.push_back(unit('c', digit, std::string(parts[1])));
            } else if (name.starts_with("wait") && parts.size() == 2 &&
                       (name == "wait" || (name.size() == 5 && is_digit(name[4]))) && all_number(parts[1])) {
                units.push_back(unit('j', name.size() == 5 ? name[4] : '0', std::string(parts[1])));
            } else {
                return Result<std::vector<Unit>>::fail("unknown markup {" + std::string(tag) + "}");
            }
            continue;
        }
        if (c == '}') {
            i += text.substr(i, 2) == "}}" ? 2 : 1;
            units.push_back(unit('g', 0, {}, "}"));
            continue;
        }
        if (c == '\n') {
            units.push_back(style == Style::Message ? unit('c', 'N') : unit('n'));
            ++i;
        } else if (c == ' ') {
            units.push_back(unit('s'));
            ++i;
        } else if (c == '\t' || c == '\r') {
            ++i;  // layout is the encoder's business
        } else {
            const std::size_t len = utf8_len(text, i);
            units.push_back(unit('g', 0, {}, std::string(text.substr(i, len))));
            i += len;
        }
    }
    return Result<std::vector<Unit>>::ok(std::move(units));
}

// The ws record's runs by slot, or nothing when it is absent, stale or malformed.
std::optional<std::map<std::size_t, std::string>> parse_ws(std::string_view ws, std::string_view text,
                                                           std::size_t count) {
    if (ws.empty()) {
        return std::nullopt;
    }
    const std::size_t colon = ws.find(':');
    const std::string_view head = ws.substr(0, colon);
    const std::string_view body = colon == std::string_view::npos ? std::string_view() : ws.substr(colon + 1);
    if (head != text_hash(text)) {
        return std::nullopt;  // the text was edited since the record was made
    }
    std::map<std::size_t, std::string> out;
    std::size_t p = 0;
    while (p <= body.size()) {
        const std::size_t comma = body.find(',', p);
        const std::string_view item =
            body.substr(p, comma == std::string_view::npos ? std::string_view::npos : comma - p);
        p = comma == std::string_view::npos ? body.size() + 1 : comma + 1;
        if (item.empty()) {
            continue;
        }
        const std::size_t eq = item.find('=');
        const std::string_view k = item.substr(0, eq);
        const std::string_view run = eq == std::string_view::npos ? std::string_view() : item.substr(eq + 1);
        if (k.empty() || k.size() > 9) {
            return std::nullopt;
        }
        std::size_t slot = 0;
        for (char c : k) {
            if (!is_digit(c)) {
                return std::nullopt;
            }
            slot = slot * 10 + static_cast<std::size_t>(c - '0');
        }
        if (slot > count) {
            return std::nullopt;
        }
        std::string bytes;
        if (run != "-") {
            for (char c : run) {
                if (c == 's') {
                    bytes += ' ';
                } else if (c == 't') {
                    bytes += '\t';
                } else if (c == 'n') {
                    bytes += '\n';
                } else {
                    return std::nullopt;
                }
            }
        }
        out[slot] = std::move(bytes);
    }
    return out;
}

}  // namespace

Style style_for_key(std::string_view key) {
    if (key.starts_with("ui.")) {
        return Style::Plain;
    }
    return key.starts_with("item.") || key.starts_with("menu.") ? Style::Ui : Style::Message;
}

std::string text_hash(std::string_view text) {
    std::uint32_t h = 0x811C9DC5u;
    for (char c : text) {
        h ^= static_cast<unsigned char>(c);
        h *= 0x01000193u;
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "%08x", static_cast<unsigned>(h));
    return buf;
}

Decoded decode(std::string_view engine, Style style) {
    if (style == Style::Plain) {
        Decoded plain{std::string(engine), {}};
        std::replace(plain.text.begin(), plain.text.end(), '_', ' ');
        return plain;
    }
    std::vector<Unit> units;
    std::vector<std::string> runs;
    engine_units(engine, style, units, runs);
    Decoded out;
    out.text = readable(units);
    const bool wait = has_wait(units);
    std::string dev;
    for (std::size_t k = 0; k < runs.size(); ++k) {
        if (runs[k] == canonical_run(units, k, style, wait)) {
            continue;
        }
        if (!dev.empty()) {
            dev += ',';
        }
        dev += std::to_string(k) + "=";
        if (runs[k].empty()) {
            dev += '-';
        }
        for (char c : runs[k]) {
            dev += run_letter(c);
        }
    }
    if (!dev.empty()) {
        out.ws = text_hash(out.text) + ":" + dev;
    }
    return out;
}

Result<std::string> encode(std::string_view text, std::string_view ws, Style style) {
    if (style == Style::Plain) {
        std::string plain(text);
        std::replace(plain.begin(), plain.end(), ' ', '_');
        return Result<std::string>::ok(std::move(plain));
    }
    auto parsed = parse_readable(text, style);
    if (!parsed) {
        return Result<std::string>::fail(parsed.error());
    }
    const std::vector<Unit>& units = parsed.value();
    const auto exact = parse_ws(ws, text, units.size());
    const bool wait = has_wait(units);
    std::string out;
    out.reserve(text.size() + 16);
    for (std::size_t k = 0;; ++k) {
        std::string run = canonical_run(units, k, style, wait);
        if (exact) {
            const auto it = exact->find(k);
            if (it != exact->end() && run_ok(units, k, it->second)) {
                run = it->second;
            }
        }
        out += run;
        if (k == units.size()) {
            break;
        }
        const Unit& u = units[k];
        switch (u.kind) {
            case 's': out += '_'; break;
            case 'n': out += '\n'; break;
            case 'j': out += "~J" + std::string(1, u.code) + "(" + u.arg + ")"; break;
            case 'c': out += '~'; out += u.code; out += u.arg; break;
            default: out += u.text; break;  // glyph, byte, raw
        }
    }
    return Result<std::string>::ok(std::move(out));
}

}  // namespace sh::convert::text::markup
